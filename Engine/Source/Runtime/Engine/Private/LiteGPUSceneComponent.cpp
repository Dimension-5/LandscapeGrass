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
	if (!PendingOps.IsEmpty())
	{
		auto Ops = PendingOps.Pop(true);
		if (Ops.Value == OP_ADD)
		{
			Handler->OnAdd(this, Ops.Key);
		}
		else if (Ops.Value == OP_REMOVE)
		{
			Handler->OnRemove(this, Ops.Key);
		}
	}
}

TArray<int64> ULiteGPUSceneComponent::AddInstancesWS(const TArray<FTransform>& InstanceTransforms)
{
	static auto CVarX = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.TileDistanceX"));
	static auto CVarY = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.TileDistanceY"));
	const float TileDistanceX = CVarX ? CVarX->GetFloat() : 1000.0f;
	const float TileDistanceY = CVarY ? CVarY->GetFloat() : 1000.0f;
	TArray<int64> InstanceIDs;
	TArray64<FLiteGPUSceneInstance> NewInstances;
	InstanceIDs.Reserve(InstanceIDs.Num() + InstanceTransforms.Num());
	NewInstances.Reserve(NewInstances.Num() + InstanceTransforms.Num());
	// TODO: parallel for ?
	for (const auto& Transform : InstanceTransforms)
	{
		auto& NewInstance = PersistantInstances.AddZeroed_GetRef();
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
		IDToSlotMap.Add(NewInstance.IDWithinComponent, PersistantInstances.Num() - 1);
		InstanceIDs.Add(NewInstance.IDWithinComponent);
		NewInstances.Add(NewInstance);
	}
	if (Handler)
	{
		Handler->OnAdd(this, NewInstances);
	}
	else
	{
		PendingOps.Add({ NewInstances, OP_ADD });
	}
	UE_LOG(LogLiteGPUScene, Log, TEXT("Added %d Instances to LiteGPUScene."), InstanceIDs.Num());
	return InstanceIDs;
}

bool ULiteGPUSceneComponent::GetInstanceTransformWS(int64 InstanceIndex, FTransform& OutInstanceTransform) const
{
	if (InstanceIndex < 0 || InstanceIndex >= PersistantInstances.Num())
	{
		return false;
	}
	static auto CVarX = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.TileDistanceX"));
	static auto CVarY = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LiteGPUScene.TileDistanceY"));
	const float TileDistanceX = CVarX ? CVarX->GetFloat() : 1000.0f;
	const float TileDistanceY = CVarY ? CVarY->GetFloat() : 1000.0f;
	const auto Slot = IDToSlotMap.FindRef(InstanceIndex);
	const auto& Instance = PersistantInstances[Slot];
	const auto TileX = Instance.TilePosition.X * TileDistanceX;
	const auto TileY = Instance.TilePosition.Y * TileDistanceY;
	OutInstanceTransform.SetLocation(FVector(TileX + Instance.XOffset, TileY + Instance.YOffset, Instance.ZOffset));
	OutInstanceTransform.SetRotation(FQuat::MakeFromEuler(FVector(Instance.XRot, Instance.YRot, Instance.ZRot)));
	OutInstanceTransform.SetScale3D(FVector(Instance.Scale, Instance.Scale, Instance.Scale));
	return true;
}

bool ULiteGPUSceneComponent::RemoveInstances(const TArray<int64>& InstancesToRemove)
{
	TArray64<FLiteGPUSceneInstance> RemovedInstances;
	RemovedInstances.Reserve(InstancesToRemove.Num());
	for (auto ID : InstancesToRemove)
	{
		const auto Slot = IDToSlotMap.FindRef(ID);
		if (Slot != INDEX_NONE)
		{
			RemovedInstances.Add(PersistantInstances[Slot]);
			PersistantInstances[Slot] = PersistantInstances.Last();
			IDToSlotMap[PersistantInstances[Slot].IDWithinComponent] = Slot;
			IDToSlotMap.Remove(ID);
		}
	}
	if (Handler)
	{
		Handler->OnRemove(this, RemovedInstances);
	}
	else
	{
		PendingOps.Add({ RemovedInstances, OP_REMOVE });
	}
	PersistantInstances.Shrink();
	return false;
}

bool ULiteGPUSceneComponent::ClearInstances()
{
	if (Handler)
	{
		Handler->OnRemove(this, PersistantInstances);
	}
	else
	{
		PendingOps.Add({ PersistantInstances, OP_REMOVE });
	}
	PersistantInstances.SetNum(0, true);
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