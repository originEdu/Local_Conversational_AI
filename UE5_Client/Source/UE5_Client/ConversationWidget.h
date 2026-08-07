// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "ConversationClient.h"
#include "CoreMinimal.h"
#include "ConversationWidget.generated.h"

class UButton;
class UMultiLineEditableTextBox;
class UScrollBox;
class UTextBlock;

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

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UMultiLineEditableTextBox> InputBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> SendButton;

	/** 대화 로그. 말풍선은 여기에 런타임으로 붙는다. 안을 비워둬라. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UScrollBox> ChatScroll;

	UPROPERTY(EditAnywhere, Category = "Conversation")
	FLinearColor UserBubbleColor = FLinearColor(0.13f, 0.32f, 0.55f);

	UPROPERTY(EditAnywhere, Category = "Conversation")
	FLinearColor AiBubbleColor = FLinearColor(0.16f, 0.16f, 0.18f);

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
	void HandleConnectionChanged(bool bConnected);

	UFUNCTION()
	void HandleBusyChanged(bool bBusy);

	UFUNCTION()
	void HandleSentenceStarted(const FSpeechFrame& Frame);

	UFUNCTION()
	void HandleServerError(const FString& Code, const FString& Message);

	void Send();

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
};
