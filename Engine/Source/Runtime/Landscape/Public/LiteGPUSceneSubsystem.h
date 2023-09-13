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

class FLiteGPUSceneProxy : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FLiteGPUSceneProxy(ULiteGPUSceneRenderComponent* Component, ERHIFeatureLevel::Type InFeatureLevel);
	virtual ~FLiteGPUSceneProxy();

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;

	virtual bool CanBeOccluded() const override { return false; }
	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	virtual void PostUpdateBeforePresent(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily);
	uint32 GetAllocatedSize(void) const { return FPrimitiveSceneProxy::GetAllocatedSize(); }
	void DrawMeshBatches(int32 ViewIndex, const FSceneView* View, const FSceneViewFamily& ViewFamily, FMeshElementCollector& Collector) const;
	void Init_RenderingThread();
	LANDSCAPE_API bool IsInitialized() const;

	TSharedPtr<FLiteGPUScene> Scene = nullptr;
	FLiteGPUSceneVertexFactory* pGPUDrivenVertexFactory = nullptr;
	FLiteGPUSceneVertexFactoryUserData* pVFUserData = nullptr;
};

UCLASS()
class LANDSCAPE_API ULiteGPUSceneRenderComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	friend class ALiteGPUSceneManager;
	friend class FLiteGPUSceneProxy;

	ALiteGPUSceneManager* Owner = nullptr;
	TSharedPtr<FLiteGPUScene> Scene = nullptr;
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