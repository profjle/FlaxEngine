// Copyright (c) 2012-2023 Wojciech Figat. All rights reserved.

#pragma once

#if PLATFORM_WINDOWS || PLATFORM_UWP || PLATFORM_XBOX_ONE || PLATFORM_XBOX_SCARLETT
#include "Win32/Win32Network.h"
#elif PLATFORM_LINUX || PLATFORM_ANDROID || PLATFORM_MAC
#include "Unix/UnixNetwork.h"
#elif PLATFORM_PS4
#include "Platforms/PS4/Engine/Platform/PS4Network.h"
#elif PLATFORM_PS5
#include "Platforms/PS5/Engine/Platform/PS5Network.h"
#elif PLATFORM_SWITCH
#include "Platforms/Switch/Engine/Platform/SwitchNetwork.h"
#elif PLATFORM_MAC
#include "Mac/MacNetwork.h"
#else
#error Missing Network implementation!
#endif

#include "Types.h"
