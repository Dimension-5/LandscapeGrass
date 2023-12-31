﻿// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"

/** Pin factory for the render graph. */
class FMovieGraphPanelPinFactory : public FGraphPanelPinFactory
{
public:
	//~ FGraphPanelPinFactory interface
	virtual TSharedPtr<class SGraphPin> CreatePin(class UEdGraphPin* InPin) const override;
	//~ End FGraphPanelPinFactory interface
};