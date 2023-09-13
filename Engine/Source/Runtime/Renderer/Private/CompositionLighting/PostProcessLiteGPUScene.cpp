#include "CompositionLighting/PostProcessLiteGPUScene.h"
#include "LiteGPUSceneSubsystem.h"
#include "SceneUtils.h"
#include "ScenePrivate.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"
#include "GlobalShader.h"
#include "RHICommandList.h"
#include "RHIUtilities.h"
#include "UniformBuffer.h"
#include "ShaderParameterUtils.h"
#include "ShadowRendering.h"
#include "Components/LiteGPUSceneComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogLiteGPUSceneCulling, Log, All);
DECLARE_GPU_STAT_NAMED(LiteGPUSceneCulling, TEXT("GPUDriven LiteGPUScene Culling"));

static int32 GEnableDebugLiteGPUSceneBounds = 0;
static FAutoConsoleVariableRef CVarEnableDebugBounds(
	TEXT("r.LiteGPUScene.DebugBounds"),
	GEnableDebugLiteGPUSceneBounds,
	TEXT("Enable to debug lite gpu scene bounds(default = 1 (enabled))"));

static float sLiteGPUSceneMinScreenSize = 0.012f;
static FAutoConsoleVariableRef CVarLiteGPUSceneMinScreenSize(
	TEXT("r.LiteGPUScene.MinScreenSize"),
	sLiteGPUSceneMinScreenSize,
	TEXT("Var for lite gpu scene screen size culling"),
	ECVF_Scalability
);

static int32 GLiteGPUSceneCullingUseWaveOps = 1;
static FAutoConsoleVariableRef CVarLiteGPUSceneCullingUseWaveOps(
	TEXT("r.LiteGPUScene.UseWaveOps"),
	GLiteGPUSceneCullingUseWaveOps,
	TEXT("Use wave intrisics in DX12. (default = 1)"));

static int32 sLiteGPUSceneHZBMinMipLevel = 0;
static FAutoConsoleVariableRef CVarLiteGPUSceneMinMipLevel(
	TEXT("r.LiteGPUScene.HZBMinMipLevel"),
	sLiteGPUSceneHZBMinMipLevel,
	TEXT(""));

static int32 sLiteGPUSceneHZBMaxMipLevel = 9;
static FAutoConsoleVariableRef CVarLiteGPUSceneMaxMipLevel(
	TEXT("r.LiteGPUScene.HZBMaxMipLevel"),
	sLiteGPUSceneHZBMaxMipLevel,
	TEXT(""));

namespace Detail
{
	void Cull(FRHICommandList& RHICmdList
		, const FViewInfo& View
		, class FLiteGPUSceneProxy* CullingProxy)
	{

	}

	void DepthDrawTest(FRHICommandList& RHICmdList
		, const FViewInfo& View
		, class FLiteGPUSceneProxy* CullingProxy)
	{

	}

	void Debug(FRHICommandList& RHICmdList
		, const FViewInfo& View
		, class FLiteGPUSceneProxy* CullingProxy)
	{

	}
};

void AddLiteGPUSceneCullingPass(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	static auto CVarEnableLiteGPUScene = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.Enable"));
	bool bLiteGPUScene = CVarEnableLiteGPUScene && CVarEnableLiteGPUScene->GetInt() > 0;
	if (!bLiteGPUScene)
	{
		return;
	}
	check(IsInRenderingThread());
	if (View.bIsSceneCapture || View.bIsReflectionCapture || View.bIsPlanarReflection)
	{
		return;
	}

	GraphBuilder.AddPass(
		{},
		ERDGPassFlags::NeverParallel,
		[&View](FRHICommandList& RHICmdList)
		{
			const FSceneViewFamily& ViewFamily = *(View.Family);
			FScene& Scene = *(FScene*)ViewFamily.Scene;
			for (auto Proxy : Scene.CachedLiteGPUScene)
			{
				if (Proxy && Proxy->IsInitialized())
				{

				}
			}
		});
}