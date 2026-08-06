// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConversationWidget.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "SpeechQueue.h"

void UConversationWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UConversationClient* Client = GetGameInstance()->GetSubsystem<UConversationClient>();
	USpeechQueue* Queue = GetGameInstance()->GetSubsystem<USpeechQueue>();

	SendButton->OnClicked.AddDynamic(this, &UConversationWidget::HandleSendClicked);
	InputBox->OnTextCommitted.AddDynamic(this, &UConversationWidget::HandleTextCommitted);

	Client->OnConnectionChanged.AddDynamic(this, &UConversationWidget::HandleConnectionChanged);
	Client->OnServerError.AddDynamic(this, &UConversationWidget::HandleServerError);
	Queue->OnBusyChanged.AddDynamic(this, &UConversationWidget::HandleBusyChanged);
	Queue->OnSentenceStarted.AddDynamic(this, &UConversationWidget::HandleSentenceStarted);

	RefreshInputEnabled();

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

void UConversationWidget::HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	// 포커스가 빠질 때도 불린다. 엔터로 친 것만 보낸다.
	if (CommitMethod == ETextCommit::OnEnter)
	{
		Send();
	}
}

void UConversationWidget::Send()
{
	const FString Text = InputBox->GetText().ToString().TrimStartAndEnd();
	if (Text.IsEmpty())
	{
		return;
	}

	GetGameInstance()->GetSubsystem<UConversationClient>()->SendUserMessage(Text);
	InputBox->SetText(FText::GetEmpty());
}

void UConversationWidget::HandleConnectionChanged(bool bConnected)
{
	RefreshInputEnabled();
}

void UConversationWidget::HandleBusyChanged(bool bBusy)
{
	RefreshInputEnabled();
}

void UConversationWidget::HandleSentenceStarted(const FSpeechFrame& Frame)
{
	SubtitleText->SetText(FText::FromString(Frame.Text));
}

void UConversationWidget::HandleServerError(const FString& Code, const FString& Message)
{
	SubtitleText->SetText(FText::FromString(FString::Printf(TEXT("[%s] %s"), *Code, *Message)));
}

void UConversationWidget::RefreshInputEnabled()
{
	const UConversationClient* Client = GetGameInstance()->GetSubsystem<UConversationClient>();
	const USpeechQueue* Queue = GetGameInstance()->GetSubsystem<USpeechQueue>();

	const bool bCanType = Client->IsConnected() && !Queue->IsBusy();
	InputBox->SetIsEnabled(bCanType);
	SendButton->SetIsEnabled(bCanType);
}
