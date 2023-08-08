// Copyright D5, Inc. All Rights Reserved.
#pragma once
#include "Subsystems/EngineSubsystem.h"
#include "LiteGPUSceneSubsystem.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
using HISMComponent = UHierarchicalInstancedStaticMeshComponent;
using HISMArray = TArray<HISMComponent*>;

UCLASS()
class LANDSCAPE_API ULiteGPUSceneSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()
public:
	//~UEngineSubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~End of UEngineSubsystem interface

	static ULiteGPUSceneSubsystem& Get() { return *GEngine->GetEngineSubsystem<ULiteGPUSceneSubsystem>(); }

	void BuildLiteGPUScenes();
};

UCLASS(hidecategories = (Collision, Brush, Attachment, Physics, Volume))
class LANDSCAPE_API ALiteGPUSceneManager : public AActor
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, meta = (WorldContext = WorldContext), Category = "ALiteGPUSceneManager")
	static ALiteGPUSceneManager* Get(const UObject* WorldContext);

	void BuildLiteGPUScene();

private:
	virtual void PreRegisterAllComponents() override;
	virtual void UnregisterAllComponents(bool bForReregister = false) override;
	void BuildHSIMComponents(HISMArray& Arr);
};