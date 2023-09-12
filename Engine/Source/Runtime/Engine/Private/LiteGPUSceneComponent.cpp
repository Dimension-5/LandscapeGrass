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

TArray<int64> ULiteGPUSceneComponent::AddInstancesWS(const TArray<FTransform>& InstanceTransforms)
{
	static auto CVarX = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.TileDistanceX"));
	static auto CVarY = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.TileDistanceY"));
	const float TileDistanceX = CVarX ? CVarX->GetFloat() : 1000.0f;
	const float TileDistanceY = CVarY ? CVarY->GetFloat() : 1000.0f;
	TArray<int64> InstanceIDs;
	InstanceIDs.Reserve(InstanceTransforms.Num());
	// TODO: parallel for ?
	for (const auto& Transform : InstanceTransforms)
	{
		auto& NewInstance = Instances.AddZeroed_GetRef();
		const auto TileX = Transform.GetLocation().X / TileDistanceX;
		const auto TileY = Transform.GetLocation().Y / TileDistanceY;
		NewInstance.IDWithinComponent = NextInstanceID++;
		NewInstance.TilePosition = FInt32Vector2(TileX, TileY);
		NewInstance.XOffset = Transform.GetLocation().X - TileX;
		NewInstance.YOffset = Transform.GetLocation().Y - TileY;
		NewInstance.ZOffset = Transform.GetLocation().Z;
		NewInstance.XRot = FFloat16(Transform.GetRotation().Rotator().Roll);
		NewInstance.YRot = FFloat16(Transform.GetRotation().Rotator().Pitch);
		NewInstance.ZRot = FFloat16(Transform.GetRotation().Rotator().Yaw);
		NewInstance.Scale = FFloat16(Transform.GetScale3D().X);
		check(IDToSlotMap.Find(NewInstance.IDWithinComponent) == nullptr);
		IDToSlotMap.Add(NewInstance.IDWithinComponent, Instances.Num() - 1);
		InstanceIDs.Add(NewInstance.IDWithinComponent);
	}
	UE_LOG(LogLiteGPUScene, Log, TEXT("Added %d Instances to LiteGPUScene."), InstanceIDs.Num());
	return InstanceIDs;
}

bool ULiteGPUSceneComponent::GetInstanceTransformWS(int64 InstanceIndex, FTransform& OutInstanceTransform) const
{
	if (InstanceIndex < 0 || InstanceIndex >= Instances.Num())
	{
		return false;
	}
	static auto CVarX = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.TileDistanceX"));
	static auto CVarY = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.TileDistanceY"));
	const float TileDistanceX = CVarX ? CVarX->GetFloat() : 1000.0f;
	const float TileDistanceY = CVarY ? CVarY->GetFloat() : 1000.0f;
	const auto Slot = IDToSlotMap.FindRef(InstanceIndex);
	const auto& Instance = Instances[Slot];
	const auto TileX = Instance.TilePosition.X * TileDistanceX;
	const auto TileY = Instance.TilePosition.Y * TileDistanceY;
	OutInstanceTransform.SetLocation(FVector(TileX + Instance.XOffset, TileY + Instance.YOffset, Instance.ZOffset));
	OutInstanceTransform.SetRotation(FQuat::MakeFromEuler(FVector(Instance.XRot, Instance.YRot, Instance.ZRot)));
	OutInstanceTransform.SetScale3D(FVector(Instance.Scale, Instance.Scale, Instance.Scale));
	return true;
}

bool ULiteGPUSceneComponent::RemoveInstances(const TArray<int64>& InstancesToRemove)
{
	for (auto ID : InstancesToRemove)
	{
		const auto Slot = IDToSlotMap.FindRef(ID);
		if (Slot != INDEX_NONE)
		{
			Instances[Slot] = Instances.Last();
			IDToSlotMap[Instances[Slot].IDWithinComponent] = Slot;
			IDToSlotMap.Remove(ID);
			RemovedInstances.Add(Instances[Slot]);
		}
	}
	Instances.Shrink();
	return false;
}

bool ULiteGPUSceneComponent::ClearInstances()
{
	RemovedInstances = MoveTemp(Instances);
	Instances.SetNum(0, true);
	IDToSlotMap.Empty();
	IDToSlotMap.Shrink();
	return false;
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