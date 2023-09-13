#include "LiteGPUScene.h"
#include "RenderGraphBuilder.h"

DEFINE_LOG_CATEGORY_STATIC(LogLiteGPUScene, Log, All);

FLiteGPUScene::FLiteGPUScene()
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
		BufferState.SectionInfoBuffer = ResizeStructuredBufferSOAIfNeeded(GraphBuilder, SectionInfoBuffer, ResizeParams, TEXT("LiteGPUScene.SectionInfos"));
	
		struct FTaskContext
		{
			FRDGScatterUploader* SectionInfoUploader = nullptr;
		};
		FTaskContext& TaskContext = *GraphBuilder.AllocObject<FTaskContext>();
		TaskContext.SectionInfoUploader = SectionInfoUploadBuffer.Begin(GraphBuilder, BufferState.SectionInfoBuffer, SceneData.TotalSectionNum * 2, sizeof(FVector4f), TEXT("LiteGPUScene.Upload.SectionInfos"));
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
		SectionInfoBuffer.SafeRelease();
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
		BufferState.MeshAABBBuffer = ResizeStructuredBufferSOAIfNeeded(GraphBuilder, MeshAABBBuffer, ResizeParams, TEXT("LiteGPUScene.AABBs"));
	
		struct FTaskContext
		{
			FRDGScatterUploader* AABBUploader = nullptr;
		};
		FTaskContext& TaskContext = *GraphBuilder.AllocObject<FTaskContext>();
		TaskContext.AABBUploader = MeshAABBUploadBuffer.Begin(GraphBuilder, BufferState.MeshAABBBuffer, SceneData.InstanceTypeNum * 2, sizeof(FVector4f), TEXT("LiteGPUScene.Upload.AABBs"));
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
		MeshAABBBuffer.SafeRelease();
	}
}

DECLARE_GPU_STAT(LiteGPUSceneUpdate)

void FLiteGPUScene::UpdateInstanceData(FRDGBuilder& GraphBuilder)
{
	const auto Capacity = SceneData.InstanceCapacity;
	if (Capacity != 0)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, LiteGPUSceneUpdate);

		// TYPE & SECTION INFOS
		BufferState.InstanceIndicesBuffer = ResizeStructuredBufferIfNeeded(GraphBuilder, InstanceIndicesBuffer, Capacity * sizeof(uint32), TEXT("LiteGPUScene.Indices"));
		BufferState.InstanceAttributeBuffer = ResizeStructuredBufferIfNeeded(GraphBuilder, InstanceAttributeBuffer, Capacity * sizeof(FLiteGPUInstanceAttribute), TEXT("LiteGPUScene.Attributes"));

		// SECTORS
		const auto SectorCount = SceneData.SectorInfos.Num();
		BufferState.SectorInfoBuffer = ResizeStructuredBufferIfNeeded(GraphBuilder, SectorInfoBuffer, SectorCount * sizeof(FSectorInfo), TEXT("LiteGPUScene.SectorInfos"));
		BufferState.SectorInfoBufferSRV = GraphBuilder.CreateSRV(BufferState.SectorInfoBuffer);

		// TRANSFORMS
		BufferState.InstanceTransformBuffer = ResizeStructuredBufferIfNeeded(GraphBuilder, InstanceTransformBuffer, 4 * Capacity * sizeof(FVector4f), TEXT("LiteGPUScene.Transforms"));
		BufferState.InstanceTransformBufferSRV = GraphBuilder.CreateSRV(BufferState.InstanceTransformBuffer);
		BufferState.InstanceSectorIDBuffer = ResizeStructuredBufferIfNeeded(GraphBuilder, InstanceSectorIDBuffer, Capacity * sizeof(uint32), TEXT("LiteGPUScene.SectorIDs"));
		BufferState.InstanceSectorIDBufferSRV = GraphBuilder.CreateSRV(BufferState.InstanceSectorIDBuffer);
		
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
			FRDGScatterUploader* SectorInfoUploader = nullptr;
			FRDGScatterUploader* InstanceTransformUploader = nullptr;
			FRDGScatterUploader* InstanceSectorIDUploader = nullptr;
			FRDGScatterUploader* InstanceAttributeUploader = nullptr;
		};
		FTaskContext& TaskContext = *GraphBuilder.AllocObject<FTaskContext>();
		TaskContext.SectorInfoUploader = SectorInfoUploadBuffer.Begin(GraphBuilder, BufferState.SectorInfoBuffer, SectorCount, sizeof(FSectorInfo), TEXT("LiteGPUScene.Upload.SectorInfos"));
		TaskContext.InstanceTransformUploader = InstanceTransformUploadBuffer.Begin(GraphBuilder, BufferState.InstanceTransformBuffer, 4 * InstanceCount, sizeof(FVector4f), TEXT("LiteGPUScene.Upload.Transforms"));
		TaskContext.InstanceSectorIDUploader = InstanceSectorIDUploadBuffer.Begin(GraphBuilder, BufferState.InstanceSectorIDBuffer, InstanceCount, sizeof(uint32), TEXT("LiteGPUScene.Upload.SectorIDs"));
		TaskContext.InstanceAttributeUploader = InstanceAttributeUploadBuffer.Begin(GraphBuilder, BufferState.InstanceAttributeBuffer, InstanceCount, sizeof(FLiteGPUInstanceAttribute), TEXT("LiteGPUScene.Upload.Attributes"));
		GraphBuilder.AddCommandListSetupTask(
			[this, &TaskContext, Update](FRHICommandListBase& RHICmdList)
			{
				SCOPED_NAMED_EVENT(UpdateLiteGPUScene_Instances, FColor::Green);

				LockIfValid(RHICmdList, TaskContext.SectorInfoUploader);
				LockIfValid(RHICmdList, TaskContext.InstanceTransformUploader);
				LockIfValid(RHICmdList, TaskContext.InstanceSectorIDUploader);
				LockIfValid(RHICmdList, TaskContext.InstanceAttributeUploader);

				uint32 SectorIndex = 0;
				for (auto SectorInfo : SceneData.SectorInfos)
				{
					TaskContext.SectorInfoUploader->Add(SectorIndex, &SectorInfo);
					SectorIndex += 1;
				}

				for (auto Index : Update.InstanceIndices)
				{
					TaskContext.InstanceTransformUploader->Add(Index, SceneData.InstanceTransforms.GetData() + Index);
					TaskContext.InstanceSectorIDUploader->Add(Index, SceneData.InstanceSectorIDs.GetData() + Index);

					FLiteGPUInstanceAttribute Attribute;
					Attribute.Type = SceneData.InstanceTypes[Index];
					Attribute.SectionID = SceneData.InstanceSectionIDs[Index];
					Attribute.SectionNum = SceneData.InstanceSectionNums[Index];
					TaskContext.InstanceAttributeUploader->Add(Index, &Attribute);
				}

				UnlockIfValid(RHICmdList, TaskContext.SectorInfoUploader);
				UnlockIfValid(RHICmdList, TaskContext.InstanceTransformUploader);
				UnlockIfValid(RHICmdList, TaskContext.InstanceSectorIDUploader);
				UnlockIfValid(RHICmdList, TaskContext.InstanceAttributeUploader);
			});
		{
			SectorInfoUploadBuffer.End(GraphBuilder, TaskContext.SectorInfoUploader);
			InstanceTransformUploadBuffer.End(GraphBuilder, TaskContext.InstanceTransformUploader);
			InstanceSectorIDUploadBuffer.End(GraphBuilder, TaskContext.InstanceSectorIDUploader);
			InstanceAttributeUploadBuffer.End(GraphBuilder, TaskContext.InstanceAttributeUploader);
		}
		{
			SectorInfoUploadBuffer.Release();
			InstanceTransformUploadBuffer.Release();
			InstanceSectorIDUploadBuffer.Release();
			InstanceAttributeUploadBuffer.Release();
		}
	}
	else
	{
		InstanceIndicesBuffer.SafeRelease();
		InstanceTransformBuffer.SafeRelease();
		InstanceSectorIDBuffer.SafeRelease();
		InstanceAttributeBuffer.SafeRelease();
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