#pragma once
#include "RHI.h"
#include "CoreMinimal.h"
#include "VertexFactory.h"
#include "LocalVertexFactory.h"
#include "ShaderCore.h"
#include "MeshMaterialShader.h"
#include "Components/MeshComponent.h"
#include "LiteGPUSceneComponent.generated.h"

struct FLiteGPUSceneInstance
{
	int64 IDWithinComponent;
	FInt32Vector2 TilePosition;
	FFloat16 XOffset, YOffset;
	float ZOffset;
	FFloat16 XRot, YRot, ZRot, Scale;
};

class FLiteGPUSceneProxy : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FLiteGPUSceneProxy(ULiteGPUSceneComponent* Component, ERHIFeatureLevel::Type InFeatureLevel);
	virtual ~FLiteGPUSceneProxy();

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;

	virtual bool CanBeOccluded() const override { return false; }
	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	virtual void PostUpdateBeforePresent(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily);
	uint32 GetAllocatedSize(void) const { return FPrimitiveSceneProxy::GetAllocatedSize(); }
	void DrawMeshBatches(int32 ViewIndex, const FSceneView* View, const FSceneViewFamily& ViewFamily, FMeshElementCollector& Collector) const;
	void Init_RenderingThread();
	ENGINE_API bool IsInitialized() const;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class ENGINE_API ULiteGPUSceneComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	void ManagedTick(float DeltaTime);

	/** Add multiple instances to this component. Transform is given in local space of this component unless bWorldSpace is set. */
	UFUNCTION(BlueprintCallable, Category = "Components|LiteGPUScene")
	virtual TArray<int64> AddInstancesWS(const TArray<FTransform>& InstanceTransforms);

	/** Get the transform for the instance specified. Instance is returned in local space of this component unless bWorldSpace is set.  Returns True on success. */
	UFUNCTION(BlueprintCallable, Category = "Components|LiteGPUScene")
	bool GetInstanceTransformWS(int64 InstanceIndex, FTransform& OutInstanceTransform) const;

	UFUNCTION(BlueprintCallable, Category = "Components|LiteGPUScene")
	virtual bool RemoveInstances(const TArray<int64>& InstancesToRemove);

	UFUNCTION(BlueprintCallable, Category = "Components|LiteGPUScene")
	virtual bool ClearInstances();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	TObjectPtr<class UStaticMesh> GetUnderlyingMesh() { return StaticMesh; }

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = StaticMesh, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UStaticMesh> StaticMesh;

	int64 NextInstanceID = 0;
	TMap<int64, int64> IDToSlotMap;
	TArray64<FLiteGPUSceneInstance> Instances;
	TArray64<FLiteGPUSceneInstance> RemovedInstances;
};