// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ConversationClient.h"
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SpeechQueue.generated.h"

class UAudioComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBusyChanged, bool, bBusy);

/**
 * 수신한 speech 프레임을 문장 단위로 순서 재생한다.
 *
 * 서버는 문장을 만드는 대로 보내므로 프레임이 재생보다 빨리 도착한다. 그대로 재생하면
 * 소리가 겹친다. 앞 문장이 끝나야 다음 문장을 시작한다.
 *
 * UConversationClient의 OnSpeech를 직접 구독하므로 별도 배선이 필요 없다.
 */
UCLASS()
class UE5_CLIENT_API USpeechQueue : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 큐 뒤에 붙인다. 재생 중이 아니면 바로 시작한다. */
	UFUNCTION(BlueprintCallable, Category = "Conversation")
	void Enqueue(const FSpeechFrame& Frame);

	/** 재생 중인 문장을 끊고 대기 중인 문장을 버린다. */
	UFUNCTION(BlueprintCallable, Category = "Conversation")
	void Clear();

	UFUNCTION(BlueprintPure, Category = "Conversation")
	bool IsSpeaking() const;

	/**
	 * 사용자 입력을 막아야 하는 구간인가.
	 *
	 * 서버 응답 대기 중이거나 재생 중이면 참이다. 첫 프레임이 오기까지 수 초가 걸리는데
	 * 그 동안은 재생 중이 아니므로 IsSpeaking()만으로는 부족하다.
	 */
	UFUNCTION(BlueprintPure, Category = "Conversation")
	bool IsBusy() const { return bTurnPending || IsSpeaking(); }

	/** 현재 문장의 재생 경과 밀리초. 재생 중이 아니면 -1. viseme을 고를 때 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Conversation")
	int32 GetPlaybackMs() const;

	/** 현재 재생 중인 문장. 재생 중이 아니면 비어 있다. */
	UFUNCTION(BlueprintPure, Category = "Conversation")
	const FSpeechFrame& GetCurrentFrame() const { return CurrentFrame; }

	/** 문장 재생이 시작될 때. 자막을 여기서 바꾼다. */
	UPROPERTY(BlueprintAssignable, Category = "Conversation")
	FOnSpeechFrame OnSentenceStarted;

	/** IsBusy()가 바뀔 때. 입력 버튼 활성화를 여기에 물리면 된다. */
	UPROPERTY(BlueprintAssignable, Category = "Conversation")
	FOnBusyChanged OnBusyChanged;

private:
	UFUNCTION()
	void HandleTurnStarted();

	UFUNCTION()
	void HandleSpeech(const FSpeechFrame& Frame);

	UFUNCTION()
	void HandleTurnEnd(int32 Seq);

	UFUNCTION()
	void HandleServerError(const FString& Code, const FString& Message);

	UFUNCTION()
	void HandleAudioFinished();

	void PlayNext();

	/** IsBusy()가 바뀌었으면 브로드캐스트한다. */
	void UpdateBusy();

	/** 서버에 보냈고 아직 turn_end를 못 받았다. */
	bool bTurnPending = false;

	/** 마지막으로 브로드캐스트한 IsBusy() 값. */
	bool bLastBroadcastBusy = false;

	UPROPERTY()
	TObjectPtr<UAudioComponent> Current;

	TArray<FSpeechFrame> Pending;
	FSpeechFrame CurrentFrame;

	/**
	 * 재생 시작 시각. GetPlaybackMs가 여기서 경과를 계산한다.
	 *
	 * 오디오 클럭이 아니라 벽시계다. 문장 하나(수 초) 동안의 드리프트는 무시할
	 * 수준이지만, 입 모양이 소리보다 밀린다고 느껴지면 UAudioComponent의 실제
	 * 재생 위치를 쓰는 쪽으로 바꿔야 한다.
	 */
	double StartedAt = 0.0;
};
