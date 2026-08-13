// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UE5_Client : ModuleRules
{
	public UE5_Client(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });

		// AudioCapture는 플러그인 모듈이다. 여기 없으면 WASAPI 구현이 등록되지 않아
		// Audio::FAudioCapture가 스트림을 열지 못한다.
		PrivateDependencyModuleNames.AddRange(new string[] { "WebSockets", "Json", "UMG", "Slate", "SlateCore", "AudioCapture", "AudioCaptureCore" });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
