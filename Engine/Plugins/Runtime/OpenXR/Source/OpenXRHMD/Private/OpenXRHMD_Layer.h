// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IStereoLayers.h"
#include "OpenXRCore.h"
#include "XRSwapChain.h"

union FXrCompositionLayerUnion;

struct FOpenXRLayer
{
	struct FPerEyeTextureData
	{
		FXRSwapChainPtr			Swapchain = nullptr;
		FVector2D				SwapchainSize{};
		bool					bUpdateTexture = false;

		void					SetSwapchain(FXRSwapChainPtr InSwapchain, const FVector2D& InSwapchainSize)
		{
			Swapchain = InSwapchain;
			SwapchainSize = InSwapchainSize;
			bUpdateTexture = true;
		}
	};

	IStereoLayers::FLayerDesc	Desc;

	/** Texture tracking data for the right eye.*/
	FPerEyeTextureData			RightEye;

	/** Texture tracking data for the left eye, may not be present.*/
	FPerEyeTextureData			LeftEye;

	FOpenXRLayer(const IStereoLayers::FLayerDesc& InLayerDesc)
		: Desc(InLayerDesc)
	{ }

	void SetLayerId(uint32 InId) { Desc.SetLayerId(InId); }
	uint32 GetLayerId() const { return Desc.GetLayerId(); }

	bool NeedReallocateRightTexture();
	bool NeedReallocateLeftTexture();

	FIntRect GetRightViewportSize() const;
	FVector2D GetRightQuadSize() const;

	FIntRect GetLeftViewportSize() const;
	FVector2D GetLeftQuadSize() const;

	TArray<FXrCompositionLayerUnion> CreateOpenXRLayer(FTransform InvTrackingToWorld, float WorldToMeters, XrSpace Space) const;
	
private:
	void CreateOpenXRQuadLayer(bool bIsStereo, bool bNoAlpha, FTransform PositionTransform, float WorldToMeters, XrSpace Space, TArray<FXrCompositionLayerUnion>& Headers) const;
	void CreateOpenXRCylinderLayer(bool bIsStereo, bool bNoAlpha, FTransform PositionTransform, float WorldToMeters, XrSpace Space, TArray<FXrCompositionLayerUnion>& Headers) const;
	void CreateOpenXREquirectLayer(bool bIsStereo, bool bNoAlpha, FTransform PositionTransform, float WorldToMeters, XrSpace Space, TArray<FXrCompositionLayerUnion>& Headers) const;

};

bool GetLayerDescMember(const FOpenXRLayer& Layer, IStereoLayers::FLayerDesc& OutLayerDesc);
void SetLayerDescMember(FOpenXRLayer& OutLayer, const IStereoLayers::FLayerDesc& InLayerDesc);
void MarkLayerTextureForUpdate(FOpenXRLayer& Layer);
