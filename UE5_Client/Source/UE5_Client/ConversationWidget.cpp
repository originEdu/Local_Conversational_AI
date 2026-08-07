// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConversationWidget.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "SpeechQueue.h"

void UConversationWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UConversationClient* Client = GetGameInstance()->GetSubsystem<UConversationClient>();
	USpeechQueue* Queue = GetGameInstance()->GetSubsystem<USpeechQueue>();

	SendButton->OnClicked.AddDynamic(this, &UConversationWidget::HandleSendClicked);

	Client->OnConnectionChanged.AddDynamic(this, &UConversationWidget::HandleConnectionChanged);
	Client->OnServerError.AddDynamic(this, &UConversationWidget::HandleServerError);
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
	// 서브시스템은 위젯보다 오래 산다. 안 끊으면 죽은 위젯으로 델리게이트가 날아온다.
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UConversationClient* Client = GameInstance->GetSubsystem<UConversationClient>())
		{
			Client->OnConnectionChanged.RemoveAll(this);
			Client->OnServerError.RemoveAll(this);
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
}
