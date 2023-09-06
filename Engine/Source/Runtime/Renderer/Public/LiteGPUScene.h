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

struct RENDERER_API FLiteGPUSceneMeshVertex
{
public:
	FLiteGPUSceneMeshVertex() :
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
	FLiteGPUSceneMeshVertex(const FLiteGPUSceneMeshVertex& Another)
	{
		Position = Another.Position;
		Color = Another.Color;
		TextureFirstCoordinate = Another.TextureFirstCoordinate;
		TextureSecondCoordinate = Another.TextureSecondCoordinate;
		TangentX = Another.TangentX;
		TangentZ = Another.TangentZ;
	}

	FLiteGPUSceneMeshVertex(const FVector3f& InPosition) :
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

	FLiteGPUSceneMeshVertex(const FVector3f& InPosition, const FVector2f& InUV) :
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

	FLiteGPUSceneMeshVertex(const FVector3f& InPosition, const FVector3f& InTangentX, const FVector3f& InTangentZ, const FVector2f& InTexCoord, const FVector2f& InTexCoord2, const FVector2f& InTexCoord3, const FColor& InColor) :
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
		Color = Another.Color;
		TextureFirstCoordinate = Another.TextureFirstCoordinate;
		TextureSecondCoordinate = Another.TextureSecondCoordinate;
		TangentX = Another.TangentX;
		TangentZ = Another.TangentZ;
		return *this;
	}

	FVector3f Position;
	FColor Color;
	FPackedNormal TangentX;
	FPackedNormal TangentZ;
	FVector2f TextureFirstCoordinate;
	FVector2f TextureSecondCoordinate;
};

struct RENDERER_API FLiteGPUCombinedData
{
	TArray<uint32> Indices;
	TArray<FLiteGPUSceneMeshVertex> Vertices;
	TArray<TObjectPtr<UMaterialInterface>> Materials;
	TMap<int32, TArray<FLiteGPUSceneMeshSectionInfo>> SectionsMap;
};

struct RENDERER_API FLiteGPUScene
{
public:
	FLiteGPUScene();
	~FLiteGPUScene();

	void ConstructCombinedVertices(const TArray<TObjectPtr<UStaticMesh>> InAllMeshes);
	void FillMeshLODSectionData(int32 LodIndex, const FStaticMeshRenderData* MeshRenderData, 
		const FStaticMeshLODResources& LODResource, const FStaticMeshSection& RenderSection, 
		TArray<FLiteGPUSceneMeshVertex>& OutCombinedVertexBufferData, TArray<uint32>& OutCombinedIndiceBufferData, 
		uint32& OutFirstVertexOffset, uint32& OutVertexCount, 
		uint32& OutFirstIndexOffset, uint32& OutIndicesCount, 
		float& OutScreenSizeMin, float& OutScreenSizeMax);

	FLiteGPUCombinedData CombinedData;

	// [PivotX, PivotY]
	TArray<FInt32Vector2> TilesPositions;
	// [X-Offset, Y-Offset]
	TArray<FLiteGPUHalf2> InstanceXYOffsets;
	// [Z-Offset, TileIndex]
	TArray<FVector2D> InstanceZWOffsets;
	// [X-Rotation, Y-Rotation, Z-Rotation, Scale]
	TArray<FLiteGPUHalf4> InstanceRotationScales;
};