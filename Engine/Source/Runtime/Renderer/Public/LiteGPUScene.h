#pragma once
#include "RenderResource.h"
#include "RendererInterface.h"
#include "UnifiedBuffer.h"

class UStaticMesh;
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
public:
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
public:
	FLiteGPUCombinedBuffer()
		: VertexBuffer(nullptr), IndexBuffer(nullptr)
		, UsedBytes(0), VertexNum(0)
		, IndiceNum(0), bIntialized(false)
	{

	}

	~FLiteGPUCombinedBuffer()
	{
		Release_RenderingThread();
	}

	void Initialize(const TArray<FLiteGPUSceneMeshVertex>& Vertices, const TArray<uint32>& Indices);
	void Release_RenderingThread();

	FLiteGPUSceneMeshVertexBuffer* VertexBuffer;
	FLiteGPUSceneMeshIndexBuffer* IndexBuffer;
	uint64 UsedBytes;
	uint32 VertexNum;
	uint32 IndiceNum;
	bool bIntialized;
};

struct FLiteGPUCounterBuffers
{
	/*
	 * Since it's instancing , A section might be drawed multiple times
	 * This buffer stores the draw number of each section (section at certain lod)
	 * SectionCount(SectionID) = RWSectionCountCopyBuffer[SectionID]
	 */
	FRDGBufferHandle RWSectionCountCopyBuffer;
	FRDGBufferHandle RWSectionCountBuffer;

	/*
	 * Since it's instancing , A section might be drawed multiple times
	 * We organize the draw of same section together and close
	 * This buffer stores the starting offset of one type of section
	 * SectionStartOffset(SectionID) =RWSectionCountOffsetBuffer[SectionID]
	 *
	 * Therefore, the data we need to draw a section of "SectionID" locates at
	 * [SectionStartOffset(SectionID),SectionStartOffset(SectionID)+SectionCount(SectionID)]
	 */
	FRDGBufferHandle RWSectionCountOffsetBuffer;
	/*
	 * This buffer is a size-1 uint which helps calculate the SectionStartOffset
	 *
	 *		uint SectionStartOffset;
	 *		InterlockedAdd(RWNextSectionhCountOffsetBuffer[0], SectionCount, SectionStartOffset);
	 *		RWSectionCountOffsetBuffer[UniqueID] = SectiontartOffset;
	 *
	 */
	FRDGBufferHandle RWNextSectionCountOffsetBuffer;
};

struct FLiteGPUViewData
{
	FConvexVolume ViewFrustum;
	FVector3f ViewLocation;
	FVector3f ViewForward;
	FMatrix44f ProjMatrix;
	FSphere3f ShadowBounds;
	int32 MaxGPUInstanceToDraw = 0;
	bool bGPUCulling = false;
	bool bDirty = false;
	bool bFirstGPUCullinged = false;
	bool bCreateImposterBuffer = false;
	uint32 GPUByteCount = 0;
	int32 PerSectionMaxNum = 0;
};

struct FLiteGPUViewBuffers
{
	FRDGBufferHandle RWIndirectDrawDispatchIndiretBuffer;
	FRDGBufferHandle RWInstanceIndiceBuffer;
	FRDGBufferHandle RWIndirectDrawBuffer;
	FRDGBufferHandle RWGPUUnCulledInstanceBuffer;
	FRDGBufferHandle RWGPUUnCulledInstanceScreenSize;
	FRDGBufferHandle RWGPUUnCulledInstanceNum;
	FRDGBufferHandle RWDispatchStagingBuffer;
	FRDGBufferHandle RWGPUUnCulledInstanceIndirectParameters;
};

struct FLiteGPUSceneUpdate
{
	int32 InstanceNum = 0;
	TArray<uint32> InstanceIndices; // The Indices of the instance that are dirty
	TArray<FInt32Vector2> TilesPositions; // [PivotX, PivotY]
	TArray<FLiteGPUHalf2> InstanceXYOffsets; // [X-Offset, Y-Offset]
	TArray<FVector2D> InstanceZWOffsets; // [Z-Offset, TileIndex]
	TArray<FLiteGPUHalf4> InstanceRotationScales; // [X-Rotation, Y-Rotation, Z-Rotation, Scale]
	TArray<uint8> InstanceTypes;
	TArray<uint8> InstanceSectionIDs;
	TArray<uint8> InstanceSectionNums;
};

struct FLiteGPUSceneActive
{
	TArray<int32> SectionInstanceNums; // Number of instances that references the section
	int32 InstanceNum = 0;
	bool bUpdateFrame = false;
};

struct FLiteGPUSceneData
{
	// const per build
	TArray<FLiteGPUSceneMeshSectionInfo> SectionInfos;
	int32 TotalSectionNum = 0;
	TArray<TObjectPtr<UStaticMesh>> SourceMeshes;
	/*
	 * Array which  stores the AAbb data of the instances, the length is equal instance_num x 2 x sizeof(Vector4).
	 * 2 stands for the BottomLeft and Top Right Pos of the AABB box
	 */
	TArray<uint8> SectionAABBData;
	/*
	 * Array which stores Mesh Index of each section,
	 * the length is equal instance_num x lod_num x section_num
	 */
	TArray<int32> SectionMeshIndices;
	uint32 GPUByteCount = 0;
	int32 InstanceTypeNum = 0;
	// const per build

	FLiteGPUSceneUpdate Update;
	FLiteGPUSceneActive Active;
};

struct FLiteGPUSceneBuffers
{
	TRefCountPtr<FRDGPooledBuffer> AllSectionInfoBuffer;
	// TRefCountPtr<FRDGPooledBuffer> RWDrawedTriangleCountBuffer;
	TRefCountPtr<FRDGPooledBuffer> MeshAABBBuffer;
	/*
	 * This Buffer stores the instance id of section draw
	 * InstanceID(SectionID,i-th draw) = RWInstanceIndiceBuffer[SectionStartOffset(SectionID)+i]
	 */
	TRefCountPtr<FRDGPooledBuffer> RWGPUInstanceIndicesBuffer;
	TRefCountPtr<FRDGPooledBuffer> RWInstanceScaleBuffer;
	TRefCountPtr<FRDGPooledBuffer> RWInstanceTransformBuffer;
	TRefCountPtr<FRDGPooledBuffer> RWInstanceTypeBuffer;
	TRefCountPtr<FRDGPooledBuffer> RWInstanceSectionNumBuffer;
	TRefCountPtr<FRDGPooledBuffer> RWInstanceSectionIDsBuffer;
};

struct RENDERER_API FLiteGPUScene
{
public:
	FLiteGPUScene();
	~FLiteGPUScene();
	void ConstructScene(const TArray<TObjectPtr<UStaticMesh>> InAllMeshes);

protected:
	void ConstructCombinedData(const TArray<TObjectPtr<UStaticMesh>> InAllMeshes);
	void ConstructSceneData(const TArray<TObjectPtr<UStaticMesh>> InAllMeshes);

	void FillMeshLODSectionData(int32 LodIndex, const FStaticMeshRenderData* MeshRenderData, 
		const FStaticMeshLODResources& LODResource, const FStaticMeshSection& RenderSection, 
		TArray<FLiteGPUSceneMeshVertex>& OutCombinedVertexBufferData, TArray<uint32>& OutCombinedIndiceBufferData, 
		uint32& OutFirstVertexOffset, uint32& OutVertexCount, 
		uint32& OutFirstIndexOffset, uint32& OutIndicesCount, 
		float& OutScreenSizeMin, float& OutScreenSizeMax);

	int32 PerSectionMaxNum;
	TArray<FInt32Vector2> TilesPositions; // [PivotX, PivotY]
	TArray<FLiteGPUHalf2> InstanceXYOffsets; // [X-Offset, Y-Offset]
	TArray<FVector2D> InstanceZWOffsets; // [Z-Offset, TileIndex]
	TArray<FLiteGPUHalf4> InstanceRotationScales; // [X-Rotation, Y-Rotation, Z-Rotation, Scale]

	FLiteGPUCombinedData CombinedData;
	FLiteGPUSceneData SceneData;
	FLiteGPUViewData ViewData;

	FLiteGPUCombinedBuffer CombinedBuffer;
	FLiteGPUSceneBuffers SceneBuffers;
	FLiteGPUViewBuffers ViewBuffers;
	
	FLiteGPUCounterBuffers CounterBuffers;
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