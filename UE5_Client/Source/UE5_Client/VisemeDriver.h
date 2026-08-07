// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "VisemeDriver.generated.h"

/**
 * 재생 중인 문장의 viseme 타임라인을 메타휴먼 얼굴 커브값으로 바꾼다.
 *
 * 메타휴먼 액터에 붙이고, Face AnimBP의 Modify Curve 노드 CurveMap 핀에 GetCurveMap()을
 * 꽂는다. Modify Curve는 RigLogic 노드보다 앞에 둬야 한다 — RigLogic이 이 커브들을
 * 입력으로 읽어 관절과 블렌드셰이프를 만든다.
 *
 * 이 컴포넌트는 USpeechQueue만 읽는다. 서브시스템이 없으면(에디터 프리뷰 등) 아무것도
 * 하지 않는다.
 */
UCLASS(ClassGroup = (Conversation), meta = (BlueprintSpawnableComponent))
class UE5_CLIENT_API UVisemeDriver : public UActorComponent
{
	GENERATED_BODY()

public:
	UVisemeDriver();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Modify Curve 노드의 CurveMap 핀에 그대로 연결한다.
	 *
	 * 애님그래프는 워커 스레드에서 돈다. BlueprintThreadSafe가 없으면 컴파일 경고가 뜬다.
	 * 안전한 근거는 Curves의 키 집합이 생성자 이후 절대 변하지 않는다는 것이다 — 재해시도
	 * 재할당도 없고 Tick은 기존 항목의 float 값만 덮어쓴다. 값 하나를 이전 프레임 것으로
	 * 읽는 경우는 있어도 깨지지는 않는다.
	 *
	 * Curves에 런타임으로 항목을 넣거나 빼게 되면 이 전제가 무너진다. 그때는 여기 대신
	 * 애님 인스턴스 변수에 복사해두고 그래프가 그 변수를 읽게 바꿔야 한다.
	 */
	UFUNCTION(BlueprintPure, Category = "Conversation", meta = (BlueprintThreadSafe))
	const TMap<FName, float>& GetCurveMap() const { return Curves; }

	/**
	 * 목표값으로 수렴하는 속도.
	 *
	 * viseme 전환은 이산적이다. 그대로 적용하면 입이 딱딱 끊긴다. 값이 클수록 빠르고
	 * 각지며, 작을수록 부드럽지만 소리보다 밀린다. 눈으로 보며 맞춘다.
	 */
	UPROPERTY(EditAnywhere, Category = "Conversation")
	float InterpSpeed = 15.f;

private:
	/**
	 * 이번 프레임의 커브값 전체.
	 *
	 * 표에 나오는 모든 커브를 매 프레임 쓴다. 안 쓰는 커브를 빼면 마지막 값에 그대로
	 * 멈춰 버린다.
	 */
	TMap<FName, float> Curves;
};
