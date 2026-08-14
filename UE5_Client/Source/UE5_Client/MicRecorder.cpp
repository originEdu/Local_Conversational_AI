// Copyright Epic Games, Inc. All Rights Reserved.

#include "MicRecorder.h"

#include "Audio.h"
#include "ConversationClient.h"

namespace
{
	/**
	 * 이 진폭을 넘으면 말하는 중으로 본다.
	 *
	 * 마이크 감도와 방 소음에 따라 달라진다. 말해도 안 끊기면 낮추고, 가만히 있는데
	 * 안 끝나면 올린다. 콜백 한 블록(약 20ms)의 RMS와 비교한다.
	 */
	TAutoConsoleVariable<float> CVarSpeechLevel(
		TEXT("conv.SpeechLevel"),
		0.02f,
		TEXT("말로 판정할 최소 진폭(RMS). 0~1."));

	/**
	 * 이만큼 조용하면 말이 끝났다고 본다.
	 *
	 * 짧으면 문장 중간 숨 쉬는 데서 끊기고, 길면 다 말하고 기다리게 된다.
	 */
	TAutoConsoleVariable<int32> CVarSilenceMs(
		TEXT("conv.SilenceMs"),
		800,
		TEXT("말이 끝났다고 볼 무음 길이(밀리초)."));

	/**
	 * 켜면 마이크 입력 크기를 0.5초마다 로그에 찍는다.
	 *
	 * conv.SpeechLevel을 맞추려면 실제 숫자를 봐야 한다. 말할 때와 가만히 있을 때의
	 * 값을 보고 그 사이에서 고른다.
	 */
	TAutoConsoleVariable<int32> CVarMicDebug(
		TEXT("conv.MicDebug"),
		0,
		TEXT("마이크 입력 크기를 로그에 찍는다."));
}

void UMicRecorder::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ConsoleCommands.Add(MakeUnique<FAutoConsoleCommand>(
		TEXT("conv.Rec"),
		TEXT("녹음을 토글한다. 멈출 때 서버로 보내고 결과는 LogConversation에 찍힌다."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			if (IsRecording())
			{
				GetGameInstance()->GetSubsystem<UConversationClient>()->SendAudio(Stop());
			}
			else
			{
				Start();
			}
		})));
}

void UMicRecorder::Deinitialize()
{
	// 콜백이 this를 잡고 있다. 죽기 전에 스트림을 닫아야 한다.
	Stop();
	ConsoleCommands.Empty();
	Super::Deinitialize();
}

bool UMicRecorder::Start()
{
	if (Capture.IsStreamOpen())
	{
		return false;
	}

	{
		FScopeLock Lock(&SamplesLock);
		Samples.Reset();
	}

	// 스트림을 열기 전에 초기화한다. 열고 나면 콜백이 이미 돌고 있을 수 있다.
	bEndpointed = false;
	bHeardSpeech = false;
	SilentFrames = 0;
	DebugFrames = 0;
	DebugPeak = 0.f;

	const Audio::FAudioCaptureDeviceParams Params;
	const Audio::FOnAudioCaptureFunction OnCapture =
		[this](const void* Audio, int32 NumFrames, int32 NumChannels, int32, double, bool)
		{
			// 기본 인코딩이 FLOATING_POINT_32다. Params를 바꾸면 이 캐스트도 바꿔야 한다.
			Append(static_cast<const float*>(Audio), NumFrames, NumChannels);
		};

	if (!Capture.OpenAudioCaptureStream(Params, OnCapture, 1024))
	{
		UE_LOG(LogConversation, Error,
			TEXT("마이크 스트림을 열지 못했다. 윈도우 설정 > 개인 정보 > 마이크 권한을 확인해라."));
		return false;
	}

	SampleRate = Capture.GetSampleRate();

	if (!Capture.StartStream())
	{
		UE_LOG(LogConversation, Error, TEXT("마이크 스트림을 시작하지 못했다"));
		Capture.CloseStream();
		return false;
	}

	UE_LOG(LogConversation, Log, TEXT("녹음 시작 (%dHz)"), SampleRate);
	return true;
}

TArray<uint8> UMicRecorder::Snapshot()
{
	FScopeLock Lock(&SamplesLock);
	return BuildWav();
}

TArray<uint8> UMicRecorder::Stop()
{
	if (!Capture.IsStreamOpen())
	{
		return {};
	}

	Capture.StopStream();
	Capture.CloseStream();

	FScopeLock Lock(&SamplesLock);
	TArray<uint8> Wav = BuildWav();
	Samples.Reset();

	if (Wav.Num() == 0)
	{
		UE_LOG(LogConversation, Warning, TEXT("녹음된 소리가 없다"));
	}

	return Wav;
}

bool UMicRecorder::IsRecording() const
{
	return Capture.IsCapturing();
}

float UMicRecorder::GetRecordedSeconds() const
{
	FScopeLock Lock(&SamplesLock);
	return SampleRate > 0 ? Samples.Num() / static_cast<float>(SampleRate) : 0.f;
}

TArray<uint8> UMicRecorder::BuildWav() const
{
	if (Samples.Num() == 0)
	{
		return {};
	}

	TArray<uint8> Wav;
	SerializeWaveFile(Wav, reinterpret_cast<const uint8*>(Samples.GetData()),
		Samples.Num() * sizeof(int16), 1, SampleRate);
	return Wav;
}

void UMicRecorder::Append(const float* Audio, int32 NumFrames, int32 NumChannels)
{
	double SumSquares = 0.0;

	{
		FScopeLock Lock(&SamplesLock);
		Samples.Reserve(Samples.Num() + NumFrames);

		for (int32 Frame = 0; Frame < NumFrames; ++Frame)
		{
			float Sum = 0.f;
			for (int32 Channel = 0; Channel < NumChannels; ++Channel)
			{
				Sum += Audio[Frame * NumChannels + Channel];
			}

			const float Mono = FMath::Clamp(Sum / NumChannels, -1.f, 1.f);
			SumSquares += static_cast<double>(Mono) * Mono;

			// 클램프가 없으면 1을 넘는 샘플이 int16에서 감싸 돌아 딱딱 튀는 잡음이 된다.
			Samples.Add(static_cast<int16>(Mono * 32767.f));
		}
	}

	if (NumFrames <= 0 || SampleRate <= 0)
	{
		return;
	}

	const float Rms = FMath::Sqrt(SumSquares / NumFrames);
	const float Threshold = CVarSpeechLevel.GetValueOnAnyThread();

	if (CVarMicDebug.GetValueOnAnyThread() != 0)
	{
		DebugPeak = FMath::Max(DebugPeak, Rms);
		DebugFrames += NumFrames;
		if (DebugFrames >= SampleRate / 2)
		{
			UE_LOG(LogConversation, Log, TEXT("마이크 RMS %.4f (임계 %.4f, %s)"),
				DebugPeak, Threshold, DebugPeak >= Threshold ? TEXT("말") : TEXT("무음"));
			DebugFrames = 0;
			DebugPeak = 0.f;
		}
	}

	if (Rms >= Threshold)
	{
		bHeardSpeech = true;
		SilentFrames = 0;
		return;
	}

	// 말을 시작하기 전의 정적은 세지 않는다. 마이크를 켜자마자 끝났다고 하면 안 된다.
	if (!bHeardSpeech)
	{
		return;
	}

	SilentFrames += NumFrames;
	if (SilentFrames * 1000 >= CVarSilenceMs.GetValueOnAnyThread() * SampleRate)
	{
		bEndpointed = true;
	}
}
