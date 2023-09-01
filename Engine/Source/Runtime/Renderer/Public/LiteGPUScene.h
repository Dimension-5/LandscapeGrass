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

struct FLiteGPUScene
{
public:
	FLiteGPUScene();
	~FLiteGPUScene();

	void ConstructCombinedVertices(const TArray<TObjectPtr<UStaticMesh>> InAllMeshes);

	// [PivotX, PivotY]
	TArray<FInt32Vector2> TilesPositions;
	// [X-Offset, Y-Offset]
	TArray<FLiteGPUHalf2> InstanceXYOffsets;
	// [Z-Offset, TileIndex]
	TArray<FVector2D> InstanceZWOffsets;
	// [X-Rotation, Y-Rotation, Z-Rotation, Scale]
	TArray<FLiteGPUHalf4> InstanceRotationScales;
};