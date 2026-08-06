// Copyright Epic Games, Inc. All Rights Reserved.

#include "SpeechQueue.h"

#include "Components/AudioComponent.h"

void USpeechQueue::Initialize(FSubsystemCollectionBase& Collection)
{
	// 클라이언트가 먼저 만들어져야 델리게이트를 붙일 수 있다.
	UConversationClient* Client = Collection.InitializeDependency<UConversationClient>();
	Super::Initialize(Collection);

	Client->OnTurnStarted.AddDynamic(this, &USpeechQueue::HandleTurnStarted);
	Client->OnSpeech.AddDynamic(this, &USpeechQueue::HandleSpeech);
	Client->OnTurnEnd.AddDynamic(this, &USpeechQueue::HandleTurnEnd);
	Client->OnServerError.AddDynamic(this, &USpeechQueue::HandleServerError);
}

void USpeechQueue::Deinitialize()
{
	Clear();
	Super::Deinitialize();
}

void USpeechQueue::HandleTurnStarted()
{
	bTurnPending = true;
	UpdateBusy();
}

void USpeechQueue::HandleSpeech(const FSpeechFrame& Frame)
{
	Enqueue(Frame);
}

void USpeechQueue::HandleTurnEnd(int32 Seq)
{
	// 서버는 끝났지만 아직 재생이 남아 있을 수 있다. IsBusy가 그걸 감안한다.
	bTurnPending = false;
	UpdateBusy();
}

void USpeechQueue::HandleServerError(const FString& Code, const FString& Message)
{
	bTurnPending = false;
	UpdateBusy();
}

void USpeechQueue::Enqueue(const FSpeechFrame& Frame)
{
	Pending.Add(Frame);

	if (!IsSpeaking())
	{
		PlayNext();
	}
	else
	{
		UE_LOG(LogConversation, Log, TEXT("대기 seq=%d (앞에 %d문장)"), Frame.Seq, Pending.Num() - 1);
	}
}

void USpeechQueue::Clear()
{
	Pending.Empty();

	if (Current != nullptr)
	{
		Current->OnAudioFinished.RemoveAll(this);
		Current->Stop();
		Current = nullptr;
	}

	CurrentFrame = FSpeechFrame();
	StartedAt = 0.0;
	bTurnPending = false;
	UpdateBusy();
}

void USpeechQueue::UpdateBusy()
{
	const bool bBusy = IsBusy();
	if (bBusy == bLastBroadcastBusy)
	{
		return;
	}

	bLastBroadcastBusy = bBusy;
	UE_LOG(LogConversation, Log, TEXT("입력 %s"), bBusy ? TEXT("잠금") : TEXT("해제"));
	OnBusyChanged.Broadcast(bBusy);
}

bool USpeechQueue::IsSpeaking() const
{
	return Current != nullptr;
}

int32 USpeechQueue::GetPlaybackMs() const
{
	if (!IsSpeaking())
	{
		return -1;
	}

	return static_cast<int32>((FPlatformTime::Seconds() - StartedAt) * 1000.0);
}

void USpeechQueue::PlayNext()
{
	if (Pending.Num() == 0)
	{
		CurrentFrame = FSpeechFrame();
		UE_LOG(LogConversation, Log, TEXT("큐가 비었다"));
		UpdateBusy();
		return;
	}

	CurrentFrame = Pending[0];
	Pending.RemoveAt(0);

	UConversationClient* Client = GetGameInstance()->GetSubsystem<UConversationClient>();
	Current = Client->PlaySpeech(CurrentFrame);

	if (Current == nullptr)
	{
		// 오디오가 없거나 재생에 실패했다. 자막만 내보내고 다음 문장으로 넘어간다.
		UE_LOG(LogConversation, Warning, TEXT("seq=%d 재생 실패, 건너뛴다"), CurrentFrame.Seq);
		OnSentenceStarted.Broadcast(CurrentFrame);
		PlayNext();
		return;
	}

	StartedAt = FPlatformTime::Seconds();
	Current->OnAudioFinished.AddDynamic(this, &USpeechQueue::HandleAudioFinished);
	UpdateBusy();
	OnSentenceStarted.Broadcast(CurrentFrame);
}

void USpeechQueue::HandleAudioFinished()
{
	// SpawnSound2D가 만든 컴포넌트는 재생이 끝나면 스스로 파괴된다. 붙잡고 있으면 안 된다.
	Current = nullptr;
	StartedAt = 0.0;

	PlayNext();
}
