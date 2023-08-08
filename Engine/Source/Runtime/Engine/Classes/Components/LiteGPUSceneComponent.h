#pragma once
#include "RHI.h"
#include "CoreMinimal.h"
#include "VertexFactory.h"
#include "LocalVertexFactory.h"
#include "ShaderCore.h"
#include "MeshMaterialShader.h"
#include "Components/MeshComponent.h"
#include "LiteGPUSceneComponent.generated.h"

DECLARE_STATS_GROUP(TEXT("LiteGPUScene"), STATGROUP_LiteGPUScene, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("LiteGPUScene Generate Instances"), STAT_GenerateInstance, STATGROUP_LiteGPUScene);
DECLARE_CYCLE_STAT(TEXT("LiteGPUScene Update"), STAT_LiteGPUSceneUpdate, STATGROUP_LiteGPUScene);
DECLARE_CYCLE_STAT(TEXT("LiteGPUScene RemoveInstance Group"), STAT_RemoveInstanceGroup, STATGROUP_LiteGPUScene);
DECLARE_CYCLE_STAT(TEXT("LiteGPUScene Resource Upload "), STAT_ResourceUpload, STATGROUP_LiteGPUScene);

// #define ENABLE_LITE_GPU_SCENE_DEBUG (!UE_BUILD_TEST &&!UE_BUILD_SHIPPING)
// A temporary workaround, not sure why above flag is not correctly triggerred and passed when cooking
#define ENABLE_LITE_GPU_SCENE_DEBUG 0
#define MAX_QUEUED_FRAME_COUNT	3	

class FLiteGPUSceneBufferManagerInterface
{
public:
	FLiteGPUSceneBufferManagerInterface() {}
	virtual void UpdateUploader(FRHICommandList& RHICmdList, class FLiteGPUSceneInstanceData* InstanceDataPtr, int32 DirtyCount) {}
};
class FLiteGPUSceneBufferManager;

struct FInstancedLiteGPUSceneData
{
public:
	FInstancedLiteGPUSceneData()
	{
		TransformsData = FMatrix::Identity;
		MeshIndex = INDEX_NONE;
		Scale = FVector::OneVector;
	};
public:
	FMatrix				TransformsData;
	int32				MeshIndex;
	FVector				Scale;

	inline bool operator==(const FInstancedLiteGPUSceneData& Data) const
	{
		return TransformsData == Data.TransformsData && MeshIndex == Data.MeshIndex && Scale == Data.Scale;
	}
};

USTRUCT()
struct ENGINE_API FLiteGPUSceneMeshVertex
{
	GENERATED_USTRUCT_BODY()
public:
	FLiteGPUSceneMeshVertex() :
		Position(FVector3f(0, 0, 0)),
		TangentX(FVector3f(1, 0, 0)),
		TangentZ(FVector3f(0, 0, 1)),
		Color(FColor(255, 255, 255)),
		TextureFirstCoordinate(FVector2f::ZeroVector),
		TextureSecondCoordinate(FVector2f::ZeroVector)
	{
		// basis determinant default to +1.0
		// but here we not this represents in byte, where TangentZ is a FPackedNormal, see PackedNormal.h
		TangentZ.Vector.W = 127;
	}
	FLiteGPUSceneMeshVertex(const FLiteGPUSceneMeshVertex& Another)
	{
		Position = Another.Position;
		TextureFirstCoordinate = Another.TextureFirstCoordinate;
		TextureSecondCoordinate = Another.TextureSecondCoordinate;
		TangentX = Another.TangentX;
		TangentZ = Another.TangentZ;
		Color = Another.Color;
	}

	FLiteGPUSceneMeshVertex(const FVector3f& InPosition) :
		Position(InPosition),
		TangentX(FVector3f(1, 0, 0)),
		TangentZ(FVector3f(0, 0, 1)),
		Color(FColor(255, 255, 255)),
		TextureFirstCoordinate(FVector2f::ZeroVector),
		TextureSecondCoordinate(FVector2f::ZeroVector)
	{
		// basis determinant default to +1.0
		// but here we not this represents in byte, where TangentZ is a FPackedNormal, see PackedNormal.h
		TangentZ.Vector.W = 127;
	}

	FLiteGPUSceneMeshVertex(const FVector3f& InPosition, const FVector2f& InUV) :
		Position(InPosition),
		TangentX(FVector3f(1, 0, 0)),
		TangentZ(FVector3f(0, 0, 1)),
		Color(FColor(255, 255, 255)),
		TextureFirstCoordinate(InUV),
		TextureSecondCoordinate(InUV)
	{
		// basis determinant default to +1.0
		// but here we not this represents in byte, where TangentZ is a FPackedNormal, see PackedNormal.h
		TangentZ.Vector.W = 127;
	}

	FLiteGPUSceneMeshVertex(const FVector3f& InPosition, const FVector3f& InTangentX, const FVector3f& InTangentZ, const FVector2f& InTexCoord, const FVector2f& InTexCoord2, const FVector2f& InTexCoord3, const FColor& InColor) :
		Position(InPosition),
		TangentX(InTangentX),
		TangentZ(InTangentZ),
		Color(InColor),
		TextureFirstCoordinate(InTexCoord),
		TextureSecondCoordinate(InTexCoord2)
	{
		// basis determinant default to +1.0
		// but here we not this represents in byte, where TangentZ is a FPackedNormal, see PackedNormal.h
		TangentZ.Vector.W = 127;
	}

	FORCEINLINE float GetBasisDeterminantSign(const FVector3f& XAxis, const FVector3f& YAxis, const FVector3f& ZAxis)
	{
		FMatrix44f Basis(
			FPlane4f(XAxis, 0),
			FPlane4f(YAxis, 0),
			FPlane4f(ZAxis, 0),
			FPlane4f(0, 0, 0, 1)
		);
		return (Basis.Determinant() < 0) ? -1.0f : +1.0f;
	}

	/*Set Tangent by input*/
	void SetTangents(const FVector3f& InTangentX, const FVector3f& InTangentY, const FVector3f& InTangentZ)
	{
		TangentX = InTangentX;
		TangentZ = InTangentZ;
		// store determinant of basis in w component of normal vector
		// TangentZ is a FPackedNormal, see PackedNormal.h
		TangentZ.Vector.W = GetBasisDeterminantSign(InTangentX, InTangentY, InTangentZ) < 0.0f ? -127 : 127;
	}

	/*Genrate Y Tangent*/
	FVector3f GetTangentY()
	{
		FVector3f TanX = TangentX.ToFVector3f();
		FVector3f TanZ = TangentZ.ToFVector3f();
		//  TangentZ is a FPackedNormal, see PackedNormal.h
		return (TanZ ^ TanX) * ((float)TangentZ.Vector.W / 127.5f - 1.0f);
	}

	FLiteGPUSceneMeshVertex& operator =(const FLiteGPUSceneMeshVertex& Another)
	{
		Position = Another.Position;
		TextureFirstCoordinate = Another.TextureFirstCoordinate;
		TextureSecondCoordinate = Another.TextureSecondCoordinate;
		TangentX = Another.TangentX;
		TangentZ = Another.TangentZ;
		Color = Another.Color;
		return *this;
	}

	UPROPERTY()
	FVector3f Position;
	UPROPERTY()
	FPackedNormal TangentX;
	UPROPERTY()
	FPackedNormal TangentZ;
	UPROPERTY()
	FColor Color;
	UPROPERTY()
	FVector2f TextureFirstCoordinate;
	UPROPERTY()
	FVector2f TextureSecondCoordinate;
};

USTRUCT()
struct ENGINE_API FLiteGPUSceneMeshPatchInfo
{
	GENERATED_USTRUCT_BODY()

	FLiteGPUSceneMeshPatchInfo();
	FLiteGPUSceneMeshPatchInfo(const FLiteGPUSceneMeshPatchInfo& Another);
	FLiteGPUSceneMeshPatchInfo& operator = (const FLiteGPUSceneMeshPatchInfo& Another);

public:
	UPROPERTY()
	FString MeshName;
	UPROPERTY()
	uint32 MeshIndex;
	UPROPERTY()
	uint32 LODIndex;
	UPROPERTY()
	uint32 SectionIndex;
	UPROPERTY()
	uint32 FirstVertexOffset;
	UPROPERTY()
	uint32 FirstIndexOffset;
	UPROPERTY()
	uint32 IndexCount;

	UPROPERTY()
	uint32 PatchID;
	UPROPERTY()
	uint32 VertexCount;
	UPROPERTY()
	uint32 MaterialIndex;
	UPROPERTY()
	float ScreenSizeMin;
	UPROPERTY()
	float ScreenSizeMax;
};

class FLiteGPUSceneProxyVisibilityData
{
public:
	FLiteGPUSceneProxyVisibilityData();
	~FLiteGPUSceneProxyVisibilityData();

	void InitVisibilityData(int32 InPatchNum, bool bInGPUCulling, int32 InPerMatchMaxIndexNum);

	void InitRenderingThread();

	int32 TotalPatchNum;
	bool bInitialized;

	// GPU Culling

	FVertexBuffer* pInstanceIndexVertexBuffer;

	struct FRWBuffer* RWIndirectDrawDispatchIndiretBuffer;
	struct FRWBuffer* RWInstanceIndiceBuffer;
	struct FRWBuffer* RWIndirectDrawBuffer;

	struct FRWBuffer* RWGPUUnCulledInstanceBuffer;
	// Store whether the instance pass the depth test
	struct FRWBuffer* RWDepthDrawTestResultBuffer;
	// store the location for the above passed instance in UnCulled InstanceBuffer
	struct FRWBuffer* RWGPUUnCulledInstanceScreenSize;
	struct FRWBuffer* RWGPUUnCulledInstanceNum;

	struct FRWBuffer* RWDispatchStagingBuffer;
	struct FRWBuffer* RWGPUUnCulledInstanceIndirectParameters;

	FConvexVolume ViewFrustum;
	FVector ViewLocation;
	FVector ViewForward;
	FMatrix ProjMatrix;
	FSphere ShadowBounds;

	int32 MaxGPUInstanceToDraw;

	bool bGPUCulling;
	bool bDirty;

	bool bFirstGPUCullinged;
	bool bCreateImposterBuffer;
	uint32 GPUByte;
	int32 PerPatchMaxNum;
#if ENABLE_LITE_GPU_SCENE_DEBUG
	struct FRWBuffer* RWGPUCulledInstanceBuffer;
	struct FRWBuffer* RWGPUCulledInstanceDebugIndirectParameters;
#endif
};
typedef TSharedPtr<FLiteGPUSceneProxyVisibilityData, ESPMode::ThreadSafe> FLiteGPUSceneProxyVisibilityDataPtr;

class FLiteGPUSceneInstanceData
{
public:
	FLiteGPUSceneInstanceData();
	~FLiteGPUSceneInstanceData();

	void InitInstanceData(class ULiteGPUSceneComponent* Comp, const TArray<FLiteGPUSceneMeshPatchInfo>& InAllPatches);

	/*
	 * Because of HISM enter our vision scope, prepare the upload data for later gpu buffer updating
	 */
	void UpdateIncreasedData(class ULiteGPUSceneComponent* Comp, class UHierarchicalInstancedStaticMeshComponent* Key);
	/*
	 * Because of HISM leave our vision scope, prepare the upload data for later gpu buffer updating
	 */
	void UpdateRemoveData(class ULiteGPUSceneComponent* Comp, class UHierarchicalInstancedStaticMeshComponent* Key);
	
	void UpdateInstanceData(class ULiteGPUSceneComponent* Comp);

	void Initialize_RenderingThread();

	void Release_RenderingThread();

	FReadBuffer* AllPatchInfoBuffer;
	FRWBuffer* RWPatchCountBuffer;
	/*
	 * Since it's instancing , A patch might be drawed multiple times
	 * This buffer stores the draw number of each patch (section at certain lod)
	 * PatchCount(PatchID) = RWPatchCountCopyBuffer[PatchID]
	 */
	FRWBuffer* RWPatchCountCopyBuffer;
	/*
	 * Since it's instancing , A patch might be drawed multiple times
	 * We organize the draw of same patch together and close
	 * This buffer stores the starting offset of one type of patch
	 * PatchStartOffset(PatchID) =RWPatchCountOffsetBuffer[PatchID] 
	 *
	 * Therefore, the data we need to draw a patch of "PatchID" locates at
	 * [PatchStartOffset(PatchID),PatchStartOffset(PatchID)+PatchCount(PatchID)]
	 */
	FRWBuffer* RWPatchCountOffsetBuffer;
	/*
	 * This buffer is a size-1 uint which helps calculate the PatchStartOffset
	 *
	 *		uint PatchStartOffset;
	 *		InterlockedAdd(RWNextPatchCountOffsetBuffer[0], PatchCount, PatchStartOffset);
	 *		RWPatchCountOffsetBuffer[UniqueID] = PatchStartOffset;
	 *
	 */
	FRWBuffer* RWNextPatchCountOffsetBuffer;

	FRWBuffer* RWDrawedTriangleCountBuffer;
#if ENABLE_LITE_GPU_SCENE_DEBUG
	class FRHIAsyncGPUBufferReadback* RWDrawedTriangleCountBufferAsyncReadBack;
#endif
	
	FReadBuffer* MeshAABBBuffer;

	/*
	 * This Buffer stores the instance id of patch draw
	 * InstanceID(PatchID,i-th draw) = RWInstanceIndiceBuffer[PatchStartOffset(PatchID)+i]
	 */
	FRWBuffer* RWGPUInstanceIndicesBuffer;
	FRWBuffer* RWInstanceScaleBuffer;
	FRWBuffer* RWInstanceTransformBuffer;
	FRWBuffer* RWInstanceTypeBuffer;
	FRWBuffer* RWInstancePatchNumBuffer;
	FRWBuffer* RWInstancePatchIDsBuffer;

	TArray<FVector4> UploadInstanceScaleData;
	TArray<FVector4> UploadInstanceTransformData;
	TArray<uint8>  UploadInstanceFoliageTypeData;
	/*
	 * Array which  stores the AAbb data of the foliages,
	 * the length is equal foliage_num x 2 x sizeof(Vector4),
	 * 2 stands for the BottomLeft and Top Right Pos of the AABB box
	 */
	TArray<uint8> PatchAABBData;

	/*
	 *  The Indices of the instance that are dirty
	*/
	TArray<uint32> UpdateDirtyInstanceIndices;

	int32 DirtyInstanceNum;

	int32 InstanceNum;
	int32 FoliageTypeNum;
	int32 AllPatchNum;

	uint32 CullingInstancedNum;
	int32 CulledAllPatchNum;

	/*
	 * All the patch data 
	 */
	TArray<FLiteGPUSceneMeshPatchInfo> AllPatches;
	/*
	 * Array the stores the ids of Patch on Instance k that needed to be upload
	 * The length is  PerPatchMaxNum x len(instances_to_upload)
	 */
	TArray<uint8> UploadInstancePatchIDs;
	/*
	 * Array the stores the num of Patch on Instance k that needed to be upload
	 * The length is a multiplier of PerPatchMaxNum
	 */
	TArray<uint8> UploadInstancePatchNum;
	/*
	 * Number of instances that references the patch
	 */
	TArray<int32> PatchInstanceNum;
	/*
	 * Array which  stores Mesh Index of each patch,
	 * the length is equal foliage_num x lod_num x section_num
	 */
	TArray<int32> PatchInfoMeshIndices;
	uint32 GPUByte;

	/*
	 * Array that stores the corresponding HISM of the Instance at Index k
	 */
	TArray<class UHierarchicalInstancedStaticMeshComponent*> InstancedKeyComponents;

	/*
	 * Is it ok now to update on this frame
	 * This is not expected to modify by others
	 */
	bool bUpdateFrame;

	int32 AllInstanceIndexNum;
	/*
	 * Actually, this stores the max lod numbers among the gpu scene meshes
	 */
	int32 PerPatchMaxNum;
	bool bInitialized;
};

struct FLiteGPUSceneMeshVertexBuffer final : public FVertexBuffer
{
	void InitRHI(FRHICommandListBase& RHICmdList);

	TArray<FLiteGPUSceneMeshVertex> Vertices;
	int32 VerticesNum;
};

struct FLiteGPUSceneMeshIndexBuffer final  : public FIndexBuffer
{
	void InitRHI(FRHICommandListBase& RHICmdList);

	TArray<uint32> Indices;
	int32 IndicesNum;
};

struct FLiteGPUSceneVertexIndexBuffer
{
public:
	FLiteGPUSceneVertexIndexBuffer()
		: VertexBuffer(nullptr), IndexBuffer(nullptr)
		, UsedBytes(0), VertexNum(0)
		, IndiceNum(0), bIntialized(false)
	{

	}

	~FLiteGPUSceneVertexIndexBuffer()
	{
		Release_RenderingThread();
	}

	void InitVertexIndexBuffer(const TArray<FLiteGPUSceneMeshVertex>& Vertices, const TArray<uint32>& Indices);
	void Release_RenderingThread();

	FLiteGPUSceneMeshVertexBuffer* VertexBuffer;
	FLiteGPUSceneMeshIndexBuffer* IndexBuffer;
	uint64 UsedBytes;
	uint32 VertexNum;
	uint32 IndiceNum;
	bool bIntialized;
};

typedef TSharedPtr<FLiteGPUSceneInstanceData, ESPMode::ThreadSafe> FLiteGPUSceneInstanceDataPtr;
typedef TSharedPtr<FLiteGPUSceneVertexIndexBuffer, ESPMode::ThreadSafe> FLiteGPUSceneVertexIndexBufferPtr;

struct FLiteGPUSceneVertexFactoryDataType : public FLocalVertexFactory::FDataType
{
	/** Most Important, per instance offset in indirect draw only make effect in input assamble binding */
	FVertexStreamComponent InstanceIndicesComponent;
	// TODO : this fading is not used here...
	FVertexStreamComponent InstanceDitherFadingComponent;
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

struct FLiteGPUSceneVertexFactory : public FLocalVertexFactory
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
		OutEnvironment.SetDefine(TEXT("USE_INSTANCING_SKELETALMESH_GPUDRIVEN"), TEXT("0"));
		OutEnvironment.SetDefine(TEXT("USE_INSTANCING"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("USE_INSTANCING_SKELETALMESH"), TEXT("0"));
		OutEnvironment.SetDefine(TEXT("USE_INSTANCING_SKELETALMESH_LOD0"), TEXT("0"));
		OutEnvironment.SetDefine(TEXT("USE_INSTANCING_EMULATED"), TEXT("0"));
		OutEnvironment.SetDefine(TEXT("USE_DITHERED_LOD_TRANSITION_FOR_INSTANCED"), 1);
		FLocalVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	/**
	* An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
	*/
	void SetData(const FLiteGPUSceneVertexFactoryDataType& InData)
	{
		FLocalVertexFactory::Data = InData;
		Data = InData;
		UpdateRHI();
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

	virtual bool CanBeOccluded() const override
	{
		return false;
	}

	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

	virtual bool NeedPostUpdate() const { return true; }

	virtual void PostUpdateBeforePresent(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily);

	uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }

	void DrawMeshBatches(int32 ViewIndex, const FSceneView* View, const FSceneViewFamily& ViewFamily, FMeshElementCollector& Collector) const;

	void GenerateIndirectDrawBuffer(const FSceneView* View, const FSceneViewFamily& ViewFamily, FLiteGPUSceneProxyVisibilityData* ResVisibilityData, FLiteGPUSceneInstanceData* PerInstanceData) const;

	ENGINE_API bool IsInitialized() const;
public:
	FLiteGPUSceneInstanceDataPtr SharedPerInstanceData;

	FLiteGPUSceneVertexIndexBufferPtr SharedVertexIndexBufferData;
	FTexture2DRHIRef FoliageSpriteTexture;

	TArray<FBoxSphereBounds> FoliageBounds;
	int32 InstanceNum;
	int32 FoliageTypeNum;

	ERHIFeatureLevel::Type ProxyFeatureLevel;
	FLiteGPUSceneProxyVisibilityDataPtr SharedMainVisibilityData;

	FVector LastFrameViewForward;

	FLiteGPUSceneVertexFactory* pGPUDrivenVertexFactory;

	FMaterialRenderProxy* MaterialProxy;

	FLiteGPUSceneVertexFactoryUserData* UserData;

	TMap<FMaterialRenderProxy*, TArray<int32>> MatToPatchIndiceMap;
	TArray<FLiteGPUSceneMeshPatchInfo> AllPatches;

	void Init_RenderingThread();

	FSceneInterface* CachedSceneInterface;
#if !UE_BUILD_SHIPPING
	class ULiteGPUSceneComponent* CachedComponent;
#endif
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class ENGINE_API ULiteGPUSceneComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:	
	// Sets default values for this component's properties
	ULiteGPUSceneComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;


	virtual void BeginDestroy() override;
	virtual void OnComponentCreated() override;

	virtual void PostLoad() override;

	virtual bool NeedsLoadForServer() const override { return false; }

	virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
	virtual void DestroyRenderState_Concurrent() override;

	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials /* = false */) const override;

	UFUNCTION(CallInEditor, Category="LandscapeComponent")
	void DrawDebugBoxLine();
	UFUNCTION(CallInEditor, Category = "LandscapeComponent")
	void CheckData();


	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	
	/** Begin UPrimitiveComponent  interface */
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	void BuildLiteGPUScene(const TArray<UStaticMesh*> InAllMeshes);

	/*
	 * Function that was called externally to tell the component this HISM requires a Loading for GPUDriven
	 */
	void AddInstanceGroup(class UHierarchicalInstancedStaticMeshComponent* HComponent);

	/*
	 * Function that was called externally to tell the component this HISM requires a Unloading for GPUDriven
	 */
	void RemoveInstanceGroup(class UHierarchicalInstancedStaticMeshComponent* Key);

	/*
	 * Update the mark of HISM groups which are required to load data for GPUDriven
	 * Also updates the data in cpu side, for later upload to gpu buffer
	* This generally happens when the HISM just enters our vision scope
	 */
	void UpdateIncreasedGroup();
	/*
	* Update the marking of HISM groups which are required to remove from GPUDriven
	* Also updates the data in cpu side, for later upload to gpu buffer
	* This generally happens when the HISM is out of vision range
	*/
	void UpdateRemovedGroup();
	/*
	 * Clean up InstanceData,VertexBuffer+IndexBuffer, Visibility Data Buffer
	 */
	void CleanupInstances();

	/*
	 * Cleanup and re-initialize the data buffer
	 */
	void FlushLiteGPUScene();
	/*
	 * This combine the vertices and indices of all mesh into two seperate buffers
	 * 
	 * The structs looks like:
	 * Vertex Buffer->
	 *		----------------------------------------------------------------------------------------
	 *		|                                  Mesh 0                                 |....| Mesh N|
	 *		----------------------------------------------------------------------------------------
	 *		|                   LOD 0                                   |.....| LOD N |            |
	 *		----------------------------------------------------------------------------------------
	 *		|            Section0	    | ... |           Section N      |                          |
	 *		----------------------------------------------------------------------------------------
	 *		| Vertex 0 | ...| Vertex N |                                                           |
	 *		----------------------------------------------------------------------------------------
	 *	Index Buffer ->
	 *		Looks exactly the same as Vertex Buffer, replace "Vertex" with "Index"
	 *	Patches Array	(All patches) -> 
	 *		Store the info
	 *			[
	 *				Mesh Name, Mesh Id, Material Id, Section Id,
	 *				Lod id,	                        ScreenSize range of this LOD(min/max)
	 *				start offset(vertex buffer),    start offset(index buffer),
	 *				vertex count,                   index count
	 *			]
	 *		Note that the order of element in this array is non-deterministic as it's converted from a map
	 *		Check code for further details
	 */
	void ConstructCombinedFoliageVertices(const TArray<UStaticMesh*> InAllMeshes);

	void UpdateRenderThread();
	bool IsInitialized() const;
	void AddMeshLodSectionToBuffer(
		int32 LodIndex,
		const FStaticMeshRenderData* MeshRenderData,
		const FStaticMeshLODResources& LODResource,
		const FStaticMeshSection& RenderSection,
		TArray<FLiteGPUSceneMeshVertex>& OutCombinedVertexBufferData,
		TArray<uint32>& OutCombinedIndiceBufferData,
		uint32& OutFirstVertexOffset,
		uint32& OutVertexCount,
		uint32& OutFirstIndexOffset,
		uint32& OutIndicesCount,
		float& OutScreenSizeMin,
		float& OutScreenSizeMax
	);

	void InitSharedResources();
	FLiteGPUSceneBufferManager* GetGPUDrivenBufferManager();
public:
	int32 TotalTriangleDraw;
	int32									AllInstanceNum;
	int32									CurInstanceNum;
	int32									MaxInstanceNum;
	TMap<int32, TArray<FLiteGPUSceneMeshPatchInfo>> MaterialToPatchMap;
	FLiteGPUSceneInstanceDataPtr SharedPerInstanceData;

	TArray<class UHierarchicalInstancedStaticMeshComponent*> RemovedHISMs;
	TArray<class UHierarchicalInstancedStaticMeshComponent*> IncreasedHISMs;

	FLiteGPUSceneVertexIndexBufferPtr SharedVertexIndexBufferData;

	FLiteGPUSceneProxyVisibilityDataPtr SharedMainVisibilityData;

	/*
	 * Actually, this stores the max lod numbers among the meshes
	 */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Components|LiteGPUSceneComponent")
	int32 PerPatchMaxNum;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Components|LiteGPUSceneComponent")
	TArray<TObjectPtr<UStaticMesh>>				AllSourceMeshes;
	UPROPERTY()
	TArray<FLiteGPUSceneMeshPatchInfo>	AllPatches;
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>>		AllUsedMats;
	UPROPERTY()
	TArray<FLiteGPUSceneMeshVertex>		CombinedVertexBufferData;
	UPROPERTY()
	TArray<uint32>							CombinedIndiceBufferData;
};