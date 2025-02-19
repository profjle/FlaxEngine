// Copyright (c) 2012-2023 Wojciech Figat. All rights reserved.

#include "StaticModel.h"
#include "Engine/Engine/Engine.h"
#include "Engine/Graphics/GPUBuffer.h"
#include "Engine/Graphics/GPUBufferDescription.h"
#include "Engine/Graphics/GPUDevice.h"
#include "Engine/Graphics/RenderTask.h"
#include "Engine/Serialization/Serialization.h"
#include "Engine/Level/Prefabs/PrefabManager.h"
#include "Engine/Level/Scene/Scene.h"
#include "Engine/Renderer/GlobalSignDistanceFieldPass.h"
#include "Engine/Renderer/GI/GlobalSurfaceAtlasPass.h"
#include "Engine/Utilities/Encryption.h"
#if USE_EDITOR
#include "Editor/Editor.h"
#endif

StaticModel::StaticModel(const SpawnParams& params)
    : ModelInstanceActor(params)
    , _scaleInLightmap(1.0f)
    , _boundsScale(1.0f)
    , _lodBias(0)
    , _forcedLod(-1)
    , _vertexColorsDirty(false)
    , _vertexColorsCount(0)
{
    Model.Changed.Bind<StaticModel, &StaticModel::OnModelChanged>(this);
    Model.Loaded.Bind<StaticModel, &StaticModel::OnModelLoaded>(this);
}

StaticModel::~StaticModel()
{
    for (int32 lodIndex = 0; lodIndex < _vertexColorsCount; lodIndex++)
        SAFE_DELETE_GPU_RESOURCE(_vertexColorsBuffer[lodIndex]);
}

void StaticModel::SetScaleInLightmap(float value)
{
    _scaleInLightmap = value;
}

void StaticModel::SetBoundsScale(float value)
{
    if (Math::NearEqual(_boundsScale, value))
        return;

    _boundsScale = value;
    UpdateBounds();
}

MaterialBase* StaticModel::GetMaterial(int32 meshIndex, int32 lodIndex) const
{
    auto model = Model.Get();
    ASSERT(model &&
        Math::IsInRange(lodIndex, 0, model->GetLODsCount()) &&
        Math::IsInRange(meshIndex, 0, model->LODs[lodIndex].Meshes.Count()));
    const auto& mesh = model->LODs[lodIndex].Meshes[meshIndex];
    const auto materialSlotIndex = mesh.GetMaterialSlotIndex();
    MaterialBase* material = Entries[materialSlotIndex].Material.Get();
    return material ? material : model->MaterialSlots[materialSlotIndex].Material.Get();
}

Color32 StaticModel::GetVertexColor(int32 lodIndex, int32 meshIndex, int32 vertexIndex) const
{
    if (Model && !Model->WaitForLoaded() && _vertexColorsCount == Model->GetLODsCount())
    {
        if (lodIndex < 0 || lodIndex >= Model->GetLODsCount())
        {
            LOG(Warning, "Specified model LOD index {0} was out of range.", lodIndex);
            return Color32::Black;
        }

        int32 index = 0;
        const ModelLOD& lod = Model->LODs[lodIndex];
        auto& vertexColorsData = _vertexColorsData[lodIndex];
        if (vertexColorsData.Count() != lod.GetVertexCount())
            return Color32::Black;
        for (int32 i = 0; i < lod.Meshes.Count(); i++)
        {
            const Mesh& mesh = lod.Meshes[i];
            if (i == meshIndex)
            {
                if (vertexIndex < 0 || vertexIndex >= mesh.GetVertexCount())
                {
                    LOG(Warning, "Specified vertex index {3} was out of range. LOD{0} mesh {1} has {2}.", lodIndex, meshIndex, mesh.GetVertexCount(), vertexIndex);
                    return Color32::Black;
                }
                index += vertexIndex;
                return _vertexColorsData[lodIndex][index];
            }
            index += mesh.GetVertexCount();
        }

        LOG(Warning, "Specified model mesh index was out of range. LOD{0} mesh {1}.", lodIndex, meshIndex);
    }

    return Color32::Black;
}

void StaticModel::SetVertexColor(int32 lodIndex, int32 meshIndex, int32 vertexIndex, const Color32& color)
{
    if (!Model || Model->WaitForLoaded())
    {
        LOG(Warning, "Cannot set vertex color if model is missing or failed to load.");
        return;
    }

    if (lodIndex < 0 || lodIndex >= Model->GetLODsCount())
    {
        LOG(Warning, "Specified model LOD index {0} was out of range.", lodIndex);
        return;
    }

    if (_vertexColorsCount != Model->GetLODsCount())
    {
        // Initialize vertex colors data for all LODs
        RemoveVertexColors();
        _vertexColorsCount = Model->GetLODsCount();
        for (int32 i = 0; i < _vertexColorsCount; i++)
            _vertexColorsBuffer[i] = nullptr;
        _vertexColorsDirty = false;
    }

    int32 index = 0;
    const ModelLOD& lod = Model->LODs[lodIndex];
    auto& vertexColorsData = _vertexColorsData[lodIndex];
    if (vertexColorsData.Count() != lod.GetVertexCount())
    {
        vertexColorsData.Resize(lod.GetVertexCount());
        vertexColorsData.SetAll(Color32::Black);
    }
    for (int32 i = 0; i < lod.Meshes.Count(); i++)
    {
        const Mesh& mesh = lod.Meshes[i];
        if (i == meshIndex)
        {
            if (vertexIndex < 0 || vertexIndex >= mesh.GetVertexCount())
            {
                LOG(Warning, "Specified vertex index {3} was out of range. LOD{0} mesh {1} has {2}.", lodIndex, meshIndex, mesh.GetVertexCount(), vertexIndex);
                return;
            }
            index += vertexIndex;
            vertexColorsData[index] = color;
            _vertexColorsDirty = true;
            return;
        }
        index += mesh.GetVertexCount();
    }

    LOG(Warning, "Specified model mesh index was out of range. LOD{0} mesh {1}.", lodIndex, meshIndex);
}

void StaticModel::RemoveVertexColors()
{
    for (int32 lodIndex = 0; lodIndex < _vertexColorsCount; lodIndex++)
        _vertexColorsData[lodIndex].Resize(0);
    for (int32 lodIndex = 0; lodIndex < _vertexColorsCount; lodIndex++)
        SAFE_DELETE_GPU_RESOURCE(_vertexColorsBuffer[lodIndex]);
    _vertexColorsCount = 0;
    _vertexColorsDirty = false;
}

void StaticModel::OnModelChanged()
{
    if (_residencyChangedModel)
    {
        _residencyChangedModel = nullptr;
        Model->ResidencyChanged.Unbind<StaticModel, &StaticModel::OnModelResidencyChanged>(this);
    }
    RemoveVertexColors();
    Entries.Release();
    if (Model && !Model->IsLoaded())
        UpdateBounds();
    else if (!Model && _sceneRenderingKey != -1)
        GetSceneRendering()->RemoveActor(this, _sceneRenderingKey);
}

void StaticModel::OnModelLoaded()
{
    Entries.SetupIfInvalid(Model);
    UpdateBounds();
    if (_sceneRenderingKey == -1 && _scene && _isActiveInHierarchy && _isEnabled && !_residencyChangedModel)
    {
        // Register for rendering but once the model has any LOD loaded
        if (Model->GetLoadedLODs() == 0)
        {
            _residencyChangedModel = Model;
            _residencyChangedModel->ResidencyChanged.Bind<StaticModel, &StaticModel::OnModelResidencyChanged>(this);
        }
        else
        {
            GetSceneRendering()->AddActor(this, _sceneRenderingKey);
        }
    }
}

void StaticModel::OnModelResidencyChanged()
{
    if (_sceneRenderingKey == -1 && _scene && Model && Model->GetLoadedLODs() > 0 && _residencyChangedModel)
    {
        GetSceneRendering()->AddActor(this, _sceneRenderingKey);
        _residencyChangedModel->ResidencyChanged.Unbind<StaticModel, &StaticModel::OnModelResidencyChanged>(this);
        _residencyChangedModel = nullptr;
    }
}

void StaticModel::UpdateBounds()
{
    if (Model && Model->IsLoaded())
    {
        Transform transform = _transform;
        transform.Scale *= _boundsScale;
        _box = Model->GetBox(transform);
    }
    else
    {
        _box = BoundingBox(_transform.Translation);
    }
    BoundingSphere::FromBox(_box, _sphere);
    if (_sceneRenderingKey != -1)
        GetSceneRendering()->UpdateActor(this, _sceneRenderingKey);
}

bool StaticModel::HasContentLoaded() const
{
    return (Model == nullptr || Model->IsLoaded()) && Entries.HasContentLoaded();
}

void StaticModel::Draw(RenderContext& renderContext)
{
    const DrawPass drawModes = (DrawPass)(DrawModes & renderContext.View.Pass);
    if (!Model || !Model->IsLoaded() || !Model->CanBeRendered() || drawModes == DrawPass::None)
        return;
    if (renderContext.View.Pass == DrawPass::GlobalSDF)
    {
        GlobalSignDistanceFieldPass::Instance()->RasterizeModelSDF(this, Model->SDF, _transform, _box);
        return;
    }
    if (renderContext.View.Pass == DrawPass::GlobalSurfaceAtlas)
    {
        GlobalSurfaceAtlasPass::Instance()->RasterizeActor(this, this, _sphere, _transform, Model->LODs.Last().GetBox());
        return;
    }
    Matrix world;
    renderContext.View.GetWorldMatrix(_transform, world);
    GEOMETRY_DRAW_STATE_EVENT_BEGIN(_drawState, world);

    // Flush vertex colors if need to
    if (_vertexColorsDirty)
    {
        for (int32 lodIndex = 0; lodIndex < _vertexColorsCount; lodIndex++)
        {
            auto& vertexColorsData = _vertexColorsData[lodIndex];
            auto& vertexColorsBuffer = _vertexColorsBuffer[lodIndex];
            if (vertexColorsData.HasItems())
            {
                const uint32 size = vertexColorsData.Count() * sizeof(Color32);
                if (!vertexColorsBuffer)
                    vertexColorsBuffer = GPUDevice::Instance->CreateBuffer(TEXT("VertexColors"));
                if (vertexColorsBuffer->GetSize() != size)
                {
                    if (vertexColorsBuffer->Init(GPUBufferDescription::Vertex(sizeof(Color32), vertexColorsData.Count())))
                        return;
                }
                GPUDevice::Instance->GetMainContext()->UpdateBuffer(vertexColorsBuffer, vertexColorsData.Get(), size);
            }
            else
            {
                SAFE_DELETE_GPU_RESOURCE(vertexColorsBuffer);
            }
        }
        _vertexColorsDirty = false;
    }

#if USE_EDITOR
    // Disable motion blur effects in editor without play mode enabled to hide minor artifacts on objects moving
    if (!Editor::IsPlayMode)
        _drawState.PrevWorld = world;
#endif

    Mesh::DrawInfo draw;
    draw.Buffer = &Entries;
    draw.World = &world;
    draw.DrawState = &_drawState;
    draw.Lightmap = _scene->LightmapsData.GetReadyLightmap(Lightmap.TextureIndex);
    draw.LightmapUVs = &Lightmap.UVsArea;
    draw.Flags = _staticFlags;
    draw.DrawModes = drawModes;
    draw.Bounds = _sphere;
    draw.Bounds.Center -= renderContext.View.Origin;
    draw.PerInstanceRandom = GetPerInstanceRandom();
    draw.LODBias = _lodBias;
    draw.ForcedLOD = _forcedLod;
    draw.VertexColors = _vertexColorsCount ? _vertexColorsBuffer : nullptr;

    Model->Draw(renderContext, draw);

    GEOMETRY_DRAW_STATE_EVENT_END(_drawState, world);
}

bool StaticModel::IntersectsItself(const Ray& ray, Real& distance, Vector3& normal)
{
    bool result = false;

    if (Model != nullptr && Model->IsLoaded())
    {
        Mesh* mesh;
        result = Model->Intersects(ray, _transform, distance, normal, &mesh);
    }

    return result;
}

void StaticModel::Serialize(SerializeStream& stream, const void* otherObj)
{
    // Base
    ModelInstanceActor::Serialize(stream, otherObj);

    SERIALIZE_GET_OTHER_OBJ(StaticModel);

    SERIALIZE_MEMBER(ScaleInLightmap, _scaleInLightmap);
    SERIALIZE_MEMBER(BoundsScale, _boundsScale);
    SERIALIZE(Model);
    SERIALIZE_MEMBER(LODBias, _lodBias);
    SERIALIZE_MEMBER(ForcedLOD, _forcedLod);
    SERIALIZE(DrawModes);

    if (HasLightmap()
#if USE_EDITOR
        && PrefabManager::IsNotCreatingPrefab
#endif
    )
    {
        stream.JKEY("LightmapIndex");
        stream.Int(Lightmap.TextureIndex);

        stream.JKEY("LightmapArea");
        stream.Rectangle(Lightmap.UVsArea);
    }

    stream.JKEY("Buffer");
    stream.Object(&Entries, other ? &other->Entries : nullptr);

    if (_vertexColorsCount)
    {
        stream.JKEY("VertexColors");
        stream.StartArray();
        Array<char> encodedData;
        for (int32 lodIndex = 0; lodIndex < _vertexColorsCount; lodIndex++)
        {
            auto& vertexColorsData = _vertexColorsData[lodIndex];
            if (vertexColorsData.HasItems())
            {
                const int32 size = vertexColorsData.Count() * sizeof(Color32);
                Encryption::Base64Encode((byte*)vertexColorsData.Get(), size, encodedData);
                stream.String(encodedData.Get(), encodedData.Count());
            }
            else
            {
                stream.String("", 0);
            }
        }
        stream.EndArray();
    }
}

void StaticModel::Deserialize(DeserializeStream& stream, ISerializeModifier* modifier)
{
    // Base
    ModelInstanceActor::Deserialize(stream, modifier);

    DESERIALIZE_MEMBER(ScaleInLightmap, _scaleInLightmap);
    DESERIALIZE_MEMBER(BoundsScale, _boundsScale);
    DESERIALIZE(Model);

    {
        const auto member = stream.FindMember("LODBias");
        if (member != stream.MemberEnd() && member->value.IsInt())
        {
            SetLODBias(member->value.GetInt());
        }
    }
    {
        const auto member = stream.FindMember("ForcedLOD");
        if (member != stream.MemberEnd() && member->value.IsInt())
        {
            SetForcedLOD(member->value.GetInt());
        }
    }

    DESERIALIZE(DrawModes);
    DESERIALIZE_MEMBER(LightmapIndex, Lightmap.TextureIndex);
    DESERIALIZE_MEMBER(LightmapArea, Lightmap.UVsArea);

    Entries.DeserializeIfExists(stream, "Buffer", modifier);

    {
        const auto member = stream.FindMember("VertexColors");
        if (member != stream.MemberEnd() && member->value.IsArray())
        {
            // TODO: don't stall but just check the length of the loaded vertex colors arrays size later when asset gets loaded
            if (Model && !Model->WaitForLoaded())
            {
                RemoveVertexColors();
                auto& array = member->value;
                _vertexColorsCount = array.Size();
                Array<byte> decodedData;
                if (_vertexColorsCount == Model->GetLODsCount())
                {
                    for (int32 lodIndex = 0; lodIndex < _vertexColorsCount; lodIndex++)
                    {
                        _vertexColorsBuffer[lodIndex] = nullptr;
                        auto& vertexColorsData = _vertexColorsData[lodIndex];
                        vertexColorsData.Clear();
                        auto& v = array[lodIndex];
                        if (v.IsString())
                        {
                            Encryption::Base64Decode(v.GetString(), v.GetStringLength(), decodedData);
                            const int32 length = decodedData.Count() / sizeof(Color32);
                            vertexColorsData.Resize(length);
                            Platform::MemoryCopy(vertexColorsData.Get(), decodedData.Get(), decodedData.Count());
                        }
                    }
                }
                else
                {
                    LOG(Error, "Loaded vertex colors data for {0} has different size than the model {1} LODs count.", ToString(), Model->ToString());
                }
                _vertexColorsDirty = true;
            }
        }
    }

    // [Deprecated on 11.10.2019, expires on 11.10.2020]
    if (modifier->EngineBuild <= 6187)
    {
        const auto member = stream.FindMember("HiddenShadow");
        if (member != stream.MemberEnd() && member->value.IsBool() && member->value.GetBool())
        {
            DrawModes = DrawPass::Depth;
        }
    }
    // [Deprecated on 07.02.2022, expires on 07.02.2024]
    if (modifier->EngineBuild <= 6330)
        DrawModes |= DrawPass::GlobalSDF;
    // [Deprecated on 27.04.2022, expires on 27.04.2024]
    if (modifier->EngineBuild <= 6331)
        DrawModes |= DrawPass::GlobalSurfaceAtlas;

    {
        const auto member = stream.FindMember("RenderPasses");
        if (member != stream.MemberEnd() && member->value.IsInt())
        {
            DrawModes = (DrawPass)member->value.GetInt();
        }
    }
}

bool StaticModel::IntersectsEntry(int32 entryIndex, const Ray& ray, Real& distance, Vector3& normal)
{
    auto model = Model.Get();
    if (!model || !model->IsInitialized() || model->GetLoadedLODs() == 0)
        return false;

    // Find mesh in the highest loaded LOD that is using the given material slot index and ray hits it
    auto& meshes = model->LODs[model->HighestResidentLODIndex()].Meshes;
    for (int32 i = 0; i < meshes.Count(); i++)
    {
        const auto& mesh = meshes[i];
        if (mesh.GetMaterialSlotIndex() == entryIndex && mesh.Intersects(ray, _transform, distance, normal))
            return true;
    }

    distance = 0;
    normal = Vector3::Up;
    return false;
}

bool StaticModel::IntersectsEntry(const Ray& ray, Real& distance, Vector3& normal, int32& entryIndex)
{
    auto model = Model.Get();
    if (!model || !model->IsInitialized() || model->GetLoadedLODs() == 0)
        return false;

    // Find mesh in the highest loaded LOD that is using the given material slot index and ray hits it
    bool result = false;
    Real closest = MAX_Real;
    Vector3 closestNormal = Vector3::Up;
    int32 closestEntry = -1;
    auto& meshes = model->LODs[model->HighestResidentLODIndex()].Meshes;
    for (int32 i = 0; i < meshes.Count(); i++)
    {
        // Test intersection with mesh and check if is closer than previous
        const auto& mesh = meshes[i];
        Real dst;
        Vector3 nrm;
        if (mesh.Intersects(ray, _transform, dst, nrm) && dst < closest)
        {
            result = true;
            closest = dst;
            closestNormal = nrm;
            closestEntry = mesh.GetMaterialSlotIndex();
        }
    }

    distance = closest;
    normal = closestNormal;
    entryIndex = closestEntry;
    return result;
}

void StaticModel::OnTransformChanged()
{
    // Base
    ModelInstanceActor::OnTransformChanged();

    UpdateBounds();
}

void StaticModel::OnEnable()
{
    // If model is set and loaded but we still don't have residency registered do it here (eg. model is streaming LODs right now)
    if (_scene && _sceneRenderingKey == -1 && !_residencyChangedModel && Model && Model->IsLoaded())
    {
        // Register for rendering but once the model has any LOD loaded
        if (Model->GetLoadedLODs() == 0)
        {
            _residencyChangedModel = Model;
            _residencyChangedModel->ResidencyChanged.Bind<StaticModel, &StaticModel::OnModelResidencyChanged>(this);
        }
        else
        {
            GetSceneRendering()->AddActor(this, _sceneRenderingKey);
        }
    }

    // Skip ModelInstanceActor (add to SceneRendering manually)
    Actor::OnEnable();
}

void StaticModel::OnDisable()
{
    // Skip ModelInstanceActor (add to SceneRendering manually)
    Actor::OnDisable();

    if (_sceneRenderingKey != -1)
    {
        GetSceneRendering()->RemoveActor(this, _sceneRenderingKey);
    }
    if (_residencyChangedModel)
    {
        _residencyChangedModel->ResidencyChanged.Unbind<StaticModel, &StaticModel::OnModelResidencyChanged>(this);
        _residencyChangedModel = nullptr;
    }
}

void StaticModel::WaitForModelLoad()
{
    if (Model)
        Model->WaitForLoaded();
}
