// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConversationClient.h"

#include "Audio.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "IWebSocket.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Base64.h"
#include "Sound/SoundWaveProcedural.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "WebSocketsModule.h"

DEFINE_LOG_CATEGORY(LogConversation);

// localhost는 Windows에서 IPv6 ::1로 먼저 풀린다. uvicorn은 IPv4에만 바인딩하므로
// 리터럴 IPv4를 쓴다. 콘솔에서 인자로 넘길 때는 FParse::Token이 '/'에서 끊으니
// 따옴표가 필요하다: conv.Connect "ws://..."
const TCHAR* UConversationClient::DefaultUrl = TEXT("ws://127.0.0.1:8000/ws");

void UConversationClient::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ConsoleCommands.Add(MakeUnique<FAutoConsoleCommand>(
		TEXT("conv.Connect"),
		TEXT("서버에 연결한다. 인자를 주면 그 주소로, 없으면 ws://localhost:8000/ws로."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			Connect(Args.Num() > 0 ? Args[0] : DefaultUrl);
		})));

	ConsoleCommands.Add(MakeUnique<FAutoConsoleCommand>(
		TEXT("conv.Say"),
		TEXT("서버에 user_message를 보낸다. 나머지 인자 전체가 문장이 된다."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			SendUserMessage(FString::Join(Args, TEXT(" ")));
		})));

	ConsoleCommands.Add(MakeUnique<FAutoConsoleCommand>(
		TEXT("conv.Play"),
		TEXT("마지막으로 받은 speech 프레임의 오디오를 재생한다."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>&)
		{
			PlaySpeech(LastFrame);
		})));

	ConsoleCommands.Add(MakeUnique<FAutoConsoleCommand>(
		TEXT("conv.Disconnect"),
		TEXT("서버 연결을 끊는다."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>&)
		{
			Disconnect();
		})));
}

void UConversationClient::Deinitialize()
{
	Disconnect();
	ConsoleCommands.Empty();
	Super::Deinitialize();
}

void UConversationClient::Connect(const FString& Url)
{
	// 연결에 실패한 소켓도 핸들은 남는다. 살아 있든 아니든 먼저 정리한다.
	// Disconnect가 재연결 의사를 지우므로 그 뒤에 다시 세운다.
	if (Socket.IsValid())
	{
		Disconnect();
	}

	bWantConnected = true;
	LastUrl = Url;

	// 모듈이 아직 안 올라왔을 수 있다. CreateWebSocket 전에 보장한다.
	FWebSocketsModule& Module = FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));

	UE_LOG(LogConversation, Log, TEXT("연결 시도: %s"), *Url);

	Socket = Module.CreateWebSocket(Url, TEXT(""));

	// 기본 상한이 1MB다. 오디오를 base64로 실어 보내므로 9초쯤 되는 문장이면 넘어서고,
	// 넘으면 lws가 코드 1009로 연결을 끊는다. 44100Hz 16bit mono 기준 16MB는 약 2분치다.
	Socket->SetTextMessageMemoryLimit(16 * 1024 * 1024);

	Socket->OnConnected().AddUObject(this, &UConversationClient::HandleConnected);
	Socket->OnConnectionError().AddUObject(this, &UConversationClient::HandleConnectionError);
	Socket->OnClosed().AddUObject(this, &UConversationClient::HandleClosed);
	Socket->OnMessage().AddUObject(this, &UConversationClient::HandleMessage);
	Socket->Connect();
}

void UConversationClient::Disconnect()
{
	// 의도한 종료다. 아래 Close가 부르는 OnClosed가 재연결을 걸지 않게 먼저 지운다.
	bWantConnected = false;
	GetGameInstance()->GetTimerManager().ClearTimer(ReconnectTimer);

	if (!Socket.IsValid())
	{
		return;
	}

	// 델리게이트를 먼저 끊는다. Close 이후 콜백이 파괴 중인 this를 건드리지 않게.
	Socket->OnConnected().RemoveAll(this);
	Socket->OnConnectionError().RemoveAll(this);
	Socket->OnClosed().RemoveAll(this);
	Socket->OnMessage().RemoveAll(this);

	if (Socket->IsConnected())
	{
		Socket->Close();
	}
	Socket.Reset();
}

void UConversationClient::SendUserMessage(const FString& Text)
{
	if (!IsConnected())
	{
		UE_LOG(LogConversation, Error, TEXT("연결되지 않았다. 메시지를 보내지 않는다: %s"), *Text);
		return;
	}

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("type"), TEXT("user_message"));
	Payload->SetStringField(TEXT("text"), Text);

	FString Serialized;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
	FJsonSerializer::Serialize(Payload, Writer);

	ExpectedSeq = 0;
	UE_LOG(LogConversation, Log, TEXT("전송: %s"), *Text);
	Socket->Send(Serialized);
	OnTurnStarted.Broadcast();
}

bool UConversationClient::IsConnected() const
{
	return Socket.IsValid() && Socket->IsConnected();
}

UAudioComponent* UConversationClient::PlaySpeech(const FSpeechFrame& Frame)
{
	if (Frame.AudioWav.Num() == 0)
	{
		UE_LOG(LogConversation, Error, TEXT("재생할 오디오가 없다 (seq=%d)"), Frame.Seq);
		return nullptr;
	}

	// USoundWaveProcedural은 원시 PCM만 먹는다. 엔진 파서로 WAV 헤더를 벗긴다.
	FWaveModInfo WaveInfo;
	FString Reason;
	if (!WaveInfo.ReadWaveInfo(Frame.AudioWav.GetData(), Frame.AudioWav.Num(), &Reason))
	{
		UE_LOG(LogConversation, Error, TEXT("WAV 헤더 파싱 실패 (%d바이트): %s"),
			Frame.AudioWav.Num(), *Reason);
		return nullptr;
	}

	const int32 SampleRate = static_cast<int32>(*WaveInfo.pSamplesPerSec);
	const int32 NumChannels = static_cast<int32>(*WaveInfo.pChannels);
	const int32 BitsPerSample = static_cast<int32>(*WaveInfo.pBitsPerSample);

	if (BitsPerSample != 16)
	{
		UE_LOG(LogConversation, Error, TEXT("16비트 PCM만 재생한다. %d비트가 왔다."), BitsPerSample);
		return nullptr;
	}

	const int32 BytesPerFrame = NumChannels * (BitsPerSample / 8);
	const int32 NumFrames = WaveInfo.SampleDataSize / BytesPerFrame;

	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>();
	Wave->SetSampleRate(SampleRate);
	Wave->NumChannels = NumChannels;
	Wave->Duration = static_cast<float>(NumFrames) / SampleRate;
	Wave->SoundGroup = SOUNDGROUP_Voice;
	Wave->bLooping = false;
	Wave->QueueAudio(WaveInfo.SampleDataStart, WaveInfo.SampleDataSize);

	UAudioComponent* Component = UGameplayStatics::SpawnSound2D(GetGameInstance(), Wave);
	if (Component == nullptr)
	{
		UE_LOG(LogConversation, Error, TEXT("SpawnSound2D가 컴포넌트를 만들지 못했다"));
		return nullptr;
	}

	UE_LOG(LogConversation, Log, TEXT("재생 seq=%d %dHz %d채널 %.3f초 (PCM %d바이트)"),
		Frame.Seq, SampleRate, NumChannels, Wave->Duration, WaveInfo.SampleDataSize);

	return Component;
}

void UConversationClient::HandleConnected()
{
	UE_LOG(LogConversation, Log, TEXT("연결됨"));
	ReconnectDelay = 1.f;
	OnConnectionChanged.Broadcast(true);
}

void UConversationClient::HandleConnectionError(const FString& Error)
{
	UE_LOG(LogConversation, Error, TEXT("연결 실패: %s"), *Error);
	OnConnectionChanged.Broadcast(false);
	ScheduleReconnect();
}

void UConversationClient::HandleClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	UE_LOG(LogConversation, Log, TEXT("연결 종료: code=%d clean=%d %s"), StatusCode, bWasClean ? 1 : 0, *Reason);
	OnConnectionChanged.Broadcast(false);
	ScheduleReconnect();
}

void UConversationClient::ScheduleReconnect()
{
	if (!bWantConnected)
	{
		return;
	}

	UE_LOG(LogConversation, Log, TEXT("%.0f초 후 재연결한다"), ReconnectDelay);

	GetGameInstance()->GetTimerManager().SetTimer(ReconnectTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]() { Connect(LastUrl); }),
		ReconnectDelay, false);

	// 서버가 죽어 있으면 1초마다 재시도해봐야 로그만 찬다. 15초까지 늘린다.
	ReconnectDelay = FMath::Min(ReconnectDelay * 2.f, 15.f);
}

void UConversationClient::HandleMessage(const FString& Message)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogConversation, Error, TEXT("JSON 파싱 실패 (%d자): %s"), Message.Len(), *Message.Left(200));
		return;
	}

	FString Type;
	if (!Root->TryGetStringField(TEXT("type"), Type))
	{
		UE_LOG(LogConversation, Error, TEXT("type 필드가 없다: %s"), *Message.Left(200));
		return;
	}

	if (Type == TEXT("speech"))
	{
		FSpeechFrame Frame;
		if (!ParseSpeechFrame(Root.ToSharedRef(), Frame))
		{
			return;
		}

		const int32 LastEnd = Frame.Visemes.Num() > 0 ? Frame.Visemes.Last().EndMs : 0;
		UE_LOG(LogConversation, Log, TEXT("speech seq=%d wav=%d바이트 viseme=%d개 (0~%dms) \"%s\""),
			Frame.Seq, Frame.AudioWav.Num(), Frame.Visemes.Num(), LastEnd, *Frame.Text);

		CheckFrame(Frame);
		LastFrame = Frame;
		OnSpeech.Broadcast(Frame);
		return;
	}

	if (Type == TEXT("turn_end"))
	{
		int32 Seq = 0;
		Root->TryGetNumberField(TEXT("seq"), Seq);
		UE_LOG(LogConversation, Log, TEXT("turn_end seq=%d"), Seq);

		if (Seq != ExpectedSeq)
		{
			UE_LOG(LogConversation, Error, TEXT("turn_end의 seq가 %d가 아니라 %d다"), ExpectedSeq, Seq);
		}
		ExpectedSeq = 0;
		OnTurnEnd.Broadcast(Seq);
		return;
	}

	if (Type == TEXT("error"))
	{
		FString Code;
		FString Detail;
		Root->TryGetStringField(TEXT("code"), Code);
		Root->TryGetStringField(TEXT("message"), Detail);
		UE_LOG(LogConversation, Error, TEXT("서버 오류 %s: %s"), *Code, *Detail);
		ExpectedSeq = 0;
		OnServerError.Broadcast(Code, Detail);
		return;
	}

	UE_LOG(LogConversation, Warning, TEXT("모르는 프레임 타입: %s"), *Type);
}

bool UConversationClient::ParseSpeechFrame(const TSharedRef<FJsonObject>& Root, FSpeechFrame& OutFrame) const
{
	Root->TryGetNumberField(TEXT("seq"), OutFrame.Seq);
	Root->TryGetStringField(TEXT("text"), OutFrame.Text);

	// audioBase64는 TTS가 실패하면 null로 온다. 그때는 자막만 쓴다.
	FString AudioBase64;
	if (Root->TryGetStringField(TEXT("audioBase64"), AudioBase64) && !AudioBase64.IsEmpty())
	{
		if (!FBase64::Decode(AudioBase64, OutFrame.AudioWav))
		{
			UE_LOG(LogConversation, Error, TEXT("audioBase64 디코딩 실패 (seq=%d, %d자)"),
				OutFrame.Seq, AudioBase64.Len());
			return false;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Spans = nullptr;
	if (Root->TryGetArrayField(TEXT("visemes"), Spans))
	{
		OutFrame.Visemes.Reserve(Spans->Num());
		for (const TSharedPtr<FJsonValue>& Value : *Spans)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object))
			{
				continue;
			}

			FVisemeSpan Span;
			(*Object)->TryGetStringField(TEXT("v"), Span.V);
			(*Object)->TryGetNumberField(TEXT("start"), Span.StartMs);
			(*Object)->TryGetNumberField(TEXT("end"), Span.EndMs);
			OutFrame.Visemes.Add(Span);
		}
	}

	return true;
}

void UConversationClient::CheckFrame(const FSpeechFrame& Frame)
{
	if (Frame.Seq != ExpectedSeq)
	{
		UE_LOG(LogConversation, Error, TEXT("seq가 %d가 아니라 %d다"), ExpectedSeq, Frame.Seq);
	}
	ExpectedSeq = Frame.Seq + 1;

	int32 PreviousEnd = -1;
	for (const FVisemeSpan& Span : Frame.Visemes)
	{
		if (Span.StartMs >= Span.EndMs)
		{
			UE_LOG(LogConversation, Error, TEXT("빈 구간: %s %d~%d"), *Span.V, Span.StartMs, Span.EndMs);
		}
		if (PreviousEnd >= 0 && Span.StartMs < PreviousEnd)
		{
			UE_LOG(LogConversation, Error, TEXT("구간 겹침: %s가 %d에서 시작하는데 앞 구간이 %d에서 끝난다"),
				*Span.V, Span.StartMs, PreviousEnd);
		}
		PreviousEnd = Span.EndMs;
	}

	// WAV는 최소한 44바이트 헤더는 있어야 한다. 큰 메시지가 잘려 오는지 여기서 걸린다.
	if (Frame.AudioWav.Num() > 0 && Frame.AudioWav.Num() < 44)
	{
		UE_LOG(LogConversation, Error, TEXT("WAV가 %d바이트뿐이다. 잘렸다."), Frame.AudioWav.Num());
	}
}
