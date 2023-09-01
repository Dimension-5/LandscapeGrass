#pragma once
#include "RHI.h"
#include "CoreMinimal.h"
#include "VertexFactory.h"
#include "LocalVertexFactory.h"
#include "ShaderCore.h"
#include "MeshMaterialShader.h"
#include "Components/MeshComponent.h"
#include "LiteGPUSceneComponent.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class ENGINE_API ULiteGPUSceneComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	void ManagedTick(float DeltaTime);

	/** Add multiple instances to this component. Transform is given in local space of this component unless bWorldSpace is set. */
	UFUNCTION(BlueprintCallable, Category = "Components|LiteGPUScene")
	virtual TArray<int32> AddInstances(const TArray<FTransform>& InstanceTransforms, bool bShouldReturnIndices, bool bWorldSpace = false);

	/** Get the transform for the instance specified. Instance is returned in local space of this component unless bWorldSpace is set.  Returns True on success. */
	UFUNCTION(BlueprintCallable, Category = "Components|LiteGPUScene")
	bool GetInstanceTransform(int32 InstanceIndex, FTransform& OutInstanceTransform, bool bWorldSpace = false) const;

	UFUNCTION(BlueprintCallable, Category = "Components|LiteGPUScene")
	virtual bool RemoveInstances(const TArray<int32>& InstancesToRemove);

	UFUNCTION(BlueprintCallable, Category = "Components|LiteGPUScene")
	virtual bool ClearInstances();
private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = StaticMesh, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UStaticMesh> StaticMesh;
};