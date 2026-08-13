// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioCaptureCore.h"
#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/IConsoleManager.h"
#include "HAL/ThreadSafeBool.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MicRecorder.generated.h"

/**
 * 마이크를 켜고 끄며 녹음한 소리를 WAV 바이트로 돌려준다.
 *
 * 서버로 보내는 일은 하지 않는다. UConversationClient::SendAudio가 맡는다.
 *
 * 말이 끝났는지도 여기서 판단한다. 소리 크기만 본다 — 라이브러리를 붙일 만한 일이
 * 아니고, 마이크 버튼으로 이미 한 번 걸러진 뒤라 이 정도로 충분하다. 임계값은
 * conv.SpeechLevel과 conv.SilenceMs로 PIE 중에 바꿀 수 있다.
 *
 * 장치가 주는 샘플레이트를 그대로 쓴다. whisper는 어차피 16kHz로 리샘플하므로
 * 여기서 미리 맞출 이유가 없고, 장치가 원하는 레이트를 거부할 수도 있다.
 */
UCLASS()
class UE5_CLIENT_API UMicRecorder : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 녹음을 시작한다. 마이크를 열지 못하면 거짓을 돌려준다. */
	UFUNCTION(BlueprintCallable, Category = "Conversation")
	bool Start();

	/**
	 * 녹음을 멈추고 지금까지 받은 소리를 WAV로 돌려준다.
	 *
	 * 녹음 중이 아니었거나 아무 소리도 못 받았으면 빈 배열이다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Conversation")
	TArray<uint8> Stop();

	/**
	 * 멈추지 않고 지금까지 받은 소리를 WAV로 돌려준다.
	 *
	 * 말하는 도중에 중간 결과를 받아보려고 쓴다. 매번 처음부터 전부 담는다 —
	 * 뒷부분만 잘라 보내면 단어 중간이 잘려 인식이 망가진다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Conversation")
	TArray<uint8> Snapshot();

	UFUNCTION(BlueprintPure, Category = "Conversation")
	bool IsRecording() const;

	/**
	 * 말을 하다가 조용해졌다. 이제 보내면 된다는 뜻이다.
	 *
	 * 오디오 스레드가 세우고 게임 스레드가 폴링한다. 오디오 스레드에서 델리게이트를
	 * 쏘면 UMG를 그쪽에서 건드리게 된다.
	 *
	 * 소리를 낸 적이 없으면 절대 참이 되지 않는다. 안 그러면 마이크를 켜자마자
	 * 조용하다는 이유로 빈 녹음을 보낸다.
	 */
	UFUNCTION(BlueprintPure, Category = "Conversation")
	bool HasEndpointed() const { return bEndpointed; }

	/** 이번 녹음에서 말소리를 들은 적이 있다. 정적만 받아적으러 보내지 않으려고 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Conversation")
	bool HasHeardSpeech() const { return bHeardSpeech; }

private:
	/**
	 * 오디오 스레드에서 불린다. 채널을 모노로 섞고 16비트로 줄여 쌓으면서 소리 크기를 본다.
	 *
	 * whisper는 모노만 쓰고, 16비트면 전송량이 float의 절반이다. 어차피 버릴 정보를
	 * 여기서 버려야 큐에 쌓이는 양도 준다.
	 */
	void Append(const float* Audio, int32 NumFrames, int32 NumChannels);

	/** SamplesLock을 이미 잡은 채로 부른다. */
	TArray<uint8> BuildWav() const;

	/** 위젯 없이 PIE 콘솔에서 녹음을 확인하기 위한 명령. 소멸 시 자동 해제된다. */
	TArray<TUniquePtr<FAutoConsoleCommand>> ConsoleCommands;

	Audio::FAudioCapture Capture;

	/** Append는 오디오 스레드, Stop과 Snapshot은 게임 스레드다. */
	mutable FCriticalSection SamplesLock;
	TArray<int16> Samples;

	/** 스트림을 연 뒤 장치가 알려준 값. WAV 헤더에 그대로 쓴다. */
	int32 SampleRate = 0;

	FThreadSafeBool bEndpointed;
	FThreadSafeBool bHeardSpeech;

	/** 아래 셋은 오디오 스레드만 만진다. Start가 스트림을 열기 전에 초기화한다. */
	int32 SilentFrames = 0;
	int32 DebugFrames = 0;
	float DebugPeak = 0.f;
};
