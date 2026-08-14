// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConversationWidget.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "MicRecorder.h"
#include "SpeechQueue.h"
#include "TimerManager.h"

void UConversationWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UConversationClient* Client = GetGameInstance()->GetSubsystem<UConversationClient>();
	USpeechQueue* Queue = GetGameInstance()->GetSubsystem<USpeechQueue>();

	SendButton->OnClicked.AddDynamic(this, &UConversationWidget::HandleSendClicked);
	MicButton->OnClicked.AddDynamic(this, &UConversationWidget::HandleMicClicked);
	RealtimeButton->OnClicked.AddDynamic(this, &UConversationWidget::HandleRealtimeClicked);
	PushToTalkButton->OnClicked.AddDynamic(this, &UConversationWidget::HandlePushToTalkClicked);
	ModeButton->OnClicked.AddDynamic(this, &UConversationWidget::HandleModeClicked);

	// 에디터에 어떻게 저장돼 있든 처음엔 떠 있어야 한다.
	ModePanel->SetVisibility(ESlateVisibility::Visible);

	Client->OnConnectionChanged.AddDynamic(this, &UConversationWidget::HandleConnectionChanged);
	Client->OnServerError.AddDynamic(this, &UConversationWidget::HandleServerError);
	Client->OnTranscript.AddDynamic(this, &UConversationWidget::HandleTranscript);
	Queue->OnBusyChanged.AddDynamic(this, &UConversationWidget::HandleBusyChanged);
	Queue->OnSentenceStarted.AddDynamic(this, &UConversationWidget::HandleSentenceStarted);

	RefreshSendEnabled();

	// 연결 정책을 위젯이 정하는 건 임시다. 로그인이나 서버 선택이 생기면 그쪽으로 옮긴다.
	if (!Client->IsConnected())
	{
		Client->Connect(UConversationClient::DefaultUrl);
	}
}

void UConversationWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SendTimer);
	}

	// 서브시스템은 위젯보다 오래 산다. 안 끊으면 죽은 위젯으로 델리게이트가 날아온다.
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UConversationClient* Client = GameInstance->GetSubsystem<UConversationClient>())
		{
			Client->OnConnectionChanged.RemoveAll(this);
			Client->OnServerError.RemoveAll(this);
			Client->OnTranscript.RemoveAll(this);
		}
		// 위젯이 사라져도 마이크는 열린 채로 남는다. 여기서 닫는다.
		if (UMicRecorder* Recorder = GameInstance->GetSubsystem<UMicRecorder>())
		{
			Recorder->Stop();
		}
		if (USpeechQueue* Queue = GameInstance->GetSubsystem<USpeechQueue>())
		{
			Queue->OnBusyChanged.RemoveAll(this);
			Queue->OnSentenceStarted.RemoveAll(this);
		}
	}

	Super::NativeDestruct();
}

void UConversationWidget::HandleSendClicked()
{
	Send();
}

void UConversationWidget::HandleMicClicked()
{
	if (bVoiceMode)
	{
		// 푸시 투 토크는 버튼을 다시 누르는 게 "말 다 했다"는 뜻이다. 마지막 녹음을
		// 받아적어야 한다. 실시간 모드는 무음이 그 역할을 하므로 재누름은 취소다.
		if (Mode == EConversationMode::PushToTalk)
		{
			FinishPushToTalk();
		}
		else
		{
			StopVoiceMode();
		}
		return;
	}

	StartVoiceMode();
}

void UConversationWidget::HandleRealtimeClicked()
{
	SelectMode(EConversationMode::Realtime);
}

void UConversationWidget::HandlePushToTalkClicked()
{
	SelectMode(EConversationMode::PushToTalk);
}

void UConversationWidget::HandleModeClicked()
{
	ModePanel->SetVisibility(ESlateVisibility::Visible);
}

void UConversationWidget::SelectMode(EConversationMode NewMode)
{
	// 두 모드는 마이크를 여닫는 규칙이 다르다. 켜진 채로 넘어가면 상태가 섞인다.
	if (bVoiceMode)
	{
		StopVoiceMode();
	}

	Mode = NewMode;
	ModePanel->SetVisibility(ESlateVisibility::Collapsed);
	RefreshModeLabel();

	// 실시간 모드는 고른 즉시 대화가 시작된다. 그게 이 모드의 뜻이다. 푸시 투 토크는
	// 사용자가 마이크 버튼을 누를 때까지 기다린다.
	if (Mode == EConversationMode::Realtime)
	{
		StartVoiceMode();
	}
}

void UConversationWidget::StartVoiceMode()
{
	// 연결이 없으면 받아적을 곳이 없다. 녹음해봐야 버린다.
	if (!GetGameInstance()->GetSubsystem<UConversationClient>()->IsConnected())
	{
		UE_LOG(LogConversation, Error, TEXT("연결되지 않았다. 녹음하지 않는다."));
		return;
	}

	bVoiceMode = true;
	bSendOnTranscript = false;
	bPendingSend = false;
	PendingAudio = 0;
	PartialTimer = 0.f;
	TextBeforeRecording = InputBox->GetText().ToString().TrimStartAndEnd();

	ShowMicOpen(true);
	GetGameInstance()->GetSubsystem<UMicRecorder>()->Start();
}

void UConversationWidget::FinishRealtimeTurn()
{
	bSendOnTranscript = true;
	++PendingAudio;
	GetGameInstance()->GetSubsystem<UConversationClient>()->SendAudio(
		GetGameInstance()->GetSubsystem<UMicRecorder>()->Stop());
	ShowMicOpen(false);
}

void UConversationWidget::FinishPushToTalk()
{
	const TArray<uint8> Wav = GetGameInstance()->GetSubsystem<UMicRecorder>()->Stop();

	bVoiceMode = false;
	PartialTimer = 0.f;

	MicButton->SetBackgroundColor(FLinearColor::White);
	InputBox->SetHintText(FText::GetEmpty());

	// bSendOnTranscript를 세우지 않는다. HandleTranscript가 입력란만 채우고 끝난다.
	// TextBeforeRecording도 지우지 않는다 -- 마지막 받아적기가 아직 안 왔다.
	if (Wav.Num() > 0)
	{
		++PendingAudio;
		GetGameInstance()->GetSubsystem<UConversationClient>()->SendAudio(Wav);
	}

	// 마이크는 이미 닫혔고 PendingAudio가 남아 있다. 다음 프레임을 기다리지 말고
	// 버튼을 누른 그 자리에서 "변환 중"으로 바뀌어야 한다.
	RefreshModeLabel();
}

void UConversationWidget::RefreshModeLabel()
{
	const UMicRecorder* Recorder = GetGameInstance()->GetSubsystem<UMicRecorder>();

	FString Status;
	if (Recorder->IsRecording())
	{
		Status = TEXT("● 녹음 중");
	}
	else if (PendingAudio > 0)
	{
		Status = TEXT("텍스트로 변환 중...");
	}
	else
	{
		Status = Mode == EConversationMode::Realtime
			? TEXT("실시간 대화")
			: TEXT("푸시 투 토크");
	}

	if (Status == LastModeLabelText)
	{
		return;
	}
	LastModeLabelText = Status;
	ModeLabel->SetText(FText::FromString(Status));
}

void UConversationWidget::StopVoiceMode()
{
	bVoiceMode = false;
	bSendOnTranscript = false;
	bPendingSend = false;
	TextBeforeRecording.Reset();

	// 답이 영영 안 오는 요청을 세고 있으면 다음 음성 모드에서 중간 결과가 아예 안 나간다.
	// 연결이 끊겨 여기로 온 경우가 그렇다.
	PendingAudio = 0;

	GetWorld()->GetTimerManager().ClearTimer(SendTimer);

	MicButton->SetBackgroundColor(FLinearColor::White);
	InputBox->SetHintText(FText::GetEmpty());

	// 녹음분은 버린다. 음성 모드를 끈다는 건 지금 말한 걸 안 보낸다는 뜻이다.
	GetGameInstance()->GetSubsystem<UMicRecorder>()->Stop();
}

void UConversationWidget::ShowMicOpen(bool bOpen)
{
	MicButton->SetBackgroundColor(bOpen ? RecordingColor : PausedColor);

	// 안내문은 입력란이 비었을 때만 보인다. 마이크가 닫히는 건 방금 보내서 비운
	// 직후이므로 딱 필요한 때에 뜬다.
	InputBox->SetHintText(bOpen
		? FText::GetEmpty()
		: FText::FromString(TEXT("마이크 꺼짐 — 지금 말해도 입력되지 않는다")));
}

void UConversationWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 음성 모드가 꺼진 뒤에도 갱신해야 한다. 푸시 투 토크는 마지막 받아적기를
	// 기다리는 동안 이미 bVoiceMode가 false다.
	RefreshModeLabel();

	if (!bVoiceMode)
	{
		return;
	}

	// 방금 한 말을 받아적는 중이거나 화면에 띄워둔 채 보내기를 기다리는 중이다.
	// 이 사이에 마이크를 다시 열면 곧 닫힐 창에 대고 말하게 된다.
	if (bSendOnTranscript || bPendingSend)
	{
		return;
	}

	UMicRecorder* Recorder = GetGameInstance()->GetSubsystem<UMicRecorder>();
	UConversationClient* Client = GetGameInstance()->GetSubsystem<UConversationClient>();

	// AI가 말하는 동안은 마이크를 닫는다. 스피커 소리가 그대로 들어와 자기 말에
	// 자기가 대답한다. 헤드폰을 써도 이 편이 안전하다.
	const bool bShouldRecord = !GetGameInstance()->GetSubsystem<USpeechQueue>()->IsBusy();
	if (bShouldRecord != Recorder->IsRecording())
	{
		if (bShouldRecord)
		{
			PartialTimer = 0.f;
			Recorder->Start();
		}
		else
		{
			Recorder->Stop();
		}
		ShowMicOpen(bShouldRecord);
		return;
	}

	if (!Recorder->IsRecording())
	{
		return;
	}

	// 한 발화가 너무 길어졌다. 실시간 모드는 쉬지 않고 말하면 무음이 안 오고, 푸시 투
	// 토크는 버튼을 다시 누르기 전까지 계속 쌓인다. 중간 결과마다 녹음 전체를 다시
	// 보내는 구조라 여기서 끊지 않으면 초당 전송량이 눌러앉는다.
	if (Recorder->GetRecordedSeconds() >= MaxRecordSeconds)
	{
		UE_LOG(LogConversation, Warning, TEXT("발화가 %.0f초를 넘겨 여기서 끊는다"), MaxRecordSeconds);
		if (Mode == EConversationMode::Realtime)
		{
			FinishRealtimeTurn();
		}
		else
		{
			FinishPushToTalk();
		}
		return;
	}

	// 말이 끝났다. 마지막 오디오를 보내고, 받아적기가 오는 대로 전송한다.
	// 푸시 투 토크는 무음을 보지 않는다 -- 끝냈는지는 버튼이 정한다.
	if (Mode == EConversationMode::Realtime && Recorder->HasEndpointed())
	{
		FinishRealtimeTurn();
		return;
	}

	// 앞 요청이 아직 안 왔으면 기다린다. 인식이 느려지면 간격이 알아서 벌어진다.
	// 아직 아무 소리도 안 났으면 정적을 받아적으러 보낼 이유가 없다.
	if (PendingAudio > 0 || !Recorder->HasHeardSpeech())
	{
		return;
	}

	PartialTimer += InDeltaTime;
	if (PartialTimer < PartialInterval)
	{
		return;
	}
	PartialTimer = 0.f;

	// ponytail: 매번 녹음 전체를 다시 보낸다. 48kHz 모노 16비트면 초당 96KB씩 늘고
	// 서버도 매번 처음부터 다시 받아적는다. 30초 발화까지는 로컬에서 문제가 안 된다.
	// 더 길게 갈 일이 생기면 서버가 버퍼를 들고 증분만 받는 구조로 바꿔야 한다.
	const TArray<uint8> Wav = Recorder->Snapshot();
	if (Wav.Num() > 0)
	{
		++PendingAudio;
		Client->SendAudio(Wav);
	}
}

void UConversationWidget::HandleTranscript(const FString& Text)
{
	// StopVoiceMode가 0으로 되돌린 뒤에 옛 요청의 답이 도착할 수 있다.
	PendingAudio = FMath::Max(0, PendingAudio - 1);

	// 중간 결과는 매번 처음부터 다시 받아적은 전체 문장이다. 이어 붙이면 안 되고
	// 갈아쳐야 한다.
	const FString Full = TextBeforeRecording.IsEmpty()
		? Text
		: TextBeforeRecording + TEXT(" ") + Text;
	InputBox->SetText(FText::FromString(Full));

	if (!bSendOnTranscript)
	{
		return;
	}
	bSendOnTranscript = false;

	if (Text.IsEmpty())
	{
		// 잡음에 반응해 켜졌다 꺼진 경우다. 보낼 게 없으니 다음 발화를 기다린다.
		UE_LOG(LogConversation, Warning, TEXT("아무 말도 못 알아들었다"));
		return;
	}

	// 여기서 바로 Send를 부르면 같은 프레임에 입력란이 비워진다. 방금 넣은 글자는
	// 한 번도 그려지지 않고 사라진다. 한 박자 뒤에 보낸다.
	bPendingSend = true;
	GetWorld()->GetTimerManager().SetTimer(SendTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			bPendingSend = false;
			Send();
			TextBeforeRecording.Reset();
		}),
		SendDelay, false);
}

FReply UConversationWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Enter && !InKeyEvent.IsShiftDown())
	{
		Send();
		return FReply::Handled();
	}

	// Shift+엔터는 그냥 흘려보낸다. 텍스트 박스가 줄바꿈으로 처리한다.
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

void UConversationWidget::Send()
{
	// 버튼은 비활성이지만 엔터는 그대로 들어온다. 여기서도 막아야 한다.
	if (!CanSend())
	{
		return;
	}

	const FString Text = InputBox->GetText().ToString().TrimStartAndEnd();
	if (Text.IsEmpty())
	{
		return;
	}

	AppendBubble(Text, true);

	GetGameInstance()->GetSubsystem<UConversationClient>()->SendUserMessage(Text);
	InputBox->SetText(FText::GetEmpty());

	// 버튼을 클릭했으면 포커스가 버튼으로 넘어가 있다. 바로 다음 문장을 칠 수 있게 되돌린다.
	InputBox->SetKeyboardFocus();
}

UTextBlock* UConversationWidget::AppendBubble(const FString& Text, bool bFromUser)
{
	UTextBlock* Label = NewObject<UTextBlock>(this);
	Label->SetText(FText::FromString(Text));
	Label->SetAutoWrapText(true);
	Label->SetWrapTextAt(BubbleWidth);

	UBorder* Bubble = NewObject<UBorder>(this);
	Bubble->SetBrushColor(bFromUser ? UserBubbleColor : AiBubbleColor);
	Bubble->SetPadding(FMargin(12.f, 8.f));
	Bubble->SetContent(Label);

	// UWidget에 Slot 멤버가 있다. 같은 이름을 쓰면 경고가 에러로 올라온다.
	UScrollBoxSlot* BubbleSlot = Cast<UScrollBoxSlot>(ChatScroll->AddChild(Bubble));
	BubbleSlot->SetHorizontalAlignment(bFromUser ? HAlign_Right : HAlign_Left);
	BubbleSlot->SetPadding(FMargin(0.f, 4.f));

	// 이번 프레임 레이아웃이 끝난 뒤 Tick에서 소비된다. 새 말풍선 높이가 반영된 뒤
	// 계산되므로 여기서 불러도 맞는 위치로 간다.
	ChatScroll->ScrollToEnd();

	return Label;
}

void UConversationWidget::HandleConnectionChanged(bool bConnected)
{
	// 연결이 끊기면 SendAudio가 조용히 실패한다. 답이 영영 안 오므로 중간 결과를
	// 기다리는 채로 음성 모드가 멈춘다. 여기서 풀어준다.
	if (!bConnected && bVoiceMode)
	{
		StopVoiceMode();
	}

	RefreshSendEnabled();
}

void UConversationWidget::HandleBusyChanged(bool bBusy)
{
	RefreshSendEnabled();
}

void UConversationWidget::HandleSentenceStarted(const FSpeechFrame& Frame)
{
	// seq는 턴마다 0부터 다시 센다. 0이 아니면 같은 답변의 다음 문장이니 말풍선을
	// 새로 만들지 않고 이어 붙인다.
	if (Frame.Seq != 0 && LastAiLabel != nullptr)
	{
		LastAiLabel->SetText(FText::FromString(LastAiLabel->GetText().ToString() + TEXT(" ") + Frame.Text));
		ChatScroll->ScrollToEnd();
		return;
	}

	LastAiLabel = AppendBubble(Frame.Text, false);
}

void UConversationWidget::HandleServerError(const FString& Code, const FString& Message)
{
	AppendBubble(FString::Printf(TEXT("[%s] %s"), *Code, *Message), false);
	LastAiLabel = nullptr;

	// 오류로 끝난 요청은 받아적기를 돌려주지 않는다. 안 풀면 음성 모드가 멈춘다.
	PendingAudio = FMath::Max(0, PendingAudio - 1);
	bSendOnTranscript = false;
}

bool UConversationWidget::CanSend() const
{
	const UConversationClient* Client = GetGameInstance()->GetSubsystem<UConversationClient>();
	const USpeechQueue* Queue = GetGameInstance()->GetSubsystem<USpeechQueue>();

	return Client->IsConnected() && !Queue->IsBusy();
}

void UConversationWidget::RefreshSendEnabled()
{
	// InputBox는 건드리지 않는다. AI가 말하는 동안에도 다음 질문을 미리 쳐둘 수 있다.
	SendButton->SetIsEnabled(CanSend());

	// 연결이 없으면 녹음해봐야 보낼 데가 없다. 눌러도 아무 일이 안 일어나는 것보다
	// 못 누르게 하는 편이 낫다.
	MicButton->SetIsEnabled(GetGameInstance()->GetSubsystem<UConversationClient>()->IsConnected());
}
