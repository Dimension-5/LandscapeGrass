// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/RenderGridPropsSourceFactoryLocal.h"
#include "RenderGrid/RenderGridPropsSource.h"


URenderGridPropsSourceBase* UE::RenderGrid::Private::FRenderGridPropsSourceFactoryLocal::CreateInstance(UObject* PropsSourceOrigin)
{
	URenderGridPropsSourceLocal* PropsSource = NewObject<URenderGridPropsSourceLocal>();
	PropsSource->SetSourceOrigin(PropsSourceOrigin);
	return PropsSource;
}
