#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <SDL3/SDL_scancode.h>
#include <Random/randomGen.hpp>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Memory/CheckMem.hpp>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

using Poseidon::Foundation::IsOutOfMemory;
using Poseidon::Foundation::MStorage;
using Poseidon::Foundation::Time;
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/UI/Settings/GameSettingsConfig.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>
#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/World/Scene/SurfaceDrawOrder.hpp>
#include <Poseidon/World/Scene/AlphaSortOrder.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Graphics/Rendering/Primitives/Poly.hpp>
#include <Poseidon/Graphics/Shadow/ShadowMath.hpp>
#include <Poseidon/World/Simulation/Animation/Animation.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/Graphics/Rendering/Primitives/ClipVert.hpp>
#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/World/Scene/ObjLine.hpp>
#include <Poseidon/World/Scene/ObjectClasses.hpp>
#include <Poseidon/World/Simulation/FrameInv.hpp>
#include <Poseidon/AI/AI.hpp> // remove dependency
#include <Poseidon/World/World.hpp>
#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <time.h>
#include <Poseidon/Dev/Diag/DiagModes.hpp>

using namespace Poseidon;
namespace Poseidon
{
RString GetUserParams();
}

namespace Poseidon
{
Scene* GScene;
} // namespace Poseidon

#define PASS_VEC(x) x.X(), x.Y(), x.Z()

typedef float FogF(float dist, float start, float end);

#define FogLinear ((FogF*)nullptr)

float FogQuadratic(float dist, float start, float end)
{
    return Square(dist - start) / Square(end - start);
}

#define LN_20 2.9957322736f

float FogExponential(float dist, float start, float end)
{
    // scale so that at start fog is 0
    return 1 - exp(LN_20 * (start - dist) / end);
}

#define FOG_FUNCTION FogExponential

static void AdaptVolumeLight(LODShape* lodShape)
{
    lodShape->OrSpecial(NoShadow | IsAlpha | IsLight | NoZWrite | ClampU | ClampV | IsAlphaFog | IsColored);
}

namespace Poseidon::Foundation
{
template class Ref<RoadType>;
} // namespace Poseidon::Foundation

#if DENSITY_LOD
#define _drawDensity _lodInvWidth
#endif

Scene::Scene()
    : _skyColor(HWhite), _mainLight(nullptr), _camera(new Camera), _tacticalVisibility(TACTICAL_VISIBILITY),
      _rainRange(900), _constantFog(0), _constantColor(HWhite), _objectShadows(false), _vehicleShadows(true),
      _cloudlets(true), _preferredTerrainGrid(ENGINE_CONFIG.enableHWTL ? 12.5 : 25),
      _preferredViewDistance(GetSelectedPreferredViewDistance())
{
    static StaticStorage<ActiveLightPointer> aLightsS;
    _aLights.SetStorage(aLightsS.Init(64));

    LoadConfig();

    _lodInvWidth = (_minLodInvWidth + _maxLodInvWidth) * 0.5;

#if _PIII
    PoseidonAssert(((int)this & 0xf) == 0);
#endif
    ResetFog();
}

static void UpdateFogRange(float& minRange, float& maxRange, float tacVis)
{
    saturateMin(maxRange, tacVis);
    saturateMin(minRange, maxRange * 0.4f);
}

void Scene::ResetFog()
{
    float minRange, maxRange;
    // shadow fog
    minRange = MAX_SHADOWFOG * 0.3f, maxRange = MAX_SHADOWFOG;
    UpdateFogRange(minRange, maxRange, _rainRange);
    _shadowFog.Set(minRange, maxRange, FOG_FUNCTION);
    _shadowFogMinRange = minRange, _shadowFogMaxRange = maxRange;
    // display fog
    // set back clipping to fog range
    ENGINE_CONFIG.horizontZ = floatMin(_rainRange + 20, ENGINE_CONFIG.tacticalZ);
    minRange = MAX_FOG * 0.3f, maxRange = MAX_FOG;
    UpdateFogRange(minRange, maxRange, _rainRange);
    _fog.Set(minRange, maxRange, FOG_FUNCTION);
    _fogMinRange = minRange, _fogMaxRange = maxRange;
    // sky fog is constant
    _skyFog.Set(MinSkyFog, MaxSkyFog, FOG_FUNCTION);
    _tacticalFog.Set(minRange, _tacticalVisibility, FOG_FUNCTION);
}

void Scene::SetTacticalVisibility(float tv, float rainRange)
{
    _tacticalVisibility = tv;
    if (fabs(rainRange - _rainRange) < 3)
    {
        return;
    }
    _rainRange = rainRange;
    ResetFog();
}

void Scene::Init(Engine* engine, Landscape* landscape)
{
    if (_landscape != landscape)
    {
        _landscape = landscape;
    }
    AbstractTextBank* bank = GEngine->TextBank();
    if (_landscape)
    {
        _landscape->SetOvercast(0.0);

        _skyTexture = _landscape->SkyTexture();
    }
    else
    {
        _skyTexture = bank->Load("data\\zatazeno.pac");
    }

    CalculateSkyColor(_skyTexture);
}

void Scene::CalculateSkyColor(Texture* texture)
{
    if (!texture)
    {
        texture = _skyTexture;
    }
    else
    {
        _skyTexture = texture;
    }
    if (_skyTexture)
    {
        // guarantee that the mipmap is loaded
        MipInfo mip = GEngine->TextBank()->UseMipmap(_skyTexture, 0, 0);
        PoseidonAssert(mip.IsOK());
        if (mip.IsOK())
        {
            _skyColor = _skyTexture->GetPixel(0, 1, 1);
        }
    }
}

void Scene::CleanUp()
{
    for (int i = 0; i < _drawObjects.Size(); i++)
    {
        _drawObjects[i]->object->SetInList(nullptr);
    }
    _drawObjects.Resize(0);
    _drawMergers.Resize(0);
}

Scene::~Scene()
{
    SaveConfig();
    ResetLights();
    _aLights.Clear();
    if (_mainLight)
    {
        delete _mainLight, _mainLight = nullptr;
    }
    if (_camera)
    {
        delete _camera, _camera = nullptr;
    }
    _skyTexture = nullptr;
    _landscape.Free();
    GLandscape = nullptr;
}

void Scene::SetCamera(const Camera& camera)
{
    if (_camera)
    {
        *_camera = camera;
    }
    else
    {
        _camera = new Camera(camera);
    }
}

FogFunction::FogFunction()
{
    _start2 = 0;
    _end2 = 1;
    _divisor = (LEN_FOG_TABLE - 1) / (_end2 - _start2);
}

void FogFunction::Set(float start, float end, float (*function)(float distRel, float start, float end))
{
    _start2 = start * start;
    _end2 = end * end;
    _divisor = (LEN_FOG_TABLE - 1) / (_end2 - _start2);
    float invEmS = 1.0f / (end - start);
    for (int i = 0; i < LEN_FOG_TABLE; i++)
    {
        // index into the table is i, corresponding relative distance is
        float indexRel = i * (1.0f / (LEN_FOG_TABLE - 1));
        // indexRel corresponds to (dist*dist-_start*_start)/(_end*_end-_start*_start)
        // we would like to calculate (dist-_start)/(_end-_start)
        float dist = sqrt(indexRel * (_end2 - _start2) + _start2);
        float fogLF = (dist - start) * invEmS;
        float fogF = function ? function(dist, start, end) : fogLF;
        int fog = toInt(fogF * 256);
        if (fog < 0)
        {
            fog = 0;
        }
        else if (fog > 255)
        {
            fog = 255;
        }
        _fog[i] = fog;
    }
    _fog[LEN_FOG_TABLE - 1] = 0xff; // full fog
}

int FogFunction::operator()(float distSquare) const
{
    // calculate index between 0 and 1
    float indexFlt = (distSquare - _start2) * _divisor;
    int index = toIntFloor(indexFlt);
    if (index <= 0)
    {
        return _fog[0];
    }
    if (index < LEN_FOG_TABLE)
    {
        return _fog[index];
    }
    return _fog[LEN_FOG_TABLE - 1];
}

void Scene::AddLight(Light* light)
{
    int i;
    // if possible insert the light into some empty slot
    for (i = 0; i < _lights.Size(); i++)
    {
        if (!_lights[i])
        {
            _lights[i] = light;
            return;
        }
    }
    _lights.Add(light);
}

void Scene::ResetLights()
{
    _lights.Clear();
}

static int CmpALight(const ActiveLightPointer* l0, const ActiveLightPointer* l1, LightContext* context)
{
    const Light* light0 = *l0;
    const Light* light1 = *l1;
    PoseidonAssert(light0);
    PoseidonAssert(light1);
    PoseidonAssert(light0 != light1);
    return light1->Compare(*light0, *context);
}

static int CmpALight(const ActiveLightPointer* l0, const ActiveLightPointer* l1)
{
    const Light* light0 = *l0;
    const Light* light1 = *l1;
    PoseidonAssert(light0);
    PoseidonAssert(light1);
    PoseidonAssert(light0 != light1);
    return light1->Compare(*light0);
}

const LightList& Scene::SelectLights(Vector3Par pos, float radius, LightList& work)
{
    // select only the nearest lights to make lighting faster
    int i;
    for (i = 0; i < _aLights.Size(); i++)
    {
        // select only light which can add something to object lighting
        Light* light = _aLights[i];
        float dist2 = light->SquareDistance(pos);
        if (Square(radius) > dist2 * Square(0.1))
        {
            float dist = dist2 * InvSqrt(dist2) - radius;
            saturateMax(dist, 0);
            if (light->Brightness() > 0.02 * Square(dist))
            {
                work.Add(light);
            }
        }
        else
        {
            if (light->Brightness() > 0.02 * dist2)
            {
                work.Add(light);
            }
        }
    }
    // try to optimize lights
    if (work.Size() > 1)
    {
        LightContext context;
        context.position = pos;
        QSort(work.Data(), work.Size(), &context, CmpALight);
        const int maxLightsPerObject = 7;
        if (work.Size() > maxLightsPerObject)
        {
            work.Resize(maxLightsPerObject);
        }
    }
    return work;
}

#define DIAG_LIGHT_SELECT

const LightList& Scene::SelectLights(Matrix4Par objPos, const Object* object, int level, LightList& work)
{
    work.Clear();
    // no lights used - full day light
    const int spec = object->GetSpecial();
    const bool sunDisabled = render::Has(render::SplitLegacy(spec).material, render::Material::DisableSun);
    if (sunDisabled)
    {
        return _aLights;
    }

    if (!sunDisabled && MainLight()->NightEffect() < 0.01)
    {
        return work;
    }
    // may return work or something else
    LODShape* lShape = object->GetShape();
    Shape* oShape = lShape->LevelOpaque(level);

    // no light selection for sky, clouds ... etc.
    if (spec & IsShadow)
    {
        return work;
    }
    ClipFlags globalLight = oShape->GetAndHints() & ClipLightMask;
    if (globalLight != (oShape->GetOrHints() & ClipLightMask))
    {
        globalLight = 0;
    }
    switch (globalLight)
    {
        case ClipLightCloud:
        case ClipLightSky:
        case ClipLightStars:
            return work;
    }

    // select only the nearest lights to make lighting faster
    return SelectLights(objPos.Position(), lShape->BoundingSphere(), work);
}

static StaticStorage<ActiveLightPointer> LightStorage;

LightList::LightList(bool staticStorage)
{
    if (staticStorage)
    {
        SetStorage(LightStorage.Init(64));
    }
}

LightList::LightList(const LightList& src)
{
    SetStorage(LightStorage.Init(64));
    Realloc(src.Size());
    Resize(src.Size());
    // note: we assume LightList does not need any destruction
    memcpy(Data(), src.Data(), src.Size() * sizeof(ActiveLightPointer));
}

void Scene::SetActiveLights(const LightList& lights)
{
    _aLights.Resize(0);
    // copy all lights from the list
    for (int i = 0; i < lights.Size(); i++)
    {
        Light* light = lights[i];
        _aLights.Add(light);
    }
}

void Scene::SelectActiveLights(Object* dimmed)
{
    _aLights.Resize(0);

    // enumerate all light objects for drawing
    Vector3Val camPos = GetCamera()->Position();
    for (int i = 0; i < _lights.Size(); i++)
    {
        Light* light = _lights[i];
        if (light && light->IsOn())
        {
            // check if light can be ignored (very far)
            if (light->Position().Distance2(camPos) > Square(ENGINE_CONFIG.horizontZ + 500))
            {
                continue;
            }

            bool invisible = false;
            Object* attach = light->AttachedOn();
            if (attach && attach == dimmed)
            {
                invisible = true;
            }
            light->ToDraw(ClipAll, invisible);
            _aLights.Add(light);
        }
    }

    // we need to know the strongest light source
    QSort(_aLights.Data(), _aLights.Size(), CmpALight);
    if (_aLights.Size() > ENGINE_CONFIG.maxLights)
    {
        // use only strongest lights
        _aLights.Resize(ENGINE_CONFIG.maxLights);
    }
}

void Scene::MainLightChanged()
{
    if (GetLandscape())
    {
        Color sunColor = _mainLight->GetColorFull() * (GetLandscape()->SkyThrough() * 0.8f + 0.2f);
        sunColor.SaturateMinMax();
        _mainLight->SetDiffuse(sunColor);
    }
    // recalculate background color
    // calculate fog color from sky texture
    // we should apply some lighting (to have dark fog in the night)
    Color lighting = _mainLight->SkyColor();
    lighting = lighting * GEngine->GetAccomodateEye();
    lighting.SaturateMinMax();
    Color color = _skyColor * lighting;
    color.SaturateMinMax();
    color.SetA(1);
    GEngine->SetFogColor(color);
}

const Matrix4& Scene::ScaledInvTransform() const
{
    return _camera->_scaledInvTransform;
}
const Matrix3& Scene::CamNormalTrans() const
{
    return _camera->_camNormalTrans;
}
const Matrix4& Scene::CamInvTrans() const
{
    return _camera->_camInvTrans;
}

float Scene::GetMinimalTerrainGrid() const
{
    // All terrain grid options are available; modern hardware handles max
    // settings (VD=5000, TG=3.125) at 333+ FPS.
    return 3.125;
}

void Scene::SetPreferredTerrainGrid(float x)
{
    saturate(x, 0.5, 100);
    _preferredTerrainGrid = x;
    if (GWorld && GWorld->GetMode() != GModeNetware)
    {
        GWorld->AdjustSubdivision(GWorld->GetMode());
    }
}
void Scene::SetPreferredViewDistance(float x)
{
    float cliVD = AppConfig::Instance().GetViewDistanceOverride();
    if (cliVD > 0)
        x = cliVD;
    else
        saturate(x, GameSettingsConfig::kMinViewDistance, GameSettingsConfig::kMaxViewDistance);
    _preferredViewDistance = x;

    if (GWorld && GWorld->GetMode() != GModeNetware)
    {
        GWorld->AdjustSubdivision(GWorld->GetMode());
    }
}

void Scene::SetObjectShadows(bool set)
{
    _objectShadows = set;
}

void Scene::SetVehicleShadows(bool set)
{
    _vehicleShadows = set;
}

void Scene::SetCloudlets(bool set)
{
    _cloudlets = set;
}

void Scene::SetObjectLODBias(float bias)
{
    if (bias < 0.25f)
        bias = 0.25f;
    if (bias > 4.0f)
        bias = 4.0f;
    _objectLODBias = bias;
    // Driven by the new Graphics screen's Object LOD tier row.  Read by
    // the LOD selection path on the next render frame; no extra Reset
    // needed — the bias multiplies the projected screen size before
    // LODSelect compares against the per-LOD pixel-size thresholds.
}

void Scene::SetQualitySettings(float val)
{
    _qualitySettings = val;

    // allow immediate change
    _lastScaleBetterTime = UITIME_MIN;
    _lastScaleWorseTime = UITIME_MIN;

    _minLodInvWidth = _qualitySettings;
    _maxLodInvWidth = _qualitySettings * 16;

#if !DENSITY_LOD
    saturate(_lodInvWidth, _minLodInvWidth, _maxLodInvWidth);
#endif
}

void Scene::SetFrameRateSettings(float val)
{
    _frameRateSettings = val;

    float minFPS = _frameRateSettings * (10.0 / 15);
    float maxFPS = _frameRateSettings * (20.0 / 15);
    _maxTargetFrameDuration = 1000 / minFPS;
    _minTargetFrameDuration = 1000 / maxFPS;

    // allow immediate change
    _lastScaleBetterTime = UITIME_MIN;
    _lastScaleWorseTime = UITIME_MIN;
}

RString Scene::GetQualityText() const
{
    char buffer[256];
    float minQ = -10 * log10(_minLodInvWidth);
    float maxQ = -10 * log10(_maxLodInvWidth);

    snprintf(buffer, sizeof(buffer), "%.0f..%.0f", maxQ, minQ);

    return buffer;
}
RString Scene::GetFrameRateText() const
{
    char buffer[256];
    float minFPS = 1000 / _maxTargetFrameDuration;
    float maxFPS = 1000 / _minTargetFrameDuration;

    snprintf(buffer, sizeof(buffer), "%.0f..%.0f", minFPS, maxFPS);

    return buffer;
}
void Scene::LoadConfig()
{
    RString name = Poseidon::GetUserParams();

    ParamFile cfg;
    cfg.Parse(name);
    if (cfg.FindEntry("frameRate"))
    {
        float value = cfg >> "frameRate";
        SetFrameRateSettings(value);
    }
    else
    {
        SetFrameRateSettings(15);
    }
    if (cfg.FindEntry("visualQuality"))
    {
        float value = cfg >> "visualQuality";
        SetQualitySettings(value);
    }
    else
    {
        const float qFactor = ENGINE_CONFIG.lodCoef * 2 / GEngine->Width();

        SetQualitySettings(qFactor);
    }

    if (cfg.FindEntry("objectShadows"))
    {
        bool value = cfg >> "objectShadows";
        SetObjectShadows(value);
    }
    if (cfg.FindEntry("vehicleShadows"))
    {
        bool value = cfg >> "vehicleShadows";
        SetVehicleShadows(value);
    }
    if (cfg.FindEntry("cloudlets"))
    {
        bool value = cfg >> "cloudlets";
        SetCloudlets(value);
    }
    if (cfg.FindEntry("viewDistance"))
    {
        float value = cfg >> "viewDistance";
        SetPreferredViewDistance(value);
    }
    if (cfg.FindEntry("terrainGrid"))
    {
        float value = cfg >> "terrainGrid";
        SetPreferredTerrainGrid(value);
    }

    SetPreferredViewDistance(GetSelectedPreferredViewDistance());
}

void Scene::SaveConfig() const
{
    if (!IsOutOfMemory())
    {
        RString name = Poseidon::GetUserParams();

        ParamFile cfg;
        cfg.Parse(name);
        cfg.Add("frameRate", GetFrameRateSettings());
        cfg.Add("visualQuality", GetQualitySettings());
        cfg.Add("objectShadows", GetObjectShadows());
        cfg.Add("vehicleShadows", GetVehicleShadows());
        cfg.Add("cloudlets", GetCloudlets());
        cfg.Add("viewDistance", GetSelectedPreferredViewDistance());
        cfg.Add("terrainGrid", GetPreferredTerrainGrid());

        cfg.Save(name);
    }
}

extern bool ObjViewer;

namespace Poseidon
{
bool EnableObjOcc = true;
} // namespace Poseidon

#include <Poseidon/World/Terrain/Occlusion.hpp>

namespace Poseidon
{
SRef<Occlusion>& GetOcclusions()
{
    static SRef<Occlusion> Occlusions = new Occlusion(256, 256);
    return Occlusions;
}
} // namespace Poseidon

void Scene::BeginObjects()
{
    // mark all objects and not in list:
    for (int i = 0; i < _drawObjects.Size(); i++)
    {
        _drawObjects[i]->notUsed = true;
    }

    _shadowCache.CleanUp(); // remove old shadows
    GetOcclusions()->Clear();

#if _ENABLE_CHEATS
#if !DENSITY_LOD
    auto& input = InputSubsystem::Instance();
    const int FRStep = 1;
    if (input.GetCheat1ToDo(SDL_SCANCODE_LEFTBRACKET))
    {
        float val = GetFrameRateSettings() - FRStep;
        saturateMax(val, 5);
        SetFrameRateSettings(val);
        GEngine->ShowMessage(500, "FPS@%s", (const char*)GetFrameRateText());
    }
    if (input.GetCheat1ToDo(SDL_SCANCODE_RIGHTBRACKET))
    {
        float val = GetFrameRateSettings() + FRStep;
        saturateMin(val, 100);
        SetFrameRateSettings(val);
        GEngine->ShowMessage(500, "FPS@%s", (const char*)GetFrameRateText());
    }

    if (input.GetCheat2ToDo(SDL_SCANCODE_LEFTBRACKET))
    {
        float val = GetQualitySettings() / 1.2f;
        saturate(val, 0.001, 1000);
        SetQualitySettings(val);
        GEngine->ShowMessage(500, "LOD@%s", (const char*)GetQualityText());
    }
    if (input.GetCheat2ToDo(SDL_SCANCODE_RIGHTBRACKET))
    {
        float val = GetQualitySettings() * 1.2f;
        saturate(val, 0.001, 1000);
        SetQualitySettings(val);
        GEngine->ShowMessage(500, "LOD@%s", (const char*)GetQualityText());
    }
#endif
#endif

    if (ObjViewer)
    {
        _lodInvWidth = ENGINE_CONFIG.lodCoef * 2 / (GEngine->Width());
    }

    if (_camera)
    {
        _camera->Adjust(GEngine);
    }
    else
    {
        Fail("No camera.");
    }

    // precalculate shadow properties - constant precalculation
    float addLightsFactor = GScene->MainLight()->NightEffect();
    float skyCoef = floatMin(GScene->GetLandscape()->SkyThrough(), 0.6);
    float shadowFactor = skyCoef * 0.3 + 0.1;
    int shadowFactorI = toIntFloor(shadowFactor * (1 - addLightsFactor) * 255);
    GEngine->SetShadowFactor(shadowFactorI);
}

DEFINE_FAST_ALLOCATOR(SortObject)

#define MaxDistance2(obj) (Square(ENGINE_CONFIG.objectsZ))

void Scene::ObjectForDrawing(Object* obj, int forceLOD, ClipFlags clip)
{
    LODShapeWithShadow* shape = obj->GetShapeOnPos(obj->Position());

    // some objects may be trivially clipped
    // get rid of them ...
    bool invisible = false;
    Vector3Val pos = obj->Position();
    if (!shape)
    {
        return; // nothing to draw
    }
    float radius = obj->GetRadius();
    // get object position in clipping coordinates

    if (obj != GWorld->CameraOn())
    {
        if (_camera->IsClipped(pos, radius, 1))
        {
            // check if shadow is enabled
            if (shape->Special() & NoShadow)
            {
                return;
            }
            // some invisible objects have visible shadows
            // simple test - shadow possible
            if (_camera->IsClipped(pos, radius + 50, 1))
            {
                return;
            }

            Vector3 objTop = obj->Position() + Vector3(0, radius, 0);
            Vector3 shadowPos = objTop;

            // estimate shadow position
            if (!ShadowPos(objTop, shadowPos, _mainLight))
            {
                return;
            }
            if (_camera->IsClipped(shadowPos, 0.5f, 1))
            {
                // shadow of object top is clipped
                // check whole shadow shape
                // estimate shadow radius and shadow center
                float distTopBot = shadowPos.Distance(pos) * 0.5f;
                Vector3 shadowCenter = (shadowPos + pos) * 0.5f;
                float shadowRadius = floatMax(distTopBot, radius);
                if (_camera->IsClipped(shadowCenter, shadowRadius, 1))
                {
                    return;
                }
            }
            invisible = true;
        }
    }

    // calculate distance
    float dist2 = _camera->Position().Distance2Inline(pos);

#if DENSITY_LOD
    // if object is too far, we ignore its shadow
    if (dist2 > Square(ENGINE_CONFIG.objectsZ))
        return;

    // if object is smaller than 1 pixel, do not draw it
    // it would cause aliasing artifacts
    float areaK = _camera->InvLeft() * _camera->InvTop() * GEngine->Width() * GEngine->Height();
    if (Square(radius * 2) * areaK < dist2)
    {
        return;
    }
#endif

    if ((shape->GetAndHints() & ClipFogMask) == ClipFogShadow && (shape->GetOrHints() & ClipFogMask) == ClipFogShadow)
    {
        if (dist2 >= Square(_shadowFogMaxRange + radius))
        {
            return;
        }
    }

    SortObject* sObj = obj->GetInList();
    if (!sObj)
    {
        int index = _drawObjects.Add(new SortObject);
        sObj = _drawObjects[index];

        sObj->object = obj;
        sObj->shape = shape;
        sObj->radius = radius;
        obj->SetInList(sObj); // this object is in list
    }

    if (invisible)
    {
        sObj->forceDrawLOD = LOD_INVISIBLE;
    }
    else
    {
        sObj->forceDrawLOD = forceLOD;
    }

    // alpha-pass sort key: far extent (planar camera-space depth + radius), so the
    // object's depth-writing blend sections draw before interpenetrating dust
    sObj->zCoord = AlphaSort::AlphaObjectDepth((ScaledInvTransform() * pos).Z(), radius);

    // if object is near we use nearest distance instead of center distance
    // this avoid degenerate LODs when beign near
    // if radius is 0.25 of distance, it is considered significant
    if (Square(radius) > dist2 * Square(0.25))
    {
        float dist = dist2 * InvSqrt(dist2);
        float distNear = dist - radius;
        saturateMax(distNear, 0);
        dist2 = Square(distNear);
    }

#if DENSITY_LOD
    sObj->shadowLOD = -1; // override with autodetection
    sObj->drawLOD = -1;   // override with autodetection
#else
    sObj->shadowLOD = LOD_INVISIBLE; // override with autodetection
    sObj->drawLOD = LOD_INVISIBLE;   // override with autodetection
#endif
    sObj->distance2 = dist2;
    sObj->passNum = -1; // noninit

    sObj->orClip = clip;
    sObj->notUsed = false;
}

#define DRAW_OBJS 1

void Scene::CloudletForDrawing(Object* obj)
{
    if (!GetCloudlets())
    {
        return;
    }
#if DRAW_OBJS
    // some objects may be trivially clipped
    // get rid of them ...
    Vector3Val pos = obj->Position();
    float radius = obj->GetRadius();

    // perform clip test
    if (_camera->IsClipped(pos, radius, 1))
    {
        return;
    }

    Vector3 cPos = GScene->ScaledInvTransform() * pos;

    // camera plane clip test
    // perform more distant clipping than normal
    // in case of real 3d object peform normal clipping
    float nearest = _camera->Near() * obj->CloudletClippingCoef();
    if (cPos.Z() < nearest)
    {
        return;
    }

    float dist2 = _camera->Position().Distance2Inline(pos);

    // estimate area
    float size2 = Square(radius * GEngine->Width() * _camera->InvLeft());
    if (size2 < dist2)
    {
        return;
    }

    SortObject* sObj = obj->GetInList();
    if (!sObj)
    {
        int index = _drawObjects.Add(new SortObject);
        sObj = _drawObjects[index];

        sObj->object = obj;
        sObj->radius = radius;
        sObj->shape = obj->GetShape();
        obj->SetInList(sObj); // this object is in list
                              // new object - never occluded?
    }
    sObj->object = obj;
    sObj->drawLOD = 0;
    sObj->forceDrawLOD = 0;
    sObj->shadowLOD = LOD_INVISIBLE;
    sObj->distance2 = dist2;
    sObj->zCoord = cPos.Z(); // alpha-pass sort key: centre camera-space depth (cPos computed above)
    sObj->radius = radius;
    sObj->passNum = 2; // all cloudlets drawn in alpha pass
    sObj->notUsed = false;
#endif
}

void Scene::ObjectForDrawing(Object* obj)
{
    // used for drawdiags and volume lights
    ObjectForDrawing(obj, -1, ClipAll);
}
