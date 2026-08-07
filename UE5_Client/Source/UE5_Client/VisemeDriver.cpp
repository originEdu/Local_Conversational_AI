// Copyright Epic Games, Inc. All Rights Reserved.

#include "VisemeDriver.h"

#include "GameFramework/Actor.h"
#include "SpeechQueue.h"

namespace
{
	/**
	 * viseme 이름 -> 그 viseme의 커브값.
	 *
	 * 커브 이름은 에디터에서 확인한 실제 이름이다. 메타휴먼은 ARKit 블렌드셰이프 이름을
	 * 노출하지 않고 RigLogic 컨트롤 보드 이름을 쓴다. Face 스켈레톤의 Curves 패널에서
	 * 전체 목록을 볼 수 있다.
	 *
	 * 발음용 입 모양은 좌우 대칭이므로 L/R에 같은 값을 넣는다. Funnel과 Purse는
	 * 상하까지 나뉘어 UL/UR/DL/DR 네 개다.
	 *
	 * "sil"은 표에 없다. 못 찾은 viseme은 전부 0으로 수렴하므로 그게 곧 무음 자세다.
	 *
	 * 값은 설계 문서 6.3절의 초기 추정치다. 눈으로 보며 맞춰야 한다.
	 */
	const TMap<FName, TMap<FName, float>>& PoseTable()
	{
		static const TMap<FName, TMap<FName, float>> Table = []
		{
			TMap<FName, TMap<FName, float>> T;

			// ㅏ ㅑ ㅐ ㅒ ㅘ
			T.Add(TEXT("AA")).Add(TEXT("CTRL_expressions_jawOpen"), 0.70f);

			// ㅓ ㅔ ㅕ ㅖ ㅙ ㅝ ㅞ
			TMap<FName, float>& EH = T.Add(TEXT("EH"));
			EH.Add(TEXT("CTRL_expressions_jawOpen"), 0.40f);
			EH.Add(TEXT("CTRL_expressions_mouthStretchL"), 0.30f);
			EH.Add(TEXT("CTRL_expressions_mouthStretchR"), 0.30f);

			// ㅣ
			TMap<FName, float>& IH = T.Add(TEXT("IH"));
			IH.Add(TEXT("CTRL_expressions_jawOpen"), 0.15f);
			IH.Add(TEXT("CTRL_expressions_mouthCornerPullL"), 0.50f);
			IH.Add(TEXT("CTRL_expressions_mouthCornerPullR"), 0.50f);

			// ㅗ ㅛ
			TMap<FName, float>& OH = T.Add(TEXT("OH"));
			OH.Add(TEXT("CTRL_expressions_jawOpen"), 0.40f);
			OH.Add(TEXT("CTRL_expressions_mouthFunnelDL"), 0.60f);
			OH.Add(TEXT("CTRL_expressions_mouthFunnelDR"), 0.60f);
			OH.Add(TEXT("CTRL_expressions_mouthFunnelUL"), 0.60f);
			OH.Add(TEXT("CTRL_expressions_mouthFunnelUR"), 0.60f);

			// ㅜ ㅠ ㅚ ㅟ
			TMap<FName, float>& OU = T.Add(TEXT("OU"));
			OU.Add(TEXT("CTRL_expressions_mouthPurseDL"), 0.80f);
			OU.Add(TEXT("CTRL_expressions_mouthPurseDR"), 0.80f);
			OU.Add(TEXT("CTRL_expressions_mouthPurseUL"), 0.80f);
			OU.Add(TEXT("CTRL_expressions_mouthPurseUR"), 0.80f);

			// ㅡ ㅢ
			TMap<FName, float>& EU = T.Add(TEXT("EU"));
			EU.Add(TEXT("CTRL_expressions_mouthStretchL"), 0.40f);
			EU.Add(TEXT("CTRL_expressions_mouthStretchR"), 0.40f);

			// 초성/종성 ㅁ ㅂ ㅃ ㅍ
			TMap<FName, float>& PP = T.Add(TEXT("PP"));
			PP.Add(TEXT("CTRL_expressions_mouthLipsPressL"), 1.00f);
			PP.Add(TEXT("CTRL_expressions_mouthLipsPressR"), 1.00f);

			// 초성 ㄴ ㄷ ㄸ ㅌ ㄹ
			T.Add(TEXT("NN")).Add(TEXT("CTRL_expressions_jawOpen"), 0.20f);

			// 초성 ㄱ ㄲ ㅋ
			T.Add(TEXT("KK")).Add(TEXT("CTRL_expressions_jawOpen"), 0.25f);

			// 초성 ㅅ ㅆ ㅈ ㅉ ㅊ
			TMap<FName, float>& SS = T.Add(TEXT("SS"));
			SS.Add(TEXT("CTRL_expressions_jawOpen"), 0.20f);
			SS.Add(TEXT("CTRL_expressions_mouthStretchL"), 0.20f);
			SS.Add(TEXT("CTRL_expressions_mouthStretchR"), 0.20f);

			return T;
		}();

		return Table;
	}
}

UVisemeDriver::UVisemeDriver()
{
	PrimaryComponentTick.bCanEverTick = true;

	// 표에 나오는 커브 전부를 0으로 깔아둔다. 이 집합이 매 프레임 통째로 나간다.
	for (const TPair<FName, TMap<FName, float>>& Pose : PoseTable())
	{
		for (const TPair<FName, float>& Curve : Pose.Value)
		{
			Curves.Add(Curve.Key, 0.f);
		}
	}
}

void UVisemeDriver::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const TMap<FName, float>* Target = nullptr;

	if (const UGameInstance* GameInstance = GetOwner()->GetGameInstance())
	{
		if (const USpeechQueue* Queue = GameInstance->GetSubsystem<USpeechQueue>())
		{
			const int32 Ms = Queue->GetPlaybackMs();
			if (Ms >= 0)
			{
				for (const FVisemeSpan& Span : Queue->GetCurrentFrame().Visemes)
				{
					// 구간은 시간순이다. 아직 안 온 구간을 만나면 뒤도 다 미래다.
					if (Span.StartMs > Ms)
					{
						break;
					}
					if (Ms < Span.EndMs)
					{
						Target = PoseTable().Find(FName(Span.V));
						break;
					}
				}
			}
		}
	}

	// 목표가 없으면(무음, 재생 안 함, 모르는 viseme) 전부 0으로 돌아간다.
	for (TPair<FName, float>& Curve : Curves)
	{
		const float* Goal = Target != nullptr ? Target->Find(Curve.Key) : nullptr;
		Curve.Value = FMath::FInterpTo(Curve.Value, Goal != nullptr ? *Goal : 0.f, DeltaTime, InterpSpeed);
	}
}
