#include "Components/LiteGPUSceneComponent.h"
#include "LiteGPUSceneSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogLiteGPUScene, Log, All);

void ULiteGPUSceneComponent::ManagedTick(float DeltaTime)
{
	ENQUEUE_RENDER_COMMAND(UpdateLiteGPUSceneOps)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			/*if*/ while (!PendingOps.IsEmpty())
			{
				TSharedPtr<Op> Ops = nullptr;
				{
					FScopeLock LCK(&OpsMutex);
					PendingOps.Dequeue(Ops);
				}
				if (Ops != nullptr)
				{
					if (Ops->OP == OP_ADD)
					{
						Handler->OnAdd(this, Ops->Instances);
					}
					else if (Ops->OP == OP_REMOVE)
					{
						Handler->OnRemove(this, Ops->Instances);
					}
				}
			}
		});
}

TArray<int64> ULiteGPUSceneComponent::AddInstancesWS(const TArray<FTransform>& InstanceTransforms)
{
	TArray<int64> InstanceIDs;
	TArray64<FLiteGPUSceneInstance> NewInstances;
	InstanceIDs.Reserve(InstanceIDs.Num() + InstanceTransforms.Num());
	NewInstances.Reserve(NewInstances.Num() + InstanceTransforms.Num());
	// TODO: parallel for ?
	for (const auto& Transform : InstanceTransforms)
	{
		auto& NewInstance = PersistantInstances.AddZeroed_GetRef();
		NewInstance.IDWithinComponent = NextInstanceID++;
		NewInstance.Transform = UE::Math::TTransform<float>(Transform);
		check(IDToSlotMap.Find(NewInstance.IDWithinComponent) == nullptr);
		IDToSlotMap.Add(NewInstance.IDWithinComponent, PersistantInstances.Num() - 1);
		InstanceIDs.Add(NewInstance.IDWithinComponent);
		NewInstances.Add(NewInstance);
	}
	auto NewOp = MakeShared<Op>(NewInstances, OP_ADD);
	{
		FScopeLock LCK(&OpsMutex);
		PendingOps.Enqueue(NewOp);
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
	const auto Slot = IDToSlotMap.FindRef(InstanceIndex);
	const auto& Instance = PersistantInstances[Slot];
	const auto SectorX = Instance.Transform;
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
			PersistantInstances.SetNum(PersistantInstances.Num() - 1, true);
			IDToSlotMap[PersistantInstances[Slot].IDWithinComponent] = Slot;
			IDToSlotMap.Remove(ID);
		}
	}
	auto NewOp = MakeShared<Op>(RemovedInstances, OP_REMOVE);
	{
		FScopeLock LCK(&OpsMutex);
		PendingOps.Enqueue(NewOp);
	}
	PersistantInstances.Shrink();
	return false;
}

bool ULiteGPUSceneComponent::ClearInstances()
{
	auto NewOp = MakeShared<Op>(PersistantInstances, OP_REMOVE);
	{
		FScopeLock LCK(&OpsMutex);
		PendingOps.Enqueue(NewOp);
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

// TODO: REMOVE THIS
FPrimitiveSceneProxy* ULiteGPUSceneComponent::CreateSceneProxy()
{
	return nullptr;
}