#include "LiteGPUScene.h"

FLiteGPUScene::FLiteGPUScene()
{

}

FLiteGPUScene::~FLiteGPUScene()
{

}

void FLiteGPUScene::FillMeshLODSectionData(int32 LodIndex, const FStaticMeshRenderData* MeshRenderData, 
	const FStaticMeshLODResources& LODResource, const FStaticMeshSection& RenderSection, 
	TArray<FLiteGPUSceneMeshVertex>& OutCombinedVertexBufferData, TArray<uint32>& OutCombinedIndiceBufferData, 
	uint32& OutFirstVertexOffset, uint32& OutVertexCount, 
	uint32& OutFirstIndexOffset, uint32& OutIndicesCount, 
	float& OutScreenSizeMin, float& OutScreenSizeMax)
{
	TArray<uint32> TotalIndices;
	LODResource.IndexBuffer.GetCopy(TotalIndices);

	OutFirstVertexOffset = OutCombinedVertexBufferData.Num();
	OutFirstIndexOffset = OutCombinedIndiceBufferData.Num();

	TArray<FLiteGPUSceneMeshVertex> RawVertices;
	TArray<uint32> RawIndices;
	TMap<uint32, uint32> IndicesMap;

	uint32 RawVertexIdx = 0;
	for (uint32 VertIdx = RenderSection.MinVertexIndex; VertIdx <= RenderSection.MaxVertexIndex; VertIdx++)
	{
		uint32 RealVertexIdx = VertIdx;
		const auto Pos = LODResource.VertexBuffers.PositionVertexBuffer.VertexPosition(RealVertexIdx);
		const auto TangentX = LODResource.VertexBuffers.StaticMeshVertexBuffer.VertexTangentX(RealVertexIdx);
		const auto TangentZ = LODResource.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(RealVertexIdx);
		const auto UV = LODResource.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(RealVertexIdx, 0);
		auto UV2 = UV;
		auto UV3 = UV;
		if (LODResource.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() > 1)
		{
			UV2 = LODResource.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(RealVertexIdx, 1);
		}
		if (LODResource.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() > 2)
		{
			UV3 = LODResource.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(RealVertexIdx, 2);
		}
		FColor VertexColor = FColor(255, 255, 255, 255);
		if (LODResource.VertexBuffers.ColorVertexBuffer.GetNumVertices() > 0 && LODResource.VertexBuffers.ColorVertexBuffer.GetNumVertices() == LODResource.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices())
		{
			VertexColor = LODResource.VertexBuffers.ColorVertexBuffer.VertexColor(RealVertexIdx);
		}
		FLiteGPUSceneMeshVertex FoliageMeshVertex(Pos, TangentX, TangentZ, UV, UV2, UV3, VertexColor);
		OutCombinedVertexBufferData.Add(FoliageMeshVertex);
		RawVertices.Add(FoliageMeshVertex);
		IndicesMap.Add(RealVertexIdx, RawVertexIdx);

		RawVertexIdx++;
	}

	for (uint32 IndiceIdx = 0; IndiceIdx < (RenderSection.NumTriangles * 3); IndiceIdx++)
	{
		uint32 RealVertexIndex = TotalIndices[RenderSection.FirstIndex + IndiceIdx];
		uint32* pMappedIndex = IndicesMap.Find(RealVertexIndex);
		RawIndices.Add(*pMappedIndex);
		OutCombinedIndiceBufferData.Add(*pMappedIndex);
	}

	if (LodIndex == 0)
	{
		OutScreenSizeMax = 10000;
	}
	else
	{
		OutScreenSizeMax = MeshRenderData->ScreenSize[LodIndex].Default;
	}

	if (LodIndex < MeshRenderData->LODResources.Num() - 1)
	{
		OutScreenSizeMin = MeshRenderData->ScreenSize[LodIndex + 1].Default;
	}
	else
	{
		OutScreenSizeMin = 0;
	}

	OutIndicesCount = RawIndices.Num();
	OutVertexCount = RawVertices.Num();
}

void FLiteGPUScene::ConstructCombinedData(const TArray<TObjectPtr<UStaticMesh>> AllSourceMeshes)
{
	CombinedData.Materials.Empty();
	CombinedData.SectionsMap.Empty();

	for (int32 MeshIndex = 0; MeshIndex < AllSourceMeshes.Num(); MeshIndex++)
	{
		if (UStaticMesh* StaticMesh = AllSourceMeshes[MeshIndex])
		{
			TArray<FStaticMaterial>& StaticMaterials = StaticMesh->GetStaticMaterials();
			FStaticMeshRenderData* MeshRenderData = StaticMesh->GetRenderData();
			for (int32 LODIndex = 0; LODIndex < MeshRenderData->LODResources.Num(); LODIndex++)
			{
				FStaticMeshLODResources& LODResource = MeshRenderData->LODResources[LODIndex];
				for (int32 SectionIndex = 0; SectionIndex < LODResource.Sections.Num(); SectionIndex++)
				{
					FStaticMeshSection& RenderSection = LODResource.Sections[SectionIndex];
					FStaticMaterial& StaticMaterial = StaticMaterials[RenderSection.MaterialIndex];
					int32 MappedMatIndex = CombinedData.Materials.AddUnique(StaticMaterial.MaterialInterface);
					FLiteGPUSceneMeshSectionInfo SectionInfo;
					SectionInfo.MeshName = StaticMesh->GetName();
					SectionInfo.MaterialIndex = MappedMatIndex;
					SectionInfo.SectionIndex = SectionIndex;
					SectionInfo.LODIndex = LODIndex;
					SectionInfo.MeshIndex = MeshIndex;
					FillMeshLODSectionData(
						LODIndex
						, MeshRenderData
						, LODResource
						, RenderSection
						, CombinedData.Vertices
						, CombinedData.Indices
						, SectionInfo.FirstVertexOffset
						, SectionInfo.VertexCount
						, SectionInfo.FirstIndexOffset
						, SectionInfo.IndexCount
						, SectionInfo.ScreenSizeMin
						, SectionInfo.ScreenSizeMax
					);
					if (auto* pSections = CombinedData.SectionsMap.Find(MappedMatIndex))
					{
						pSections->Add(SectionInfo);
					}
					else
					{
						TArray<FLiteGPUSceneMeshSectionInfo> Sections;
						Sections.Add(SectionInfo);
						CombinedData.SectionsMap.Add(MappedMatIndex, Sections);
					}
				}
			}
		}
	}
	// A safety check
	for (uint32 Index : CombinedData.Indices)
	{
		check((int32)Index < CombinedData.Vertices.Num());
	}	
	// Do upload
	CombinedBuffer.Initialize(CombinedData.Vertices, CombinedData.Indices);
}

void FLiteGPUScene::ConstructSceneData(const TArray<TObjectPtr<UStaticMesh>> AllSourceMeshes)
{
	SceneData.SourceMeshes = AllSourceMeshes;
	for (auto& [K, V] : CombinedData.SectionsMap)
	{
		for (auto& Patch : V)
		{
			Patch.PatchID = SceneData.SectionInfos.Num();
			SceneData.SectionInfos.Add(Patch);
		}
	}
	// Calculate teh max lod numbers among all meshes
	for (UStaticMesh* StaticMesh : AllSourceMeshes)
	{
		if (StaticMesh)
		{
			if (FStaticMeshRenderData* MeshRenderData = StaticMesh->GetRenderData())
			{
				PerSectionMaxNum = FMath::Max(PerSectionMaxNum, MeshRenderData->LODResources.Num());
			}
		}
	}
	// Another safety check
	for (auto& SectionInfo : SceneData.SectionInfos)
	{
		check(SectionInfo.FirstVertexOffset + SectionInfo.VertexCount <= (uint32)CombinedData.Vertices.Num());
		check(SectionInfo.FirstIndexOffset + SectionInfo.IndexCount <= (uint32)CombinedData.Indices.Num());
	}
	// Some initialization
	SceneData.InstanceTypeNum = SceneData.SourceMeshes.Num();
	SceneData.TotalSectionNum = SceneData.SectionInfos.Num();
	SceneData.SectionMeshIndices.SetNum(SceneData.TotalSectionNum);
	SceneData.Active.SectionInstanceNums.SetNum(SceneData.TotalSectionNum);
	// Collects the <patchid, meshid> array
	int32 PatchIndex = 0;
	for (auto& Pair : CombinedData.SectionsMap)
	{
		const TArray<FLiteGPUSceneMeshSectionInfo>& SectionInfoArray = Pair.Value;
		for (int32 Index = 0; Index < SectionInfoArray.Num(); Index++)
		{
			const auto& SectionInfo = SectionInfoArray[Index];
			SceneData.SectionMeshIndices[PatchIndex] = SectionInfo.MeshIndex;
			PatchIndex++;
		}
	}
	// Stores AABB with BL-UR format
	TArray<FVector4f> AABBs;
	AABBs.SetNum(2 * SceneData.InstanceTypeNum);
	for (int32 FoliageIndex = 0; FoliageIndex < SceneData.InstanceTypeNum; FoliageIndex++)
	{
		if (AllSourceMeshes[FoliageIndex])
		{
			FBoxSphereBounds MeshBound = AllSourceMeshes[FoliageIndex]->GetBounds();
			FBox Box = MeshBound.GetBox();
			FVector Min = Box.Min;
			FVector Max = Box.Max;
			AABBs[FoliageIndex * 2 + 0] = FVector4f(Min.X, Min.Y, Min.Z, MeshBound.SphereRadius);
			AABBs[FoliageIndex * 2 + 1] = FVector4f(Max.X, Max.Y, Max.Z, 0);
		}
		else
		{
			AABBs[FoliageIndex * 2 + 0] = FVector4f(-50.0f, -50.0f, -50.0f, 0);
			AABBs[FoliageIndex * 2 + 1] = FVector4f(50.0f, 50.0f, 50.0f, 0);
		}
	}
	SceneData.SectionAABBData.SetNum(2 * sizeof(FVector4f) * SceneData.InstanceTypeNum);
	FMemory::Memcpy(SceneData.SectionAABBData.GetData(), AABBs.GetData(), 2 * SceneData.InstanceTypeNum * sizeof(FVector4f));
	// Do upload
}

void FLiteGPUScene::ConstructScene(const TArray<TObjectPtr<UStaticMesh>> AllSourceMeshes)
{
	// CombinedData
	ConstructCombinedData(AllSourceMeshes);
	// SceneData
	ConstructSceneData(AllSourceMeshes);
}

void FLiteGPUCombinedBuffer::Initialize(const TArray<FLiteGPUSceneMeshVertex>& Vertices, const TArray<uint32>& Indices)
{

}

void FLiteGPUCombinedBuffer::Release_RenderingThread()
{

}