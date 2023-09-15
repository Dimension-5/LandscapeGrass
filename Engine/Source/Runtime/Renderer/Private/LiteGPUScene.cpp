#include "LiteGPUScene.h"
#include "Components/LiteGPUSceneComponent.h"
#include "RenderGraphBuilder.h"

DEFINE_LOG_CATEGORY_STATIC(LogLiteGPUScene, Log, All);

FLiteGPUScene::FLiteGPUScene(ERHIFeatureLevel::Type InFeatureLevel)
	: FeatureLevel(InFeatureLevel)
{

}

FLiteGPUScene::~FLiteGPUScene()
{

}

void FLiteGPUScene::fillMeshLODSectionData(int32 LodIndex, const FStaticMeshRenderData* MeshRenderData, 
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

	uint32 VerticesCount = 0;
	uint32 IndicesCount = 0;
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
		FLiteGPUSceneMeshVertex MeshVertex(Pos, TangentX, TangentZ, UV, UV2, UV3, VertexColor);
		OutCombinedVertexBufferData.Add(MeshVertex);
		VerticesCount += 1;
		IndicesMap.Add(RealVertexIdx, RawVertexIdx);

		RawVertexIdx++;
	}

	for (uint32 IndiceIdx = 0; IndiceIdx < (RenderSection.NumTriangles * 3); IndiceIdx++)
	{
		uint32 RealVertexIndex = TotalIndices[RenderSection.FirstIndex + IndiceIdx];
		uint32* pMappedIndex = IndicesMap.Find(RealVertexIndex);
		IndicesCount += 1;
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

	OutIndicesCount = IndicesCount;
	OutVertexCount = VerticesCount;
}

void FLiteGPUScene::buildCombinedData(const TArray<TObjectPtr<UStaticMesh>> AllSourceMeshes)
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
					fillMeshLODSectionData(
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

void FLiteGPUScene::buildSceneData(const TArray<TObjectPtr<UStaticMesh>> AllSourceMeshes)
{
	SceneData.SourceMeshes = AllSourceMeshes;
	for (auto& [K, V] : CombinedData.SectionsMap)
	{
		for (auto& Section : V)
		{
			Section.PatchID = SceneData.SectionInfos.Num();
			SceneData.SectionInfos.Add(Section);
		}
	}
	// Calculate teh max lod numbers among all meshes
	for (UStaticMesh* StaticMesh : AllSourceMeshes)
	{
		if (StaticMesh)
		{
			if (FStaticMeshRenderData* MeshRenderData = StaticMesh->GetRenderData())
			{
				SceneData.PerSectionMaxNum = FMath::Max(SceneData.PerSectionMaxNum, MeshRenderData->LODResources.Num());
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
	SceneData.SectionInstanceNums.SetNum(SceneData.TotalSectionNum);
	// Stores AABB with BL-UR format
	TArray<FVector4f> AABBs;
	AABBs.SetNum(2 * SceneData.InstanceTypeNum);
	for (int32 Index = 0; Index < SceneData.InstanceTypeNum; Index++)
	{
		if (AllSourceMeshes[Index])
		{
			FBoxSphereBounds MeshBound = AllSourceMeshes[Index]->GetBounds();
			FBox Box = MeshBound.GetBox();
			FVector Min = Box.Min;
			FVector Max = Box.Max;
			AABBs[Index * 2 + 0] = FVector4f(Min.X, Min.Y, Min.Z, MeshBound.SphereRadius);
			AABBs[Index * 2 + 1] = FVector4f(Max.X, Max.Y, Max.Z, 0);
		}
		else
		{
			AABBs[Index * 2 + 0] = FVector4f(-50.0f, -50.0f, -50.0f, 0);
			AABBs[Index * 2 + 1] = FVector4f(50.0f, 50.0f, 50.0f, 0);
		}
	}
	SceneData.SectionAABBData.SetNum(2 * sizeof(FVector4f) * SceneData.InstanceTypeNum);
	FMemory::Memcpy(SceneData.SectionAABBData.GetData(), AABBs.GetData(), 2 * SceneData.InstanceTypeNum * sizeof(FVector4f));
}

void FLiteGPUScene::BuildScene(const TArray<TObjectPtr<UStaticMesh>> AllSourceMeshes)
{
	buildCombinedData(AllSourceMeshes);
	buildSceneData(AllSourceMeshes);
}

void FLiteGPUScene::UpdateSectionInfos(FRDGBuilder& GraphBuilder)
{
	if (SceneData.TotalSectionNum != 0)
	{
		TResourceArray<FVector4f, VERTEXBUFFER_ALIGNMENT> SectionInfoBufferArray;
		SectionInfoBufferArray.SetNumUninitialized(SceneData.TotalSectionNum * 2);
		for (int32 SectionIndex = 0; SectionIndex < SceneData.TotalSectionNum; SectionIndex++)
		{
			const FLiteGPUSceneMeshSectionInfo& SectionInfo = SceneData.SectionInfos[SectionIndex];
			SectionInfoBufferArray[2 * SectionIndex + 0] = FVector4f(SectionInfo.FirstIndexOffset, SectionInfo.IndexCount, SectionInfo.FirstVertexOffset, SectionInfo.VertexCount);
			SectionInfoBufferArray[2 * SectionIndex + 1] = FVector4f(SectionInfo.ScreenSizeMin, SectionInfo.ScreenSizeMax, SectionInfo.MeshIndex, SectionInfo.MaterialIndex);
		}
		FResizeResourceSOAParams ResizeParams;
		ResizeParams.NumBytes = SceneData.TotalSectionNum * 2 * sizeof(FVector4f);
		ResizeParams.NumArrays = SceneData.TotalSectionNum * 2;
		auto SectionInfoBuffer = ResizeStructuredBufferSOAIfNeeded(GraphBuilder, BufferState.SectionInfoBuffer, ResizeParams, TEXT("LiteGPUScene.SectionInfos"));
	
		struct FTaskContext
		{
			FRDGScatterUploader* SectionInfoUploader = nullptr;
		};
		FTaskContext& TaskContext = *GraphBuilder.AllocObject<FTaskContext>();
		TaskContext.SectionInfoUploader = SectionInfoUploadBuffer.Begin(GraphBuilder, SectionInfoBuffer, SceneData.TotalSectionNum * 2, sizeof(FVector4f), TEXT("LiteGPUScene.Upload.SectionInfos"));
		GraphBuilder.AddCommandListSetupTask([&TaskContext, SectionInfoBufferArray](FRHICommandListBase& RHICmdList) {
			SCOPED_NAMED_EVENT(UpdateLiteGPUScene_SectionInfos, FColor::Green);
			LockIfValid(RHICmdList, TaskContext.SectionInfoUploader);

			TaskContext.SectionInfoUploader->Add(0, SectionInfoBufferArray.GetData(), SectionInfoBufferArray.Num());

			UnlockIfValid(RHICmdList, TaskContext.SectionInfoUploader);
		});
		SectionInfoUploadBuffer.End(GraphBuilder, TaskContext.SectionInfoUploader);
		SectionInfoUploadBuffer.Release();
	}
	else
	{
		BufferState.SectionInfoBuffer.SafeRelease();
	}
}

void FLiteGPUScene::UpdateAABBData(FRDGBuilder& GraphBuilder)
{
	if (SceneData.InstanceTypeNum != 0)
	{
		TResourceArray<FVector4f, VERTEXBUFFER_ALIGNMENT> AABBArray;
		AABBArray.SetNumUninitialized(SceneData.InstanceTypeNum * 2);
		FMemory::Memcpy(AABBArray.GetData(), SceneData.SectionAABBData.GetData(), 2 * SceneData.InstanceTypeNum * sizeof(FVector4f));
		FResizeResourceSOAParams ResizeParams;
		ResizeParams.NumBytes = SceneData.InstanceTypeNum * 2 * sizeof(FVector4f);
		ResizeParams.NumArrays = SceneData.InstanceTypeNum * 2;
		auto MeshAABBBuffer = ResizeStructuredBufferSOAIfNeeded(GraphBuilder, BufferState.MeshAABBBuffer, ResizeParams, TEXT("LiteGPUScene.AABBs"));
	
		struct FTaskContext
		{
			FRDGScatterUploader* AABBUploader = nullptr;
		};
		FTaskContext& TaskContext = *GraphBuilder.AllocObject<FTaskContext>();
		TaskContext.AABBUploader = MeshAABBUploadBuffer.Begin(GraphBuilder, MeshAABBBuffer, SceneData.InstanceTypeNum * 2, sizeof(FVector4f), TEXT("LiteGPUScene.Upload.AABBs"));
		GraphBuilder.AddCommandListSetupTask([&TaskContext, AABBArray](FRHICommandListBase& RHICmdList) {
			SCOPED_NAMED_EVENT(UpdateLiteGPUScene_AABBs, FColor::Green);
			LockIfValid(RHICmdList, TaskContext.AABBUploader);

			TaskContext.AABBUploader->Add(0, AABBArray.GetData(), AABBArray.Num());

			UnlockIfValid(RHICmdList, TaskContext.AABBUploader);
			});
		MeshAABBUploadBuffer.End(GraphBuilder, TaskContext.AABBUploader);
		MeshAABBUploadBuffer.Release();
	}
	else
	{
		BufferState.MeshAABBBuffer.SafeRelease();
	}
}

DECLARE_GPU_STAT(LiteGPUSceneUpdate)

void FLiteGPUScene::UpdateSectionBuffers(FRDGBuilder& GraphBuilder)
{
	const auto SectionCount = SceneData.TotalSectionNum;
	if (SectionCount != 0)
	{
		{
			auto Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), SectionCount);
			ResizeBufferIfNeeded(GraphBuilder, CounterBufferState.RWSectionCountBuffer, Desc, TEXT("LiteGPUScene.Counters.SectionCount"));
		}		
		{
			auto Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), SectionCount);
			ResizeBufferIfNeeded(GraphBuilder, CounterBufferState.RWSectionCountCopyBuffer, Desc, TEXT("LiteGPUScene.Counters.SectionCountCopy"));
		}
		{
			auto Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), SectionCount);
			ResizeBufferIfNeeded(GraphBuilder, CounterBufferState.RWSectionCountOffsetBuffer, Desc, TEXT("LiteGPUScene.Counters.SectionCountOffset"));
		}		
		{
			auto Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), SectionCount);
			ResizeBufferIfNeeded(GraphBuilder, CounterBufferState.RWNextSectionCountOffsetBuffer, Desc, TEXT("LiteGPUScene.Counters.NextSectionCountOffset"));
		}
	}
}

void FLiteGPUScene::UpdateViewBuffers(FRDGBuilder& GraphBuilder)
{
	const auto Capacity = SceneData.InstanceCapacity;
	if (Capacity != 0)
	{
		// VIEW BUFFERS
		{
			auto Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(float), Capacity);
			// Desc.Usage |= EBufferUsageFlags::DrawIndirect;
			auto UnCulledInstanceScreenSize = ResizeBufferIfNeeded(GraphBuilder, ViewBufferState.RWUnCulledInstanceScreenSize, Desc, TEXT("LiteGPUScene.View.ScreenSizes"));
		}
		{
			auto Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), Capacity);
			// Desc.Usage |= EBufferUsageFlags::DrawIndirect;
			auto UnCulledInstanceBuffer = ResizeBufferIfNeeded(GraphBuilder, ViewBufferState.RWUnCulledInstanceBuffer, Desc, TEXT("LiteGPUScene.View.UnCulledInstances"));
		}
		{
			auto Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1);
			// Desc.Usage |= EBufferUsageFlags::DrawIndirect;
			auto UnCulledInstanceNum = ResizeBufferIfNeeded(GraphBuilder, ViewBufferState.RWUnCulledInstanceNum, Desc, TEXT("LiteGPUScene.View.UnCulledInstanceNum"));
		}
		{
			auto Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 3);
			Desc.Usage |= EBufferUsageFlags::DrawIndirect;
			auto IndirectDrawDispatchIndiretBuffer = ResizeBufferIfNeeded(GraphBuilder, ViewBufferState.RWIndirectDrawDispatchIndiretBuffer, Desc, TEXT("LiteGPUScene.View.DrawDispatchIndirectBuffer"));
		}
		{
			auto Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(float), Capacity * SceneData.PerSectionMaxNum);
			Desc.Usage |= EBufferUsageFlags::VertexBuffer;
			auto InstanceIndiceBuffer = ResizeBufferIfNeeded(GraphBuilder, ViewBufferState.RWInstanceIndiceBuffer, Desc, TEXT("LiteGPUScene.View.DrawIndices"));
		
			CurrentIndicesVertexBuffer = ViewBufferState.RWInstanceIndiceBuffer->GetRHI();
			auto pVF = DynamicVFMap.Find(CurrentIndicesVertexBuffer);
			if (pVF == nullptr)
			{
				DynamicVF NewVF = {};
				NewVF.GPUIndicesVertexBuffer = new FVertexBuffer();
				NewVF.GPUIndicesVertexBuffer->InitResource(FRHICommandListExecutor::GetImmediateCommandList());
				NewVF.GPUIndicesVertexBuffer->VertexBufferRHI = CurrentIndicesVertexBuffer;
				
				NewVF.pGPUDrivenVertexFactory = new FLiteGPUSceneVertexFactory(FeatureLevel);
				NewVF.pGPUDrivenVertexFactory->Init_RenderThread(CombinedBuffer.VertexBuffer, NewVF.GPUIndicesVertexBuffer);
				BeginInitResource(NewVF.pGPUDrivenVertexFactory);

				DynamicVFMap.Add(CurrentIndicesVertexBuffer, NewVF);
			}
		}
		{
			auto Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 5 * SceneData.TotalSectionNum);
			Desc.Usage |= EBufferUsageFlags::DrawIndirect;
			auto IndirectDrawBuffer = ResizeBufferIfNeeded(GraphBuilder, ViewBufferState.RWIndirectDrawBuffer, Desc, TEXT("LiteGPUScene.View.IndirectDrawBuffer"));
		}
		{
			auto Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 5);
			Desc.Usage |= EBufferUsageFlags::DrawIndirect;
			auto UnCulledInstanceIndirectParameters = ResizeBufferIfNeeded(GraphBuilder, ViewBufferState.RWUnCulledInstanceIndirectParameters, Desc, TEXT("LiteGPUScene.View.UnCulledInstanceIndirectParameters"));
		}
	}
}


FRDGBuffer* ResizeStructuredBufferIfNeededAligned(FRDGBuilder& GraphBuilder, TRefCountPtr<FRDGPooledBuffer>& ExternalBuffer, uint32 NumBytes, const TCHAR* Name)
{
	NumBytes = Align(NumBytes, 16);
	return ResizeStructuredBufferIfNeeded(GraphBuilder, ExternalBuffer, NumBytes, Name);
}

void FLiteGPUScene::UpdateInstanceData(FRDGBuilder& GraphBuilder)
{
	const auto Capacity = SceneData.InstanceCapacity;
	const auto SectorCount = SceneData.SectorInfos.Num();
	if (Capacity != 0)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, LiteGPUSceneUpdate);

		UpdateSectionBuffers(GraphBuilder);
		UpdateViewBuffers(GraphBuilder);

		auto InstanceTypeBufferDesc = FRDGBufferDesc::CreateByteAddressDesc(sizeof(uint32) *  Capacity);
		auto InstanceTypeBuffer = ResizeBufferIfNeeded(GraphBuilder, BufferState.InstanceTypeBuffer, InstanceTypeBufferDesc, TEXT("LiteGPUScene.Types"));
		
		auto InstanceSectionNumBufferDesc = FRDGBufferDesc::CreateByteAddressDesc(sizeof(uint32) * Capacity);
		auto InstanceSectionNumBuffer = ResizeBufferIfNeeded(GraphBuilder, BufferState.InstanceSectionNumBuffer, InstanceSectionNumBufferDesc, TEXT("LiteGPUScene.SectionNums"));
		
		auto InstanceSectionIDBufferDesc = FRDGBufferDesc::CreateByteAddressDesc(sizeof(uint32) * Capacity * SceneData.PerSectionMaxNum);
		auto InstanceSectionIDBuffer = ResizeBufferIfNeeded(GraphBuilder, BufferState.InstanceSectionIDBuffer, InstanceSectionIDBufferDesc, TEXT("LiteGPUScene.SectionIDs"));

		auto SectorInfoBufferDesc = FRDGBufferDesc::CreateByteAddressDesc(sizeof(FSectorInfo) * SectorCount);
		auto SectorInfoBuffer = ResizeBufferIfNeeded(GraphBuilder, BufferState.SectorInfoBuffer, SectorInfoBufferDesc, TEXT("LiteGPUScene.SectorInfos"));
		
		auto InstanceSectorIDBufferDesc = FRDGBufferDesc::CreateByteAddressDesc(sizeof(uint32) * Capacity);
		auto InstanceSectorIDBuffer = ResizeBufferIfNeeded(GraphBuilder, BufferState.InstanceSectorIDBuffer, InstanceSectorIDBufferDesc, TEXT("LiteGPUScene.SectorIDs"));

		auto InstanceTransformBufferA = ResizeStructuredBufferIfNeededAligned(GraphBuilder, BufferState.InstanceTransformBufferA, Capacity * sizeof(FVector4f), TEXT("LiteGPUScene.TransformsA"));
		auto InstanceTransformBufferB = ResizeStructuredBufferIfNeededAligned(GraphBuilder, BufferState.InstanceTransformBufferB, Capacity * sizeof(FVector4f), TEXT("LiteGPUScene.TransformsB"));
		auto InstanceTransformBufferC = ResizeStructuredBufferIfNeededAligned(GraphBuilder, BufferState.InstanceTransformBufferC, Capacity * sizeof(FVector4f), TEXT("LiteGPUScene.TransformsC"));
		auto InstanceTransformBufferD = ResizeStructuredBufferIfNeededAligned(GraphBuilder, BufferState.InstanceTransformBufferD, Capacity * sizeof(FVector4f), TEXT("LiteGPUScene.TransformsD"));
		
		FLiteGPUSceneUpdate Update;
		{
			FScopeLock LCK(&SceneUpdatesMutex);
			if (!SceneData.Updates.Dequeue(Update))
				return;
			if (Update.InstanceNum == 0)
				return;
		}
		const auto InstanceCount = Update.InstanceNum;
		struct FTaskContext
		{
			FRDGScatterUploader* InstanceTypeUploader = nullptr;
			FRDGScatterUploader* InstanceSectionNumUploader = nullptr;
			FRDGScatterUploader* InstanceSectionIDUploader = nullptr;

			FRDGScatterUploader* SectorInfoUploader = nullptr;
			FRDGScatterUploader* InstanceTransformUploaderA = nullptr;
			FRDGScatterUploader* InstanceTransformUploaderB = nullptr;
			FRDGScatterUploader* InstanceTransformUploaderC = nullptr;
			FRDGScatterUploader* InstanceTransformUploaderD = nullptr;
			FRDGScatterUploader* InstanceSectorIDUploader = nullptr;
		};
		FTaskContext& TaskContext = *GraphBuilder.AllocObject<FTaskContext>();
		TaskContext.SectorInfoUploader = SectorInfoUploadBuffer.Begin(GraphBuilder, SectorInfoBuffer, SectorCount, sizeof(FSectorInfo), TEXT("LiteGPUScene.Upload.SectorInfos"));
		TaskContext.InstanceTransformUploaderA = InstanceTransformUploadBufferA.Begin(GraphBuilder, InstanceTransformBufferA, InstanceCount, sizeof(FVector4f), TEXT("LiteGPUScene.Upload.TransformsA"));
		TaskContext.InstanceTransformUploaderB = InstanceTransformUploadBufferB.Begin(GraphBuilder, InstanceTransformBufferB, InstanceCount, sizeof(FVector4f), TEXT("LiteGPUScene.Upload.TransformsB"));
		TaskContext.InstanceTransformUploaderC = InstanceTransformUploadBufferC.Begin(GraphBuilder, InstanceTransformBufferC, InstanceCount, sizeof(FVector4f), TEXT("LiteGPUScene.Upload.TransformsC"));
		TaskContext.InstanceTransformUploaderD = InstanceTransformUploadBufferD.Begin(GraphBuilder, InstanceTransformBufferD, InstanceCount, sizeof(FVector4f), TEXT("LiteGPUScene.Upload.TransformsD"));
		TaskContext.InstanceSectorIDUploader = InstanceSectorIDUploadBuffer.Begin(GraphBuilder, InstanceSectorIDBuffer, InstanceCount, sizeof(uint32), TEXT("LiteGPUScene.Upload.SectorIDs"));
		TaskContext.InstanceTypeUploader = InstanceTypeUploadBuffer.Begin(GraphBuilder, InstanceTypeBuffer, InstanceCount, sizeof(uint32), TEXT("LiteGPUScene.Upload.Types"));
		TaskContext.InstanceSectionNumUploader = InstanceSectionNumUploadBuffer.Begin(GraphBuilder, InstanceSectionNumBuffer, InstanceCount, sizeof(uint32), TEXT("LiteGPUScene.Upload.SectionNums"));
		TaskContext.InstanceSectionIDUploader = InstanceSectionIDUploadBuffer.Begin(GraphBuilder, InstanceSectionIDBuffer, SceneData.PerSectionMaxNum * InstanceCount, sizeof(uint32), TEXT("LiteGPUScene.Upload.SectionIDs"));
		GraphBuilder.AddCommandListSetupTask(
			[this, &TaskContext, Update](FRHICommandListBase& RHICmdList)
			{
				SCOPED_NAMED_EVENT(UpdateLiteGPUScene_Instances, FColor::Green);

				LockIfValid(RHICmdList, TaskContext.SectorInfoUploader);
				LockIfValid(RHICmdList, TaskContext.InstanceTransformUploaderA);
				LockIfValid(RHICmdList, TaskContext.InstanceTransformUploaderB);
				LockIfValid(RHICmdList, TaskContext.InstanceTransformUploaderC);
				LockIfValid(RHICmdList, TaskContext.InstanceTransformUploaderD);
				LockIfValid(RHICmdList, TaskContext.InstanceSectorIDUploader);
				LockIfValid(RHICmdList, TaskContext.InstanceTypeUploader);
				LockIfValid(RHICmdList, TaskContext.InstanceSectionNumUploader);
				LockIfValid(RHICmdList, TaskContext.InstanceSectionIDUploader);

				uint32 SectorIndex = 0;
				for (auto SectorInfo : SceneData.SectorInfos)
				{
					TaskContext.SectorInfoUploader->Add(SectorIndex, &SectorInfo);
					SectorIndex += 1;
				}

				for (auto Index : Update.InstanceIndices)
				{
					TaskContext.InstanceSectorIDUploader->Add(Index, SceneData.InstanceSectorIDs.GetData() + Index);

					const auto Transform = SceneData.InstanceTransforms[Index];
					const auto Matrix = Transform.ToMatrixWithScale();
					const FVector4f* pMatrix = (FVector4f*)&Matrix;
					TaskContext.InstanceTransformUploaderA->Add(Index, pMatrix);
					TaskContext.InstanceTransformUploaderB->Add(Index, pMatrix + 1);
					TaskContext.InstanceTransformUploaderC->Add(Index, pMatrix + 2);
					TaskContext.InstanceTransformUploaderD->Add(Index, pMatrix + 3);

					uint32 Type = SceneData.InstanceTypes[Index];
					uint32 SectionNum = SceneData.InstanceSectionNums[Index];
					TaskContext.InstanceTypeUploader->Add(Index, &Type);
					TaskContext.InstanceSectionNumUploader->Add(Index, &SectionNum);

					const int32 InstanceSectionStart = Index * SceneData.PerSectionMaxNum;
					for (int32 SectionIndex = 0; SectionIndex < SceneData.PerSectionMaxNum; SectionIndex++)
					{
						const auto SlotID = InstanceSectionStart + SectionIndex;
						uint32 SectionID = SceneData.InstanceSectionIDs[SlotID];
						TaskContext.InstanceSectionIDUploader->Add(SlotID, &SectionID);
					}
				}

				UnlockIfValid(RHICmdList, TaskContext.SectorInfoUploader);
				UnlockIfValid(RHICmdList, TaskContext.InstanceTransformUploaderA);
				UnlockIfValid(RHICmdList, TaskContext.InstanceTransformUploaderB);
				UnlockIfValid(RHICmdList, TaskContext.InstanceTransformUploaderC);
				UnlockIfValid(RHICmdList, TaskContext.InstanceTransformUploaderD);
				UnlockIfValid(RHICmdList, TaskContext.InstanceSectorIDUploader);
				UnlockIfValid(RHICmdList, TaskContext.InstanceTypeUploader);
				UnlockIfValid(RHICmdList, TaskContext.InstanceSectionNumUploader);
				UnlockIfValid(RHICmdList, TaskContext.InstanceSectionIDUploader);
			});
		{
			SectorInfoUploadBuffer.End(GraphBuilder, TaskContext.SectorInfoUploader);
			InstanceTransformUploadBufferA.End(GraphBuilder, TaskContext.InstanceTransformUploaderA);
			InstanceTransformUploadBufferB.End(GraphBuilder, TaskContext.InstanceTransformUploaderB);
			InstanceTransformUploadBufferC.End(GraphBuilder, TaskContext.InstanceTransformUploaderC);
			InstanceTransformUploadBufferD.End(GraphBuilder, TaskContext.InstanceTransformUploaderD);
			InstanceSectorIDUploadBuffer.End(GraphBuilder, TaskContext.InstanceSectorIDUploader);
			InstanceTypeUploadBuffer.End(GraphBuilder, TaskContext.InstanceTypeUploader);
			InstanceSectionNumUploadBuffer.End(GraphBuilder, TaskContext.InstanceSectionNumUploader);
			InstanceSectionIDUploadBuffer.End(GraphBuilder, TaskContext.InstanceSectionIDUploader);
		}
		{
			SectorInfoUploadBuffer.Release();
			InstanceTransformUploadBufferA.Release();
			InstanceTransformUploadBufferB.Release();
			InstanceTransformUploadBufferC.Release();
			InstanceTransformUploadBufferD.Release();
			InstanceSectorIDUploadBuffer.Release();
			InstanceTypeUploadBuffer.Release();
			InstanceSectionNumUploadBuffer.Release();
			InstanceSectionIDUploadBuffer.Release();
		}
	}
	else
	{
		// InstanceIndicesBuffer.SafeRelease();
		BufferState.InstanceTransformBufferA.SafeRelease();
		BufferState.InstanceTransformBufferB.SafeRelease();
		BufferState.InstanceTransformBufferC.SafeRelease();
		BufferState.InstanceTransformBufferD.SafeRelease();
		BufferState.InstanceSectorIDBuffer.SafeRelease();
		BufferState.InstanceTypeBuffer.SafeRelease();
		BufferState.InstanceSectionNumBuffer.SafeRelease();
		BufferState.InstanceSectionIDBuffer.SafeRelease();
	}
}

void FLiteGPUScene::EnqueueUpdates_TS(const FLiteGPUSceneUpdate&& Update)
{
	FScopeLock Lock(&SceneUpdatesMutex);
	SceneData.Updates.Enqueue(Update);
}

void FLiteGPUSceneMeshVertexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	const auto VertSize = sizeof(FLiteGPUSceneMeshVertex);
	FRHIResourceCreateInfo CreateInfo(TEXT("LiteGPUScene.VertexBuffer"));
	VertexBufferRHI = RHICmdList.CreateVertexBuffer(Vertices.Num() * VertSize, BUF_Static, CreateInfo);

	// Copy the vertex data into the vertex buffer.
	void* Data = RHICmdList.LockBuffer(VertexBufferRHI, 0, Vertices.Num() * VertSize, RLM_WriteOnly);
	FMemory::Memcpy(Data, Vertices.GetData(), Vertices.Num() * VertSize);
	RHICmdList.UnlockBuffer(VertexBufferRHI);

	VerticesNum = Vertices.Num();
	Vertices.Empty();
}

void FLiteGPUSceneMeshIndexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	const auto IndexStride = sizeof(uint32);
	FRHIResourceCreateInfo CreateInfo(TEXT("LiteGPUScene.IndexBuffer"));
	IndexBufferRHI = RHICmdList.CreateIndexBuffer(IndexStride, Indices.Num() * IndexStride, BUF_Static, CreateInfo);

	// Copy the vertex data into the vertex buffer.
	void* Data = RHICmdList.LockBuffer(IndexBufferRHI, 0, Indices.Num() * IndexStride, RLM_WriteOnly);
	FMemory::Memcpy(Data, Indices.GetData(), Indices.Num() * IndexStride);
	RHICmdList.UnlockBuffer(IndexBufferRHI);

	IndicesNum = Indices.Num();
	Indices.Empty();
}

FLiteGPUCombinedBuffer::FLiteGPUCombinedBuffer()
	: VertexBuffer(nullptr), IndexBuffer(nullptr)
	, UsedBytes(0), VertexNum(0)
	, IndiceNum(0), bInitialized(false)
{

}

FLiteGPUCombinedBuffer::~FLiteGPUCombinedBuffer()
{
	Release_AnyThread();
}

void FLiteGPUCombinedBuffer::Initialize(const TArray<FLiteGPUSceneMeshVertex>& Vertices, const TArray<uint32>& Indices)
{
	if (bInitialized)
	{
		return;
	}
	UE_LOG(LogLiteGPUScene, Log, TEXT("FLiteGPUCombinedBuffer::Initialize"));

	check(VertexBuffer == nullptr && IndexBuffer == nullptr);
	VertexBuffer = new FLiteGPUSceneMeshVertexBuffer();
	IndexBuffer = new FLiteGPUSceneMeshIndexBuffer();

	VertexBuffer->Vertices = Vertices;
	IndexBuffer->Indices = Indices;

	VertexNum = Vertices.Num();
	IndiceNum = Indices.Num();
	if (VertexNum > 0)
	{
		BeginInitResource(VertexBuffer);
	}
	if (IndiceNum > 0)
	{
		BeginInitResource(IndexBuffer);
	}

	UsedBytes += sizeof(FLiteGPUSceneMeshVertex) * Vertices.Num();
	UsedBytes += sizeof(uint32) * Indices.Num();
	bInitialized = true;
}

void FLiteGPUCombinedBuffer::Release_RenderingThread()
{
	check(IsInRenderingThread());

	if (bInitialized)
	{
		bInitialized = false;
		VertexBuffer->ReleaseResource();
		IndexBuffer->ReleaseResource();

		delete VertexBuffer;
		delete IndexBuffer;

		VertexBuffer = nullptr;
		IndexBuffer = nullptr;
	}
}

void FLiteGPUCombinedBuffer::Release_AnyThread()
{
	ENQUEUE_RENDER_COMMAND(FLiteGPUCombinedBuffer_Release)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			Release_RenderingThread();
		});
}