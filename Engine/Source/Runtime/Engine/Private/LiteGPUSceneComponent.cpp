#include "Components/LiteGPUSceneComponent.h"
#include "LiteGPUSceneSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogLiteGPUScene, Log, All);

FLiteGPUSceneProxy::FLiteGPUSceneProxy(ULiteGPUSceneComponent* Component, ERHIFeatureLevel::Type InFeatureLevel)
	:FPrimitiveSceneProxy(Component, TEXT("LiteGPUScene"))
{

}

FLiteGPUSceneProxy::~FLiteGPUSceneProxy()
{
	if (IsInRenderingThread())
	{

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
	return;
}

void FLiteGPUSceneProxy::PostUpdateBeforePresent(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily)
{

}

void FLiteGPUSceneProxy::DrawMeshBatches(int32 ViewIndex, const FSceneView* View, const FSceneViewFamily& ViewFamily, FMeshElementCollector& Collector) const
{
	return;
}

bool FLiteGPUSceneProxy::IsInitialized() const
{
	return false;
}

void FLiteGPUSceneProxy::Init_RenderingThread()
{
	return;
}

void ULiteGPUSceneComponent::ManagedTick(float DeltaTime)
{

}

TArray<int32> ULiteGPUSceneComponent::AddInstances(const TArray<FTransform>& InstanceTransforms, bool bShouldReturnIndices, bool bWorldSpace)
{
	UE_LOG(LogLiteGPUScene, Log, TEXT("Added %d Instances to LiteGPUScene."), InstanceTransforms.Num());
	return {};
}

bool ULiteGPUSceneComponent::GetInstanceTransform(int32 InstanceIndex, FTransform& OutInstanceTransform, bool bWorldSpace) const
{
	return true;
}

bool ULiteGPUSceneComponent::RemoveInstances(const TArray<int32>& InstancesToRemove)
{
	return true;
}

bool ULiteGPUSceneComponent::ClearInstances()
{
	return true;
}

void ULiteGPUSceneComponent::OnRegister()
{
	Super::OnRegister();
}

void ULiteGPUSceneComponent::OnUnregister()
{
	Super::OnUnregister();
}

FPrimitiveSceneProxy* ULiteGPUSceneComponent::CreateSceneProxy()
{
	return new FLiteGPUSceneProxy(this, GetWorld()->FeatureLevel);
}