#include "Components/LiteGPUSceneComponent.h"
#include "LiteGPUSceneSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogLiteGPUScene, Log, All);

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