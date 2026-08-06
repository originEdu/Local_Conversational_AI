// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "ConversationClient.h"
#include "CoreMinimal.h"
#include "ConversationWidget.generated.h"

class UButton;
class UEditableTextBox;
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

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> InputBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> SendButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SubtitleText;

private:
	UFUNCTION()
	void HandleSendClicked();

	UFUNCTION()
	void HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleConnectionChanged(bool bConnected);

	UFUNCTION()
	void HandleBusyChanged(bool bBusy);

	UFUNCTION()
	void HandleSentenceStarted(const FSpeechFrame& Frame);

	UFUNCTION()
	void HandleServerError(const FString& Code, const FString& Message);

	void Send();

	/** 연결됐고 AI가 말하는 중이 아닐 때만 입력을 받는다. */
	void RefreshInputEnabled();
};
