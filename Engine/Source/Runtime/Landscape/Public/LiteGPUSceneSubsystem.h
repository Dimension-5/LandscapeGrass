// Copyright D5, Inc. All Rights Reserved.
#pragma once
#include "LiteGPUScene.h"
#include "Subsystems/EngineSubsystem.h"
#include "Components/LiteGPUSceneComponent.h"
#include "LiteGPUSceneSubsystem.generated.h"

class ULiteGPUSceneComponent;
class ULiteGPUSceneRenderComponent;
class ULiteGPUSceneSubsystem;
class ALiteGPUSceneManager;

struct FLiteGPUSceneSubsystemTickFunction : public FTickFunction
{
	FLiteGPUSceneSubsystemTickFunction()
		: FTickFunction(), Manager(nullptr)
	{
	}
	virtual ~FLiteGPUSceneSubsystemTickFunction() {}

	// Begin FTickFunction overrides
	virtual void ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed) override;
	// End FTickFunction overrides

	ALiteGPUSceneManager* Manager = nullptr;
};

UCLASS()
class LANDSCAPE_API ULiteGPUSceneSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()
public:
	//~ UEngineSubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End of UEngineSubsystem interface

	void BuildLiteGPUScenes();
};

UCLASS(BlueprintType, Blueprintable, hidecategories = (Collision, Brush, Attachment, Physics, Volume))
class LANDSCAPE_API ALiteGPUSceneManager : public AActor, public ILiteGPUSceneInstanceHandler
{
	GENERATED_BODY()
public:
	virtual void OnAdd(TObjectPtr<ULiteGPUSceneComponent>, const TArrayView<FLiteGPUSceneInstance> Instances) override;
	virtual void OnRemove(TObjectPtr<ULiteGPUSceneComponent>, const TArrayView<FLiteGPUSceneInstance> Instances) override;
	
	UFUNCTION(BlueprintPure, meta = (WorldContext = WorldContext), Category = "ALiteGPUSceneManager")
	static ALiteGPUSceneManager* Get(const UObject* WorldContext);
	
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "ALiteGPUSceneManager")
	TArray<TObjectPtr<ULiteGPUSceneComponent>> Components;

	TMap<TObjectPtr<ULiteGPUSceneComponent>, TMap<int64, int64>> CIDToPID;

	void BuildLiteGPUScene();
	TSharedPtr<FLiteGPUScene> Scene;
private:
	virtual void PreRegisterAllComponents() override;
	virtual void UnregisterAllComponents(bool bForReregister = false) override;

	friend class ULiteGPUSceneSubsystem;
	void DoTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);

	friend struct FLiteGPUSceneSubsystemTickFunction;
	FLiteGPUSceneSubsystemTickFunction TickFunction;

	UPROPERTY()
	TObjectPtr<ULiteGPUSceneRenderComponent> RenderComp = nullptr;
};