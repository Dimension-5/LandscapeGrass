#pragma once
#include "RHI.h"
#include "CoreMinimal.h"
#include "VertexFactory.h"
#include "LocalVertexFactory.h"
#include "ShaderCore.h"
#include "MeshMaterialShader.h"
#include "Components/MeshComponent.h"
#include "LiteGPUScene.h"
#include "LiteGPUSceneComponent.generated.h"

class ULiteGPUSceneComponent;
struct FLiteGPUSceneVertexFactory;
struct FLiteGPUSceneVertexFactoryUserData;

struct FLiteGPUSceneInstance
{
	int64 IDWithinComponent;
	UE::Math::TTransform<float> Transform;
};

struct ILiteGPUSceneInstanceHandler
{
	virtual void OnAdd(TObjectPtr<ULiteGPUSceneComponent>, const TArrayView<FLiteGPUSceneInstance> Instances) = 0;
	virtual void OnRemove(TObjectPtr<ULiteGPUSceneComponent>, const TArrayView<FLiteGPUSceneInstance> Instances) = 0;
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
	TArray64<FLiteGPUSceneInstance> PersistantInstances;

	enum OPS
	{
		OP_ADD,
		OP_REMOVE
	};
	struct Op
	{
		TArray64<FLiteGPUSceneInstance> Instances;
		OPS OP;
	};
	TQueue<TSharedPtr<Op>, EQueueMode::Spsc> PendingOps;
	FCriticalSection OpsMutex;

	friend class ALiteGPUSceneManager;
	ILiteGPUSceneInstanceHandler* Handler;
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
	uint32 GetAllocatedSize(void) const { return FPrimitiveSceneProxy::GetAllocatedSize(); }
	void DrawMeshBatches(int32 ViewIndex, const FSceneView* View, const FSceneViewFamily& ViewFamily, FMeshElementCollector& Collector) const;
	void Init_RenderingThread();
	ENGINE_API bool IsInitialized() const;

	TSharedPtr<FLiteGPUScene> Scene = nullptr;
	FLiteGPUSceneVertexFactory* pGPUDrivenVertexFactory = nullptr;
	FLiteGPUSceneVertexFactoryUserData* pVFUserData = nullptr;
	FSceneInterface* CachedSceneInterface = nullptr;
	TMap<FMaterialRenderProxy*, TArray<int32>> MaterialToSectionIDsMap;
	TArray<FLiteGPUSceneMeshSectionInfo> AllSections;
};

UCLASS()
class ENGINE_API ULiteGPUSceneRenderComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	FPrimitiveSceneProxy* CreateSceneProxy() override;
	void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials /* = false */) const;

	friend class ALiteGPUSceneManager;
	friend class FLiteGPUSceneProxy;

	TSharedPtr<FLiteGPUScene> Scene = nullptr;
};

struct FLiteGPUSceneVertexFactoryDataType : public FLocalVertexFactory::FDataType
{
	/** Most Important, per instance offset in indirect draw only make effect in input assamble binding */
	FVertexStreamComponent InstanceIndicesComponent;
};

struct FLiteGPUSceneVertexFactoryShaderParameters : public FLocalVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FLiteGPUSceneVertexFactoryShaderParameters, NonVirtual);
	void Bind(const FShaderParameterMap& ParameterMap);

	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* View,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
	) const;

private:
	LAYOUT_FIELD(FShaderResourceParameter, VertexFetch_PerInstanceTransformParameter);
	LAYOUT_FIELD(FShaderResourceParameter, VertexFetch_PerInstanceScale);
};

struct FLiteGPUSceneMeshVertexBuffer;

struct ENGINE_API FLiteGPUSceneVertexFactory : public FLocalVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FLiteGPUSceneVertexFactory);
	FLiteGPUSceneVertexFactory(ERHIFeatureLevel::Type InFeatureLevel);

	/** Init function that should only be called on render thread. */
	/** Initialization */
	void InitVertexFactory(const FLiteGPUSceneMeshVertexBuffer* VertexBuffer, const FVertexBuffer* InstanceIndiceBuffer);
	/** Initialization */
	void Init_RenderThread(const FLiteGPUSceneMeshVertexBuffer* VertexBuffer, const FVertexBuffer* InstanceIndiceBuffer);

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;

	/**
	* Should we cache the material's shadertype on this platform with this vertex factory?
	*/
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
	{
		return (Parameters.MaterialParameters.bIsUsedWithInstancedStaticMeshes || Parameters.MaterialParameters.bIsSpecialEngineMaterial)
			// && Parameters.MaterialParameters.TessellationMode == MTM_NoTessellation
			&& FLocalVertexFactory::ShouldCompilePermutation(Parameters);
	}

	/**
	* Modify compile environment to enable instancing
	* @param OutEnvironment - shader compile environment to modify
	*/
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("MANUAL_VERTEX_FETCH"), TEXT("0"));
		OutEnvironment.SetDefine(TEXT("USE_INSTANCING"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("USE_INSTANCING_EMULATED"), TEXT("0"));
		FLocalVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	/**
	* An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
	*/
	void SetData(const FLiteGPUSceneVertexFactoryDataType& InData)
	{
		FLocalVertexFactory::Data = InData;
		Data = InData;
		UpdateRHI(FRHICommandListImmediate::Get());
	}

	/**
	* Copy the data from another vertex factory
	* @param Other - factory to copy from
	*/
	void Copy(const FLiteGPUSceneVertexFactory& Other)
	{
		FLiteGPUSceneVertexFactory* VertexFactory = this;
		const FLiteGPUSceneVertexFactoryDataType* DataCopy = &Other.Data;
		ENQUEUE_RENDER_COMMAND(FLiteGPUSceneVertexFactoryCopyData)(
			[VertexFactory, DataCopy](FRHICommandList& RHICmdList)
			{
				VertexFactory->Data = *DataCopy;
			}
		);
		BeginUpdateResourceRHI(this);
	}

	static FVertexFactoryShaderParameters* ConstructShaderParameters(EShaderFrequency ShaderFrequency)
	{
		return ShaderFrequency == SF_Vertex ? new FLiteGPUSceneVertexFactoryShaderParameters() : NULL;
	}

	bool IsInitialized() { return bInitialized; }

private:
	bool bInitialized;
	FLiteGPUSceneVertexFactoryDataType Data;
};

struct FLiteGPUSceneVertexFactoryUserData
{
	class FLiteGPUSceneProxy* SceneProxy;
};