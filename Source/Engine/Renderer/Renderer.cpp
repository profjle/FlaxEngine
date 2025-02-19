// Copyright (c) 2012-2023 Wojciech Figat. All rights reserved.

#include "Renderer.h"
#include "Engine/Graphics/GPUContext.h"
#include "Engine/Graphics/RenderTargetPool.h"
#include "Engine/Graphics/RenderBuffers.h"
#include "Engine/Graphics/RenderTask.h"
#include "Engine/Engine/EngineService.h"
#include "GBufferPass.h"
#include "ForwardPass.h"
#include "ShadowsPass.h"
#include "LightPass.h"
#include "ReflectionsPass.h"
#include "ScreenSpaceReflectionsPass.h"
#include "AmbientOcclusionPass.h"
#include "DepthOfFieldPass.h"
#include "EyeAdaptationPass.h"
#include "PostProcessingPass.h"
#include "ColorGradingPass.h"
#include "MotionBlurPass.h"
#include "VolumetricFogPass.h"
#include "HistogramPass.h"
#include "AtmospherePreCompute.h"
#include "GlobalSignDistanceFieldPass.h"
#include "GI/GlobalSurfaceAtlasPass.h"
#include "GI/DynamicDiffuseGlobalIllumination.h"
#include "Utils/MultiScaler.h"
#include "Utils/BitonicSort.h"
#include "AntiAliasing/FXAA.h"
#include "AntiAliasing/TAA.h"
#include "AntiAliasing/SMAA.h"
#include "Engine/Level/Actor.h"
#include "Engine/Level/Level.h"
#include "Engine/Core/Config/GraphicsSettings.h"
#if USE_EDITOR
#include "Editor/Editor.h"
#include "Editor/QuadOverdrawPass.h"
#endif

#if USE_EDITOR
// Additional options used in editor for lightmaps baking
bool IsRunningRadiancePass = false;
bool IsBakingLightmaps = false;
bool EnableLightmapsUsage = true;
#endif

Array<RendererPassBase*> PassList(64);

class RendererService : public EngineService
{
public:
    RendererService()
        : EngineService(TEXT("Renderer"), 20)
    {
    }

    bool Init() override;
    void Dispose() override;
};

RendererService RendererServiceInstance;

void RenderInner(SceneRenderTask* task, RenderContext& renderContext);

bool RendererService::Init()
{
    // Register passes
    PassList.Add(GBufferPass::Instance());
    PassList.Add(ShadowsPass::Instance());
    PassList.Add(LightPass::Instance());
    PassList.Add(ForwardPass::Instance());
    PassList.Add(ReflectionsPass::Instance());
    PassList.Add(ScreenSpaceReflectionsPass::Instance());
    PassList.Add(AmbientOcclusionPass::Instance());
    PassList.Add(DepthOfFieldPass::Instance());
    PassList.Add(ColorGradingPass::Instance());
    PassList.Add(VolumetricFogPass::Instance());
    PassList.Add(EyeAdaptationPass::Instance());
    PassList.Add(PostProcessingPass::Instance());
    PassList.Add(MotionBlurPass::Instance());
    PassList.Add(MultiScaler::Instance());
    PassList.Add(BitonicSort::Instance());
    PassList.Add(FXAA::Instance());
    PassList.Add(TAA::Instance());
    PassList.Add(SMAA::Instance());
    PassList.Add(HistogramPass::Instance());
    PassList.Add(GlobalSignDistanceFieldPass::Instance());
    PassList.Add(GlobalSurfaceAtlasPass::Instance());
    PassList.Add(DynamicDiffuseGlobalIlluminationPass::Instance());
#if USE_EDITOR
    PassList.Add(QuadOverdrawPass::Instance());
#endif

    // Skip when using Null renderer
    if (GPUDevice::Instance->GetRendererType() == RendererType::Null)
    {
        return false;
    }

    // Init child services
    for (int32 i = 0; i < PassList.Count(); i++)
    {
        if (PassList[i]->Init())
        {
            LOG(Fatal, "Cannot init {0}. Please see a log file for more info.", PassList[i]->ToString());
            return true;
        }
    }

    return false;
}

void RendererService::Dispose()
{
    // Dispose child services
    for (int32 i = 0; i < PassList.Count(); i++)
    {
        PassList[i]->Dispose();
    }
}

void RenderAntiAliasingPass(RenderContext& renderContext, GPUTexture* input, GPUTextureView* output)
{
    auto context = GPUDevice::Instance->GetMainContext();
    context->SetViewportAndScissors(renderContext.View.ScreenSize.X, renderContext.View.ScreenSize.Y);

    const auto aaMode = renderContext.List->Settings.AntiAliasing.Mode;
    if (aaMode == AntialiasingMode::FastApproximateAntialiasing)
    {
        FXAA::Instance()->Render(renderContext, input, output);
    }
    else if (aaMode == AntialiasingMode::SubpixelMorphologicalAntialiasing)
    {
        SMAA::Instance()->Render(renderContext, input, output);
    }
    else
    {
        PROFILE_GPU("Copy frame");
        context->SetRenderTarget(output);
        context->Draw(input);
    }
}

bool Renderer::IsReady()
{
    // Warm up first (state getters initialize content loading so do it for all first)
    AtmosphereCache atmosphereCache;
    AtmospherePreCompute::GetCache(&atmosphereCache);
    for (int32 i = 0; i < PassList.Count(); i++)
        PassList[i]->IsReady();

    // Now check state
    if (!AtmospherePreCompute::GetCache(&atmosphereCache))
        return false;
    for (int32 i = 0; i < PassList.Count(); i++)
    {
        if (!PassList[i]->IsReady())
            return false;
    }
    return true;
}

void Renderer::Render(SceneRenderTask* task)
{
    PROFILE_GPU_CPU_NAMED("Render Frame");

    auto context = GPUDevice::Instance->GetMainContext();

    context->ClearState();
    context->FlushState();

    const Viewport viewport = task->GetViewport();
    context->SetViewportAndScissors(viewport);

    // Prepare
    RenderContext renderContext(task);
    renderContext.List = RenderList::GetFromPool();

#if USE_EDITOR
    // Turn on low quality rendering during baking lightmaps (leave more GPU power for baking)
    const auto flags = renderContext.View.Flags;
    if (!renderContext.View.IsOfflinePass && IsBakingLightmaps)
    {
        renderContext.View.Flags &= ~(ViewFlags::AO
            | ViewFlags::Shadows
            | ViewFlags::AntiAliasing
            | ViewFlags::CustomPostProcess
            | ViewFlags::Bloom
            | ViewFlags::ToneMapping
            | ViewFlags::EyeAdaptation
            | ViewFlags::CameraArtifacts
            | ViewFlags::Reflections
            | ViewFlags::SSR
            | ViewFlags::LensFlares
            | ViewFlags::MotionBlur
            | ViewFlags::Fog
            | ViewFlags::PhysicsDebug
            | ViewFlags::Decals
            | ViewFlags::GI
            | ViewFlags::DebugDraw
            | ViewFlags::ContactShadows
            | ViewFlags::DepthOfField);
    }
#endif

    // Perform the actual rendering
    task->OnPreRender(context, renderContext);
    RenderInner(task, renderContext);
    task->OnPostRender(context, renderContext);

#if USE_EDITOR
    // Restore flags
    renderContext.View.Flags = flags;
#endif

    // Copy back the view (modified during rendering with rendering state like TAA frame index and jitter)
    task->View = renderContext.View;

    // Cleanup
    RenderList::ReturnToPool(renderContext.List);
}

bool Renderer::NeedMotionVectors(RenderContext& renderContext)
{
    const int32 screenWidth = renderContext.Buffers->GetWidth();
    const int32 screenHeight = renderContext.Buffers->GetHeight();
    if (screenWidth < 16 || screenHeight < 16 || renderContext.Task->IsCameraCut)
        return false;
    MotionBlurSettings& motionBlurSettings = renderContext.List->Settings.MotionBlur;
    return ((renderContext.View.Flags & ViewFlags::MotionBlur) != 0 && motionBlurSettings.Enabled && motionBlurSettings.Scale > ZeroTolerance) ||
            renderContext.View.Mode == ViewMode::MotionVectors ||
            ScreenSpaceReflectionsPass::NeedMotionVectors(renderContext) ||
            TAA::NeedMotionVectors(renderContext);
}

void Renderer::DrawSceneDepth(GPUContext* context, SceneRenderTask* task, GPUTexture* output, const Array<Actor*>& customActors)
{
    CHECK(context && task && output && output->IsDepthStencil());

    // Prepare
    RenderContext renderContext(task);
    renderContext.List = RenderList::GetFromPool();
    renderContext.View.Pass = DrawPass::Depth;
    renderContext.View.Prepare(renderContext);

    // Call drawing (will collect draw calls)
    if (customActors.HasItems())
    {
        // Draw custom actors
        for (auto actor : customActors)
        {
            if (actor && actor->GetIsActive())
                actor->Draw(renderContext);
        }
    }
    else
    {
        // Draw scene actors
        Level::DrawActors(renderContext);
    }

    // Sort draw calls
    renderContext.List->SortDrawCalls(renderContext, false, DrawCallsListType::Depth);

    // Execute draw calls
    const float width = (float)output->Width();
    const float height = (float)output->Height();
    context->SetViewport(width, height);
    context->SetRenderTarget(output->View(), static_cast<GPUTextureView*>(nullptr));
    renderContext.List->ExecuteDrawCalls(renderContext, DrawCallsListType::Depth);

    // Cleanup
    RenderList::ReturnToPool(renderContext.List);
}

void Renderer::DrawPostFxMaterial(GPUContext* context, const RenderContext& renderContext, MaterialBase* material, GPUTexture* output, GPUTextureView* input)
{
    CHECK(material && material->IsPostFx());
    CHECK(context && output);

    context->ResetSR();
    context->SetViewport((float)output->Width(), (float)output->Height());
    context->SetRenderTarget(output->View());
    context->FlushState();

    MaterialBase::BindParameters bindParams(context, renderContext);
    bindParams.Input = input;
    material->Bind(bindParams);

    context->DrawFullscreenTriangle();
    context->ResetRenderTarget();
}

void RenderInner(SceneRenderTask* task, RenderContext& renderContext)
{
    auto context = GPUDevice::Instance->GetMainContext();
    auto* graphicsSettings = GraphicsSettings::Get();
    auto& view = renderContext.View;
    ASSERT(renderContext.Buffers && renderContext.Buffers->GetWidth() > 0);

    // Perform postFx volumes blending and query before rendering
    task->CollectPostFxVolumes(renderContext);
    renderContext.List->BlendSettings();
    auto aaMode = (renderContext.View.Flags & ViewFlags::AntiAliasing) != 0 ? renderContext.List->Settings.AntiAliasing.Mode : AntialiasingMode::None;
    if (aaMode == AntialiasingMode::TemporalAntialiasing && view.IsOrthographicProjection())
        aaMode = AntialiasingMode::None; // TODO: support TAA in ortho projection (see RenderView::Prepare to jitter projection matrix better)
    renderContext.List->Settings.AntiAliasing.Mode = aaMode;

    // Prepare
    renderContext.View.Prepare(renderContext);
    if (renderContext.View.Origin != renderContext.View.PrevOrigin)
        renderContext.Task->CameraCut(); // Cut any temporal effects on rendering origin change
    renderContext.Buffers->Prepare();
    for (auto& postFx : task->CustomPostFx)
    {
        if (postFx.Target)
            renderContext.List->PostFx.Add(&postFx);
    }

    // Collect renderable objects and construct draw call list
    view.Pass = DrawPass::GBuffer | DrawPass::Forward | DrawPass::Distortion;
    if (Renderer::NeedMotionVectors(renderContext))
        view.Pass |= DrawPass::MotionVectors;
    task->OnCollectDrawCalls(renderContext);

    // Sort draw calls
    renderContext.List->SortDrawCalls(renderContext, false, DrawCallsListType::GBuffer);
    renderContext.List->SortDrawCalls(renderContext, false, DrawCallsListType::GBufferNoDecals);
    renderContext.List->SortDrawCalls(renderContext, true, DrawCallsListType::Forward);
    renderContext.List->SortDrawCalls(renderContext, false, DrawCallsListType::Distortion);

    // Get the light accumulation buffer
    auto outputFormat = renderContext.Buffers->GetOutputFormat();
    auto tempDesc = GPUTextureDescription::New2D(renderContext.Buffers->GetWidth(), renderContext.Buffers->GetHeight(), outputFormat);
    auto lightBuffer = RenderTargetPool::Get(tempDesc);

#if USE_EDITOR
    if (renderContext.View.Mode == ViewMode::QuadOverdraw)
    {
        QuadOverdrawPass::Instance()->Render(renderContext, context, lightBuffer->View());
        context->ResetRenderTarget();
        context->SetRenderTarget(task->GetOutputView());
        context->SetViewportAndScissors(task->GetOutputViewport());
        context->Draw(lightBuffer);
        RenderTargetPool::Release(lightBuffer);
        return;
    }
#endif

    // Global SDF rendering (can be used by materials later on)
    if (graphicsSettings->EnableGlobalSDF && view.Flags & ViewFlags::GlobalSDF)
    {
        GlobalSignDistanceFieldPass::BindingData bindingData;
        GlobalSignDistanceFieldPass::Instance()->Render(renderContext, context, bindingData);
    }

    // Fill GBuffer
    GBufferPass::Instance()->Fill(renderContext, lightBuffer->View());

    // Debug drawing
    if (renderContext.View.Mode == ViewMode::GlobalSDF)
        GlobalSignDistanceFieldPass::Instance()->RenderDebug(renderContext, context, lightBuffer);
    else if (renderContext.View.Mode == ViewMode::GlobalSurfaceAtlas)
        GlobalSurfaceAtlasPass::Instance()->RenderDebug(renderContext, context, lightBuffer);
    if (renderContext.View.Mode == ViewMode::Emissive ||
        renderContext.View.Mode == ViewMode::LightmapUVsDensity ||
        renderContext.View.Mode == ViewMode::GlobalSurfaceAtlas ||
        renderContext.View.Mode == ViewMode::GlobalSDF)
    {
        context->ResetRenderTarget();
        context->SetRenderTarget(task->GetOutputView());
        context->SetViewportAndScissors(task->GetOutputViewport());
        context->Draw(lightBuffer->View());
        RenderTargetPool::Release(lightBuffer);
        return;
    }
#if USE_EDITOR
    if (renderContext.View.Mode == ViewMode::MaterialComplexity)
    {
        GBufferPass::Instance()->DrawMaterialComplexity(renderContext, context, lightBuffer->View());
        RenderTargetPool::Release(lightBuffer);
        return;
    }
#endif

    // Render motion vectors
    MotionBlurPass::Instance()->RenderMotionVectors(renderContext);

    // Render ambient occlusion
    AmbientOcclusionPass::Instance()->Render(renderContext);

    // Check if use custom view mode
    if (GBufferPass::IsDebugView(renderContext.View.Mode))
    {
        context->ResetRenderTarget();
        context->SetRenderTarget(task->GetOutputView());
        context->SetViewportAndScissors(task->GetOutputViewport());
        GBufferPass::Instance()->RenderDebug(renderContext);
        RenderTargetPool::Release(lightBuffer);
        return;
    }

    // Render lighting
    LightPass::Instance()->RenderLight(renderContext, *lightBuffer);
    if (renderContext.View.Flags & ViewFlags::GI)
    {
        switch (renderContext.List->Settings.GlobalIllumination.Mode)
        {
        case GlobalIlluminationMode::DDGI:
            DynamicDiffuseGlobalIlluminationPass::Instance()->Render(renderContext, context, *lightBuffer);
            break;
        }
    }
    if (renderContext.View.Mode == ViewMode::LightBuffer)
    {
        auto colorGradingLUT = ColorGradingPass::Instance()->RenderLUT(renderContext);
        GPUTexture* tempBuffer = renderContext.Buffers->RT2_FloatRGB;
        EyeAdaptationPass::Instance()->Render(renderContext, lightBuffer);
        PostProcessingPass::Instance()->Render(renderContext, lightBuffer, tempBuffer, colorGradingLUT);
        RenderTargetPool::Release(colorGradingLUT);
        RenderTargetPool::Release(lightBuffer);
        context->ResetRenderTarget();
        context->SetRenderTarget(task->GetOutputView());
        context->SetViewportAndScissors(task->GetOutputViewport());
        context->Draw(tempBuffer);
        return;
    }

    // Material and Custom PostFx
    renderContext.List->RunPostFxPass(context, renderContext, MaterialPostFxLocation::BeforeReflectionsPass, PostProcessEffectLocation::BeforeReflectionsPass, lightBuffer);

    // Render reflections
    ReflectionsPass::Instance()->Render(renderContext, *lightBuffer);
    if (renderContext.View.Mode == ViewMode::Reflections)
    {
        context->ResetRenderTarget();
        context->SetRenderTarget(task->GetOutputView());
        context->SetViewportAndScissors(task->GetOutputViewport());
        context->Draw(lightBuffer);
        RenderTargetPool::Release(lightBuffer);
        return;
    }

    // Material and Custom PostFx
    renderContext.List->RunPostFxPass(context, renderContext, MaterialPostFxLocation::BeforeForwardPass, PostProcessEffectLocation::BeforeForwardPass, lightBuffer);

    // Render fog
    context->ResetSR();
    if (renderContext.List->AtmosphericFog)
    {
        PROFILE_GPU_CPU("Atmospheric Fog");
        renderContext.List->AtmosphericFog->DrawFog(context, renderContext, *lightBuffer);
        context->ResetSR();
    }
    if (renderContext.List->Fog)
    {
        VolumetricFogPass::Instance()->Render(renderContext);

        PROFILE_GPU_CPU("Fog");
        renderContext.List->Fog->DrawFog(context, renderContext, *lightBuffer);
        context->ResetSR();
    }

    // Run forward pass
    GPUTexture* frameBuffer = renderContext.Buffers->RT1_FloatRGB;
    GPUTexture* tempBuffer = renderContext.Buffers->RT2_FloatRGB;
    ForwardPass::Instance()->Render(renderContext, lightBuffer, frameBuffer);

    // Cleanup
    context->ResetRenderTarget();
    context->ResetSR();
    context->FlushState();
    RenderTargetPool::Release(lightBuffer);

    // Check if skip post-processing
    if (renderContext.View.Mode == ViewMode::NoPostFx || renderContext.View.Mode == ViewMode::Wireframe)
    {
        context->SetRenderTarget(task->GetOutputView());
        context->SetViewportAndScissors(task->GetOutputViewport());
        context->Draw(frameBuffer);
        return;
    }

    // Material and Custom PostFx
    renderContext.List->RunMaterialPostFxPass(context, renderContext, MaterialPostFxLocation::BeforePostProcessingPass, frameBuffer, tempBuffer);
    renderContext.List->RunCustomPostFxPass(context, renderContext, PostProcessEffectLocation::BeforePostProcessingPass, frameBuffer, tempBuffer);

    // Temporal Anti-Aliasing (goes before post processing)
    if (aaMode == AntialiasingMode::TemporalAntialiasing)
    {
        TAA::Instance()->Render(renderContext, frameBuffer, tempBuffer->View());
        Swap(frameBuffer, tempBuffer);
    }

    // Depth of Field
    auto dofTemporary = DepthOfFieldPass::Instance()->Render(renderContext, frameBuffer);
    frameBuffer = dofTemporary ? dofTemporary : frameBuffer;

    // Motion Blur
    MotionBlurPass::Instance()->Render(renderContext, frameBuffer, tempBuffer);

    // Color Grading LUT generation
    auto colorGradingLUT = ColorGradingPass::Instance()->RenderLUT(renderContext);

    // Post processing
    EyeAdaptationPass::Instance()->Render(renderContext, frameBuffer);
    PostProcessingPass::Instance()->Render(renderContext, frameBuffer, tempBuffer, colorGradingLUT);
    RenderTargetPool::Release(colorGradingLUT);
    RenderTargetPool::Release(dofTemporary);
    Swap(frameBuffer, tempBuffer);

    // Cleanup
    context->ResetRenderTarget();
    context->ResetSR();
    context->FlushState();

    // Custom Post Processing
    renderContext.List->RunMaterialPostFxPass(context, renderContext, MaterialPostFxLocation::AfterPostProcessingPass, frameBuffer, tempBuffer);
    renderContext.List->RunCustomPostFxPass(context, renderContext, PostProcessEffectLocation::Default, frameBuffer, tempBuffer);
    renderContext.List->RunMaterialPostFxPass(context, renderContext, MaterialPostFxLocation::AfterCustomPostEffects, frameBuffer, tempBuffer);

    // Cleanup
    context->ResetRenderTarget();
    context->ResetSR();
    context->FlushState();

    // Debug motion vectors
    if (renderContext.View.Mode == ViewMode::MotionVectors)
    {
        context->ResetRenderTarget();
        context->SetRenderTarget(task->GetOutputView());
        context->SetViewportAndScissors(task->GetOutputViewport());
        MotionBlurPass::Instance()->RenderDebug(renderContext, frameBuffer->View());
        return;
    }

    // Anti Aliasing
    if (!renderContext.List->HasAnyPostFx(renderContext, PostProcessEffectLocation::AfterAntiAliasingPass, MaterialPostFxLocation::AfterAntiAliasingPass) && Math::IsOne(task->RenderingPercentage))
    {
        // AA -> Back Buffer
        RenderAntiAliasingPass(renderContext, frameBuffer, task->GetOutputView());
    }
    else
    {
        // AA -> PostFx
        RenderAntiAliasingPass(renderContext, frameBuffer, *tempBuffer);
        context->ResetRenderTarget();
        Swap(frameBuffer, tempBuffer);
        renderContext.List->RunCustomPostFxPass(context, renderContext, PostProcessEffectLocation::AfterAntiAliasingPass, frameBuffer, tempBuffer);
        renderContext.List->RunMaterialPostFxPass(context, renderContext, MaterialPostFxLocation::AfterAntiAliasingPass, frameBuffer, tempBuffer);

        // PostFx -> (up-scaling) -> Back Buffer
        if (task->RenderingPercentage >= 1.0f)
        {
            PROFILE_GPU("Copy frame");
            context->SetRenderTarget(task->GetOutputView());
            context->SetViewportAndScissors(task->GetOutputViewport());
            context->Draw(frameBuffer);
        }
        else if (renderContext.List->HasAnyPostFx(renderContext, PostProcessEffectLocation::CustomUpscale, MaterialPostFxLocation::MAX))
        {
            renderContext.List->RunCustomPostFxPass(context, renderContext, PostProcessEffectLocation::CustomUpscale, frameBuffer, frameBuffer);
        }
        else
        {
            MultiScaler::Instance()->Upscale(context, task->GetOutputViewport(), frameBuffer, task->GetOutputView());
        }
    }
}
