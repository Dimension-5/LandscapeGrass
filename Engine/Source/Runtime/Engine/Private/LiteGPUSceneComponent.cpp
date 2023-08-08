#include "Components/LiteGPUSceneComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "RHIGPUReadback.h"
#include "DrawDebugHelpers.h"
#include "RHICommandList.h"
#include "../Private/SceneRendering.h"
#include "../Private/PostProcess/SceneRenderTargets.h"
#include "../Private/ScenePrivate.h"

DECLARE_CYCLE_STAT(TEXT("GPUDriven Copy Instance Data"), STAT_CopyInstanceData, STATGROUP_LiteGPUScene);
DECLARE_CYCLE_STAT(TEXT("GPUDriven Set Dirty Buffer"), STAT_SetInstanceBuffer, STATGROUP_LiteGPUScene);
DECLARE_CYCLE_STAT(TEXT("GPUDriven Remove Data Memory Swap "), STAT_RemoveDataMemorySwap, STATGROUP_LiteGPUScene);
DECLARE_CYCLE_STAT(TEXT("GPUDriven Update Instance Data"), STAT_UpdateInstanceData, STATGROUP_LiteGPUScene);
DECLARE_CYCLE_STAT(TEXT("GPUDriven Update Increase Instance Data"), STAT_UpdateIncreaseData, STATGROUP_LiteGPUScene);
DECLARE_CYCLE_STAT(TEXT("GPUDriven Update Remove Instance Data"), STAT_UpdateRemoveData, STATGROUP_LiteGPUScene);
DECLARE_CYCLE_STAT(TEXT("GPUDriven Add Instance Group"), STAT_AddInstanceGroup, STATGROUP_LiteGPUScene);
DECLARE_CYCLE_STAT(TEXT("GPUDriven Read Back Buffer"), STAT_ReadBackBuffer, STATGROUP_LiteGPUScene);

DEFINE_LOG_CATEGORY_STATIC(LogLiteGPUScene, Log, All);

#if ENABLE_LITE_GPU_SCENE_DEBUG
static int sDebugLiteGPUSceneReadBack = 1;
static FAutoConsoleVariableRef CVarDebugLiteGPUScene(
	TEXT("r.LiteGPUScene.ReadBackEnable"),
	sDebugLiteGPUSceneReadBack,
	TEXT("Display GPUDriven Scene\n")
);
#endif

static int sEnableRemoveInstanceMultiFrame = 1;
static FAutoConsoleVariableRef CVarEnableRemoveMultiFrame(
	TEXT("r.LiteGPUScene.EnableRemoveInstanceMultiFrame"),
	sEnableRemoveInstanceMultiFrame,
	TEXT("EnableRemoveInstanceMultiFrame\n")
);

static int sEnableOutputLiteGPUSceneLog = 0;
static FAutoConsoleVariableRef CVarEnableOutputLiteGPUSceneLog(
	TEXT("r.LiteGPUScene.EnableLog"),
	sEnableOutputLiteGPUSceneLog,
	TEXT("Enable Output LiteGPUScene Log\n")
);

static int sDisplayLiteGPUScene = 1;
static FAutoConsoleVariableRef CVarDrawLiteGPUScene(
	TEXT("r.LiteGPUScene.Enable"),
	sDisplayLiteGPUScene,
	TEXT(" Display GPUDriven Scene.\n"),
	ECVF_Scalability
);

#define ONEFRAME_UPDATE_INSTANCE_NUM 4096
#define ONEFRAME_REMOVE_INSTANCE_NUM 256
#define ONEFRAME_UPDATE_GROUP_NUM 2
#define MAX_DIRTY_BUFFER_NUM ((ONEFRAME_UPDATE_INSTANCE_NUM  + ONEFRAME_REMOVE_INSTANCE_NUM) * ONEFRAME_UPDATE_GROUP_NUM)
#define MAX_BUFFER_ARRAY_NUM 1024 * 1024 * 16

FLiteGPUSceneMeshPatchInfo::FLiteGPUSceneMeshPatchInfo()
	:MeshIndex(0)
	, LODIndex(0)
	, SectionIndex(0)
	, FirstVertexOffset(0)
	, FirstIndexOffset(0)
	, IndexCount(0)
	, PatchID(0)
	, VertexCount(0)
	, MaterialIndex(0)
	, ScreenSizeMin(0.0f)
	, ScreenSizeMax(0.0f)
{
}

FLiteGPUSceneMeshPatchInfo::FLiteGPUSceneMeshPatchInfo(const FLiteGPUSceneMeshPatchInfo& Another)
{
	MeshName = Another.MeshName;
	MeshIndex = Another.MeshIndex;
	LODIndex = Another.LODIndex;
	SectionIndex = Another.SectionIndex;
	FirstVertexOffset = Another.FirstVertexOffset;
	FirstIndexOffset = Another.FirstIndexOffset;
	IndexCount = Another.IndexCount;
	PatchID = Another.PatchID;
	VertexCount = Another.VertexCount;
	MaterialIndex = Another.MaterialIndex;
	ScreenSizeMin = Another.ScreenSizeMin;
	ScreenSizeMax = Another.ScreenSizeMax;
}

FLiteGPUSceneMeshPatchInfo& FLiteGPUSceneMeshPatchInfo::operator=(const FLiteGPUSceneMeshPatchInfo& Another)
{
	MeshName = Another.MeshName;
	MeshIndex = Another.MeshIndex;
	LODIndex = Another.LODIndex;
	SectionIndex = Another.SectionIndex;
	FirstVertexOffset = Another.FirstVertexOffset;
	FirstIndexOffset = Another.FirstIndexOffset;
	IndexCount = Another.IndexCount;
	PatchID = Another.PatchID;
	VertexCount = Another.VertexCount;
	MaterialIndex = Another.MaterialIndex;
	ScreenSizeMin = Another.ScreenSizeMin;
	ScreenSizeMax = Another.ScreenSizeMax;
	return *this;
}

FLiteGPUSceneProxyVisibilityData::FLiteGPUSceneProxyVisibilityData()
{
	pInstanceIndexVertexBuffer = nullptr;

	bInitialized = false;

	RWIndirectDrawDispatchIndiretBuffer = nullptr;
	RWInstanceIndiceBuffer = nullptr;
	RWIndirectDrawBuffer = nullptr;

	MaxGPUInstanceToDraw = 0;

	RWGPUUnCulledInstanceScreenSize = nullptr;

	RWGPUUnCulledInstanceBuffer = nullptr;
	RWDepthDrawTestResultBuffer = nullptr;
	RWGPUUnCulledInstanceNum = nullptr;

	RWDispatchStagingBuffer = nullptr;

	bDirty = false;
	bFirstGPUCullinged = false;
	bCreateImposterBuffer = false;

	PerPatchMaxNum = 0;

	RWGPUUnCulledInstanceIndirectParameters = nullptr;

#if ENABLE_LITE_GPU_SCENE_DEBUG
	RWGPUCulledInstanceBuffer = nullptr;
	RWGPUCulledInstanceDebugIndirectParameters = nullptr;
#endif
}

FLiteGPUSceneProxyVisibilityData::~FLiteGPUSceneProxyVisibilityData()
{
	bInitialized = false;
	if (IsInRenderingThread())
	{
		if (pInstanceIndexVertexBuffer)
		{
			pInstanceIndexVertexBuffer->ReleaseResource();
			delete pInstanceIndexVertexBuffer;
			pInstanceIndexVertexBuffer = nullptr;
		}
		if (RWIndirectDrawDispatchIndiretBuffer)
		{
			RWIndirectDrawDispatchIndiretBuffer->Release();
			delete RWIndirectDrawDispatchIndiretBuffer;
			RWIndirectDrawDispatchIndiretBuffer = nullptr;
		}
		if (RWInstanceIndiceBuffer)
		{
			RWInstanceIndiceBuffer->Release();
			delete RWInstanceIndiceBuffer;
			RWInstanceIndiceBuffer = nullptr;
		}
		if (RWIndirectDrawBuffer)
		{
			RWIndirectDrawBuffer->Release();
			delete RWIndirectDrawBuffer;
			RWIndirectDrawBuffer = nullptr;
		}
		if (RWGPUUnCulledInstanceScreenSize)
		{
			RWGPUUnCulledInstanceScreenSize->Release();
			delete RWGPUUnCulledInstanceScreenSize;
			RWGPUUnCulledInstanceScreenSize = nullptr;
		}
		if (RWGPUUnCulledInstanceBuffer)
		{
			RWGPUUnCulledInstanceBuffer->Release();
			delete RWGPUUnCulledInstanceBuffer;
			RWGPUUnCulledInstanceBuffer = nullptr;
		}
		if (RWDepthDrawTestResultBuffer)
		{
			RWDepthDrawTestResultBuffer->Release();
			delete RWDepthDrawTestResultBuffer;
			RWDepthDrawTestResultBuffer = nullptr;
		}
		if (RWGPUUnCulledInstanceNum)
		{
			RWGPUUnCulledInstanceNum->Release();
			delete RWGPUUnCulledInstanceNum;
			RWGPUUnCulledInstanceNum = nullptr;
		}

		if (RWDispatchStagingBuffer)
		{
			RWDispatchStagingBuffer->Release();
			delete RWDispatchStagingBuffer;
			RWDispatchStagingBuffer = nullptr;
		}
		if (RWGPUUnCulledInstanceIndirectParameters)
		{
			RWGPUUnCulledInstanceIndirectParameters->Release();
			delete RWGPUUnCulledInstanceIndirectParameters;
			RWGPUUnCulledInstanceIndirectParameters = nullptr;
		}
#if ENABLE_LITE_GPU_SCENE_DEBUG
		if (RWGPUCulledInstanceBuffer)
		{
			RWGPUCulledInstanceBuffer->Release();
			delete RWGPUCulledInstanceBuffer;
			RWGPUCulledInstanceBuffer = nullptr;
		}
		if (RWGPUCulledInstanceDebugIndirectParameters)
		{
			RWGPUCulledInstanceDebugIndirectParameters->Release();
			delete RWGPUCulledInstanceDebugIndirectParameters;
			RWGPUCulledInstanceDebugIndirectParameters = nullptr;
		}
#endif
	}
}

FLiteGPUSceneInstanceData::FLiteGPUSceneInstanceData()
{
	InstanceNum = 0;
	FoliageTypeNum = 0;
	AllPatchInfoBuffer = nullptr;
	RWPatchCountBuffer = nullptr;
	RWPatchCountCopyBuffer = nullptr;
	RWPatchCountOffsetBuffer = nullptr;
	RWNextPatchCountOffsetBuffer = nullptr;
#if ENABLE_LITE_GPU_SCENE_DEBUG
	RWDrawedTriangleCountBuffer = nullptr;
	RWDrawedTriangleCountBufferAsyncReadBack = nullptr;
#endif
	RWGPUInstanceIndicesBuffer = nullptr;
	RWInstanceScaleBuffer = nullptr;
	RWInstanceTransformBuffer = nullptr;
	RWInstanceTypeBuffer = nullptr;
	RWInstancePatchNumBuffer = nullptr;
	RWInstancePatchIDsBuffer = nullptr;
	MeshAABBBuffer = nullptr;
#if PLATFORM_WINDOWS
	bUpdateFrame = false;
#endif
	CullingInstancedNum = 0;
	AllInstanceIndexNum = 0;
	PerPatchMaxNum = 0;
	bInitialized = false;
}

FLiteGPUSceneInstanceData::~FLiteGPUSceneInstanceData()
{
	bInitialized = false;
	Release_RenderingThread();
}

void FLiteGPUSceneMeshVertexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	const auto VertSize = sizeof(FLiteGPUSceneMeshVertex);
	FRHIResourceCreateInfo CreateInfo(TEXT("LiteGPUScene/VertexBuffer"));
	VertexBufferRHI = RHICreateVertexBuffer(Vertices.Num() * VertSize, BUF_Static, CreateInfo);

	// Copy the vertex data into the vertex buffer.
	void* Data = RHICmdList.LockBuffer(VertexBufferRHI, 0, Vertices.Num() * VertSize, RLM_WriteOnly);
	FMemory::Memcpy(Data, Vertices.GetData(), Vertices.Num() * VertSize);
	RHICmdList.UnlockBuffer(VertexBufferRHI);

	VerticesNum = Vertices.Num();
	Vertices.Empty();
}

void FLiteGPUSceneMeshIndexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	const auto IndexSize = sizeof(uint32);
	FRHIResourceCreateInfo CreateInfo(TEXT("LiteGPUScene/IndexBuffer"));
	IndexBufferRHI = RHICreateVertexBuffer(Indices.Num() * IndexSize, BUF_Static, CreateInfo);

	// Copy the vertex data into the vertex buffer.
	void* Data = RHICmdList.LockBuffer(IndexBufferRHI, 0, Indices.Num() * IndexSize, RLM_WriteOnly);
	FMemory::Memcpy(Data, Indices.GetData(), Indices.Num() * IndexSize);
	RHICmdList.UnlockBuffer(IndexBufferRHI);

	IndicesNum = Indices.Num();
	Indices.Empty();
}

struct LiteGPUSceneIndirectArguments
{
	static void Init(LiteGPUSceneIndirectArguments& ia)
	{
		ia.IndexCountPerInstance = 0;
		ia.InstanceCount = 1;
		ia.StartIndexLocation = 0;
		ia.BaseVertexLocation = 0;
		ia.StartInstanceLocation = 0;
	}
	uint32 IndexCountPerInstance;
	uint32 InstanceCount;
	uint32 StartIndexLocation;
	int32 BaseVertexLocation;
	uint32 StartInstanceLocation;
};

void CreateInstanceBufferFromCreatInfo(uint32 BytesPerElement, uint32 NumElements, EPixelFormat Format, EBufferUsageFlags AdditionalUsage, FRHIResourceCreateInfo& CreateInfo, FReadBuffer& OutReadBuffer)
{
	check(CreateInfo.ResourceArray->GetResourceDataSize() == BytesPerElement * NumElements);
	OutReadBuffer.NumBytes = BytesPerElement * NumElements;
	{
		OutReadBuffer.Buffer = RHICreateVertexBuffer(OutReadBuffer.NumBytes, BUF_ShaderResource | AdditionalUsage, CreateInfo);
	}
	OutReadBuffer.SRV = RHICreateShaderResourceView(OutReadBuffer.Buffer, BytesPerElement, Format);
}

void FLiteGPUSceneInstanceData::InitInstanceData(class ULiteGPUSceneComponent* Comp, const TArray<FLiteGPUSceneMeshPatchInfo>& InAllPatches)
{
	// Actually Here PerPathMaxNum is number of lods
	PerPatchMaxNum = Comp->PerPatchMaxNum;
	if (PerPatchMaxNum == 0)
	{
		return;
	}
	GPUByte = 0;
	AllPatches = InAllPatches;
	FoliageTypeNum = Comp->AllSourceMeshes.Num();
	AllPatchNum = AllPatches.Num();
	PatchInstanceNum.SetNum(AllPatchNum);
	PatchInfoMeshIndices.SetNum(AllPatchNum);

	// This collects the <patchid,meshid> array
	// Why not directly iterate over AllPatches?
	int32 PatchIndex = 0;
	for (auto& pair : Comp->MaterialToPatchMap)
	{
		const TArray<FLiteGPUSceneMeshPatchInfo>& PatchInfoArray = pair.Value;
		for (int32 PIndex = 0; PIndex < PatchInfoArray.Num(); PIndex++)
		{
			const FLiteGPUSceneMeshPatchInfo& PatchInfo = PatchInfoArray[PIndex];
			PatchInfoMeshIndices[PatchIndex] = PatchInfo.MeshIndex;
			PatchIndex++;
		}
	}

	// This stores AABB with BL-UR format
	TArray<FVector4f> FoliageAABBs;
	FoliageAABBs.SetNum(2 * FoliageTypeNum);
	for (int32 FoliageIndex = 0; FoliageIndex < FoliageTypeNum; FoliageIndex++)
	{
		if (Comp->AllSourceMeshes[FoliageIndex])
		{
			FBoxSphereBounds MeshBound = Comp->AllSourceMeshes[FoliageIndex]->GetBounds();
			FBox Box = MeshBound.GetBox();
			FVector Min = Box.Min;
			FVector Max = Box.Max;
			FoliageAABBs[FoliageIndex * 2 + 0] = FVector4f(Min.X, Min.Y, Min.Z, MeshBound.SphereRadius);
			FoliageAABBs[FoliageIndex * 2 + 1] = FVector4f(Max.X, Max.Y, Max.Z, 0);
		}
		else
		{
			FoliageAABBs[FoliageIndex * 2 + 0] = FVector4f(-50.0f, -50.0f, -50.0f, 0);
			FoliageAABBs[FoliageIndex * 2 + 1] = FVector4f(50.0f, 50.0f, 50.0f, 0);
		}
	}
	// Copy that in
	PatchAABBData.SetNum(2 * sizeof(FVector4f) * FoliageTypeNum);
	FMemory::Memcpy(PatchAABBData.GetData(), FoliageAABBs.GetData(), 2 * FoliageTypeNum * sizeof(FVector4f));

	UploadInstanceScaleData.SetNum(MAX_BUFFER_ARRAY_NUM);
	UploadInstanceTransformData.SetNum(MAX_BUFFER_ARRAY_NUM * 4);
	UploadInstanceFoliageTypeData.SetNum(MAX_BUFFER_ARRAY_NUM);
	UploadInstancePatchIDs.SetNum(MAX_BUFFER_ARRAY_NUM * PerPatchMaxNum);
	UploadInstancePatchNum.SetNum(MAX_BUFFER_ARRAY_NUM);
#if PLATFORM_WINDOWS
	UpdateDirtyInstanceIndices.SetNum(MAX_DIRTY_BUFFER_NUM);
#else
	for (uint32 i = 0; i < MAX_QUEUED_FRAME_COUNT; i++)
	{
		FrameBufferedData[i].UpdateDirtyInstanceIndices.SetNum(MAX_DIRTY_BUFFER_NUM);
		FrameBufferedData[i].DirtyInstanceNum = 0;
		FrameBufferedData[i].bUpdateFrame = false;
	}
	FrameBufferSelectionCounter = 0;
#endif
	InstancedKeyComponents.SetNum(MAX_BUFFER_ARRAY_NUM);

	FLiteGPUSceneInstanceData* InitDataPtr = this;
	ENQUEUE_RENDER_COMMAND(InitInstancedData)(
		[InitDataPtr](FRHICommandList& RHICmdList)
		{
			if (InitDataPtr)
			{
				InitDataPtr->Initialize_RenderingThread();
			}
		}
	);
}

void FLiteGPUSceneInstanceData::UpdateIncreasedData(class ULiteGPUSceneComponent* Comp, class UHierarchicalInstancedStaticMeshComponent* Key)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateIncreaseData);

	if (!bInitialized)
	{
		return;
	}

	PerPatchMaxNum = Comp->PerPatchMaxNum;
	if (AllPatchNum <= 0 || PerPatchMaxNum <= 0)
	{
		return;
	}

	//  the Increased offset records the last number of Instance count on this Key(HISM)
	// Thus a compare and subtraction can give the real number that needs to be updated
	if (Key->IncreasedOffset < Key->InstanceCount)
	{
		// The number to update is limited for efficiency
		int32 NewInstanceCount = FMath::Min<uint32>(ONEFRAME_UPDATE_INSTANCE_NUM, Key->InstanceCount - Key->IncreasedOffset);

		uint32 LastInstancedIndicesNum = InstanceNum;

		//Update instance count for both holders
		InstanceNum += NewInstanceCount;
		Comp->AllInstanceNum += NewInstanceCount;

		// Safety check
		check(Comp->AllInstanceNum <= MAX_BUFFER_ARRAY_NUM);

		// Store transform, scale and mesh index for later update
		for (int32 InstanceIndex = 0; InstanceIndex < NewInstanceCount; InstanceIndex++)
		{
			SCOPE_CYCLE_COUNTER(STAT_CopyInstanceData);

			const FMatrix& T = Key->LiteGPUSceneDatas[InstanceIndex + Key->IncreasedOffset].TransformsData;
			for (int i = 0; i < 4; i++)
			{
				UploadInstanceTransformData[(InstanceIndex + LastInstancedIndicesNum) * 4 + i] = FVector4f(T.M[i][0], T.M[i][1], T.M[i][2], T.M[i][3]);
			}
			UploadInstanceScaleData[InstanceIndex + LastInstancedIndicesNum] = Key->LiteGPUSceneDatas[InstanceIndex + Key->IncreasedOffset].Scale;
			check(Key->LiteGPUSceneDatas[InstanceIndex + Key->IncreasedOffset].MeshIndex <= 255);
			UploadInstanceFoliageTypeData[InstanceIndex + LastInstancedIndicesNum] = Key->LiteGPUSceneDatas[InstanceIndex + Key->IncreasedOffset].MeshIndex;
			// This stores the Key to record the relations
			InstancedKeyComponents[InstanceIndex + LastInstancedIndicesNum] = Key;
		}

#if !PLATFORM_WINDOWS
		FrameData& CurrentFrameData = FrameBufferedData[FrameBufferSelectionCounter];
#endif

		// Mark these instances as GPUDriven and Mark Dirty for updating
		{
			uint32 PreCompInstanceNum = Key->GPUDrivenInstanceIndices.Num();
			Key->GPUDrivenInstanceIndices.SetNum(NewInstanceCount + PreCompInstanceNum, false);

#if PLATFORM_WINDOWS
			int32 LastDirtyInstanceIndex = DirtyInstanceNum;
			DirtyInstanceNum += NewInstanceCount;

			check(DirtyInstanceNum <= MAX_DIRTY_BUFFER_NUM);
#else
			int32 LastDirtyInstanceIndex = CurrentFrameData.DirtyInstanceNum;
			CurrentFrameData.DirtyInstanceNum += NewInstanceCount;

			check(CurrentFrameData.DirtyInstanceNum <= MAX_DIRTY_BUFFER_NUM);
#endif

			for (int32 Index = 0; Index < NewInstanceCount; Index++)
			{
				SCOPE_CYCLE_COUNTER(STAT_SetInstanceBuffer);
				Key->GPUDrivenInstanceIndices[PreCompInstanceNum + Index] = LastInstancedIndicesNum + Index;

#if PLATFORM_WINDOWS
				UpdateDirtyInstanceIndices[LastDirtyInstanceIndex + Index] = LastInstancedIndicesNum + Index;
#else
				CurrentFrameData.UpdateDirtyInstanceIndices[LastDirtyInstanceIndex + Index] = LastInstancedIndicesNum + Index;
#endif
			}
		}


		// Find patch that share the same mesh with the new instance 
		int32 PatchIndex = 0;
		for (auto& pair : Comp->MaterialToPatchMap)
		{
			const TArray<FLiteGPUSceneMeshPatchInfo>& PatchInfoArray = pair.Value;
			check(PatchInfoArray.Num() <= 255);
			for (const FLiteGPUSceneMeshPatchInfo& PatchInfo : PatchInfoArray)
			{
				for (int32 i = 0; i < NewInstanceCount; i++)
				{
					int32 NewInstanceIndex = i + LastInstancedIndicesNum;

					int32 InstanceStart = NewInstanceIndex * PerPatchMaxNum;

					const FInstancedLiteGPUSceneData& InstanceData = Key->LiteGPUSceneDatas[Key->IncreasedOffset + i];
					if (PatchInfo.MeshIndex == InstanceData.MeshIndex)
					{
						int32 InsertIndex = UploadInstancePatchNum[NewInstanceIndex];
						// Record the number of patch of this instance that needs to upload
						UploadInstancePatchNum[NewInstanceIndex] = UploadInstancePatchNum[NewInstanceIndex] + 1;
						// Record the patch id that needs to upload
						UploadInstancePatchIDs[InstanceStart + InsertIndex] = PatchIndex;
						// Add the instance count of that patch
						// may consider to use PatchInfo.PatchID instead of PatchIndex, though they matches with this ugly logic
						PatchInstanceNum[PatchIndex] = PatchInstanceNum[PatchIndex] + 1;
					}
				}

				PatchIndex++;
			}
		}

#if PLATFORM_WINDOWS
		bUpdateFrame = true;
#else
		CurrentFrameData.bUpdateFrame = true;
#endif

		//Update the offset
		Key->IncreasedOffset += NewInstanceCount;
	}
}

void FLiteGPUSceneInstanceData::UpdateRemoveData(class ULiteGPUSceneComponent* Comp, class UHierarchicalInstancedStaticMeshComponent* Key)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateRemoveData);
	int32 RemovedMeshIndex = Comp->AllSourceMeshes.Find(Key->GetStaticMesh());
	if (RemovedMeshIndex == INDEX_NONE)
	{
		return;
	}

#if !PLATFORM_WINDOWS
	FrameData& CurrentFrameData = FrameBufferedData[FrameBufferSelectionCounter];
#endif

	if (Key->RemoveOffset < Key->InstanceCount)
	{
		int32 InstanceCountToRemove = FMath::Min<uint32>(ONEFRAME_REMOVE_INSTANCE_NUM, Key->InstanceCount - Key->RemoveOffset);
		uint32 Num = InstanceNum;
		InstanceNum -= InstanceCountToRemove;
		Comp->AllInstanceNum -= InstanceCountToRemove;

#if PLATFORM_WINDOWS
		int32 LastDirtyIndex = DirtyInstanceNum;
#else
		int32 LastDirtyIndex = CurrentFrameData.DirtyInstanceNum;
#endif

		for (int32 Index = 0; Index < InstanceCountToRemove; Index++)
		{
			SCOPE_CYCLE_COUNTER(STAT_RemoveDataMemorySwap);

			int32 LastIndex = Num - 1;
			auto LastComp = InstancedKeyComponents[LastIndex];

			if (LastIndex != Key->GPUDrivenInstanceIndices[Index + Key->RemoveOffset])
			{
				uint32 RemovedIndex = Key->GPUDrivenInstanceIndices[Index + Key->RemoveOffset];//
				UploadInstanceFoliageTypeData[RemovedIndex] = UploadInstanceFoliageTypeData[LastIndex];
				UploadInstanceScaleData[RemovedIndex] = UploadInstanceScaleData[LastIndex];
				for (int i = 0; i < 4; i++)
				{
					UploadInstanceTransformData[RemovedIndex * 4 + i] = UploadInstanceTransformData[LastIndex * 4 + i];
				}

				UploadInstancePatchNum[RemovedIndex] = UploadInstancePatchNum[LastIndex];
				UploadInstancePatchNum[LastIndex] = 0;

				for (int32 i = 0; i < PerPatchMaxNum; i++)
				{
					UploadInstancePatchIDs[RemovedIndex * PerPatchMaxNum + i] = UploadInstancePatchIDs[LastIndex * PerPatchMaxNum + i];
					UploadInstancePatchIDs[LastIndex * PerPatchMaxNum + i] = 0;
				}

				int32 IndexInLastComp = LastComp->GPUDrivenInstanceIndices.FindLast(LastIndex);
				check(INDEX_NONE != IndexInLastComp);
				LastComp->GPUDrivenInstanceIndices[IndexInLastComp] = RemovedIndex;
				InstancedKeyComponents[RemovedIndex] = LastComp;

#if PLATFORM_WINDOWS
				DirtyInstanceNum++;
				check(DirtyInstanceNum <= MAX_DIRTY_BUFFER_NUM);
				UpdateDirtyInstanceIndices[LastDirtyIndex + Index] = RemovedIndex;
#else
				CurrentFrameData.DirtyInstanceNum++;
				check(CurrentFrameData.DirtyInstanceNum <= MAX_DIRTY_BUFFER_NUM);
				CurrentFrameData.UpdateDirtyInstanceIndices[LastDirtyIndex + Index] = RemovedIndex;
#endif
			}
			Num--;
		}

		// record the new instance num for patches share the same mesh
		// as each instance have these patches
		for (int32 pIndex = 0; pIndex < PatchInstanceNum.Num(); pIndex++)
		{
			if (PatchInfoMeshIndices[pIndex] == RemovedMeshIndex)
			{
				PatchInstanceNum[pIndex] -= InstanceCountToRemove;
				break;
			}
		}

#if PLATFORM_WINDOWS
		bUpdateFrame = true;
#else
		CurrentFrameData.bUpdateFrame = true;
#endif
		Key->RemoveOffset += InstanceCountToRemove;
	}
}

void FLiteGPUSceneInstanceData::UpdateInstanceData(ULiteGPUSceneComponent* Comp)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateInstanceData);

#if !PLATFORM_WINDOWS
	FrameData& CurrentFrameData = FrameBufferedData[FrameBufferSelectionCounter];
#endif

#if PLATFORM_WINDOWS
	if (!bUpdateFrame)
#else
	if (!CurrentFrameData.bUpdateFrame)
#endif
	{
		return;
	}
	if (!bInitialized)
	{
		return;
	}

	AllInstanceIndexNum = 0;
	for (int32 PatchIndex = 0; PatchIndex < PatchInstanceNum.Num(); PatchIndex++)
	{
		AllInstanceIndexNum += PatchInstanceNum[PatchIndex];
	}

	FLiteGPUSceneInstanceData* InitDataPtr = this;
	auto* pBufferManager = Comp->GetGPUDrivenBufferManager();
	CulledAllPatchNum = AllPatches.Num();

	int32 DirtyCount = DirtyInstanceNum;
	ENQUEUE_RENDER_COMMAND(UpdateInstanceDataRenderingThread)(
		[DirtyCount, InitDataPtr, pBufferManager](FRHICommandList& RHICmdList)
		{
			if (pBufferManager)
			{
				pBufferManager->UpdateUploader(RHICmdList, InitDataPtr, DirtyCount);
			}
		}
	);
	bUpdateFrame = false;
}


void FLiteGPUSceneInstanceData::Initialize_RenderingThread()
{
	UE_LOG(LogLiteGPUScene, Log, TEXT("Call FLiteGPUSceneInstanceData::Initialize_RenderingThread"));
	if (PerPatchMaxNum == 0)
	{
		return;
	}

	if (nullptr == AllPatchInfoBuffer)
	{
		AllPatchInfoBuffer = new FReadBuffer();
		TResourceArray<FVector4f, VERTEXBUFFER_ALIGNMENT> AllPatchInfoBufferArray;
		AllPatchInfoBufferArray.SetNumUninitialized(AllPatchNum * 2);

		for (int32 PatchIndex = 0; PatchIndex < AllPatchNum; PatchIndex++)
		{
			const FLiteGPUSceneMeshPatchInfo& PatchInfo = AllPatches[PatchIndex];
			AllPatchInfoBufferArray[2 * PatchIndex + 0] = FVector4f(PatchInfo.FirstIndexOffset, PatchInfo.IndexCount, PatchInfo.FirstVertexOffset, PatchInfo.VertexCount);
			AllPatchInfoBufferArray[2 * PatchIndex + 1] = FVector4f(PatchInfo.ScreenSizeMin, PatchInfo.ScreenSizeMax, PatchInfo.MeshIndex, PatchInfo.MaterialIndex);
		}

		FRHIResourceCreateInfo CreateInfo(TEXT("PatchInfo-InstanceBuffer"), &AllPatchInfoBufferArray);
		uint32 Size = AllPatchInfoBufferArray.GetResourceDataSize();
		GPUByte += Size;
		CreateInstanceBufferFromCreatInfo(sizeof(FVector4f), 2 * AllPatchNum, PF_A32B32G32R32F, BUF_None, CreateInfo, *AllPatchInfoBuffer);
	}

	if (nullptr == MeshAABBBuffer)
	{
		MeshAABBBuffer = new FReadBuffer();

		TResourceArray<FVector4f, VERTEXBUFFER_ALIGNMENT> AABBArray;
		AABBArray.SetNumUninitialized(2 * FoliageTypeNum);

		check(PatchAABBData.Num() == 2 * FoliageTypeNum * sizeof(FVector4f));
		FMemory::Memcpy(AABBArray.GetData(), PatchAABBData.GetData(), 2 * FoliageTypeNum * sizeof(FVector4f));
		uint32 Size = AABBArray.GetResourceDataSize();
		GPUByte += Size;
		FRHIResourceCreateInfo CreateInfo(TEXT("AABB-InstanceBuffer"), &AABBArray);
		CreateInstanceBufferFromCreatInfo(sizeof(FVector4f), 2 * FoliageTypeNum, PF_A32B32G32R32F, BUF_Static, CreateInfo, *MeshAABBBuffer);
	}

	/*
	 * Below are buffer of instance datas
	 */
	if (RWGPUInstanceIndicesBuffer == nullptr)
	{
		RWGPUInstanceIndicesBuffer = new FRWBuffer();
		RWGPUInstanceIndicesBuffer->Initialize(TEXT("RWGPUInstanceIndicesBuffer"), sizeof(int32), MAX_BUFFER_ARRAY_NUM, PF_R32_SINT, BUF_None);

		uint32 Size = sizeof(int32) * MAX_BUFFER_ARRAY_NUM;
		GPUByte += Size;
	}

	if (RWInstanceScaleBuffer == nullptr)
	{
		RWInstanceScaleBuffer = new FRWBuffer();
		RWInstanceScaleBuffer->Initialize(TEXT("RWInstanceScaleBuffer"), sizeof(FVector4f), MAX_BUFFER_ARRAY_NUM, PF_A32B32G32R32F, BUF_None);
		uint32 Size = sizeof(FVector4f) * MAX_BUFFER_ARRAY_NUM;
		GPUByte += Size;
	}

	if (RWInstanceTypeBuffer == nullptr)
	{
		RWInstanceTypeBuffer = new FRWBuffer();
		RWInstanceTypeBuffer->Initialize(TEXT("RWInstanceFoliageTypeBuffer"), sizeof(uint8), MAX_BUFFER_ARRAY_NUM, PF_R8_UINT, BUF_None);

		uint32 Size = sizeof(uint8) * MAX_BUFFER_ARRAY_NUM;
		GPUByte += Size;
	}

	if (RWInstanceTransformBuffer == nullptr)
	{
		RWInstanceTransformBuffer = new FRWBuffer();
		RWInstanceTransformBuffer->Initialize(TEXT("RWInstanceTransformBuffer"), sizeof(FVector4f), MAX_BUFFER_ARRAY_NUM * 4, PF_A32B32G32R32F, BUF_None);

		uint32 Size = sizeof(FVector4f) * MAX_BUFFER_ARRAY_NUM * 4;
		GPUByte += Size;
	}

	if (RWInstancePatchNumBuffer == nullptr)
	{
		RWInstancePatchNumBuffer = new FRWBuffer();
		RWInstancePatchNumBuffer->Initialize(TEXT("RWInstancePatchNumBuffer"), sizeof(uint8), MAX_BUFFER_ARRAY_NUM, PF_R8_UINT, BUF_None);

		uint32 Size = sizeof(uint8) * MAX_BUFFER_ARRAY_NUM;
		GPUByte += Size;
	}

	if (RWInstancePatchIDsBuffer == nullptr)
	{
		RWInstancePatchIDsBuffer = new FRWBuffer();
		RWInstancePatchIDsBuffer->Initialize(TEXT("RWInstancePatchIDsBuffer"), sizeof(uint8), MAX_BUFFER_ARRAY_NUM * PerPatchMaxNum, PF_R8_UINT, BUF_None);

		uint32 Size = sizeof(uint8) * MAX_BUFFER_ARRAY_NUM * PerPatchMaxNum;
		GPUByte += Size;
	}
	/*
	 * Below are some possbile debug buffers
	 */

	if (nullptr == RWPatchCountBuffer)
	{
		RWPatchCountBuffer = new FRWBuffer();
		RWPatchCountBuffer->Initialize(TEXT("RWPatchCountBuffer"), sizeof(uint32), AllPatchNum, PF_R32_UINT, BUF_None);
		uint32 Size = sizeof(uint32) * AllPatchNum;
		GPUByte += Size;
	}

	if (nullptr == RWPatchCountCopyBuffer)
	{
		RWPatchCountCopyBuffer = new FRWBuffer();
		RWPatchCountCopyBuffer->Initialize(TEXT("RWPatchCountCopyBuffer"), sizeof(uint32), AllPatchNum, PF_R32_UINT, BUF_None);

		uint32 Size = sizeof(uint32) * AllPatchNum;
		GPUByte += Size;
	}

	if (nullptr == RWPatchCountOffsetBuffer)
	{
		RWPatchCountOffsetBuffer = new FRWBuffer();
		RWPatchCountOffsetBuffer->Initialize(TEXT("RWPatchCountOffsetBuffer"), sizeof(uint32), AllPatchNum, PF_R32_UINT, BUF_None);

		uint32 Size = sizeof(uint32) * AllPatchNum;
		GPUByte += Size;
	}

	if (nullptr == RWNextPatchCountOffsetBuffer)
	{
		RWNextPatchCountOffsetBuffer = new FRWBuffer();
		RWNextPatchCountOffsetBuffer->Initialize(TEXT("RWNextPatchCountOffsetBuffer"), sizeof(uint32), 1, PF_R32_UINT, BUF_None);
		uint32 Size = sizeof(uint32);
		GPUByte += Size;

	}

#if ENABLE_LITE_GPU_SCENE_DEBUG
	FRHICommandListImmediate& RHICmdList = GRHICommandList.GetImmediateCommandList();
	if (nullptr == RWDrawedTriangleCountBuffer)
	{
		RWDrawedTriangleCountBuffer = new FRWBuffer();
		RWDrawedTriangleCountBuffer->Initialize(sizeof(uint32), 1, PF_R32_UINT, BUF_SourceCopy, TEXT("RWDrawedTriangleCountBuffer"));
		uint32 Size = sizeof(uint32);
		GPUByte += Size;
		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, RWDrawedTriangleCountBuffer->UAV);
	}
	if (nullptr == RWDrawedTriangleCountBufferAsyncReadBack)
	{

		RWDrawedTriangleCountBufferAsyncReadBack = new FRHIAsyncGPUBufferReadback(TEXT("RWDrawedTriangleCountBufferAsyncReadBack"));
		RWDrawedTriangleCountBufferAsyncReadBack->EnqueueCopy(RHICmdList, RWDrawedTriangleCountBuffer->Buffer, 1 * sizeof(uint32));

		uint32 Size = sizeof(uint32);
		GPUByte += Size;
	}
#endif
	bInitialized = true;
}

void FLiteGPUSceneInstanceData::Release_RenderingThread()
{

	if (AllPatchInfoBuffer)
	{
		AllPatchInfoBuffer->Release();
		delete AllPatchInfoBuffer;
		AllPatchInfoBuffer = nullptr;
	}
#if ENABLE_LITE_GPU_SCENE_DEBUG
	if (RWDrawedTriangleCountBuffer)
	{
		RWDrawedTriangleCountBuffer->Release();
		delete RWDrawedTriangleCountBuffer;
		RWDrawedTriangleCountBuffer = nullptr;
	}
	if (RWDrawedTriangleCountBufferAsyncReadBack)
	{
		delete RWDrawedTriangleCountBufferAsyncReadBack;
		RWDrawedTriangleCountBufferAsyncReadBack = nullptr;
	}
#endif

	if (RWPatchCountBuffer)
	{
		RWPatchCountBuffer->Release();
		delete RWPatchCountBuffer;
		RWPatchCountBuffer = nullptr;
	}

	if (RWPatchCountCopyBuffer)
	{
		RWPatchCountCopyBuffer->Release();
		delete RWPatchCountCopyBuffer;
		RWPatchCountCopyBuffer = nullptr;
	}

	if (RWPatchCountOffsetBuffer)
	{
		RWPatchCountOffsetBuffer->Release();
		delete RWPatchCountOffsetBuffer;
		RWPatchCountOffsetBuffer = nullptr;
	}

	if (RWNextPatchCountOffsetBuffer)
	{
		RWNextPatchCountOffsetBuffer->Release();
		delete RWNextPatchCountOffsetBuffer;
		RWNextPatchCountOffsetBuffer = nullptr;
	}

	if (RWGPUInstanceIndicesBuffer)
	{
		RWGPUInstanceIndicesBuffer->Release();
		delete RWGPUInstanceIndicesBuffer;
		RWGPUInstanceIndicesBuffer = nullptr;
	}
	if (RWInstanceScaleBuffer)
	{
		RWInstanceScaleBuffer->Release();
		delete RWInstanceScaleBuffer;
		RWInstanceScaleBuffer = nullptr;
	}
	if (RWInstanceTransformBuffer)
	{
		RWInstanceTransformBuffer->Release();
		delete RWInstanceTransformBuffer;
		RWInstanceTransformBuffer = nullptr;
	}
	if (RWInstanceTypeBuffer)
	{
		RWInstanceTypeBuffer->Release();
		delete RWInstanceTypeBuffer;
		RWInstanceTypeBuffer = nullptr;
	}
	if (RWInstancePatchNumBuffer)
	{
		RWInstancePatchNumBuffer->Release();
		delete RWInstancePatchNumBuffer;
		RWInstancePatchNumBuffer = nullptr;
	}
	if (RWInstancePatchIDsBuffer)
	{
		RWInstancePatchIDsBuffer->Release();
		delete RWInstancePatchIDsBuffer;
		RWInstancePatchIDsBuffer = nullptr;
	}
	if (MeshAABBBuffer)
	{
		MeshAABBBuffer->Release();
		delete MeshAABBBuffer;
		MeshAABBBuffer = nullptr;
	}
	UploadInstanceScaleData.Empty();
	UploadInstanceTransformData.Empty();
	UploadInstanceFoliageTypeData.Empty();
	UploadInstancePatchNum.Empty();
	UploadInstancePatchIDs.Empty();

}

void FLiteGPUSceneProxyVisibilityData::InitVisibilityData(int32 InPatchNum, bool bInGPUCulling, int32 InPerMatchMaxIndexNum)
{
	if (InPatchNum == 0)
	{
		return;
	}

	GPUByte = 0;
	PerPatchMaxNum = InPerMatchMaxIndexNum;
	bGPUCulling = bInGPUCulling;

	TotalPatchNum = InPatchNum;

	check(pInstanceIndexVertexBuffer == nullptr);
	pInstanceIndexVertexBuffer = new FVertexBuffer();

	BeginInitResource(pInstanceIndexVertexBuffer);

	// Init rendering thread
	FLiteGPUSceneProxyVisibilityData* InitDataPtr = this;
	ENQUEUE_RENDER_COMMAND(InitLiteGPUSceneProxyVisibilityData)(

		[InitDataPtr](FRHICommandList& RHICmdList)
		{
			if (InitDataPtr)
			{
				InitDataPtr->InitRenderingThread();
			}
		}
	);
}

void FLiteGPUSceneProxyVisibilityData::InitRenderingThread()
{
	if (PerPatchMaxNum == 0)
	{
		return;
	}

	if (nullptr == RWDispatchStagingBuffer)
	{
		RWDispatchStagingBuffer = new FRWBuffer();
		RWDispatchStagingBuffer->Initialize(TEXT("RWDispatchStagingBuffer"), sizeof(uint32), 3, PF_R32_UINT, BUF_DrawIndirect);
		uint32 Size = sizeof(uint32) * 3;
		GPUByte += Size;
	}

	if (nullptr == RWGPUUnCulledInstanceScreenSize)
	{
		RWGPUUnCulledInstanceScreenSize = new FRWBuffer();
		RWGPUUnCulledInstanceScreenSize->Initialize(TEXT("RWGPUUnCulledInstanceScreenSize"), sizeof(float), MAX_BUFFER_ARRAY_NUM, PF_R32_FLOAT, BUF_None);
		uint32 Size = sizeof(float) * MAX_BUFFER_ARRAY_NUM;
		GPUByte += Size;
	}

	if (nullptr == RWGPUUnCulledInstanceBuffer)
	{
		RWGPUUnCulledInstanceBuffer = new FRWBuffer();
		RWGPUUnCulledInstanceBuffer->Initialize(TEXT("RWGPUUnCulledInstanceIndicesBuffer"), sizeof(int32), MAX_BUFFER_ARRAY_NUM, PF_R32_SINT, BUF_None);
		uint32 Size = sizeof(int32) * MAX_BUFFER_ARRAY_NUM;
		GPUByte += Size;
	}

	if (nullptr == RWDepthDrawTestResultBuffer)
	{
		RWDepthDrawTestResultBuffer = new FRWBuffer();
		RWDepthDrawTestResultBuffer->Initialize(TEXT("RWDepthDrawTestResultBuffer"), sizeof(int32), MAX_BUFFER_ARRAY_NUM, PF_R32_SINT, BUF_None);
		uint32 Size = sizeof(int32) * MAX_BUFFER_ARRAY_NUM;
		GPUByte += Size;
	}

	if (nullptr == RWGPUUnCulledInstanceNum)
	{
		RWGPUUnCulledInstanceNum = new FRWBuffer();
		RWGPUUnCulledInstanceNum->Initialize(TEXT("RWGPUUnCulledInstanceNum"), sizeof(uint32), 1, PF_R32_UINT, BUF_None);
		uint32 Size = sizeof(uint32);
		GPUByte += Size;
	}

	if (nullptr == RWIndirectDrawDispatchIndiretBuffer)
	{
		RWIndirectDrawDispatchIndiretBuffer = new FRWBuffer();
		RWIndirectDrawDispatchIndiretBuffer->Initialize(TEXT("RWFoliageIndirectDrawDispatchIndiretBuffer"), sizeof(uint32), 3, PF_R32_UINT, BUF_DrawIndirect);
		uint32 Size = sizeof(uint32) * 3;
		GPUByte += Size;
	}

	if (nullptr == RWInstanceIndiceBuffer)
	{
		RWInstanceIndiceBuffer = new FRWBuffer();
		RWInstanceIndiceBuffer->Initialize(TEXT("RWInstanceIndiceBuffer"), sizeof(float), MAX_BUFFER_ARRAY_NUM * PerPatchMaxNum, PF_R32_FLOAT, BUF_None);
		uint32 Size = sizeof(float) * MAX_BUFFER_ARRAY_NUM * PerPatchMaxNum;
		GPUByte += Size;
		pInstanceIndexVertexBuffer->VertexBufferRHI = RWInstanceIndiceBuffer->Buffer;
	}

	if (nullptr == RWIndirectDrawBuffer)
	{
		RWIndirectDrawBuffer = new FRWBuffer();
		RWIndirectDrawBuffer->Initialize(TEXT("RWIndirectDrawBuffer"), sizeof(uint32), 5 * TotalPatchNum, PF_R32_UINT, BUF_DrawIndirect | BUF_SourceCopy);

		uint32 Size = sizeof(uint32) * 5 * TotalPatchNum;
		GPUByte += Size;
	}

	if (nullptr == RWGPUUnCulledInstanceIndirectParameters)
	{
		RWGPUUnCulledInstanceIndirectParameters = new FRWBuffer();
		RWGPUUnCulledInstanceIndirectParameters->Initialize(TEXT("RWGPUUnCulledInstanceIndirectParameters"), sizeof(uint32), 5, PF_R32_UINT, BUF_DrawIndirect);
		uint32 Size = sizeof(uint32) * 5;
		GPUByte += Size;
	}
#if ENABLE_LITE_GPU_SCENE_DEBUG
	//Debug GPU Culling
	if (nullptr == RWGPUCulledInstanceDebugIndirectParameters)
	{
		RWGPUCulledInstanceDebugIndirectParameters = new FRWBuffer();
		RWGPUCulledInstanceDebugIndirectParameters->Initialize(sizeof(uint32), 5, PF_R32_UINT, BUF_DrawIndirect);
		uint32 Size = sizeof(uint32) * 5;
		GPUByte += Size;
	}

	if (nullptr == RWGPUCulledInstanceBuffer)
	{
		RWGPUCulledInstanceBuffer = new FRWBuffer();
		RWGPUCulledInstanceBuffer->Initialize(sizeof(int32), MAX_BUFFER_ARRAY_NUM, PF_R32_SINT);
		uint32 Size = sizeof(int32) * MAX_BUFFER_ARRAY_NUM;
		GPUByte += Size;
	}
#endif
	bInitialized = true;
	UE_LOG(LogLiteGPUScene, Log, TEXT("FLiteGPUSceneProxyVisibilityData::InitRenderingThread"));
}

struct FRHICommandUpdateFoliageVertexBuffer final : public FRHICommand<FRHICommandUpdateFoliageVertexBuffer>
{
	FORCEINLINE_DEBUGGABLE FRHICommandUpdateFoliageVertexBuffer(FRHIBuffer* VertexBuffer, uint8* Data, uint32 DataLength, bool InNoOverite)
	{
		NoOverWrite = InNoOverite;
		CopyedDataLength = 0;
		RefVertexBuffer = VertexBuffer;
		check(VertexBuffer->GetSize() >= DataLength);
		if (Data && DataLength > 0)
		{
			CopiedData.SetNum(DataLength);
			FMemory::Memcpy(CopiedData.GetData(), Data, DataLength);
			CopyedDataLength = DataLength;
		}
	}

	void Execute(FRHICommandListBase& CmdList)
	{
		check(RefVertexBuffer.IsValid() && CopyedDataLength > 0);
		check(RefVertexBuffer->GetSize() == CopyedDataLength);
		if (CopyedDataLength > 0)
		{
			if ((int)(RefVertexBuffer->GetUsage() & BUF_UnorderedAccess) != 0)
			{
				return;
			}

			void* GPUData = RHILockBuffer(RefVertexBuffer, 0, CopyedDataLength, NoOverWrite ? RLM_WriteOnly_NoOverwrite : RLM_WriteOnly);
			FMemory::Memcpy(GPUData, CopiedData.GetData(), CopyedDataLength);
			RHIUnlockBuffer(RefVertexBuffer);
		}
	}

	FBufferRHIRef RefVertexBuffer;
	TArray<uint8> CopiedData;
	uint32 CopyedDataLength;
	bool NoOverWrite;
};

void DispatchVertexBuffersUpdateCommand(FRHIBuffer* VertexBuffer, uint8* Data, uint32 DataLength, bool NoOverite)
{
	if (!IsRunningRHIInSeparateThread())
	{
		FRHICommandListImmediate& RHIImmdiateCmdList = GRHICommandList.GetImmediateCommandList();
		ALLOC_COMMAND_CL(RHIImmdiateCmdList, FRHICommandUpdateFoliageVertexBuffer)(VertexBuffer, Data, DataLength, NoOverite);
	}
	else
	{
		check(VertexBuffer && Data);
		check(VertexBuffer->GetSize() == DataLength);
		if (DataLength > 0)
		{
			if ((int)(VertexBuffer->GetUsage() & BUF_UnorderedAccess) != 0)
			{
				return;
			}

			void* GPUData = RHILockBuffer(VertexBuffer, 0, DataLength, RLM_WriteOnly);
			FMemory::Memcpy(GPUData, Data, DataLength);
			RHIUnlockBuffer(VertexBuffer);
		}
	}
}
// Sets default values for this component's properties
ULiteGPUSceneComponent::ULiteGPUSceneComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	TotalTriangleDraw = 0;
	PerPatchMaxNum = 0;
	AllInstanceNum = 0;
	MaxInstanceNum = 0;
}


// Called when the game starts
void ULiteGPUSceneComponent::BeginPlay()
{
	Super::BeginPlay();
	static auto CVarEnableLiteGPUScene = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.Enable"));
	bool bEnableLiteGPUScene = CVarEnableLiteGPUScene ? CVarEnableLiteGPUScene->GetInt() > 0 : false;
	if (FApp::CanEverRender() && bEnableLiteGPUScene)
	{
		InitSharedResources();
		MarkRenderStateDirty();
	}
}


void ULiteGPUSceneComponent::BeginDestroy()
{
	Super::BeginDestroy();
	if (FApp::CanEverRender())
	{
		CleanupInstances();
	}
}

void ULiteGPUSceneComponent::PostLoad()
{
	CastShadow = false;

	Super::PostLoad();
#if WITH_EDITOR
	static auto CVarEnableLiteGPUScene = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.Enable"));
	bool bEnableLiteGPUScene = CVarEnableLiteGPUScene ? CVarEnableLiteGPUScene->GetInt() > 0 : false;
	if (FApp::CanEverRender() && bEnableLiteGPUScene)
	{
		InitSharedResources();
	}
#endif
}

void ULiteGPUSceneComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);
}

void ULiteGPUSceneComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();
}

void ULiteGPUSceneComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials /* = false */) const
{
	Super::GetUsedMaterials(OutMaterials, bGetDebugMaterials);

	for (int32 MatIndex = 0; MatIndex < AllUsedMats.Num(); MatIndex++)
	{
		OutMaterials.Add(AllUsedMats[MatIndex]);
	}
}

void ULiteGPUSceneComponent::DrawDebugBoxLine()
{	// Left for Debugging
#if (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)
	{
		UWorld* World = GetWorld();
		TArray<FColor> ColorArray;
		ColorArray.Add(FColor::Red);
		ColorArray.Add(FColor::Yellow);
		ColorArray.Add(FColor::Green);
		ColorArray.Add(FColor::Orange);
		ColorArray.Add(FColor::Purple);
		ColorArray.Add(FColor::Cyan);
		ColorArray.Add(FColor::Orange);
		ColorArray.Add(FColor::Magenta);
		ColorArray.Add(FColor::Silver);
		ColorArray.Add(FColor::White);
		ColorArray.Add(FColor::Blue);
		ColorArray.Add(FColor::Black);
		ColorArray.Add(FColor::Black);
		ColorArray.Add(FColor::Black);
		ColorArray.Add(FColor::Black);


		for (int32 Index = 0; Index < SharedPerInstanceData->InstanceNum; Index++)
		{

			// 		FVector4f Center = (SharedPerInstanceData->UploadInstanceAABB[Index * 2] + SharedPerInstanceData->UploadInstanceAABB[Index * 2 + 1]) * 0.5;
			// 		FVector4f Extend = (SharedPerInstanceData->UploadInstanceAABB[Index * 2 + 1] - SharedPerInstanceData->UploadInstanceAABB[Index * 2]) * 0.5;
			// 		int32 ColorIndex = SharedPerInstanceData->UploadInstanceFoliageTypeData[Index];
			// 		DrawDebugBox(World, Center, Extend, FQuat::Identity, ColorArray[ColorIndex], false, 0.5f);
		}
	}
#endif

}

void ULiteGPUSceneComponent::CheckData()
{

}

FBoxSphereBounds ULiteGPUSceneComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	float InfinityExtent = 5000000.0f;

	return FBoxSphereBounds(FVector::ZeroVector, FVector(InfinityExtent, InfinityExtent, InfinityExtent), InfinityExtent);
}

// Called every frame
void ULiteGPUSceneComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

}


FPrimitiveSceneProxy* ULiteGPUSceneComponent::CreateSceneProxy()
{
	if (SharedPerInstanceData.IsValid() && AllUsedMats.Num() > 0 && SharedMainVisibilityData->bInitialized)
	{
		return new FLiteGPUSceneProxy(this, GetWorld()->FeatureLevel);
	}
	return nullptr;
}

void ULiteGPUSceneComponent::BuildLiteGPUScene(const TArray<UStaticMesh*> InAllMeshes)
{
	MarkRenderStateDirty();
	CleanupInstances();
	ConstructCombinedFoliageVertices(InAllMeshes);
	InitSharedResources();
	MarkRenderStateDirty();
}

void ULiteGPUSceneComponent::AddInstanceGroup(class UHierarchicalInstancedStaticMeshComponent* HComponent)
{
	SCOPE_CYCLE_COUNTER(STAT_AddInstanceGroup);
	if (PerPatchMaxNum == 0 || !IsRegistered() || 
		(INDEX_NONE != IncreasedHISMs.Find(HComponent))
		)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!HComponent || AllSourceMeshes.Num() <= 0 || !World)
	{
		return;
	}

	int32 MeshIndex = AllSourceMeshes.Find(HComponent->GetStaticMesh());
	//IF the new mesh component's static mesh is collected,
	// add the mesh component to the IncreasedHISM, order with the distance to world view location
	// larger distance component is stored at the end of the array
	if (MeshIndex != INDEX_NONE)
	{
		for (int32 i = 0; i < HComponent->LiteGPUSceneDatas.Num(); ++i)
		{
			HComponent->LiteGPUSceneDatas[i].MeshIndex = MeshIndex;
		}
		HComponent->InstanceCount = HComponent->LiteGPUSceneDatas.Num();

		if (World->ViewLocationsRenderedLastFrame.Num() == 0 || HComponent->LiteGPUSceneDatas.Num() == 0)
		{
			IncreasedHISMs.Add(HComponent);
			CurInstanceNum += HComponent->InstanceCount;
			return;
		}

		// add the mesh component to the IncreasedHISM, order with the distance to world view location
		// larger distance component is stored at the end of the array
		FVector ViewLocation = World->ViewLocationsRenderedLastFrame[0];

		const FMatrix& T = HComponent->LiteGPUSceneDatas[0].TransformsData;
		FVector InstanceLocation = FVector(T.M[3][0], T.M[3][1], T.M[3][2]);
		float DistSq = FVector::DistSquared(ViewLocation, InstanceLocation);
		// Here using a binary search scheme to find location of the new component
		int32 Mid = IncreasedHISMs.Num();
		if (Mid == 0)
		{
			IncreasedHISMs.Add(HComponent);
		}
		else
		{
			const FMatrix& MaxT = IncreasedHISMs[Mid - 1]->LiteGPUSceneDatas[0].TransformsData;
			FVector MaxLocation = FVector(MaxT.M[3][0], MaxT.M[3][1], MaxT.M[3][2]);
			float MaxDistSq = FVector::DistSquared(ViewLocation, MaxLocation);
			if (DistSq > MaxDistSq)
			{
				IncreasedHISMs.Add(HComponent);
			}
			else
			{
				int32 InsertIndex = 0;
				int flag = 1;
				do
				{
					Mid = Mid / 2;
					InsertIndex += (flag * Mid);
					const FMatrix& Transform = IncreasedHISMs[InsertIndex]->LiteGPUSceneDatas[0].TransformsData;
					FVector MidLocation = FVector(Transform.M[3][0], Transform.M[3][1], Transform.M[3][2]);
					float MidDistSq = FVector::DistSquared(ViewLocation, MidLocation);
					flag = DistSq > MidDistSq ? 1 : -1;
				} while (Mid > 1);

				IncreasedHISMs.Insert(HComponent, InsertIndex);
			}

		}
		CurInstanceNum += HComponent->InstanceCount;
	}
}

void ULiteGPUSceneComponent::RemoveInstanceGroup(class UHierarchicalInstancedStaticMeshComponent* Key)
{
	if (PerPatchMaxNum <= 0)
	{
		return;
	}

	check(SharedPerInstanceData.IsValid());

	SCOPE_CYCLE_COUNTER(STAT_RemoveInstanceGroup);
	// Remove op is done by simply add to the array
	RemovedHISMs.AddUnique(Key);
	CurInstanceNum -= Key->InstanceCount;
}


void ULiteGPUSceneComponent::UpdateIncreasedGroup()
{
	int32 IncreasedThisFrame = 0;
	for (int32 Index = 0; Index < IncreasedHISMs.Num(); Index++)
	{
		check(IncreasedHISMs[Index]);

		int32 MaxIncreased = FMath::Min<uint32>(ONEFRAME_UPDATE_INSTANCE_NUM, IncreasedHISMs[Index]->InstanceCount - IncreasedHISMs[Index]->IncreasedOffset);

		if (AllInstanceNum + MaxIncreased <= MAX_BUFFER_ARRAY_NUM)
		{
			SharedPerInstanceData->UpdateIncreasedData(this, IncreasedHISMs[Index]);

			if (sEnableRemoveInstanceMultiFrame)
			{
				if (IncreasedThisFrame >= ONEFRAME_UPDATE_GROUP_NUM - 1)
				{
					break;
				}
			}
			IncreasedThisFrame++;
		}
	}

	// If instances are all updated, no need to update that HISM anymore
	TArray<int32> MarkForIncreased;
	for (int32 Index = 0; Index < IncreasedHISMs.Num(); Index++)
	{
		check(IncreasedHISMs[Index]);
		if (IncreasedHISMs[Index]->IncreasedOffset >= IncreasedHISMs[Index]->InstanceCount)
		{
			IncreasedHISMs[Index]->LiteGPUSceneDatas.Empty();
			MarkForIncreased.Add(Index);
		}
	}
	for (int32 i = 0; i < MarkForIncreased.Num(); i++)
	{
		IncreasedHISMs.RemoveAt(MarkForIncreased[i] - i);
	}

	// TODO : warp up logging
	if (sEnableOutputLiteGPUSceneLog)
	{
		UE_LOG(LogLiteGPUScene, Log, TEXT(" InstanceNum : %d"), AllInstanceNum);
		UE_LOG(LogLiteGPUScene, Log, TEXT("Cur  InstanceNum : %d"), CurInstanceNum);
		UWorld* World = GetWorld();

		FlushPersistentDebugLines(World);

		for (int32 ViewIndex = 0; ViewIndex < World->ViewLocationsRenderedLastFrame.Num(); ViewIndex++)
		{
			for (int32 Index = 0; Index < MAX_BUFFER_ARRAY_NUM; Index++)
			{
				FMatrix T = FMatrix::Identity;
				for (int i = 0; i < 4; i++)
				{
					for (int j = 0; j < 4; j++)
					{
						T.M[i][j] = SharedPerInstanceData->UploadInstanceTransformData[4 * Index + i][j];
					}
				}
				FVector ViewLocation = World->ViewLocationsRenderedLastFrame[ViewIndex];
				FVector InstanceLocation = FVector(T.M[3][0], T.M[3][1], T.M[3][2]);

				if (FVector::DistSquared(ViewLocation, InstanceLocation) < 10000000.0f)
				{
					uint8 MeshIndex = SharedPerInstanceData->UploadInstanceFoliageTypeData[Index];
					FBoxSphereBounds Bound = AllSourceMeshes[MeshIndex]->GetBounds();

					FBoxSphereBounds NewBound = Bound.TransformBy(T);
					FVector Center = NewBound.GetBox().GetCenter();
					FVector Extent = NewBound.GetBox().GetExtent();
					DrawDebugBox(World, Center, Extent, FQuat::Identity, FColor::Red, false, 0.5f);
				}
			}
		}
	}
}

void ULiteGPUSceneComponent::UpdateRemovedGroup()
{
	int32 RemovedThisFrame = 0;
	for (int32 Index = 0; Index < RemovedHISMs.Num(); Index++)
	{
		if (IncreasedHISMs.Contains(RemovedHISMs[Index]))
		{
			IncreasedHISMs.Remove(RemovedHISMs[Index]);
			// No need to add instance any more for this HISM
			RemovedHISMs[Index]->InstanceCount = RemovedHISMs[Index]->IncreasedOffset;
		}
		check(RemovedHISMs[Index]);
		SharedPerInstanceData->UpdateRemoveData(this, RemovedHISMs[Index]);
		RemovedThisFrame++;
		if (sEnableRemoveInstanceMultiFrame)
		{
			if (RemovedThisFrame >= ONEFRAME_UPDATE_GROUP_NUM - 1)
			{
				break;
			}
		}
	}

	// If all instances of HISM is removed, we delete this HISM  and remove it
	TArray<int32> MarkForRemoved;
	for (int32 Index = 0; Index < RemovedHISMs.Num(); Index++)
	{
		check(RemovedHISMs[Index]);
		if (RemovedHISMs[Index]->RemoveOffset >= RemovedHISMs[Index]->InstanceCount)
		{
			RemovedHISMs[Index]->GPUDrivenInstanceIndices.Empty();
			RemovedHISMs[Index]->ClearInstances();
			RemovedHISMs[Index]->DetachFromComponent(FDetachmentTransformRules(EDetachmentRule::KeepRelative, false));
			RemovedHISMs[Index]->DestroyComponent();
			MarkForRemoved.Add(Index);
		}
	}
	for (int32 i = 0; i < MarkForRemoved.Num(); i++)
	{
		RemovedHISMs.RemoveAt(MarkForRemoved[i] - i);
	}

}

void ULiteGPUSceneComponent::CleanupInstances()
{
	FLiteGPUSceneInstanceDataPtr* CleanupDataPtr = new FLiteGPUSceneInstanceDataPtr(SharedPerInstanceData);
	FLiteGPUSceneVertexIndexBufferPtr* CleanupVertexIndexBufferPtr = new FLiteGPUSceneVertexIndexBufferPtr(SharedVertexIndexBufferData);
	FLiteGPUSceneProxyVisibilityDataPtr* CleanupVisibilityDataPtr = new FLiteGPUSceneProxyVisibilityDataPtr(SharedMainVisibilityData);

	SharedPerInstanceData.Reset();
	SharedVertexIndexBufferData.Reset();
	SharedMainVisibilityData.Reset();

	AllSourceMeshes.Empty();
	CombinedVertexBufferData.Empty();
	CombinedIndiceBufferData.Empty();
	MaterialToPatchMap.Empty();
	RemovedHISMs.Empty();
	IncreasedHISMs.Empty();
	ENQUEUE_RENDER_COMMAND(CleanupLiteGPUSceneComponentData)(
		[CleanupDataPtr, CleanupVertexIndexBufferPtr, CleanupVisibilityDataPtr](FRHICommandList& RHICmdList)
		{
			if (CleanupDataPtr)
			{
				delete CleanupDataPtr;
				delete CleanupVertexIndexBufferPtr;
				delete CleanupVisibilityDataPtr;
			}
		}
	);
}

void ULiteGPUSceneComponent::FlushLiteGPUScene()
{
	MarkRenderStateDirty();
	FLiteGPUSceneInstanceDataPtr* CleanupDataPtr = new FLiteGPUSceneInstanceDataPtr(SharedPerInstanceData);
	FLiteGPUSceneVertexIndexBufferPtr* CleanupVertexIndexBufferPtr = new FLiteGPUSceneVertexIndexBufferPtr(SharedVertexIndexBufferData);
	FLiteGPUSceneProxyVisibilityDataPtr* CleanupVisibilityDataPtr = new FLiteGPUSceneProxyVisibilityDataPtr(SharedMainVisibilityData);

	SharedPerInstanceData.Reset();
	SharedVertexIndexBufferData.Reset();
	SharedMainVisibilityData.Reset();

	MaterialToPatchMap.Empty();
	TotalTriangleDraw = 0;
	AllInstanceNum = 0;
	CurInstanceNum = 0;
	RemovedHISMs.Empty();
	IncreasedHISMs.Empty();
	ENQUEUE_RENDER_COMMAND(CleanupLiteGPUSceneComponentData)(
		[CleanupDataPtr, CleanupVertexIndexBufferPtr, CleanupVisibilityDataPtr](FRHICommandList& RHICmdList)
		{
			if (CleanupDataPtr)
			{
				delete CleanupDataPtr;
				delete CleanupVertexIndexBufferPtr;
				delete CleanupVisibilityDataPtr;
			}
		}
	);

	InitSharedResources();
	MarkRenderStateDirty();
}

void ULiteGPUSceneComponent::ConstructCombinedFoliageVertices(const TArray<UStaticMesh*> InAllMeshes)
{
	AllSourceMeshes.Empty();
	AllSourceMeshes = InAllMeshes;
	PerPatchMaxNum = 0;
	AllPatches.Empty();
	AllUsedMats.Empty();
	CombinedVertexBufferData.Empty();
	CombinedIndiceBufferData.Empty();
	MaterialToPatchMap.Empty();

	for (int32 MeshIndex = 0; MeshIndex < AllSourceMeshes.Num(); MeshIndex++)
	{
		UStaticMesh* StaticMesh = AllSourceMeshes[MeshIndex];

		TArray<FStaticMaterial>& StaticMaterials = StaticMesh->GetStaticMaterials();
		FStaticMeshRenderData* MeshRenderData = StaticMesh->GetRenderData();

		for (int32 LODIndex = 0; LODIndex < MeshRenderData->LODResources.Num(); LODIndex++)
		{
			FStaticMeshLODResources& LODResource = MeshRenderData->LODResources[LODIndex];

			for (int32 SectionIndex = 0; SectionIndex < LODResource.Sections.Num(); SectionIndex++)
			{
				FStaticMeshSection& RenderSection = LODResource.Sections[SectionIndex];

				FStaticMaterial& Mat = StaticMaterials[RenderSection.MaterialIndex];
				UMaterialInterface* RealMat = Mat.MaterialInterface;
				int32 MappedMatIndex = AllUsedMats.AddUnique(RealMat);

				FLiteGPUSceneMeshPatchInfo PatchInfo;
				PatchInfo.MeshName = StaticMesh->GetName();
				PatchInfo.MaterialIndex = MappedMatIndex;
				PatchInfo.SectionIndex = SectionIndex;
				PatchInfo.LODIndex = LODIndex;
				PatchInfo.MeshIndex = MeshIndex;

				AddMeshLodSectionToBuffer(
					LODIndex
					, MeshRenderData
					, LODResource
					, RenderSection
					, CombinedVertexBufferData
					, CombinedIndiceBufferData
					, PatchInfo.FirstVertexOffset
					, PatchInfo.VertexCount
					, PatchInfo.FirstIndexOffset
					, PatchInfo.IndexCount
					, PatchInfo.ScreenSizeMin
					, PatchInfo.ScreenSizeMax
				);

				if (auto* pPatches = MaterialToPatchMap.Find(MappedMatIndex))
				{
					pPatches->Add(PatchInfo);
				}
				else
				{
					TArray<FLiteGPUSceneMeshPatchInfo> Patches;
					Patches.Add(PatchInfo);
					MaterialToPatchMap.Add(MappedMatIndex, Patches);
				}
			}
		}
	}
	// A safety check
	for (uint32 Index : CombinedIndiceBufferData)
	{
		check((int32)Index < CombinedVertexBufferData.Num());
	}
	// Collect Render Patchs/ Sections
	for (auto& [K, V] : MaterialToPatchMap)
	{
		for (auto& Patch : V)
		{
			Patch.PatchID = AllPatches.Num();
			AllPatches.Add(Patch);
		}
	}

	// Calculate teh max lod numbers among all  meshes
	for (UStaticMesh* StaticMesh : AllSourceMeshes)
	{
		if (StaticMesh)
		{
			if (FStaticMeshRenderData* MeshRenderData = StaticMesh->GetRenderData())
			{
				PerPatchMaxNum = FMath::Max(PerPatchMaxNum, MeshRenderData->LODResources.Num());
			}
		}
	}

	// Another safety check
	for (auto& PatchInfo : AllPatches)
	{
		check(PatchInfo.FirstVertexOffset + PatchInfo.VertexCount <= (uint32)CombinedVertexBufferData.Num());
		check(PatchInfo.FirstIndexOffset + PatchInfo.IndexCount <= (uint32)CombinedIndiceBufferData.Num());
	}
}

void ULiteGPUSceneComponent::UpdateRenderThread()
{
	if (SharedPerInstanceData.IsValid())
	{
		SharedPerInstanceData->UpdateInstanceData(this);
	}
}

bool ULiteGPUSceneComponent::IsInitialized() const
{
	if (SharedPerInstanceData.IsValid() && SharedPerInstanceData->bInitialized
		&& SharedMainVisibilityData.IsValid() && SharedMainVisibilityData->bInitialized
		&& SharedVertexIndexBufferData.IsValid() && SharedVertexIndexBufferData->bIntialized)
	{
		return true;
	}
	return false;
}

void ULiteGPUSceneComponent::AddMeshLodSectionToBuffer(int32 LodIndex, const FStaticMeshRenderData* MeshRenderData, const FStaticMeshLODResources& LODResource, const FStaticMeshSection& RenderSection, TArray<FLiteGPUSceneMeshVertex>& OutCombinedVertexBufferData, TArray<uint32>& OutCombinedIndiceBufferData, uint32& OutFirstVertexOffset, uint32& OutVertexCount, uint32& OutFirstIndexOffset, uint32& OutIndicesCount, float& OutScreenSizeMin, float& OutScreenSizeMax)
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

void ULiteGPUSceneComponent::InitSharedResources()
{
	// if NO LOD resources at all, return
	if (PerPatchMaxNum == 0)
	{
		return;
	}
	if (!SharedPerInstanceData.IsValid())
	{
		SharedPerInstanceData = MakeShareable(new FLiteGPUSceneInstanceData());
	}
	if (!SharedVertexIndexBufferData.IsValid())
	{
		SharedVertexIndexBufferData = MakeShareable(new FLiteGPUSceneVertexIndexBuffer());
	}
	if (!SharedMainVisibilityData.IsValid())
	{
		SharedMainVisibilityData = MakeShareable(new FLiteGPUSceneProxyVisibilityData());
	}
	if (AllPatches.Num() > 0 && CombinedVertexBufferData.Num() > 0 && CombinedIndiceBufferData.Num() > 0 && AllUsedMats.Num() > 0)
	{
		// Why redid this step?  This already added in ConstructVertices
		// Fine, if this works, lets just leave it
		MaterialToPatchMap.Empty();
		for (const auto& PatchInfo : AllPatches)
		{
			check(AllUsedMats.IsValidIndex(PatchInfo.MaterialIndex))
			if (auto* pPatches = MaterialToPatchMap.Find(PatchInfo.MaterialIndex))
			{
				pPatches->Add(PatchInfo);
			}
			else
			{
				TArray<FLiteGPUSceneMeshPatchInfo> Patches;
				Patches.Add(PatchInfo);
				MaterialToPatchMap.Add(PatchInfo.MaterialIndex, Patches);
			}
		}
		SharedPerInstanceData->InitInstanceData(this, AllPatches);
		SharedVertexIndexBufferData->InitVertexIndexBuffer(CombinedVertexBufferData, CombinedIndiceBufferData);
		SharedMainVisibilityData->InitVisibilityData(AllPatches.Num(), true, PerPatchMaxNum);
	}
}

FLiteGPUSceneBufferManager* ULiteGPUSceneComponent::GetGPUDrivenBufferManager()
{
	FSceneInterface* Scene = GetScene();
	if (Scene)
	{
		return Scene->GetLiteGPUSceneBufferManager();
	}
	return nullptr;
}

void ULiteGPUSceneComponent::OnComponentCreated()
{
	Super::OnComponentCreated();
}

FLiteGPUSceneProxy::FLiteGPUSceneProxy(ULiteGPUSceneComponent* Component, ERHIFeatureLevel::Type InFeatureLevel)
	:FPrimitiveSceneProxy(Component, TEXT("LiteGPUScene"))
	, ProxyFeatureLevel(InFeatureLevel)
{
	SharedPerInstanceData = Component->SharedPerInstanceData;

	SharedVertexIndexBufferData = Component->SharedVertexIndexBufferData;

	FoliageTypeNum = Component->AllSourceMeshes.Num();
	InstanceNum = Component->AllInstanceNum;

	FoliageBounds.SetNum(FoliageTypeNum);
	for (int32 FoliageIndex = 0; FoliageIndex < FoliageTypeNum; FoliageIndex++)
	{
		FoliageBounds[FoliageIndex] = Component->AllSourceMeshes[FoliageIndex]->GetBounds();
	}

	// Build up a stats of <mat_proxy, patch_ids> map
	for (auto& pair : Component->MaterialToPatchMap)
	{
		int32 MappedMaterialIndex = pair.Key;
		UMaterialInterface* Mat = Component->AllUsedMats[MappedMaterialIndex];
		if (Mat && Mat->GetRenderProxy())
		{
			FMaterialRenderProxy* MatProxy = Mat->GetRenderProxy();
			const TArray<FLiteGPUSceneMeshPatchInfo>& PatchInfoArray = pair.Value;
			for (const FLiteGPUSceneMeshPatchInfo& PatchInfo : PatchInfoArray)
			{
				int32 InsertPatchIndex = AllPatches.Add(PatchInfo);
				TArray<int32>* pPatchIndiceArray = MatToPatchIndiceMap.Find(MatProxy);
				if (pPatchIndiceArray)
				{
					pPatchIndiceArray->Add(InsertPatchIndex);
				}
				else
				{
					TArray<int32> PatchIndiceArray;
					PatchIndiceArray.Add(InsertPatchIndex);
					MatToPatchIndiceMap.Add(MatProxy, PatchIndiceArray);
				}
			}
		}
	}
	SharedMainVisibilityData = Component->SharedMainVisibilityData;

	pGPUDrivenVertexFactory = new FLiteGPUSceneVertexFactory(InFeatureLevel);
	FLiteGPUSceneProxy* pLiteGPUSceneProxy = this;
	UserData = new FLiteGPUSceneVertexFactoryUserData();
	UserData->SceneProxy = this;

	FSceneInterface* SceneInterface = Component->GetWorld()->Scene;
	CachedSceneInterface = SceneInterface;
	ENQUEUE_RENDER_COMMAND(InitGPUDrivenFoliageProxy)(
		[pLiteGPUSceneProxy, SceneInterface](FRHICommandList& RHICmdList)
		{
			pLiteGPUSceneProxy->Init_RenderingThread();
			SceneInterface->AddOrRemoveLiteGPUSceneProxy_RenderingThread(pLiteGPUSceneProxy, true);
		}
	);

	BeginInitResource(pGPUDrivenVertexFactory);

#if !UE_BUILD_SHIPPING
	CachedComponent = Component;
#endif
}

FLiteGPUSceneProxy::~FLiteGPUSceneProxy()
{
	if (IsInRenderingThread())
	{
		SharedPerInstanceData.Reset();

		SharedVertexIndexBufferData.Reset();

		if (CachedSceneInterface)
		{
			// TODO
			CachedSceneInterface->AddOrRemoveLiteGPUSceneProxy_RenderingThread(this, false);
		}

		if (nullptr != pGPUDrivenVertexFactory)
		{
			pGPUDrivenVertexFactory->ReleaseResource();
			delete pGPUDrivenVertexFactory;
			pGPUDrivenVertexFactory = nullptr;
		}

		if (UserData)
		{
			delete UserData;
			UserData = nullptr;
		}
	}
}

FPrimitiveViewRelevance FLiteGPUSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bShadowRelevance = IsShadowCast(View);
	Result.bDynamicRelevance = true;
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bDistortion = true;
	Result.bNormalTranslucency = true;
	Result.bSeparateTranslucency = true;

	return Result;
}

void FLiteGPUSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	const FSceneView* MainView = Views[0];
	static auto CVarEnable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.Enable"));
	bool bLiteGPUScene = CVarEnable && CVarEnable->GetInt() > 0;
	if (!bLiteGPUScene || !SharedPerInstanceData->bInitialized)
	{
		return;
	}
	if (!IsInitialized())
	{
		return;
	}
	if (!SharedMainVisibilityData->bFirstGPUCullinged)
	{
		return;
	}
	if (MainView->bIsSceneCapture || MainView->bIsReflectionCapture)
	{
		return;
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];
			const FConvexVolume* ShadowFrustomView = View->GetDynamicMeshElementsShadowCullFrustum();
			if (ShadowFrustomView)
			{
				return;
			}
			{
#if ENABLE_LITE_GPU_SCENE_DEBUG
				SCOPE_CYCLE_COUNTER(STAT_ReadBackBuffer);
				FRHICommandListImmediate& RHICmdList = GRHICommandList.GetImmediateCommandList();
				// Read last frame generated
				if (sDebugLiteGPUSceneReadBack)
				{
					RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, SharedPerInstanceData->RWDrawedTriangleCountBuffer->UAV);
					if (SharedPerInstanceData->RWDrawedTriangleCountBufferAsyncReadBack->IsReady())
					{
						uint32 TriangelNum = 0;
						SharedPerInstanceData->RWDrawedTriangleCountBufferAsyncReadBack->ReadBackLastEnqueuedBuffer((uint8*)&TriangelNum, sizeof(uint32));
						SharedPerInstanceData->RWDrawedTriangleCountBufferAsyncReadBack->EnqueueCopy(RHICmdList, SharedPerInstanceData->RWDrawedTriangleCountBuffer->Buffer, sizeof(uint32));
						CachedComponent->TotalTriangleDraw = (int32)TriangelNum;
#if STATS
						INC_DWORD_STAT_BY(STAT_RHIIndirectTriangles, CachedComponent->TotalTriangleDraw);
#endif
					}
				}
#endif
				DrawMeshBatches(ViewIndex, View, ViewFamily, Collector);
				SharedMainVisibilityData->bDirty = true;
			}
		}
	}
}

void FLiteGPUSceneProxy::PostUpdateBeforePresent(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily)
{
	const FSceneView* View = Views[0];
	if (!SharedMainVisibilityData.IsValid() || !SharedMainVisibilityData->bInitialized || View->bIsSceneCapture || View->bIsReflectionCapture)
	{
		return;
	}
	LastFrameViewForward = FVector3f(View->GetViewDirection());
}

void FLiteGPUSceneProxy::DrawMeshBatches(int32 ViewIndex, const FSceneView* View, const FSceneViewFamily& ViewFamily, FMeshElementCollector& Collector) const
{
	if (!SharedMainVisibilityData->bFirstGPUCullinged)
	{
		return;
	}
	if (SharedVertexIndexBufferData->IndexBuffer->IndicesNum <= 0)
	{
		return;
	}
	int32 IndirectDrawOffset = 0;
	for (auto& Var : MatToPatchIndiceMap)
	{
		const TArray<int32>& PatchIndices = Var.Value;

		FMeshBatch& Mesh = Collector.AllocateMesh();
		FMeshBatchElement& BatchElement = Mesh.Elements[0];
		BatchElement.IndexBuffer = SharedVertexIndexBufferData->IndexBuffer;
		Mesh.bWireframe = false;
		Mesh.VertexFactory = pGPUDrivenVertexFactory;
		Mesh.MaterialRenderProxy = Var.Key;
		Mesh.LODIndex = 0;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		Mesh.VisualizeHLODIndex = 0;
#endif
		BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
		BatchElement.FirstIndex = 0;
		BatchElement.NumPrimitives = 0;
		BatchElement.MinVertexIndex = 0;
		BatchElement.MaxVertexIndex = SharedVertexIndexBufferData->VertexBuffer->VerticesNum - 1;

		BatchElement.UserData = (void*)UserData;
		BatchElement.bUserDataIsColorVertexBuffer = false;
		BatchElement.InstancedLODIndex = 0;
		BatchElement.UserIndex = 0;

		// BatchElement.FirstInstance = 0;
		BatchElement.NumInstances = 1;

		BatchElement.IndirectArgsBuffer = SharedMainVisibilityData->RWIndirectDrawBuffer->Buffer;
		BatchElement.IndirectArgsOffset = IndirectDrawOffset * sizeof(LiteGPUSceneIndirectArguments);

		// BatchElement.IndirectDrawCount = PatchIndices.Num();
		// BatchElement.IndirectBufferStride = sizeof(LiteGPUSceneIndirectArguments);
		IndirectDrawOffset += PatchIndices.Num();
		Collector.AddMesh(ViewIndex, Mesh);
	}
}

bool FLiteGPUSceneProxy::IsInitialized() const
{
	if (pGPUDrivenVertexFactory && pGPUDrivenVertexFactory->IsInitialized()
		&& SharedPerInstanceData.IsValid() && SharedPerInstanceData->bInitialized
		&& SharedMainVisibilityData.IsValid() && SharedMainVisibilityData->bInitialized
		&& SharedVertexIndexBufferData.IsValid() && SharedVertexIndexBufferData->bIntialized)
	{
		return true;
	}
	return false;
}

void FLiteGPUSceneProxy::Init_RenderingThread()
{
	check(SharedMainVisibilityData->bInitialized);
	UE_LOG(LogLiteGPUScene, Log, TEXT("Call FLiteGPUSceneProxy::Init_RenderingThread"));
	pGPUDrivenVertexFactory->Init_RenderThread(SharedVertexIndexBufferData->VertexBuffer, SharedMainVisibilityData->pInstanceIndexVertexBuffer);
}

void FLiteGPUSceneVertexIndexBuffer::InitVertexIndexBuffer(const TArray<FLiteGPUSceneMeshVertex>& Vertices, const TArray<uint32>& Indices)
{
	if (bIntialized)
	{
		return;
	}
	UE_LOG(LogLiteGPUScene, Log, TEXT("FLiteGPUSceneVertexIndexBuffer::Init"));

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
	bIntialized = true;
}

void FLiteGPUSceneVertexIndexBuffer::Release_RenderingThread()
{
	check(IsInRenderingThread());

	if (bIntialized)
	{
		bIntialized = false;
		VertexBuffer->ReleaseResource();
		IndexBuffer->ReleaseResource();

		delete VertexBuffer;
		delete IndexBuffer;

		VertexBuffer = nullptr;
		IndexBuffer = nullptr;
	}
}
