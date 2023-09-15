#pragma once
#include "RenderGraph.h"
#include "SceneTextureParameters.h"
#include "SceneView.h"
#include "InstanceCulling/InstanceCullingContext.h"

void AddLiteGPUSceneCullingPass(FRDGBuilder& GraphBuilder, const FViewInfo& ViewInfo);