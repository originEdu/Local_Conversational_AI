// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ConversationClient.generated.h"

class FJsonObject;
class IWebSocket;
class UAudioComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogConversation, Log, All);

/** 서버가 보낸 viseme 구간 하나. start/end는 해당 문장 오디오 시작 기준 밀리초. */
USTRUCT(BlueprintType)
struct FVisemeSpan
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Conversation")
	FString V;

	UPROPERTY(BlueprintReadOnly, Category = "Conversation")
	int32 StartMs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Conversation")
	int32 EndMs = 0;
};

/** speech 프레임 하나. 문장 하나의 자막·오디오·viseme을 함께 담는다. */
USTRUCT(BlueprintType)
struct FSpeechFrame
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Conversation")
	int32 Seq = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Conversation")
	FString Text;

	/** 디코딩된 WAV 바이트. 서버가 audioBase64를 null로 보내면 비어 있다. */
	UPROPERTY(BlueprintReadOnly, Category = "Conversation")
	TArray<uint8> AudioWav;

	UPROPERTY(BlueprintReadOnly, Category = "Conversation")
	TArray<FVisemeSpan> Visemes;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSpeechFrame, const FSpeechFrame&, Frame);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTurnEnd, int32, Seq);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnServerError, const FString&, Code, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConnectionChanged, bool, bConnected);

/**
 * 서버와의 WebSocket 연결을 소유하고 수신 프레임을 파싱해 블루프린트로 넘긴다.
 *
 * 연결 하나가 대화 세션 하나다. 재연결하면 서버 쪽 대화 기록은 초기화된다.
 */
UCLASS()
class UE5_CLIENT_API UConversationClient : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** conv.Connect가 인자 없이 불렸을 때 쓰는 주소. */
	static const TCHAR* DefaultUrl;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Conversation")
	void Connect(const FString& Url);

	UFUNCTION(BlueprintCallable, Category = "Conversation")
	void Disconnect();

	UFUNCTION(BlueprintCallable, Category = "Conversation")
	void SendUserMessage(const FString& Text);

	UFUNCTION(BlueprintPure, Category = "Conversation")
	bool IsConnected() const;

	/**
	 * 프레임의 WAV 바이트를 그 자리에서 재생한다.
	 *
	 * 큐잉하지 않는다. 여러 번 부르면 소리가 겹친다. 순서 재생은 USpeechQueue가 맡는다.
	 * 재생 위치를 알아야 viseme을 맞출 수 있으므로 AudioComponent를 돌려준다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Conversation")
	UAudioComponent* PlaySpeech(const FSpeechFrame& Frame);

	UPROPERTY(BlueprintAssignable, Category = "Conversation")
	FOnSpeechFrame OnSpeech;

	UPROPERTY(BlueprintAssignable, Category = "Conversation")
	FOnTurnEnd OnTurnEnd;

	UPROPERTY(BlueprintAssignable, Category = "Conversation")
	FOnServerError OnServerError;

	UPROPERTY(BlueprintAssignable, Category = "Conversation")
	FOnConnectionChanged OnConnectionChanged;

private:
	void HandleConnected();
	void HandleConnectionError(const FString& Error);
	void HandleClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void HandleMessage(const FString& Message);

	bool ParseSpeechFrame(const TSharedRef<FJsonObject>& Root, FSpeechFrame& OutFrame) const;

	/** 스파이크 검증용. 계약 위반을 Error 로그로 남긴다. */
	void CheckFrame(const FSpeechFrame& Frame);

	/** 블루프린트 없이 PIE 콘솔에서 스파이크를 돌리기 위한 명령들. 소멸 시 자동 해제된다. */
	TArray<TUniquePtr<FAutoConsoleCommand>> ConsoleCommands;

	TSharedPtr<IWebSocket> Socket;

	/** conv.Play가 재생할 대상. 마지막으로 받은 speech 프레임이다. */
	FSpeechFrame LastFrame;

	/** 다음에 와야 할 speech 프레임의 seq. 턴이 끝나면 0으로 돌아간다. */
	int32 ExpectedSeq = 0;
};
