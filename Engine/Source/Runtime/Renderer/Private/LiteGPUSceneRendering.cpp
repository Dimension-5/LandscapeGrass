#include "Components/LiteGPUSceneComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "RHIGPUReadback.h"
#include "DrawDebugHelpers.h"
#include "RHICommandList.h"
#include "../Private/SceneRendering.h"
#include "../Private/PostProcess/SceneRenderTargets.h"
#include "../Private/ScenePrivate.h"

DEFINE_LOG_CATEGORY_STATIC(LogLiteGPUSceneRendering, Log, All);

void FScene::InitializeLiteGPUSceneBufferManager()
{
	if (pLiteGPUSceneBufferManager == nullptr)
	{
		pLiteGPUSceneBufferManager = new FLiteGPUSceneBufferManager(FeatureLevel);
		pLiteGPUSceneBufferManager->InitResource();
	}
}

FLiteGPUSceneBufferManager::FLiteGPUSceneBufferManager(ERHIFeatureLevel::Type InFeatureLevel)
{
	check(IsInRenderingThread());
	bInitialized = false;
	RWInstanceInstanceIndicesBufferUploader = nullptr;
	RWInstancedAABBBufferUploader = nullptr;

	RWInstanceLocationBufferUploader = nullptr;
	RWInstanceScaleBufferUploader = nullptr;
	RWInstanceTransformBufferUploader = nullptr;
	RWInstanceTypeBufferUploader = nullptr;

	RWInstancePatchNumBufferUploader = nullptr;
	RWInstancePatchStartBufferUploader = nullptr;
	RWInstancePatchIDsBufferUploader = nullptr;
}

void FLiteGPUSceneBufferManager::InitRHI(FRHICommandListBase& RHICmdList)
{
	InitRenderResource();
	FRenderResource::InitRHI(RHICmdList);
}

void FLiteGPUSceneBufferManager::ReleaseRHI()
{
	ReleaseRenderResource();
	FRenderResource::ReleaseRHI();
}

void FLiteGPUSceneBufferManager::UpdateUploader(FRHICommandList& RHICmdList, class FLiteGPUSceneInstanceData* InstanceDataPtr, int32 DirtyCount)
{
	SCOPE_CYCLE_COUNTER(STAT_ResourceUpload);
	if (!InstanceDataPtr)
	{
		return;
	}

	int32 DirtyBufferNum = DirtyCount;
	const TArray<uint32>& InDirtyBuffer = InstanceDataPtr->UpdateDirtyInstanceIndices;

	if (DirtyBufferNum <= 0)
	{
		return;
	}
	if (RWInstanceTransformBufferUploader)
	{
		uint32 UpdateNum = DirtyBufferNum * 4;
		const bool UploaderReady = RWInstanceTransformBufferUploader->Init(
			GPixelFormats[PF_A32B32G32R32F].BlockBytes,
			UpdateNum,
			PF_A32B32G32R32F,
			TEXT("RWInstanceLocationBufferUploader"));

		if (UploaderReady && InstanceDataPtr->UploadInstanceTransformData.GetData() != nullptr)
		{
			for (int32 Index = 0; Index < DirtyBufferNum; Index++)
			{
				int32 IndexDirty = InDirtyBuffer[Index];
				int32 InstanceUploadOffset = IndexDirty * 4;
				check(InstanceDataPtr->UploadInstanceTransformData.Num() >= InstanceUploadOffset + 4);
				FVector4f* addressA = InstanceDataPtr->UploadInstanceTransformData.GetData() + InstanceUploadOffset;
				RWInstanceTransformBufferUploader->Add(InstanceUploadOffset, (void*)addressA, 4);
			}

			RWInstanceTransformBufferUploader->ResourceUploadTo(
				RHICmdList,
				*InstanceDataPtr->RWInstanceTransformBuffer,
				EScatterUploadBufferType::E_SUBT_FLOAT4_BUFFER,
				false);
		}
	}
	if (RWInstanceScaleBufferUploader)
	{

		uint32 UpdateNum = DirtyBufferNum;
		const bool UploaderReady = RWInstanceScaleBufferUploader->Init(
			GPixelFormats[PF_A32B32G32R32F].BlockBytes,
			UpdateNum,
			PF_A32B32G32R32F,
			TEXT("RWInstanceScaleBufferUploader"));

		if (UploaderReady && InstanceDataPtr->UploadInstanceScaleData.GetData() != nullptr)
		{
			for (int32 Index = 0; Index < DirtyBufferNum; Index++)
			{
				int32 IndexDirty = InDirtyBuffer[Index];
				int32 InstanceUploadOffset = IndexDirty;
				check(InstanceDataPtr->UploadInstanceScaleData.Num() >= InstanceUploadOffset + 1);
				FVector4f* addressA = InstanceDataPtr->UploadInstanceScaleData.GetData() + InstanceUploadOffset;
				RWInstanceScaleBufferUploader->Add(InstanceUploadOffset, (void*)addressA, 1);
			}

			RWInstanceScaleBufferUploader->ResourceUploadTo(
				RHICmdList,
				*InstanceDataPtr->RWInstanceScaleBuffer,
				EScatterUploadBufferType::E_SUBT_FLOAT4_BUFFER,
				false);
		}
	}

	if (RWInstanceTypeBufferUploader)
	{
		uint32 UpdateNum = DirtyBufferNum;
		const bool UploaderReady = RWInstanceTypeBufferUploader->Init(
			GPixelFormats[PF_R8_UINT].BlockBytes,
			UpdateNum,
			PF_R8_UINT,
			TEXT("RWInstanceFoliageTypeBufferUploader"));

		if (UploaderReady && InstanceDataPtr->UploadInstanceFoliageTypeData.GetData() != nullptr)
		{
			for (int32 Index = 0; Index < DirtyBufferNum; Index++)
			{
				int32 IndexDirty = InDirtyBuffer[Index];
				int32 InstanceUploadOffset = IndexDirty;
				check(InstanceDataPtr->UploadInstanceFoliageTypeData.Num() >= InstanceUploadOffset + 1);
				uint8* addressA = InstanceDataPtr->UploadInstanceFoliageTypeData.GetData() + InstanceUploadOffset;
				RWInstanceTypeBufferUploader->Add(InstanceUploadOffset, (void*)addressA, 1);
			}

			RWInstanceTypeBufferUploader->ResourceUploadTo(
				RHICmdList,
				*InstanceDataPtr->RWInstanceTypeBuffer,
				EScatterUploadBufferType::E_SUBT_UINT_BUFFER,
				false);
		}
	}
	if (RWInstancePatchNumBufferUploader)
	{
		uint32 UpdateNum = DirtyBufferNum;
		const bool UploaderReady = RWInstancePatchNumBufferUploader->Init(
			GPixelFormats[PF_R8_UINT].BlockBytes,
			UpdateNum,
			PF_R8_UINT,
			TEXT("RWInstancePatchNumBufferUploader"));

		if (UploaderReady && InstanceDataPtr->UploadInstancePatchNum.GetData() != nullptr)
		{
			for (int32 Index = 0; Index < DirtyBufferNum; Index++)
			{
				int32 IndexDirty = InDirtyBuffer[Index];
				int32 InstanceUploadOffset = IndexDirty;
				check(InstanceDataPtr->UploadInstancePatchNum.Num() >= InstanceUploadOffset + 1);
				uint8* addressA = InstanceDataPtr->UploadInstancePatchNum.GetData() + InstanceUploadOffset;
				RWInstancePatchNumBufferUploader->Add(InstanceUploadOffset, (void*)addressA, 1);
			}

			RWInstancePatchNumBufferUploader->ResourceUploadTo(
				RHICmdList,
				*InstanceDataPtr->RWInstancePatchNumBuffer,
				EScatterUploadBufferType::E_SUBT_UINT_BUFFER,
				false);
		}
	}
	if (RWInstancePatchIDsBufferUploader)
	{

		uint32 UpdateNum = DirtyBufferNum * InstanceDataPtr->PerPatchMaxNum;
		const bool UploaderReady = RWInstancePatchIDsBufferUploader->Init(
			GPixelFormats[PF_R8_UINT].BlockBytes,
			UpdateNum,
			PF_R8_UINT,
			TEXT("RWInstancePatchIDsBufferUploader"));

		if (UploaderReady && InstanceDataPtr->UploadInstancePatchIDs.GetData() != nullptr)
		{
			for (int32 Index = 0; Index < DirtyBufferNum; Index++)
			{
				int32 IndexDirty = InDirtyBuffer[Index];
				int32 InstanceUploadOffset = IndexDirty * InstanceDataPtr->PerPatchMaxNum;
				check(InstanceDataPtr->UploadInstancePatchIDs.Num() >= InstanceUploadOffset + InstanceDataPtr->PerPatchMaxNum);
				uint8* addressA = InstanceDataPtr->UploadInstancePatchIDs.GetData() + InstanceUploadOffset;
				RWInstancePatchIDsBufferUploader->Add(InstanceUploadOffset, (void*)addressA, InstanceDataPtr->PerPatchMaxNum);
			}

			RWInstancePatchIDsBufferUploader->ResourceUploadTo(
				RHICmdList,
				*InstanceDataPtr->RWInstancePatchIDsBuffer,
				EScatterUploadBufferType::E_SUBT_UINT_BUFFER,
				false);
		}
	}
	InstanceDataPtr->CullingInstancedNum = InstanceDataPtr->InstanceNum;
}

void FLiteGPUSceneBufferManager::InitRenderResource()
{
	if (RWInstanceInstanceIndicesBufferUploader == nullptr)
	{
		RWInstanceInstanceIndicesBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	if (RWInstancedAABBBufferUploader == nullptr)
	{
		RWInstancedAABBBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	if (RWInstanceLocationBufferUploader == nullptr)
	{
		RWInstanceLocationBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	if (RWInstanceScaleBufferUploader == nullptr)
	{
		RWInstanceScaleBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	if (RWInstanceTransformBufferUploader == nullptr)
	{
		RWInstanceTransformBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	if (RWInstanceTypeBufferUploader == nullptr)
	{
		RWInstanceTypeBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	if (RWInstancePatchNumBufferUploader == nullptr)
	{
		RWInstancePatchNumBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	if (RWInstancePatchIDsBufferUploader == nullptr)
	{
		RWInstancePatchIDsBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	if (RWInstancePatchStartBufferUploader == nullptr)
	{
		RWInstancePatchStartBufferUploader = new FGVDBScatterDoubleBufferUploader();
	}
	bInitialized = true;
	UE_LOG(LogLiteGPUSceneRendering, Log, TEXT("call FLiteGPUSceneBufferManager::InitRenderResource"));
}

void FLiteGPUSceneBufferManager::ReleaseRenderResource()
{
	if (RWInstanceInstanceIndicesBufferUploader)
	{
		RWInstanceInstanceIndicesBufferUploader->Release();
		delete RWInstanceInstanceIndicesBufferUploader;
		RWInstanceInstanceIndicesBufferUploader = nullptr;
	}

	if (RWInstancedAABBBufferUploader)
	{
		RWInstancedAABBBufferUploader->Release();
		delete RWInstancedAABBBufferUploader;
		RWInstancedAABBBufferUploader = nullptr;
	}

	if (RWInstanceLocationBufferUploader)
	{
		RWInstanceLocationBufferUploader->Release();
		delete RWInstanceLocationBufferUploader;
		RWInstanceLocationBufferUploader = nullptr;
	}
	if (RWInstanceScaleBufferUploader)
	{
		RWInstanceScaleBufferUploader->Release();
		delete RWInstanceScaleBufferUploader;
		RWInstanceScaleBufferUploader = nullptr;
	}
	if (RWInstanceTransformBufferUploader)
	{
		RWInstanceTransformBufferUploader->Release();
		delete RWInstanceTransformBufferUploader;
		RWInstanceTransformBufferUploader = nullptr;
	}
	if (RWInstanceTypeBufferUploader)
	{
		RWInstanceTypeBufferUploader->Release();
		delete RWInstanceTypeBufferUploader;
		RWInstanceTypeBufferUploader = nullptr;
	}

	if (RWInstancePatchNumBufferUploader)
	{
		RWInstancePatchNumBufferUploader->Release();
		delete RWInstancePatchNumBufferUploader;
		RWInstancePatchNumBufferUploader = nullptr;
	}
	if (RWInstancePatchIDsBufferUploader)
	{
		RWInstancePatchIDsBufferUploader->Release();
		delete RWInstancePatchIDsBufferUploader;
		RWInstancePatchIDsBufferUploader = nullptr;
	}
	if (RWInstancePatchIDsBufferUploader)
	{
		RWInstancePatchIDsBufferUploader->Release();
		delete RWInstancePatchIDsBufferUploader;
		RWInstancePatchIDsBufferUploader = nullptr;
	}
}