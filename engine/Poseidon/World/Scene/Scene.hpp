#pragma once

#include <Poseidon/Core/Types.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>

#define HORIZONT_Z ENGINE_CONFIG.horizontZ
#define OBJECT_Z ENGINE_CONFIG.objectsZ

#define MIN_FOG (200.0f)
#define MAX_FOG (HORIZONT_Z - 20)

#define MIN_SHADOWFOG (100.0f)
#define MAX_SHADOWFOG (ENGINE_CONFIG.shadowsZ)

#include <Poseidon/Foundation/Memory/MemFreeReq.hpp>

namespace Poseidon
{
const float CloudScale = 0.1;

const float MinSkyFog = 4000.0f * CloudScale;
const float MaxSkyFog = 19000.0f * CloudScale;

#define LEN_FOG_TABLE 256
class FogFunction
{
  private:
    byte _fog[LEN_FOG_TABLE];
    float _start2, _end2;
    float _divisor;

  public:
    FogFunction();
    void Set(float start, float end, float (*function)(float distRel, float start, float end));
    int operator()(float distSquare) const; // avoid partial stall
};

class SortObject : public RefCount
{
  public:
    Ref<Object> object;        // must outlive drawing; Ref guarantees the shape exists
    LODShapeWithShadow* shape; // randomized shape

    signed char drawLOD, shadowLOD;
    signed char passNum;
    signed char forceDrawLOD; // forced draw LOD

    unsigned char orClip; // or clip flags only - andClip would always be 0

    bool notUsed; // not used when list was created - delete it

    float radius; // bounding sphere radius

    float distance2;

    float zCoord; // alpha-pass sort key: camera-space depth; see AlphaSortOrder.hpp

    USE_FAST_ALLOCATOR
};

typedef RefArray<SortObject> SortObjectList;

class RemmemberShadow : public RefCount, public CLRefLink
{
    friend class ShadowCache;

  private:
    Ref<Shape> _shadow;
    OLink<Object> _object;
    Vector3 _lightDir;
    Matrix4 _objectPos;
    int _level; // which LOD of object is used
    Foundation::Time _lastUsed;
    bool _splitOnly; // shadow cache is also used to contain split surfaces

  public:
    RemmemberShadow();
    // init shadow or split to fit on surface
    void Init(Object* object, Vector3Par lightDir, int level, Matrix4Par pos, bool splitOnly = false);
    bool IsShadow(Object* object, int level) const;
    Object* GetObject() const { return _object; }
    Vector3Val LightDir() const { return _lightDir; }
    Matrix4Val ObjectPos() const { return _objectPos; }

    USE_FAST_ALLOCATOR
};

class ShadowCache : public Foundation::MemoryFreeOnDemandHelper
{
  private:
    CLRefList<RemmemberShadow> _data;

  public:
    ShadowCache();
    ~ShadowCache() override;

    Ref<Shape> Shadow(Object* object, Vector3Par lightDir, int level, Matrix4Par pos, bool splitOnly = false);
    void ShadowChanged(Object* obj);
    void Clear();
    void CleanUp();

    size_t FreeOneItem() override;
    float Priority() override;

    // memory-budget observability (dev panel). ~10 KB per remembered shadow,
    // matching ShadowMemSize in Shadow.cpp.
    const char* DomainName() const override { return "Shadows.Cache"; }
    size_t HeldBytes() const override { return (size_t)_data.Size() * (10 * 1024); }
};

enum PreloadedShape
{
    CobraLight,
    SphereLight,
    HalfLight,
    Marker,
    CraterShell,
    SlopBlood,
    CloudletBasic,
    CloudletFire,
    CloudletWater,
    Cloud1,
    Cloud2,
    Cloud3,
    Cloud4,
    CinemaBorder,

    FootStepL,
    FootStepR,

    ForceArrowModel,
    SphereModel,
    RectangleModel,
    BulletLine,

    MaxPreloadedShape
};

typedef Ref<Light> ActiveLightPointer;

class LightList : public FindArray<ActiveLightPointer, Foundation::MemAllocSS>
{
  public:
    LightList(bool staticStorage = false);
    LightList(const LightList& src);
};

class PreloadedTextures
{
    RefArray<Texture> _data;

  public:
    PreloadedTextures();
    ~PreloadedTextures();

    void Preload(bool all);
    void Clear();

    Texture* New(RStringB name);                   // make texture permanent
    Texture* New(::Poseidon::PreloadedTexture id); // predefined texture ids
};

extern PreloadedTextures GPreloadedTextures;

// Scene graph management: LOD and light management.
class Scene
{
  private:
    Color _constantColor;
    Color _skyColor;
    float _constantFog;

    Ref<Texture> _skyTexture;

    Ref<LODShapeWithShadow> _preloaded[MaxPreloadedShape];

    Camera* _camera;
    LightSun* _mainLight;

    FindArray<Link<Light>> _lights;
    LightList _aLights;

    Ref<Object> _collisionStar;

    // fog functions for different types of objects
    FogFunction _fog, _skyFog, _shadowFog, _tacticalFog;
    float _tacticalVisibility;                    // AI sensors
    float _rainRange;                             // display boundaries main control
    float _fogMaxRange, _fogMinRange;             // display boundaries
    float _shadowFogMaxRange, _shadowFogMinRange; // display boundaries

    mutable float _lodInvWidth;

    float _frameRateSettings;
    float _qualitySettings;

    mutable Foundation::UITime _lastScaleBetterTime; // avoid oscillation
    mutable Foundation::UITime _lastScaleWorseTime;
    mutable float _maxTargetFrameDuration;
    mutable float _minTargetFrameDuration;

    mutable float _minLodInvWidth; // visual quality limits
    mutable float _maxLodInvWidth;

    enum
    {
        NStoreComplexities = 4
    };

    mutable int _lastComplexity[NStoreComplexities]; // complexity history

    mutable SortObjectList _drawObjects;
    mutable SortObjectList _drawMergers;

    mutable ShadowCache _shadowCache;
    SRef<Landscape> _landscape; // only pointer to landscape

    bool _objectShadows, _vehicleShadows, _cloudlets;
    float _objectLODBias = 1.0f;
    float _preferredTerrainGrid;
    float _preferredViewDistance;

  public:
    Scene();
    void Init(Engine* engine, Landscape* landscape);
    void ResetFog();
    void CleanUp();
    ~Scene();

    bool GetObjectShadows() const { return _objectShadows; }
    bool GetVehicleShadows() const { return _vehicleShadows; }
    bool GetCloudlets() const { return _cloudlets; }

    float GetMinimalTerrainGrid() const;
    float GetPreferredTerrainGrid() const { return _preferredTerrainGrid; }
    float GetPreferredViewDistance() const { return _preferredViewDistance; }

    void SetPreferredTerrainGrid(float x);
    void SetPreferredViewDistance(float x);

    void SetObjectShadows(bool set = true);
    void SetVehicleShadows(bool set = true);
    void SetCloudlets(bool set = true);

    // Object LOD bias driving entity-LOD selection — multiplier on the
    // entity's projected screen size before LOD lookup.  >1 picks a
    // higher LOD (more detail) at a given distance; <1 picks lower.
    // Range clamped 0.25..4.0 in setter.  Read by RenderShape /
    // LODSelect path; default 1.0 (no bias).
    void SetObjectLODBias(float bias);
    float GetObjectLODBias() const { return _objectLODBias; }

    Camera* GetCamera() { return _camera; }
    const Camera* GetCamera() const { return _camera; }
    void SetCamera(const Camera& camera);

    void SetConstantColor(ColorVal color) { _constantColor = color; }
    ColorVal GetConstantColor() const { return _constantColor; }

    void SetConstantFog(float fog) { _constantFog = fog; }
    float GetConstantFog() const { return _constantFog; }

    const Matrix4& ScaledInvTransform() const;
    const Matrix3& CamNormalTrans() const;
    const Matrix4& CamInvTrans() const;

    Texture* SkyTexture() const { return _skyTexture; }

    void ResetLights();
    void AddLight(Light* light);

    Light* GetLight(int i) const { return _lights[i]; }
    int NLights() const { return _lights.Size(); }

    void SelectActiveLights(Object* dimmed);
    void SetActiveLights(const LightList& lights);
    const LightList& ActiveLights() const { return _aLights; }

    // Select light affecting given position
    const LightList& SelectLights(Vector3Par pos, float radius, LightList& work); // may return work or something else

    // Select light affecting given object
    const LightList& SelectLights(Matrix4Par objPos, const Object* object, int level, LightList& work);

    LightSun* MainLight() const { return _mainLight; }
    void SetMainLight(LightSun* light) { _mainLight = light; }
    void MainLightChanged(); // fog/light color has been changed

    void SetTacticalVisibility(float tacVis, float rainRange);
    float GetTacticalVisibility() const { return _tacticalVisibility; }

    float GetLodInvWidth() const { return _lodInvWidth; }
    float GetSmokeGeneralization() const;

    float GetFrameRateSettings() const { return _frameRateSettings; }
    void SetFrameRateSettings(float val);
    RString GetFrameRateText() const;

    float GetQualitySettings() const { return _qualitySettings; }
    void SetQualitySettings(float val);
    RString GetQualityText() const;

    void LoadConfig();
    void SaveConfig() const;

    float GetFogMaxRange() const { return _fogMaxRange; }
    float GetFogMinRange() const { return _fogMinRange; }
    float GetShadowFogMaxRange() const { return _shadowFogMaxRange; }
    float GetShadowFogMinRange() const { return _shadowFogMinRange; }

    int TacticalFog8(float distSquare) const { return _tacticalFog(distSquare); }
    int Fog8(float distSquare) const { return _fog(distSquare); }
    int ShadowFog8(float distSquare) const { return _shadowFog(distSquare); }
    int SkyFog8(float distSquare) const { return _skyFog(distSquare); }

    void CalculateSkyColor(Texture* texture);

    Landscape* GetLandscape() const { return _landscape; }
    ShadowCache& GetShadowCache() const { return _shadowCache; }

    int LevelFromDistance2(LODShape* shape, float distance2, float oScale, Vector3Par direction,
                           Vector3Par viewDirection);
    int LevelShadowFromDistance2(LODShape* shape, float distance2, float oScale, Vector3Par direction,
                                 Vector3Par viewDirection);

    void AdjustComplexity();

    int AdjustComplexity(SortObjectList& objs);
    int AdjustShadowComplexity(SortObjectList& objs);

    void BeginObjects();
    void ObjectForDrawing(Object* obj, int forceLOD, ClipFlags clip);
    void CloudletForDrawing(Object* obj);
    void ObjectForDrawing(Object* obj);

    void EndObjects(); // sort all objects
    void DrawReflections(const WaterLevel& water);
    void DrawObjectsAndShadowsPass1();
    void DrawObjectsAndShadowsPass2();
    // Shadow-map depth pass (off by default): collect the visible casters and render
    // the cascade depth maps from the sun.  Lives in SceneShadowPass.cpp to keep
    // Scene.cpp under the file-size limit.  Called from Pass2 when shadow maps are on.
    void RenderShadowMapDepthPass(int nDraw);
    void DrawObjectsAndShadowsPass3(); // last draw cockpits
    void ObjectsDrawn();               // release all temporary information

    // light color and position
    void DrawFlare(ColorVal color, Vector3Par pos, bool secondary = true);
    void DrawFlares();

    void DrawRainLevel(float alpha, float yDensity, float xOffset, float yOffset, float z);
    void DrawRain();

    void DrawDiagModel(Vector3Par pos, LODShapeWithShadow* shape, float size = 0.1, PackedColor color = PackedWhite);
    void DrawCollisionStar(Vector3Par pos, float size = 0.1, PackedColor color = PackedWhite);
    void DrawVolumeLight(LODShapeWithShadow* shape, PackedColor color, const Frame& pos, float size);

    bool ShadowPos(Vector3Par pos, Vector3& aprox, LightSun* light);

    LODShapeWithShadow* ForceArrow() const { return Preloaded(ForceArrowModel); }

    LODShapeWithShadow* Preloaded(PreloadedShape type) const
    {
        PoseidonAssert(type < MaxPreloadedShape);
        return _preloaded[type];
    }
    void SetPreloaded(PreloadedShape type, LODShapeWithShadow* shape)
    {
        PoseidonAssert(type < MaxPreloadedShape);
        _preloaded[type] = shape;
    }
    void SetCollisionStar(Object* obj) { _collisionStar = obj; }
    bool HasPreloaded() const { return _preloaded[CraterShell] != nullptr; }
    Texture* Preloaded(::Poseidon::PreloadedTexture type) const { return GPreloadedTextures.New(type); }
    Texture* Preloaded(RStringB name) const { return GPreloadedTextures.New(name); }

    void DrawExShadow(SortObject* oi); // exact shadow casting
};

extern Scene* GScene;
#define GLOB_SCENE (GScene)

#define IS_SHADOW_VEHICLE (GScene ? GScene->GetVehicleShadows() : true)
#define IS_SHADOW_OBJECT (GScene ? GScene->GetObjectShadows() : true)

}  // namespace Poseidon
