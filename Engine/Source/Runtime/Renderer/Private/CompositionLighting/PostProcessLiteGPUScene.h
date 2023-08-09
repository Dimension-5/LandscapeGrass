#pragma once
#include "RenderGraph.h"
#include "SceneTextureParameters.h"
#include "SceneView.h"
#include "InstanceCulling/InstanceCullingContext.h"

#define LITE_GPU_SCENE_COMPUTE_THREAD_NUM 512

void AddLiteGPUSceneCullingPass(FRDGBuilder& GraphBuilder, const FViewInfo& ViewInfo);