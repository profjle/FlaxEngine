// Copyright (c) 2012-2023 Wojciech Figat. All rights reserved.

#pragma once

#include "Engine/Core/Collections/Array.h"
#include "Engine/Core/Math/Half.h"
#include "Engine/Graphics/PostProcessSettings.h"
#include "Engine/Graphics/DynamicBuffer.h"
#include "Engine/Scripting/ScriptingObject.h"
#include "DrawCall.h"

enum class StaticFlags;
class RenderBuffers;
class SceneRendering;
class LightWithShadow;
class IPostFxSettingsProvider;
class CubeTexture;
class PostProcessBase;
struct RenderContext;

struct RendererDirectionalLightData
{
    Float3 Position;
    float MinRoughness;

    Float3 Color;
    float ShadowsStrength;

    Float3 Direction;
    float ShadowsFadeDistance;

    float ShadowsNormalOffsetScale;
    float ShadowsDepthBias;
    float ShadowsSharpness;
    float VolumetricScatteringIntensity;

    StaticFlags StaticFlags;
    float IndirectLightingIntensity;
    int8 CastVolumetricShadow : 1;
    int8 RenderedVolumetricFog : 1;

    float ShadowsDistance;
    int32 CascadeCount;
    float ContactShadowsLength;
    ShadowsCastingMode ShadowsMode;

    Guid ID;

    void SetupLightData(LightData* data, bool useShadow) const;
};

struct RendererSpotLightData
{
    Float3 Position;
    float MinRoughness;

    Float3 Color;
    float ShadowsStrength;

    Float3 Direction;
    float ShadowsFadeDistance;

    float ShadowsNormalOffsetScale;
    float ShadowsDepthBias;
    float ShadowsSharpness;
    float VolumetricScatteringIntensity;

    float ShadowsDistance;
    float Radius;
    float FallOffExponent;
    float SourceRadius;

    Float3 UpVector;
    float OuterConeAngle;

    float CosOuterCone;
    float InvCosConeDifference;
    float ContactShadowsLength;
    float IndirectLightingIntensity;
    ShadowsCastingMode ShadowsMode;

    StaticFlags StaticFlags;
    int8 CastVolumetricShadow : 1;
    int8 RenderedVolumetricFog : 1;
    int8 UseInverseSquaredFalloff : 1;

    GPUTexture* IESTexture;
    Guid ID;

    void SetupLightData(LightData* data, bool useShadow) const;
};

struct RendererPointLightData
{
    Float3 Position;
    float MinRoughness;

    Float3 Color;
    float ShadowsStrength;

    Float3 Direction;
    float ShadowsFadeDistance;

    float ShadowsNormalOffsetScale;
    float ShadowsDepthBias;
    float ShadowsSharpness;
    float VolumetricScatteringIntensity;

    float ShadowsDistance;
    float Radius;
    float FallOffExponent;
    float SourceRadius;

    float SourceLength;
    float ContactShadowsLength;
    float IndirectLightingIntensity;
    ShadowsCastingMode ShadowsMode;

    StaticFlags StaticFlags;
    int8 CastVolumetricShadow : 1;
    int8 RenderedVolumetricFog : 1;
    int8 UseInverseSquaredFalloff : 1;

    GPUTexture* IESTexture;
    Guid ID;

    void SetupLightData(LightData* data, bool useShadow) const;
};

struct RendererSkyLightData
{
    Float3 Position;
    float VolumetricScatteringIntensity;

    Float3 Color;
    float Radius;

    Float3 AdditiveColor;
    float IndirectLightingIntensity;

    StaticFlags StaticFlags;
    int8 CastVolumetricShadow : 1;
    int8 RenderedVolumetricFog : 1;

    CubeTexture* Image;
    Guid ID;

    void SetupLightData(LightData* data, bool useShadow) const;
};

/// <summary>
/// The draw calls list types.
/// </summary>
API_ENUM() enum class DrawCallsListType
{
    /// <summary>
    /// Hardware depth rendering.
    /// </summary>
    Depth,

    /// <summary>
    /// GBuffer rendering.
    /// </summary>
    GBuffer,

    /// <summary>
    /// GBuffer rendering after decals.
    /// </summary>
    GBufferNoDecals,

    /// <summary>
    /// Transparency rendering.
    /// </summary>
    Forward,

    /// <summary>
    /// Distortion accumulation rendering.
    /// </summary>
    Distortion,

    /// <summary>
    /// Motion vectors rendering.
    /// </summary>
    MotionVectors,

    MAX,
};

/// <summary>
/// Represents a patch of draw calls that can be submitted to rendering.
/// </summary>
struct DrawBatch
{
    /// <summary>
    /// Draw calls sorting key (shared by the all draw calls withing a patch).
    /// </summary>
    uint32 SortKey;

    /// <summary>
    /// The first draw call index.
    /// </summary>
    int32 StartIndex;

    /// <summary>
    /// A number of draw calls to be submitted at once.
    /// </summary>
    int32 BatchSize;

    /// <summary>
    /// The total amount of instances (sum from all draw calls in this batch).
    /// </summary>
    int32 InstanceCount;

    bool operator<(const DrawBatch& other) const
    {
        return SortKey < other.SortKey;
    }
};

class RenderListAllocation
{
public:
    static FLAXENGINE_API void* Allocate(uintptr size);
    static FLAXENGINE_API void Free(void* ptr, uintptr size);

    template<typename T>
    class Data
    {
        T* _data = nullptr;
        uintptr _size;

    public:
        FORCE_INLINE Data()
        {
        }

        FORCE_INLINE ~Data()
        {
            if (_data)
                RenderListAllocation::Free(_data, _size);
        }

        FORCE_INLINE T* Get()
        {
            return _data;
        }

        FORCE_INLINE const T* Get() const
        {
            return _data;
        }

        FORCE_INLINE int32 CalculateCapacityGrow(int32 capacity, int32 minCapacity) const
        {
            capacity = capacity ? capacity * 2 : 64;
            if (capacity < minCapacity)
                capacity = minCapacity;
            return capacity;
        }

        FORCE_INLINE void Allocate(uint64 capacity)
        {
            _size = capacity * sizeof(T);
            _data = (T*)RenderListAllocation::Allocate(_size);
        }

        FORCE_INLINE void Relocate(uint64 capacity, int32 oldCount, int32 newCount)
        {
            T* newData = capacity != 0 ? (T*)RenderListAllocation::Allocate(capacity * sizeof(T)) : nullptr;
            if (oldCount)
            {
                if (newCount > 0)
                    Memory::MoveItems(newData, _data, newCount);
                Memory::DestructItems(_data, oldCount);
            }
            if (_data)
                RenderListAllocation::Free(_data, _size);
            _data = newData;
            _size = capacity * sizeof(T);
        }

        FORCE_INLINE void Free()
        {
            if (_data)
            {
                RenderListAllocation::Free(_data, _size);
                _data = nullptr;
            }
        }

        FORCE_INLINE void Swap(Data& other)
        {
            ::Swap(_data, other._data);
            ::Swap(_size, other._size);
        }
    };
};

struct BatchedDrawCall
{
    DrawCall DrawCall;
    Array<struct InstanceData, RenderListAllocation> Instances;
};

/// <summary>
/// Represents a list of draw calls.
/// </summary>
struct DrawCallsList
{
    /// <summary>
    /// The list of draw calls indices to render.
    /// </summary>
    Array<int32> Indices;

    /// <summary>
    /// The list of external draw calls indices to render.
    /// </summary>
    Array<int32> PreBatchedDrawCalls;

    /// <summary>
    /// The draw calls batches (for instancing).
    /// </summary>
    Array<DrawBatch> Batches;

    /// <summary>
    /// True if draw calls batches list can be rendered using hardware instancing, otherwise false.
    /// </summary>
    bool CanUseInstancing;

    void Clear();
    bool IsEmpty() const;
};

/// <summary>
/// Rendering cache container object for the draw calls collecting, sorting and executing.
/// </summary>
API_CLASS(Sealed) class FLAXENGINE_API RenderList : public ScriptingObject
{
    DECLARE_SCRIPTING_TYPE(RenderList);

    /// <summary>
    /// Allocates the new renderer list object or reuses already allocated one.
    /// </summary>
    /// <returns>The cache object.</returns>
    API_FUNCTION() static RenderList* GetFromPool();

    /// <summary>
    /// Frees the list back to the pool.
    /// </summary>
    /// <param name="cache">The cache.</param>
    API_FUNCTION() static void ReturnToPool(RenderList* cache);

    /// <summary>
    /// Cleanups the static data cache used to accelerate draw calls sorting. Use it to reduce memory pressure.
    /// </summary>
    static void CleanupCache();

public:
    /// <summary>
    /// All scenes for rendering.
    /// </summary>
    Array<SceneRendering*> Scenes;

    /// <summary>
    /// Draw calls list (for all draw passes).
    /// </summary>
    Array<DrawCall> DrawCalls;

    /// <summary>
    /// Draw calls list with pre-batched instances (for all draw passes).
    /// </summary>
    Array<BatchedDrawCall> BatchedDrawCalls;

    /// <summary>
    /// The draw calls lists. Each for the separate draw pass.
    /// </summary>
    DrawCallsList DrawCallsLists[(int32)DrawCallsListType::MAX];

    /// <summary>
    /// Light pass members - directional lights
    /// </summary>
    Array<RendererDirectionalLightData> DirectionalLights;

    /// <summary>
    /// Light pass members - point lights
    /// </summary>
    Array<RendererPointLightData> PointLights;

    /// <summary>
    /// Light pass members - spot lights
    /// </summary>
    Array<RendererSpotLightData> SpotLights;

    /// <summary>
    /// Light pass members - sky lights
    /// </summary>
    Array<RendererSkyLightData> SkyLights;

    /// <summary>
    /// Environment probes to use for rendering reflections
    /// </summary>
    Array<EnvironmentProbe*> EnvironmentProbes;

    /// <summary>
    /// Decals registered for the rendering.
    /// </summary>
    Array<Decal*> Decals;

    /// <summary>
    /// Local volumetric fog particles registered for the rendering.
    /// </summary>
    Array<DrawCall> VolumetricFogParticles;

    /// <summary>
    /// Sky/skybox renderer proxy to use (only one per frame)
    /// </summary>
    ISkyRenderer* Sky;

    /// <summary>
    /// Atmospheric fog renderer proxy to use (only one per frame)
    /// </summary>
    IAtmosphericFogRenderer* AtmosphericFog;

    /// <summary>
    /// Fog renderer proxy to use (only one per frame)
    /// </summary>
    IFogRenderer* Fog;

    /// <summary>
    /// Post effect to render (TEMPORARY! cleared after and before rendering).
    /// </summary>
    Array<PostProcessBase*> PostFx;

    /// <summary>
    /// The post process settings.
    /// </summary>
    PostProcessSettings Settings;

    struct FLAXENGINE_API BlendableSettings
    {
        IPostFxSettingsProvider* Provider;
        float Weight;
        int32 Priority;
        float VolumeSizeSqr;

        bool operator<(const BlendableSettings& other) const;
    };

    /// <summary>
    /// The blendable postFx volumes collected during frame draw calls gather pass.
    /// </summary>
    Array<BlendableSettings> Blendable;

    void AddSettingsBlend(IPostFxSettingsProvider* provider, float weight, int32 priority, float volumeSizeSqr);

    /// <summary>
    /// Camera frustum corners in World Space
    /// </summary>
    Float3 FrustumCornersWs[8];

    /// <summary>
    /// Camera frustum corners in View Space
    /// </summary>
    Float3 FrustumCornersVs[8];

private:
    DynamicVertexBuffer _instanceBuffer;

public:
    /// <summary>
    /// Blends the postprocessing settings into the final options.
    /// </summary>
    void BlendSettings();

    /// <summary>
    /// Runs the post fx materials pass. Uses input/output buffer to render all materials. Uses temporary render target as a ping pong buffer if required (the same format and description).
    /// </summary>
    /// <param name="context">The context.</param>
    /// <param name="renderContext">The rendering context.</param>
    /// <param name="locationA">The material postFx location.</param>
    /// <param name="locationB">The custom postFx location.</param>
    /// <param name="inputOutput">The input and output texture.</param>
    void RunPostFxPass(GPUContext* context, RenderContext& renderContext, MaterialPostFxLocation locationA, PostProcessEffectLocation locationB, GPUTexture*& inputOutput);

    /// <summary>
    /// Runs the material post fx pass. Uses input and output buffers as a ping pong to render all materials.
    /// </summary>
    /// <param name="context">The context.</param>
    /// <param name="renderContext">The rendering context.</param>
    /// <param name="location">The material postFx location.</param>
    /// <param name="input">The input texture.</param>
    /// <param name="output">The output texture.</param>
    void RunMaterialPostFxPass(GPUContext* context, RenderContext& renderContext, MaterialPostFxLocation location, GPUTexture*& input, GPUTexture*& output);

    /// <summary>
    /// Runs the custom post fx pass. Uses input and output buffers as a ping pong to render all effects.
    /// </summary>
    /// <param name="context">The context.</param>
    /// <param name="renderContext">The rendering context.</param>
    /// <param name="location">The custom postFx location.</param>
    /// <param name="input">The input texture.</param>
    /// <param name="output">The output texture.</param>
    void RunCustomPostFxPass(GPUContext* context, RenderContext& renderContext, PostProcessEffectLocation location, GPUTexture*& input, GPUTexture*& output);

    /// <summary>
    /// Determines whether any Custom PostFx specified by given type. Used to pick a faster rendering path by the frame rendering module.
    /// </summary>
    /// <param name="renderContext">The rendering context.</param>
    /// <param name="postProcess">The PostFx location to check (for scripts).</param>
    /// <returns>True if render any postFx of the given type, otherwise false.</returns>
    bool HasAnyPostFx(RenderContext& renderContext, PostProcessEffectLocation postProcess) const;

    /// <summary>
    /// Determines whether any Material PostFx specified by given type. Used to pick a faster rendering path by the frame rendering module.
    /// </summary>
    /// <param name="renderContext">The rendering context.</param>
    /// <param name="materialPostFx">The PostFx location to check (for materials).</param>
    /// <returns>True if render any postFx of the given type, otherwise false.</returns>
    bool HasAnyPostFx(RenderContext& renderContext, MaterialPostFxLocation materialPostFx) const;

    /// <summary>
    /// Determines whether any Custom PostFx or Material PostFx specified by given type. Used to pick a faster rendering path by the frame rendering module.
    /// </summary>
    /// <param name="renderContext">The rendering context.</param>
    /// <param name="postProcess">The PostFx location to check (for scripts).</param>
    /// <param name="materialPostFx">The PostFx location to check (for materials).</param>
    /// <returns>True if render any postFx of the given type, otherwise false.</returns>
    bool HasAnyPostFx(RenderContext& renderContext, PostProcessEffectLocation postProcess, MaterialPostFxLocation materialPostFx) const
    {
        return HasAnyPostFx(renderContext, postProcess) || HasAnyPostFx(renderContext, materialPostFx);
    }

public:
    /// <summary>
    /// Init cache for given task
    /// </summary>
    /// <param name="renderContext">The rendering context.</param>
    void Init(RenderContext& renderContext);

    /// <summary>
    /// Clear cached data
    /// </summary>
    void Clear();

public:
    /// <summary>
    /// Adds the draw call to the draw lists.
    /// </summary>
    /// <param name="drawModes">The object draw modes.</param>
    /// <param name="staticFlags">The object static flags.</param>
    /// <param name="drawCall">The draw call data.</param>
    /// <param name="receivesDecals">True if the rendered mesh can receive decals.</param>
    void AddDrawCall(DrawPass drawModes, StaticFlags staticFlags, DrawCall& drawCall, bool receivesDecals);

    /// <summary>
    /// Sorts the collected draw calls list.
    /// </summary>
    /// <param name="renderContext">The rendering context.</param>
    /// <param name="reverseDistance">If set to <c>true</c> reverse draw call distance to the view. Results in back to front sorting.</param>
    /// <param name="listType">The collected draw calls list type.</param>
    API_FUNCTION() FORCE_INLINE void SortDrawCalls(API_PARAM(Ref) const RenderContext& renderContext, bool reverseDistance, DrawCallsListType listType)
    {
        SortDrawCalls(renderContext, reverseDistance, DrawCallsLists[(int32)listType]);
    }

    /// <summary>
    /// Sorts the collected draw calls list.
    /// </summary>
    /// <param name="renderContext">The rendering context.</param>
    /// <param name="reverseDistance">If set to <c>true</c> reverse draw call distance to the view. Results in back to front sorting.</param>
    /// <param name="list">The collected draw calls list.</param>
    void SortDrawCalls(const RenderContext& renderContext, bool reverseDistance, DrawCallsList& list);

    /// <summary>
    /// Executes the collected draw calls.
    /// </summary>
    /// <param name="renderContext">The rendering context.</param>
    /// <param name="listType">The collected draw calls list type.</param>
    /// <param name="input">The input scene color. It's optional and used in forward/postFx rendering.</param>
    API_FUNCTION() FORCE_INLINE void ExecuteDrawCalls(API_PARAM(Ref) const RenderContext& renderContext, DrawCallsListType listType, GPUTextureView* input = nullptr)
    {
        ExecuteDrawCalls(renderContext, DrawCallsLists[(int32)listType], input);
    }

    /// <summary>
    /// Executes the collected draw calls.
    /// </summary>
    /// <param name="renderContext">The rendering context.</param>
    /// <param name="list">The collected draw calls list.</param>
    /// <param name="input">The input scene color. It's optional and used in forward/postFx rendering.</param>
    void ExecuteDrawCalls(const RenderContext& renderContext, DrawCallsList& list, GPUTextureView* input = nullptr);
};

/// <summary>
/// Represents data per instance element used for instanced rendering.
/// </summary>
struct FLAXENGINE_API InstanceData
{
    Float3 InstanceOrigin;
    float PerInstanceRandom;
    Float3 InstanceTransform1;
    float LODDitherFactor;
    Float3 InstanceTransform2;
    Float3 InstanceTransform3;
    Half4 InstanceLightmapArea;
};

struct SurfaceDrawCallHandler
{
    static void GetHash(const DrawCall& drawCall, int32& batchKey);
    static bool CanBatch(const DrawCall& a, const DrawCall& b);
    static void WriteDrawCall(InstanceData* instanceData, const DrawCall& drawCall);
};
