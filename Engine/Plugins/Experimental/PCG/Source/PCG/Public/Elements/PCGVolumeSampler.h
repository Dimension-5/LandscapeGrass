// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Box.h"
#include "PCGSettings.h"

#include "PCGPin.h"
#include "PCGVolumeSampler.generated.h"

class UPCGPointData;
class UPCGSpatialData;

namespace PCGVolumeSampler
{
	struct FVolumeSamplerSettings
	{
		FVector VoxelSize;
		float PointSteepness = 0.0f;
	};

	UPCGPointData* SampleVolume(FPCGContext* InContext, const UPCGSpatialData* InVolume, const FVolumeSamplerSettings& InSamplerSettings);
	UPCGPointData* SampleVolume(FPCGContext* InContext, const UPCGSpatialData* InVolume, const UPCGSpatialData* InBoundingShape, const FBox& InBounds, const FVolumeSamplerSettings& InSamplerSettings);

	void SampleVolume(FPCGContext* InContext, const UPCGSpatialData* InVolume, const UPCGSpatialData* InBoundingShape, const FBox& InBounds, const FVolumeSamplerSettings& InSamplerSettings, UPCGPointData* InOutputData);
}

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGVolumeSamplerSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data", meta = (PCG_Overridable))
	FVector VoxelSize = FVector(100.0, 100.0, 100.0);

	/** If no Bounding Shape input is provided, the actor bounds are used to limit the sample generation domain.
	* This option allows ignoring the actor bounds and generating over the entire volume. Use with caution as this
	* may generate a lot of points.
	*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bUnbounded = false;

	/** Each PCG point represents a discretized, volumetric region of world space. The points' Steepness value [0.0 to
	 * 1.0] establishes how "hard" or "soft" that volume will be represented. From 0, it will ramp up linearly
	 * increasing its influence over the density from the point's center to up to two times the bounds. At 1, it will
	 * represent a binary box function with the size of the point's bounds.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Points", meta=(ClampMin="0", ClampMax="1", PCG_Overridable))
	float PointSteepness = 0.5f;
	
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("VolumeSampler")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGVolumeSamplerSettings", "NodeTitle", "Volume Sampler"); }
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Sampler; }
#endif
	
#if WITH_EDITOR
	virtual void ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override { return Super::DefaultPointOutputPinProperties(); }
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface
};

class FPCGVolumeSamplerElement : public FSimplePCGElement
{
public:
	// Might be sampling external data like brush, worth computing a full CRC in case we can halt change propagation/re-executions
	virtual bool ShouldComputeFullOutputDataCrc(FPCGContext* Context) const override { return true; }

protected:
	bool ExecuteInternal(FPCGContext* Context) const override;
};
