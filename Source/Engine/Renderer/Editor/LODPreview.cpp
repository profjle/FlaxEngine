// Copyright (c) 2012-2023 Wojciech Figat. All rights reserved.

#if USE_EDITOR

#include "LODPreview.h"
#include "Engine/Core/Types/Variant.h"
#include "Engine/Content/Content.h"
#include "Engine/Content/Assets/Model.h"
#include "Engine/Graphics/GPUDevice.h"
#include "Engine/Graphics/RenderTask.h"
#include "Engine/Renderer/DrawCall.h"
#include "Engine/Renderer/RenderList.h"

LODPreviewMaterialShader::LODPreviewMaterialShader()
{
    _material = Content::LoadAsyncInternal<Material>(TEXT("Editor/DebugMaterials/SingleColor/Surface"));
}

const MaterialInfo& LODPreviewMaterialShader::GetInfo() const
{
    ASSERT_LOW_LAYER(_material);
    return _material->GetInfo();
}

GPUShader* LODPreviewMaterialShader::GetShader() const
{
    return _material->GetShader();
}

bool LODPreviewMaterialShader::IsReady() const
{
    return _material && _material->IsReady();
}

bool LODPreviewMaterialShader::CanUseInstancing(InstancingHandler& handler) const
{
    return _material->CanUseInstancing(handler);
}

DrawPass LODPreviewMaterialShader::GetDrawModes() const
{
    return _material->GetDrawModes();
}

void LODPreviewMaterialShader::Bind(BindParameters& params)
{
    // Find the LOD that produced this draw call
    int32 lodIndex = 0;
    auto& drawCall = *params.FirstDrawCall;
    for (auto& e : Content::GetAssetsRaw())
    {
        auto model = ScriptingObject::Cast<Model>(e.Value);
        if (!model)
            continue;
        bool found = false;
        for (const auto& lod : model->LODs)
        {
            for (const auto& mesh : lod.Meshes)
            {
                if (mesh.GetIndexBuffer() == drawCall.Geometry.IndexBuffer)
                {
                    lodIndex = mesh.GetLODIndex();
                    found = true;
                    break;
                }
            }
        }
        if (found)
            break;
    }

    // Bind
    const Color colors[MODEL_MAX_LODS] = {
        Color::White,
        Color::Red,
        Color::Orange,
        Color::Yellow,
        Color::Green,
        Color::Blue,
    };
    const Color color(colors[lodIndex]);
    _material->SetParameterValue(TEXT("Color"), Variant(color));
    _material->Bind(params);
}

#endif
