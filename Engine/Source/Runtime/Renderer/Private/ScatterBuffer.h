#pragma once
#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "Math/RandomStream.h"
#include "RHI.h"
#include "SceneCore.h"

#define GVDB_THREAD_COUNT 1024

enum class EScatterUploadBufferType
{
	E_SUBT_FLOAT_BUFFER = 0,
	E_SUBT_FLOAT2_BUFFER,
	E_SUBT_FLOAT3_BUFFER,
	E_SUBT_FLOAT4_BUFFER,

	E_SUBT_SINT_BUFFER,
	E_SUBT_SINT4_BUFFER,
	E_SUBT_UINT_BUFFER,

	E_SUBT_Count,
};

class FGVDBScatterUploadBuffer
{
public:
	FReadBuffer ScatterBuffer;
	FReadBuffer UploadBuffer;
	FGVDBScatterUploadBuffer();
	uint32* ScatterData = nullptr;
	uint8* UploadData = nullptr;

	uint32 NumScatters = 0;
	uint32 MaxScatters = 0;
	uint32 NumScattersAllocated = 0;
	uint32 BytePerElement = 0;
	EPixelFormat Format = EPixelFormat::PF_Unknown;
	uint32 ElementOffset = 0;

	void Init(uint32 InNumBytesPerElement, uint32 InNumElements, EPixelFormat InFormat, const TCHAR* DebugName, uint32 InElementOffset = 0);
	void Add(uint32 Index, const void* Data, uint32 Num = 1);

	void ResourceUploadTo(FRHICommandList& RHICmdList, FRWBuffer& DstBuffer, EScatterUploadBufferType BufferType, bool bFlush = false);

	void Release()
	{
		ScatterBuffer.Release();
		UploadBuffer.Release();
	}

	uint32 GetNumBytes() const
	{
		return ScatterBuffer.NumBytes + UploadBuffer.NumBytes;
	}
};

class FGVDBScatterDoubleBufferUploader
{
public:
	FGVDBScatterDoubleBufferUploader();
	bool Init(uint32 InNumBytesPerElement, uint32 InNumElements, EPixelFormat InFormat, const TCHAR* DebugName, uint32 InElementOffset = 0);
	void Add(uint32 Index, const void* Data, uint32 Num = 1);

	void ResourceUploadTo(
		FRHICommandList& RHICmdList,
		FRWBuffer& DstBuffer,
		EScatterUploadBufferType BufferType,
		bool bFlush = false);

	void Release();

	bool IsReady(int32 FenceIndex) const;

	FGVDBScatterUploadBuffer UploadBuffer[2];
	FGPUFenceRHIRef Fence[2];
	int32 CurrendUsedBufferIndex;
};