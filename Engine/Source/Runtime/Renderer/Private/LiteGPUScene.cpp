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
	// Collect render sections
	for (auto& [K, V] : CombinedData.SectionsMap)
	{
		for (auto& Patch : V)
		{
			Patch.PatchID = SectionInfos.Num();
			SectionInfos.Add(Patch);
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
	for (auto& SectionInfo : SectionInfos)
	{
		check(SectionInfo.FirstVertexOffset + SectionInfo.VertexCount <= (uint32)CombinedData.Vertices.Num());
		check(SectionInfo.FirstIndexOffset + SectionInfo.IndexCount <= (uint32)CombinedData.Indices.Num());
	}
}

void FLiteGPUScene::ConstructCombinedBuffers(const TArray<TObjectPtr<UStaticMesh>> AllSourceMeshes)
{

}

void FLiteGPUScene::ConstructCombinedVertices(const TArray<TObjectPtr<UStaticMesh>> AllSourceMeshes)
{
	ConstructCombinedData(AllSourceMeshes);
	ConstructCombinedBuffers(AllSourceMeshes);
}
