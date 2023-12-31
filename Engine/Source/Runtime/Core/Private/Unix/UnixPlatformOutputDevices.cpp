// Copyright Epic Games, Inc. All Rights Reserved.

#include "Unix/UnixPlatformOutputDevices.h"

#include "CoreGlobals.h"
#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "Misc/Build.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDeviceConsole.h"
#include "Misc/OutputDeviceDebug.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Unix/UnixErrorOutputDevice.h"

class FOutputDeviceError;

void FUnixOutputDevices::SetupOutputDevices()
{
	check(GLog);

	ResetCachedAbsoluteFilename();

	// add file log
	GLog->AddOutputDevice(FPlatformOutputDevices::GetLog());

	// @todo: set to false for minor utils?
	if (GLogConsole != nullptr)
	{
		bool bLogToConsole = !NO_LOGGING && !FParse::Param(FCommandLine::Get(), TEXT("NOCONSOLE"));
		if (bLogToConsole)
		{
			GLog->AddOutputDevice(GLogConsole);
		}
	}

#if USE_DEBUG_LOGGING
	// If the platform has a separate debug output channel (e.g. OutputDebugString) then add an output device
	// unless logging is turned off
		if (FPlatformMisc::HasSeparateChannelForDebugOutput() && !FParse::Param(FCommandLine::Get(), TEXT("NODEBUGOUTPUT")))
		{
			GLog->AddOutputDevice(new FOutputDeviceDebug());
		}
#endif // USE_DEBUG_LOGGING

	GLog->AddOutputDevice(FPlatformOutputDevices::GetEventLog());
}

FString FUnixOutputDevices::GetAbsoluteLogFilename()
{
	// FIXME: this function should not exist once FGenericPlatformOutputDevices::GetAbsoluteLogFilename() returns absolute filename (see UE-25650)
	return FPaths::ConvertRelativePathToFull(FGenericPlatformOutputDevices::GetAbsoluteLogFilename());
}

class FOutputDevice* FUnixOutputDevices::GetEventLog()
{
	return NULL; // @TODO No event logging
}

FOutputDeviceError* FUnixOutputDevices::GetError()
{
	static FUnixErrorOutputDevice Singleton;
	return &Singleton;
}

