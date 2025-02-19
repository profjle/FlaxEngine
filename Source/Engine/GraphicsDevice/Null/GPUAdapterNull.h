// Copyright (c) 2012-2023 Wojciech Figat. All rights reserved.

#pragma once

#if GRAPHICS_API_NULL

#include "Engine/Graphics/GPUAdapter.h"

/// <summary>
/// Graphics Device adapter implementation for Null backend.
/// </summary>
class GPUAdapterNull : public GPUAdapter
{
public:

    // [GPUAdapter]
    bool IsValid() const override
    {
        return true;
    }

    uint32 GetVendorId() const override
    {
        return 0;
    }

    String GetDescription() const override
    {
        return TEXT("Null");
    }
};

#endif
