// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "ConversationClient.h"
#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "ConversationWidget.generated.h"

class UButton;
class UMultiLineEditableTextBox;
class UScrollBox;
class UTextBlock;

/**
 * 음성 입력 방식.
 *
 * 마이크를 열고 실시간으로 받아적는 부분은 둘이 같다. 다른 건 언제 보내느냐다.
 */
UENUM(BlueprintType)
enum class EConversationMode : uint8
{
	/** 말을 멈추면 알아서 보낸다. 엔터를 칠 필요가 없다. */
	Realtime UMETA(DisplayName = "실시간 대화"),

	/** 마이크 버튼을 다시 누를 때까지 녹음하고, 받아적은 글자는 입력란에 남긴다. */
	PushToTalk UMETA(DisplayName = "푸시 투 토크"),
};

/**
 * 대화 UI의 C++ 베이스. 블루프린트에서 상속해 레이아웃만 만든다.
 *
 * 블루프린트에 아래 이름의 위젯이 반드시 있어야 한다. 없으면 컴파일이 실패한다.
 * 이름 어긋남이 런타임 크래시가 아니라 컴파일 에러로 드러나는 편이 낫다.
 *
 * 연결은 UConversationClient가 소유한다. 이 위젯은 델리게이트를 구독하고 입력을
 * 전달할 뿐이다. 위젯이 사라져도 대화 세션은 유지된다.
 */
UCLASS(Abstract)
class UE5_CLIENT_API UConversationWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/**
	 * 엔터로 보내고 Shift+엔터로 줄을 바꾼다.
	 *
	 * Slate의 SMultiLineEditableText는 ModiferKeyForNewLine으로 이걸 지원하지만 UMG가
	 * 그 값을 노출하지 않는다. 프리뷰 키 이벤트는 루트에서 포커스된 위젯 쪽으로 내려오므로
	 * 텍스트 박스가 엔터를 먹기 전에 여기서 가로챌 수 있다.
	 */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** 음성 모드의 상태 기계를 돌린다. 음성 모드가 꺼져 있으면 아무것도 하지 않는다. */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UMultiLineEditableTextBox> InputBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> SendButton;

	/**
	 * 음성 모드를 켜고 끈다.
	 *
	 * 켜져 있는 동안 말하면 글자가 실시간으로 InputBox에 찍히고, 말을 멈추면 알아서
	 * 보낸다. 엔터를 치거나 보내기를 누를 필요가 없다.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> MicButton;

	/** 대화 로그. 말풍선은 여기에 런타임으로 붙는다. 안을 비워둬라. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UScrollBox> ChatScroll;

	/**
	 * 모드 선택 화면. 시작할 때 떠 있고 하나를 고르면 숨는다.
	 *
	 * 최상위 Overlay의 자식으로 두고 화면 전체를 덮어야 한다. 뒤쪽 입력이 그대로
	 * 통하면 모드를 고르기 전에 대화가 시작된다.
	 *
	 * 타입이 UWidget인 건 Border든 VerticalBox든 받기 위해서다.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> ModePanel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RealtimeButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> PushToTalkButton;

	/** 누르면 모드 선택 화면이 다시 뜬다. 대화 중에 바꿀 수 있어야 한다. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ModeButton;

	/**
	 * 지금 무슨 상태인지 한 줄로 보여준다.
	 *
	 * 녹음 중이면 "녹음 중", 녹음을 멈추고 받아적기를 기다리는 중이면 "변환 중",
	 * 그 외에는 현재 모드 이름이 뜬다.
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ModeLabel;

	UPROPERTY(EditAnywhere, Category = "Conversation")
	FLinearColor UserBubbleColor = FLinearColor(0.13f, 0.32f, 0.55f);

	UPROPERTY(EditAnywhere, Category = "Conversation")
	FLinearColor AiBubbleColor = FLinearColor(0.16f, 0.16f, 0.18f);

	/** 음성 모드일 때 마이크 버튼 색. 켜져 있는지 눈에 보여야 다시 눌러 끌 수 있다. */
	UPROPERTY(EditAnywhere, Category = "Conversation")
	FLinearColor RecordingColor = FLinearColor(0.75f, 0.15f, 0.15f);

	/**
	 * 음성 모드지만 마이크가 닫혀 있을 때 색.
	 *
	 * AI가 말하는 동안과 방금 한 말을 처리하는 동안이다. 지금 말해도 안 들어간다는 걸
	 * 알려야 사용자가 헛말을 안 한다.
	 */
	UPROPERTY(EditAnywhere, Category = "Conversation")
	FLinearColor PausedColor = FLinearColor(0.35f, 0.35f, 0.38f);

	/**
	 * 중간 결과를 받아보는 간격(초).
	 *
	 * 짧을수록 글자가 빨리 따라오지만 GPU를 그만큼 더 쓴다. 앞 요청의 답이 오기 전에는
	 * 다음 것을 보내지 않으므로, 인식이 느려지면 간격도 알아서 벌어진다.
	 */
	UPROPERTY(EditAnywhere, Category = "Conversation")
	float PartialInterval = 1.f;

	/**
	 * 받아적은 글자를 보여주고 나서 보내기까지 기다리는 시간(초).
	 *
	 * 0이면 글자를 넣은 그 프레임에 입력란이 비워져 화면에 뜬 적이 없게 된다.
	 * 사용자가 뭐라고 인식됐는지 읽을 만큼은 줘야 한다.
	 */
	UPROPERTY(EditAnywhere, Category = "Conversation")
	float SendDelay = 0.7f;

	/**
	 * 한 발화의 최대 길이(초). 넘으면 거기서 끊는다.
	 *
	 * 실시간 모드는 무음이 끊어주지만 쉬지 않고 말하면 안 끊긴다. 푸시 투 토크는
	 * 버튼을 다시 누르기 전까지 무한정 쌓인다. 중간 결과를 보낼 때마다 녹음 전체를
	 * 다시 보내는 구조라 길어지면 전송량이 감당이 안 된다.
	 */
	UPROPERTY(EditAnywhere, Category = "Conversation")
	float MaxRecordSeconds = 60.f;

	/**
	 * 말풍선 최대 너비(픽셀).
	 *
	 * 좌/우 정렬이면 말풍선이 내용 크기를 따라간다. 이 값이 없으면 긴 문장이 줄바꿈 없이
	 * 화면 밖까지 늘어난다.
	 */
	UPROPERTY(EditAnywhere, Category = "Conversation")
	float BubbleWidth = 420.f;

private:
	UFUNCTION()
	void HandleSendClicked();

	UFUNCTION()
	void HandleMicClicked();

	UFUNCTION()
	void HandleRealtimeClicked();

	UFUNCTION()
	void HandlePushToTalkClicked();

	UFUNCTION()
	void HandleModeClicked();

	UFUNCTION()
	void HandleTranscript(const FString& Text);

	UFUNCTION()
	void HandleConnectionChanged(bool bConnected);

	UFUNCTION()
	void HandleBusyChanged(bool bBusy);

	UFUNCTION()
	void HandleSentenceStarted(const FSpeechFrame& Frame);

	UFUNCTION()
	void HandleServerError(const FString& Code, const FString& Message);

	void Send();

	/** 모드를 정하고 선택 화면을 숨긴다. 실시간이면 그 자리에서 음성 모드를 켠다. */
	void SelectMode(EConversationMode NewMode);

	/** 마이크를 열고 음성 모드를 켠다. 연결이 없으면 아무것도 하지 않는다. */
	void StartVoiceMode();

	/** 음성 모드를 끄고 마이크를 닫는다. 녹음 중이던 소리는 버린다. */
	void StopVoiceMode();

	/**
	 * 실시간 모드에서 한 발화를 마감한다. 받아적기가 오는 대로 전송한다.
	 *
	 * 무음을 감지한 시점에는 아직 마지막 오디오가 서버에 안 갔다. 그래서 여기서
	 * 바로 보내지 않고 bSendOnTranscript를 세워 받아적기 도착 시점으로 미룬다.
	 */
	void FinishRealtimeTurn();

	/**
	 * 푸시 투 토크에서 한 발화를 마감한다. 받아적어 입력란에 남기고 전송은 하지 않는다.
	 *
	 * 보낼지 말지는 사용자가 정한다. 그게 이 모드의 이유다.
	 */
	void FinishPushToTalk();

	/** 마이크가 열렸는지를 버튼 색과 입력란 안내문으로 보여준다. */
	void ShowMicOpen(bool bOpen);

	/**
	 * ModeLabel에 지금 뭘 하고 있는지 쓴다. 녹음 중 > 변환 중 > 모드 이름 순으로 우선한다.
	 *
	 * 매 프레임 불리므로 글자가 바뀔 때만 실제로 세운다.
	 */
	void RefreshModeLabel();

	/**
	 * 말풍선 하나를 만들어 로그 끝에 붙이고 맨 아래로 스크롤한다.
	 *
	 * 사용자는 오른쪽, AI는 왼쪽에 붙는다. 안의 UTextBlock을 돌려준다 — 같은 답변의
	 * 다음 문장을 이어 붙일 때 쓴다.
	 *
	 * 말풍선마다 위젯 두 개를 만든다. 수천 개가 쌓이면 느려지므로 그때 UListView로
	 * 바꾼다. 지금 대화 길이로는 문제가 안 된다.
	 */
	UTextBlock* AppendBubble(const FString& Text, bool bFromUser);

	/** 연결됐고 AI가 말하는 중이 아니어야 보낼 수 있다. 타이핑은 언제든 된다. */
	bool CanSend() const;

	void RefreshSendEnabled();

	/** 마지막 AI 말풍선의 텍스트. 같은 턴의 뒷문장을 여기에 이어 붙인다. */
	UPROPERTY()
	TObjectPtr<UTextBlock> LastAiLabel;

	EConversationMode Mode = EConversationMode::Realtime;

	/** 음성 모드가 켜져 있다. 마이크가 실제로 열려 있는지와는 다르다 — AI가 말하는 동안은 닫는다. */
	bool bVoiceMode = false;

	/**
	 * 무음으로 말이 끝났다. 다음 받아적기가 오면 곧장 보낸다.
	 *
	 * 보내기를 받아적기 완료까지 미뤄야 하는 이유는, 무음을 감지한 시점에는 아직
	 * 서버가 마지막 오디오를 받아적지 않았기 때문이다.
	 */
	bool bSendOnTranscript = false;

	/**
	 * 보내놓고 아직 받아적기가 안 온 오디오 요청 수.
	 *
	 * 두 가지에 쓴다. 0이 아니면 다음 중간 결과를 보내지 않는다 — 인식이 느려지면
	 * 간격이 알아서 벌어진다. 그리고 녹음이 끝난 뒤에도 0이 아니면 아직 변환 중이다.
	 *
	 * bool 하나로는 후자가 안 된다. 마지막 녹음을 보낼 때 앞서 보낸 중간 결과가 아직
	 * 안 왔을 수 있고, 그 답이 먼저 도착하면 "변환 중" 표시가 일찍 꺼진다.
	 */
	int32 PendingAudio = 0;

	/** 받아적은 글자를 띄워둔 채 SendDelay를 세는 중. 그동안 마이크를 다시 열지 않는다. */
	bool bPendingSend = false;

	float PartialTimer = 0.f;

	FTimerHandle SendTimer;

	/**
	 * 녹음을 시작할 때 InputBox에 있던 글자.
	 *
	 * 중간 결과는 매번 처음부터 다시 받아적은 전체 문장이라 InputBox를 통째로 갈아친다.
	 * 사용자가 미리 쳐둔 글자를 지우지 않으려면 앞에 다시 붙여야 한다.
	 */
	FString TextBeforeRecording;

	/** ModeLabel에 마지막으로 쓴 글자. 안 바뀌었으면 SetText를 건너뛴다. */
	FString LastModeLabelText;
};
