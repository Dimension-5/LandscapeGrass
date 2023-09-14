#pragma once
#include "RenderResource.h"
#include "RendererInterface.h"
#include "UnifiedBuffer.h"

class UStaticMesh;
struct FLiteGPUSceneVertexFactory;

struct FLiteGPUHalf2
{
	FFloat16 X;
	FFloat16 Y;
};

struct FLiteGPUHalf4
{
	FFloat16 X;
	FFloat16 Y;
	FFloat16 Z;
	FFloat16 W;
};

struct FLiteGPUSceneMeshSectionInfo
{
	FString MeshName;
	uint32 MeshIndex;
	uint32 LODIndex;
	uint32 SectionIndex;
	uint32 FirstVertexOffset;
	uint32 FirstIndexOffset;
	uint32 IndexCount;
	uint32 PatchID;
	uint32 VertexCount;
	uint32 MaterialIndex;
	float ScreenSizeMin;
	float ScreenSizeMax;
};

struct FLiteGPUSceneMeshVertex
{
	FLiteGPUSceneMeshVertex();
	FLiteGPUSceneMeshVertex(const FLiteGPUSceneMeshVertex& Another);
	FLiteGPUSceneMeshVertex(const FVector3f& InPosition);
	FLiteGPUSceneMeshVertex(const FVector3f& InPosition, const FVector2f& InUV);
	FLiteGPUSceneMeshVertex(const FVector3f& InPosition, const FVector3f& InTangentX, const FVector3f& InTangentZ, 
		const FVector2f& InTexCoord, const FVector2f& InTexCoord2, const FVector2f& InTexCoord3, const FColor& InColor);

	float GetBasisDeterminantSign(const FVector3f& XAxis, const FVector3f& YAxis, const FVector3f& ZAxis);
	void SetTangents(const FVector3f& InTangentX, const FVector3f& InTangentY, const FVector3f& InTangentZ);
	FVector3f GetTangentY();
	FLiteGPUSceneMeshVertex& operator =(const FLiteGPUSceneMeshVertex& Another);

	FVector3f Position;
	FColor Color;
	FPackedNormal TangentX;
	FPackedNormal TangentZ;
	FVector2f TextureFirstCoordinate;
	FVector2f TextureSecondCoordinate;
};

struct FLiteGPUCombinedData
{
	TArray<uint32> Indices;
	TArray<FLiteGPUSceneMeshVertex> Vertices;
	TArray<TObjectPtr<UMaterialInterface>> Materials;
	TMap<int32, TArray<FLiteGPUSceneMeshSectionInfo>> SectionsMap;
};

struct FLiteGPUSceneMeshVertexBuffer final : public FVertexBuffer
{
	void InitRHI(FRHICommandListBase& RHICmdList);

	TArray<FLiteGPUSceneMeshVertex> Vertices;
	int32 VerticesNum;
};

struct FLiteGPUSceneMeshIndexBuffer final : public FIndexBuffer
{
	void InitRHI(FRHICommandListBase& RHICmdList);

	TArray<uint32> Indices;
	int32 IndicesNum;
};

struct FLiteGPUCombinedBuffer
{
	FLiteGPUCombinedBuffer();
	~FLiteGPUCombinedBuffer();

	void Initialize(const TArray<FLiteGPUSceneMeshVertex>& Vertices, const TArray<uint32>& Indices);
	void Release_AnyThread();
	void Release_RenderingThread();

	FLiteGPUSceneMeshVertexBuffer* VertexBuffer = nullptr;
	FLiteGPUSceneMeshIndexBuffer* IndexBuffer = nullptr;
	uint64 UsedBytes = 0;
	uint32 VertexNum = 0;
	uint32 IndiceNum = 0;
	bool bInitialized = false;
};

struct FLiteGPUViewData
{
	FConvexVolume ViewFrustum;
	FVector3f ViewLocation;
	FVector3f ViewForward;
	FMatrix44f ProjMatrix;
	FSphere3f ShadowBounds;
	int32 MaxGPUInstanceToDraw = 0;
};

struct FLiteGPUSceneUpdate
{
	int32 InstanceNum = 0;
	TArray64<uint32> InstanceIndices; // The Indices of the instance that are dirty
};

struct FLiteGPUInstanceAttribute
{
	uint8 Type;
	uint8 SectionID;
	uint8 SectionNum;
	uint8 Padding;
};

struct FSectorInfo
{
	FInt64Vector2 Coordinate;
	uint64 InstanceCount = 0;
};

struct FLiteGPUSceneData
{
	// const per build
	int32 PerSectionMaxNum = 0;
	int32 InstanceTypeNum = 0;
	int32 TotalSectionNum = 0;
	TArray64<uint8> SectionAABBData;
	TArray64<TObjectPtr<UStaticMesh>> SourceMeshes;
	TArray64<FLiteGPUSceneMeshSectionInfo> SectionInfos;
	// const per build

	TArray<FSectorInfo> SectorInfos;
	TMap<FInt64Vector2, uint32> SectorIDMap;
	uint32 NextSectorID = 0;

	int32 InstanceNum = 0;
	int32 InstanceCapacity = 0;
	TArray64<UE::Math::TTransform<float>> InstanceTransforms;
	TArray64<uint32> InstanceSectorIDs;
	TArray64<uint8> InstanceTypes;
	TArray64<uint8> InstanceSectionIDs;
	TArray64<uint8> InstanceSectionNums;

	TQueue<FLiteGPUSceneUpdate, EQueueMode::Spsc> Updates;

	// TODO: REMOVE THIS
	TArray64<uint32> SectionInstanceNums; // Number of instances that references the section
};

struct FLiteGPUSceneBufferState
{
	TRefCountPtr<FRDGPooledBuffer> SectionInfoBuffer;
	TRefCountPtr<FRDGPooledBuffer> MeshAABBBuffer;
	TRefCountPtr<FRDGPooledBuffer> SectorInfoBuffer;
	TRefCountPtr<FRDGPooledBuffer> InstanceAttributeBuffer;
	TRefCountPtr<FRDGPooledBuffer> InstanceTransformBuffer;
	TRefCountPtr<FRDGPooledBuffer> InstanceSectorIDBuffer;
};

struct FLiteGPUViewBufferState
{
	TRefCountPtr<FRDGPooledBuffer> RWUnCulledInstanceScreenSize;
	TRefCountPtr<FRDGPooledBuffer> RWUnCulledInstanceBuffer;
	TRefCountPtr<FRDGPooledBuffer> RWUnCulledInstanceNum;

	TRefCountPtr<FRDGPooledBuffer> RWIndirectDrawDispatchIndiretBuffer;
	TRefCountPtr<FRDGPooledBuffer> RWInstanceIndiceBuffer;
	TRefCountPtr<FRDGPooledBuffer> RWIndirectDrawBuffer;
	TRefCountPtr<FRDGPooledBuffer> RWUnCulledInstanceIndirectParameters;
};

struct FLiteGPUCounterBufferState
{
	/*
	 * Since it's instancing , A section might be drawed multiple times
	 * This buffer stores the draw number of each section (section at certain lod)
	 * SectionCount(SectionID) = RWSectionCountCopyBuffer[SectionID]
	 */
	TRefCountPtr<FRDGPooledBuffer> RWSectionCountCopyBuffer;
	TRefCountPtr<FRDGPooledBuffer> RWSectionCountBuffer;
	/*
	 * Since it's instancing , A section might be drawed multiple times
	 * We organize the draw of same section together and close
	 * This buffer stores the starting offset of one type of section
	 * SectionStartOffset(SectionID) =RWSectionCountOffsetBuffer[SectionID]
	 *
	 * Therefore, the data we need to draw a section of "SectionID" locates at
	 * [SectionStartOffset(SectionID),SectionStartOffset(SectionID)+SectionCount(SectionID)]
	 */
	TRefCountPtr<FRDGPooledBuffer> RWSectionCountOffsetBuffer;
	/*
	 * This buffer is a size-1 uint which helps calculate the SectionStartOffset
	 *
	 *		uint SectionStartOffset;
	 *		InterlockedAdd(RWNextSectionhCountOffsetBuffer[0], SectionCount, SectionStartOffset);
	 *		RWSectionCountOffsetBuffer[UniqueID] = SectiontartOffset;
	 *
	 */
	TRefCountPtr<FRDGPooledBuffer> RWNextSectionCountOffsetBuffer;
};

struct FLiteGPUScene
{
public:
	RENDERER_API FLiteGPUScene(ERHIFeatureLevel::Type InFeatureLevel);
	RENDERER_API ~FLiteGPUScene();
	RENDERER_API void BuildScene(const TArray<TObjectPtr<UStaticMesh>> InAllMeshes);
	
	RENDERER_API void UpdateSectionInfos(FRDGBuilder& GraphBuilder);
	RENDERER_API void UpdateAABBData(FRDGBuilder& GraphBuilder);
	RENDERER_API void UpdateInstanceData(FRDGBuilder& GraphBuilder);

	RENDERER_API void EnqueueUpdates_TS(const FLiteGPUSceneUpdate&& UpdateToEnqueue);

	inline uint64 GetInstanceNum() const { return SceneData.InstanceNum; }
	inline uint64 GetSectionNum() const { return SceneData.TotalSectionNum; }
	inline uint64 GetPerSectionMaxNum() const { return SceneData.PerSectionMaxNum; }
	inline FLiteGPUViewBufferState GetViewBufferState() const { return ViewBufferState; }
	inline FLiteGPUSceneBufferState GetSceneBufferState() const { return BufferState; }
	inline FLiteGPUCounterBufferState GetCounterBufferState() const { return CounterBufferState; }
	inline FLiteGPUSceneVertexFactory* GetVertexFactory() const 
	{
		return CurrentIndicesVertexBuffer ? DynamicVFMap[CurrentIndicesVertexBuffer].pGPUDrivenVertexFactory : nullptr; 
	}

protected:
	friend class FLiteGPUSceneProxy;
	friend class ALiteGPUSceneManager;
	friend class ULiteGPUSceneRenderComponent;
	friend struct FLiteGPUSceneVertexFactoryShaderParameters;

	void UpdateViewBuffers(FRDGBuilder& GraphBuilder);
	void UpdateSectionBuffers(FRDGBuilder& GraphBuilder);

	void buildCombinedData(const TArray<TObjectPtr<UStaticMesh>> InAllMeshes);
	void buildSceneData(const TArray<TObjectPtr<UStaticMesh>> InAllMeshes);

	void fillMeshLODSectionData(int32 LodIndex, const FStaticMeshRenderData* MeshRenderData, 
		const FStaticMeshLODResources& LODResource, const FStaticMeshSection& RenderSection, 
		TArray<FLiteGPUSceneMeshVertex>& OutCombinedVertexBufferData, TArray<uint32>& OutCombinedIndiceBufferData, 
		uint32& OutFirstVertexOffset, uint32& OutVertexCount, 
		uint32& OutFirstIndexOffset, uint32& OutIndicesCount, 
		float& OutScreenSizeMin, float& OutScreenSizeMax);

	FLiteGPUCombinedData CombinedData;
	FLiteGPUViewData ViewData;
	FLiteGPUSceneData SceneData;
	FCriticalSection SceneUpdatesMutex;

	FLiteGPUCombinedBuffer CombinedBuffer;
	FLiteGPUViewBufferState ViewBufferState;
	FLiteGPUSceneBufferState BufferState;
	FLiteGPUCounterBufferState CounterBufferState;

	struct DynamicVF
	{
		FLiteGPUSceneVertexFactory* pGPUDrivenVertexFactory = nullptr;
		FVertexBuffer* GPUIndicesVertexBuffer;
	};
	TMap<FBufferRHIRef, DynamicVF> DynamicVFMap;
	FBufferRHIRef CurrentIndicesVertexBuffer = nullptr;

	FRDGAsyncScatterUploadBuffer SectionInfoUploadBuffer;
	FRDGAsyncScatterUploadBuffer MeshAABBUploadBuffer;
	FRDGAsyncScatterUploadBuffer SectorInfoUploadBuffer;
	FRDGAsyncScatterUploadBuffer InstanceAttributeUploadBuffer;
	FRDGAsyncScatterUploadBuffer InstanceTransformUploadBuffer;
	FRDGAsyncScatterUploadBuffer InstanceSectorIDUploadBuffer;

	const ERHIFeatureLevel::Type FeatureLevel;
};

FORCEINLINE FLiteGPUSceneMeshVertex::FLiteGPUSceneMeshVertex() :
	Position(FVector3f(0, 0, 0)),
	Color(FColor(255, 255, 255)),
	TangentX(FVector3f(1, 0, 0)),
	TangentZ(FVector3f(0, 0, 1)),
	TextureFirstCoordinate(FVector2f::ZeroVector),
	TextureSecondCoordinate(FVector2f::ZeroVector)
{
	// basis determinant default to +1.0
	// but here we not this represents in byte, where TangentZ is a FPackedNormal, see PackedNormal.h
	TangentZ.Vector.W = 127;
}

FORCEINLINE FLiteGPUSceneMeshVertex::FLiteGPUSceneMeshVertex(const FLiteGPUSceneMeshVertex& Another)
{
	Position = Another.Position;
	Color = Another.Color;
	TextureFirstCoordinate = Another.TextureFirstCoordinate;
	TextureSecondCoordinate = Another.TextureSecondCoordinate;
	TangentX = Another.TangentX;
	TangentZ = Another.TangentZ;
}

FORCEINLINE FLiteGPUSceneMeshVertex::FLiteGPUSceneMeshVertex(const FVector3f& InPosition) :
	Position(InPosition),
	Color(FColor(255, 255, 255)),
	TangentX(FVector3f(1, 0, 0)),
	TangentZ(FVector3f(0, 0, 1)),
	TextureFirstCoordinate(FVector2f::ZeroVector),
	TextureSecondCoordinate(FVector2f::ZeroVector)
{
	// basis determinant default to +1.0
	// but here we not this represents in byte, where TangentZ is a FPackedNormal, see PackedNormal.h
	TangentZ.Vector.W = 127;
}

FORCEINLINE FLiteGPUSceneMeshVertex::FLiteGPUSceneMeshVertex(const FVector3f& InPosition, const FVector2f& InUV) :
	Position(InPosition),
	Color(FColor(255, 255, 255)),
	TangentX(FVector3f(1, 0, 0)),
	TangentZ(FVector3f(0, 0, 1)),
	TextureFirstCoordinate(InUV),
	TextureSecondCoordinate(InUV)
{
	// basis determinant default to +1.0
	// but here we not this represents in byte, where TangentZ is a FPackedNormal, see PackedNormal.h
	TangentZ.Vector.W = 127;
}

FORCEINLINE FLiteGPUSceneMeshVertex::FLiteGPUSceneMeshVertex(const FVector3f& InPosition, const FVector3f& InTangentX, const FVector3f& InTangentZ, const FVector2f& InTexCoord, const FVector2f& InTexCoord2, const FVector2f& InTexCoord3, const FColor& InColor) :
	Position(InPosition),
	Color(InColor),
	TangentX(InTangentX),
	TangentZ(InTangentZ),
	TextureFirstCoordinate(InTexCoord),
	TextureSecondCoordinate(InTexCoord2)
{
	// basis determinant default to +1.0
	// but here we not this represents in byte, where TangentZ is a FPackedNormal, see PackedNormal.h
	TangentZ.Vector.W = 127;
}

FORCEINLINE float FLiteGPUSceneMeshVertex::GetBasisDeterminantSign(const FVector3f& XAxis, const FVector3f& YAxis, const FVector3f& ZAxis)
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
FORCEINLINE void FLiteGPUSceneMeshVertex::SetTangents(const FVector3f& InTangentX, const FVector3f& InTangentY, const FVector3f& InTangentZ)
{
	TangentX = InTangentX;
	TangentZ = InTangentZ;
	// store determinant of basis in w component of normal vector
	// TangentZ is a FPackedNormal, see PackedNormal.h
	TangentZ.Vector.W = GetBasisDeterminantSign(InTangentX, InTangentY, InTangentZ) < 0.0f ? -127 : 127;
}

/*Genrate Y Tangent*/
FORCEINLINE FVector3f FLiteGPUSceneMeshVertex::GetTangentY()
{
	FVector3f TanX = TangentX.ToFVector3f();
	FVector3f TanZ = TangentZ.ToFVector3f();
	//  TangentZ is a FPackedNormal, see PackedNormal.h
	return (TanZ ^ TanX) * ((float)TangentZ.Vector.W / 127.5f - 1.0f);
}

FORCEINLINE FLiteGPUSceneMeshVertex& FLiteGPUSceneMeshVertex::operator=(const FLiteGPUSceneMeshVertex& Another)
{
	Position = Another.Position;
	Color = Another.Color;
	TextureFirstCoordinate = Another.TextureFirstCoordinate;
	TextureSecondCoordinate = Another.TextureSecondCoordinate;
	TangentX = Another.TangentX;
	TangentZ = Another.TangentZ;
	return *this;
}