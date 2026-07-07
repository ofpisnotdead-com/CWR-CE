#include <Poseidon/Core/Config/EngineConfig.hpp>

#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/ArcadeTemplate.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Terrain/Visibility.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <SDL3/SDL_scancode.h>
#include <Poseidon/Game/Commands/GameStateExt.hpp>

#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <cstdlib>

#include <Poseidon/Asset/Formats/Common/FormatDetector.hpp>
#include <cmath>
#include <stdexcept>
#include <string>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using namespace Poseidon;
namespace Poseidon
{
using Poseidon::Asset::Formats::FormatInfo;
using Poseidon::Asset::Formats::P3DFormatDetector;
} // namespace Poseidon
#include <Poseidon/Asset/Formats/P3D/ODOLLoader.hpp>
#include <Poseidon/Asset/Formats/P3D/MLODLoader.hpp>
#include <Poseidon/World/Model/ShapeAdapter.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/World/Simulation/Animation/RtAnimation.hpp>
#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <PoseidonGL33/TextureGL33.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Input/ViewerController.hpp>
#include <Poseidon/Graphics/Cursor/ViewerCursorOverlay.hpp>
extern SRef<VehicleWithAI> GDummyVehicle;
void AIGlobalInit();

class ObjectViewer : public Vehicle
{
    typedef Vehicle base;

    float _animPhase, _animSpeed;

    // External .rtm (--anim) takes priority over shape->SetPhase() in Animate().
    Ref<AnimationRT> _externalAnim;
    Ref<Skeleton> _externalSkeleton;
    WeightInfo _externalWeights;

  public:
    ObjectViewer(LODShapeWithShadow* shape, RString name);
    ~ObjectViewer() override;

    void Simulate(float deltaT, SimulationImportance prec) override;
    void Draw(int forceLOD, ClipFlags clipFlags, const FrameBase& pos) override;

    void Animate(int level) override;
    void Deanimate(int level) override;

    bool IsAnimated(int level) const override { return true; }
    bool IsAnimatedShadow(int level) const override { return true; }

    float GetPhase() const
    {
        if (_animSpeed < 0.01)
        {
            return _animPhase;
        }
        else
        {
            return -1;
        }
    }
    void SetPhase(float phase) { _animPhase = phase, _animSpeed = 0; }

    void SetExternalAnimation(const char* animPath);

    // Plain accessors for TickViewerControls — bypasses Vehicle's input plumbing intentionally.
    float AnimPhase() const { return _animPhase; }
    void SetAnimPhase(float p) { _animPhase = p; }
    float AnimSpeed() const { return _animSpeed; }
    void SetAnimSpeed(float s) { _animSpeed = s; }
};

ObjectViewer::ObjectViewer(LODShapeWithShadow* shape, RString name) : base(shape, VehicleTypes.New(name), -1)
{
    _animPhase = 0;
    _animSpeed = 0.5;
}

ObjectViewer::~ObjectViewer() = default;

class CameraViewer : public Vehicle
{
    OLink<ObjectViewer> _tgt;
    typedef Vehicle base;

  public:
    CameraViewer(ObjectViewer* tgt, LODShapeWithShadow* shape, RString name);
    ~CameraViewer() override;

    ObjectViewer* GetTarget() const { return _tgt; }
    void SetTarget(ObjectViewer* tgt) { _tgt = tgt; }

    void Simulate(float deltaT, SimulationImportance prec) override;
    void Draw(int forceLOD, ClipFlags clipFlags, const FrameBase& pos) override;

    void LimitVirtual(CameraType camType, float& heading, float& dive, float& fov) const override;
    void InitVirtual(CameraType camType, float& heading, float& dive, float& fov) const override;
    bool IsContinuous(CameraType camType) const override { return true; }
};

CameraViewer::CameraViewer(ObjectViewer* tgt, LODShapeWithShadow* shape, RString name)
    : base(shape, VehicleTypes.New(name), -1), _tgt(tgt)
{
}

CameraViewer::~CameraViewer() = default;

static Vector3 InitCamPos = Vector3(0, 0, -10);

static float ViewerAutoFrameDistance(float radius)
{
    constexpr float kFovTopRad = 0.7f * 0.75f;
    constexpr float kFillFraction = 0.45f;
    float dist = radius / (kFillFraction * tanf(kFovTopRad));
    if (dist < 0.25f)
        dist = 0.25f;
    if (dist > 200.0f)
        dist = 200.0f;
    return dist;
}

// No-op: all state changes come from World::TickViewerControls; keeping the
// override prevents the base Vehicle::Simulate from running vehicle physics.
void CameraViewer::Simulate(float deltaT, SimulationImportance prec) {}

void CameraViewer::Draw(int forceLOD, ClipFlags clipFlags, const FrameBase& pos) {}

#define TEST_TEXT 0

#if TEST_TEXT
#include <Poseidon/Graphics/Core/Engine.hpp>

#endif

void ObjectViewer::SetExternalAnimation(const char* animPath)
{
    if (!animPath || !animPath[0])
    {
        _externalAnim = nullptr;
        _externalSkeleton = nullptr;
        return;
    }
    _shape->AllowAnimation();
    _externalSkeleton = new Skeleton();
    AnimationRTName name;
    name.name = animPath;
    name.skeleton = _externalSkeleton;
    _externalAnim = new AnimationRT(name, false);
    _externalAnim->Prepare(_shape, _externalSkeleton, _externalWeights, false);
    LOG_INFO(Core, "Viewer: bound external animation {}", animPath);
}

void ObjectViewer::Animate(int level)
{
    if (_externalAnim)
    {
        _externalAnim->Apply(_externalWeights, _shape, level, _animPhase);
        return;
    }
    Shape* shape = _shape->Level(level);
    if (shape->IsAnimated())
    {
        shape->SetPhase(_animPhase, 0);
    }
}

void ObjectViewer::Deanimate(int level) {}

void ObjectViewer::Draw(int forceLOD, ClipFlags clipFlags, const FrameBase& pos)
{
    base::Draw(forceLOD, clipFlags, pos);
}

// Advances animation only; input handling is in World::TickViewerControls.
void ObjectViewer::Simulate(float deltaT, SimulationImportance prec)
{
    _animPhase += deltaT * _animSpeed;
    _animPhase = fastFmod(_animPhase, 1);
}

void CameraViewer::LimitVirtual(CameraType camType, float& heading, float& dive, float& fov) const
{
    heading = AngleDifference(heading, 0);
    saturate(dive, -H_PI * 0.3, +H_PI * 0.3);
    switch (camType)
    {
        case CamInternal:
            saturate(fov, 0.01, 2);
            break;
        default:
            base::LimitVirtual(camType, heading, dive, fov);
            break;
    }
}

void CameraViewer::InitVirtual(CameraType camType, float& heading, float& dive, float& fov) const
{
    base::InitVirtual(camType, heading, dive, fov);
    // Without this, fov defaults to 0 and LimitVirtual clamps it to 0.01 — an
    // extreme telephoto (~0.6° wide). 0.7 rad ≈ 40° matches the engine default.
    if (camType == CamInternal && fov <= 0.0f)
        fov = 0.7f;
}

extern bool ObjViewer;

void World::SetViewerPhase(float time)
{
    Object* cobj = _cameraOn;
    ObjectViewer* oldViewer = dynamic_cast<ObjectViewer*>(cobj);
    CameraViewer* camViewer = dynamic_cast<CameraViewer*>(cobj);
    if (camViewer)
    {
        oldViewer = camViewer->GetTarget();
    }
    if (oldViewer)
    {
        oldViewer->SetPhase(time);
    }
}

float World::GetViewerPhase() const
{
    Object* cobj = _cameraOn;
    ObjectViewer* oldViewer = dynamic_cast<ObjectViewer*>(cobj);
    CameraViewer* camViewer = dynamic_cast<CameraViewer*>(cobj);
    if (camViewer)
    {
        oldViewer = camViewer->GetTarget();
    }
    if (oldViewer)
    {
        return oldViewer->GetPhase();
    }
    return 0;
}

void World::ReloadViewerCore(LODShapeWithShadow* shape, const char* classDesc)
{
    ObjViewer = true;

    const char* viewClass = "";
    if (shape->NLevels() > 1)
        viewClass = shape->PropertyValue(".viewclass");

    Object* obj = _cameraOn;
    VehicleWithAI* camAI = dyn_cast<VehicleWithAI>(obj);
    const char* camName = camAI ? camAI->GetType()->GetName() : "";
    if (!strcmpi(camName, viewClass))
    {
        Object* cobj = _cameraOn;
        ObjectViewer* oldViewer = dynamic_cast<ObjectViewer*>(cobj);
        CameraViewer* camViewer = dynamic_cast<CameraViewer*>(cobj);
        if (camViewer)
            oldViewer = camViewer->GetTarget();
        if (oldViewer)
        {
            Frame oldPos = *oldViewer;
            DeleteVehicle(oldViewer);
            Ref<ObjectViewer> newObj = new ObjectViewer(shape, "ObjView");
            newObj->SetTransform(oldPos.Transform());
            AddVehicle(newObj);
            Object* cobj2 = newObj;
            GetGameState()->VarSet("bis_buldozer_zero", GameValueExt(cobj2), true);
            if (camViewer)
                camViewer->SetTarget(newObj);
            else
            {
                _cameraOn = cobj2;
                GetGameState()->VarSet("bis_buldozer_cursor", GameValueExt(cobj2), true);
                _camType = _camTypeMain = CamInternal;
            }
            return;
        }
    }

    Vehicle* camVeh = dynamic_cast<Vehicle*>((Object*)_cameraOn);
    if (camVeh)
        DeleteVehicle(camVeh);
    _vehicles.Clear();

    if (!ENGINE_CONFIG.noLandscape)
        SwitchLandscape(GetWorldName("Intro"));
    else
        Reset();

    float mid = GLandscape->GetLandRange() * GLandscape->GetLandGrid() * 0.5;
    Vector3 cPos(mid, 50, mid);

    // dist = r / (F · tan(fovTop)): fov=0.7 rad, topFOV≈0.75 → half-angle 0.525 rad.
    // F=0.45 leaves margin and keeps the floor grid visible. Min 0.25 m clears the near plane.
    float radius = shape->BoundingSphere();
    float dist = ViewerAutoFrameDistance(radius);
    Vector3 camOffset = AppConfig::Instance().IsViewerMode() ? Vector3(0, 0, -dist) : InitCamPos;
    // AABB midpoint (not BoundingSphere centroid): for asymmetric
    // models the sphere envelopes all anim frames and may not match
    // the static-pose visual centre.
    Vector3 bCenter = (shape->Min() + shape->Max()) * 0.5f;
    LOG_INFO(Core, "Viewer auto-frame: radius={:.3f} m, distance={:.3f} m", radius, dist);

    if (!*viewClass)
    {
        Ref<ObjectViewer> newObj = new ObjectViewer(shape, "ObjView");
        Ref<CameraViewer> cam = new CameraViewer(newObj, shape, "ObjView");
        // Offset by AABB midpoint so models with off-origin meshes
        // (origin at a wheel / corner) still render centred.
        newObj->SetPosition(cPos - bCenter);
        newObj->SetOrientation(M3Identity);
        cam->SetPosition(cPos + camOffset);
        cam->SetOrientation(M3Identity);
        AddVehicle(newObj);
        GetGameState()->VarSet("bis_buldozer_zero", GameValueExt(newObj.GetRef()), true);
        AddVehicle(cam);
        GetGameState()->VarSet("bis_buldozer_cursor", GameValueExt(cam.GetRef()), true);
        Object* cobj = cam;
        _cameraOn = cobj;
        _camType = _camTypeMain = CamInternal;
        // InitCameraPars runs before this, so InitVirtual's seed is too late — set directly.
        _camFOV[CamInternal] = 0.7f;
        _camFOVWanted[CamInternal] = 0.7f;
        return;
    }

    const ParamEntry& vehicles = Pars >> "CfgVehicles";
    if (!vehicles.FindEntry(viewClass))
        return;

    VehicleTypeBank tempBank;
    VehicleNonAIType* vType = tempBank.New(viewClass);
    VehicleType* type = dynamic_cast<VehicleType*>(vType);
    if (!type)
        return;

    type->AttachShape(shape);

    ArcadeUnitInfo info;
    info.side = TWest;
    info.vehicle = type->GetName();
    info.type = type;
    info.position = cPos;
    TargetSide side = info.side;

    _mode = GModeArcade;
    _endMission = EMContinue;
    EnableRadio();
    Glob.header.playerSide = side;

    if (_ui)
        _ui->ResetHUD();
    if (!GDummyVehicle)
        GDummyVehicle = NewVehicle("PaperCar");

    AIGlobalInit();

    switch (side)
    {
        case TWest:
            _westCenter = new AICenter(TWest, AICMArcade);
            _westCenter->InitPreview(info);
            break;
        case TEast:
            _eastCenter = new AICenter(TEast, AICMArcade);
            _eastCenter->InitPreview(info);
            break;
        case TCivilian:
            _civilianCenter = new AICenter(TCivilian, AICMArcade);
            _civilianCenter->InitPreview(info);
            break;
        case TGuerrila:
            _guerrilaCenter = new AICenter(TGuerrila, AICMArcade);
            _guerrilaCenter->InitPreview(info);
            break;
    }

    if (_eastCenter)
        _eastCenter->InitSensors();
    if (_westCenter)
        _westCenter->InitSensors();
    if (_guerrilaCenter)
        _guerrilaCenter->InitSensors();
    if (_civilianCenter)
        _civilianCenter->InitSensors();
    GetSensorList()->UpdateAll();
    if (_eastCenter)
        _eastCenter->InitSensors(true);
    if (_westCenter)
        _westCenter->InitSensors(true);
    if (_guerrilaCenter)
        _guerrilaCenter->InitSensors(true);
    if (_civilianCenter)
        _civilianCenter->InitSensors(true);

    _sensorList->UpdateAll();
    VehicleWithAI* ai = dyn_cast<VehicleWithAI, Object>(_cameraOn);
    if (_ui && ai)
        _ui->ResetVehicle(ai);
}

void World::ReloadViewer(void* buf, int len, const char* classDesc)
{
    QIStream sFile((char*)buf, len);
    Ref<LODShapeWithShadow> shape = new LODShapeWithShadow(sFile, true);

    const char* viewClass = (shape->NLevels() > 1) ? shape->PropertyValue(".viewclass") : "";
    VehicleWithAI* camAI = dyn_cast<VehicleWithAI>((Object*)_cameraOn);
    if (camAI && *viewClass && !strcmpi(camAI->GetType()->GetName(), viewClass))
    {
        sFile.seekg(0, QIOS::beg);
        const_cast<VehicleType*>(camAI->GetType())->ReloadShape(sFile);
        return;
    }

    if (*viewClass)
    {
        const ParamEntry& vehicles = Pars >> "CfgVehicles";
        if (vehicles.FindEntry(viewClass) && (vehicles >> viewClass >> "reversed").GetInt() == 0)
            shape = new LODShapeWithShadow(sFile, false);
    }

    ReloadViewerCore(shape, classDesc);
}

void World::ReloadViewer(const char* filename, const char* classDesc)
{
    auto formatInfo = P3DFormatDetector::DetectFormat(filename);
    if (!formatInfo.isSupported)
    {
        LOG_WARN(Core, "Unsupported P3D format for {}: {}", filename, formatInfo.errorMessage);
        throw std::runtime_error(formatInfo.errorMessage);
    }

    Poseidon::Model::Model model;
    if (formatInfo.signature == "ODOL")
        model = Poseidon::Asset::Formats::ODOLLoader::load(filename);
    else if (formatInfo.signature == "MLOD")
        model = Poseidon::Asset::Formats::MLODLoader::load(filename);
    else
    {
        LOG_WARN(Core, "Unknown P3D signature '{}' for {}", formatInfo.signature, filename);
        throw std::runtime_error("Unknown P3D signature: " + formatInfo.signature);
    }

    model.compile();

    LODShapeWithShadow* shape = Poseidon::Model::ShapeAdapter::convertToLODShape(model, true);
    if (!shape || shape->NLevels() == 0)
    {
        delete shape;
        throw std::runtime_error("New pipeline produced empty shape for " + std::string(filename));
    }

    LOG_INFO(Core, "New pipeline loaded {} ({} v{}, {} LODs)", filename, formatInfo.signature,
             formatInfo.GetVersionString(), shape->NLevels());

    shape->OptimizeShapes();
    shape->CheckForcedProperties();
    ReloadViewerCore(shape, classDesc);
}

static ObjectViewer* FindActiveObjectViewer(World* w)
{
    if (!w)
        return nullptr;
    Object* cobj = w->CameraOn();
    ObjectViewer* ov = dynamic_cast<ObjectViewer*>(cobj);
    if (ov)
        return ov;
    CameraViewer* cv = dynamic_cast<CameraViewer*>(cobj);
    if (cv)
        return cv->GetTarget();
    return nullptr;
}

void World::StartViewer(const char* modelPath, const char* animPath)
{
    if (!modelPath || !modelPath[0])
    {
        LOG_ERROR(Core, "World::StartViewer: --model is required");
        return;
    }

    // ICursorOverlay lets EngineDrawing call GetCursorOverlay() without IsViewerMode() checks.
    if (!_cursorOverlay)
        _cursorOverlay = new ViewerCursorOverlay();

    ReloadViewer(modelPath, "");
    if (animPath && animPath[0])
        SetViewerAnimation(animPath);

    LOG_INFO(Core, "Viewer started: model={} anim={}", modelPath, animPath ? animPath : "(none)");
}

void World::SetViewerAnimation(const char* animPath)
{
    ObjectViewer* ov = FindActiveObjectViewer(this);
    if (!ov)
    {
        LOG_WARN(Core, "World::SetViewerAnimation: no active ObjectViewer");
        return;
    }
    ov->SetExternalAnimation(animPath);
}

void World::TickViewerControls(float deltaT)
{
    if (!AppConfig::Instance().IsViewerMode())
        return;

    static ViewerController controller;
    ViewerControls c = controller.Poll();
    (void)deltaT;

    Object* cobj = _cameraOn;
    CameraViewer* cam = dynamic_cast<CameraViewer*>(cobj);
    ObjectViewer* obj = dynamic_cast<ObjectViewer*>(cobj);
    if (cam && !obj)
        obj = cam->GetTarget();

    if (obj && cam && (c.translateX != 0.0f || c.translateY != 0.0f))
    {
        const float translateSpeed = 0.003f;
        Vector3 pos = obj->Position();
        pos += c.translateX * translateSpeed * cam->DirectionAside();
        pos += -c.translateY * translateSpeed * cam->DirectionUp();
        obj->SetPosition(pos);
    }

    if (obj && (c.rotateX != 0.0f || c.rotateY != 0.0f))
    {
        const float rotateSpeed = 0.005f;
        float head = -atan2(obj->Direction().X(), obj->Direction().Z()) + c.rotateX * rotateSpeed;
        float dive = -atan2(obj->Direction().Y(), obj->Direction().SizeXZ()) - c.rotateY * rotateSpeed;
        saturate(dive, -H_PI / 2 * 0.9999f, +H_PI / 2 * 0.9999f);
        Matrix3 rot = Matrix3(MRotationY, head) * Matrix3(MRotationX, dive);
        obj->SetOrientation(rot);
    }

    if (cam && (c.panX != 0.0f || c.panY != 0.0f))
    {
        const float panSpeed = 0.005f;
        Vector3 pos = cam->Position();
        pos += -c.panX * panSpeed * cam->DirectionAside();
        pos += c.panY * panSpeed * cam->DirectionUp();
        cam->SetPosition(pos);
    }

    // kZoomPerTick mirrors ApplyViewerControls (unit-tested); both must change together.
    if (cam && obj && c.zoom != 0.0f)
    {
        constexpr float kZoomPerTick = 0.85f;
        constexpr float kZoomMinDist = 0.10f;
        constexpr float kZoomMaxDist = 500.0f;
        Vector3 toCam = cam->Position() - obj->Position();
        float curDist = toCam.Size();
        if (curDist > 1e-4f)
        {
            Vector3 toCamDir = toCam * (1.0f / curDist);
            float ratio = powf(kZoomPerTick, c.zoom);
            float newDist = curDist * ratio;
            saturate(newDist, kZoomMinDist, kZoomMaxDist);
            cam->SetPosition(obj->Position() + toCamDir * newDist);
        }
    }

    if (obj)
    {
        if (c.animScrub != 0.0f)
            obj->SetAnimPhase(fastFmod(obj->AnimPhase() + c.animScrub * deltaT, 1.0f));
        if (c.playPauseAnim)
        {
            if (obj->AnimSpeed() > 0.001f)
                obj->SetAnimSpeed(0.0f);
            else
                obj->SetAnimSpeed(AppConfig::Instance().GetViewerAnimSpeed());
            LOG_INFO(Core, "Viewer animation {} (speed={})", obj->AnimSpeed() > 0.001f ? "playing" : "paused",
                     obj->AnimSpeed());
        }
        if (c.resetAnim)
        {
            obj->SetAnimPhase(0.0f);
            obj->SetAnimSpeed(0.0f);
            LOG_INFO(Core, "Viewer animation reset to phase 0 (paused)");
        }
        if (c.openAnim)
        {
            obj->SetAnimPhase(1.0f);
            obj->SetAnimSpeed(0.0f);
            LOG_INFO(Core, "Viewer animation set to phase 1 (open, paused)");
        }
    }

    if (c.reloadTextures)
        ReloadViewerTextures();
    if (c.resetViewer)
        ResetViewer();
    if (c.toggleCursorLock && _cursorOverlay)
    {
        _cursorOverlay->ToggleLock(GEngine);
        LOG_INFO(Core, "Viewer cursor lock: {}", _cursorOverlay->IsLocked() ? "locked" : "unlocked");
    }
    if (c.exitViewer)
    {
        LOG_INFO(Core, "Viewer: Esc pressed, exiting");
        std::_Exit(0);
    }
    // Consumed by the help-overlay drawer in EngineDrawing.cpp; suppressed until
    // the overlay migrates from polling InputSubsystem directly to ViewerControls.
    (void)c.toggleHelp;
}

Object* World::GetViewerObject() const
{
    Object* cobj = _cameraOn;
    if (!cobj)
        return nullptr;
    CameraViewer* cam = dynamic_cast<CameraViewer*>(cobj);
    if (cam)
        return cam->GetTarget();
    return cobj;
}

void World::DrawViewerSceneAddons()
{
    if (!AppConfig::Instance().IsViewerMode() || !GEngine)
        return;

    // Floor grid — 21×21 1-m squares, every 5th line bolder so the
    // user can read scale at a glance.  Drawn at the same Y as the
    // active object so panning the camera up/down doesn't make the
    // grid drift away.
    Object* cobj = _cameraOn;
    Vector3 ctr = cobj ? cobj->Position() : VZero;
    // Grid sized to the model's bounding sphere — small props get a
    // tight 1-m grid, vehicles get a wider one.  21 lines per axis
    // covers ±10 grid steps from the centre.
    float radius = 0.5f;
    if (auto* shape = cobj ? cobj->GetShape() : nullptr)
        radius = shape->BoundingSphere();
    float kStep = (radius < 1.0f) ? 0.2f : 1.0f;
    const float kHalf = 10.0f * kStep;
    // Names avoid major()/minor() which are macros in <sys/types.h>
    // on FreeBSD (devmajor/devminor extraction).
    const PackedColor minorLine(Color(0.40f, 0.42f, 0.46f, 0.45f));
    const PackedColor majorLine(Color(0.60f, 0.62f, 0.66f, 0.7f));
    // Plane sits just below the model's bounding sphere — keeps the
    // grid out of the model's visual centre but close enough that
    // the eye reads "the model is sitting on this".
    float yPlane = ctr.Y() - radius;
    for (int i = -10; i <= 10; ++i)
    {
        float t = i * kStep;
        bool isMajor = (i == 0) || (i % 5 == 0);
        PackedColor c = isMajor ? majorLine : minorLine;
        GEngine->DrawLine3D(Vector3(ctr.X() - kHalf, yPlane, ctr.Z() + t),
                            Vector3(ctr.X() + kHalf, yPlane, ctr.Z() + t), c, 0);
        GEngine->DrawLine3D(Vector3(ctr.X() + t, yPlane, ctr.Z() - kHalf),
                            Vector3(ctr.X() + t, yPlane, ctr.Z() + kHalf), c, 0);
    }
}

void World::ResetViewer()
{
    Object* cobj = _cameraOn;
    if (!cobj)
        return;
    CameraViewer* cam = dynamic_cast<CameraViewer*>(cobj);
    ObjectViewer* obj = dynamic_cast<ObjectViewer*>(cobj);
    if (cam && !obj)
        obj = cam->GetTarget();
    if (!obj)
        return;

    float mid = GLandscape ? GLandscape->GetLandRange() * GLandscape->GetLandGrid() * 0.5f : 0.0f;
    Vector3 cPos(mid, 50, mid);
    obj->SetPosition(cPos);
    obj->SetOrientation(M3Identity);
    obj->SetAnimPhase(0.0f);
    obj->SetAnimSpeed(AppConfig::Instance().GetViewerAnimSpeed());

    if (cam && obj->GetShape())
    {
        float dist = ViewerAutoFrameDistance(obj->GetShape()->BoundingSphere());
        cam->SetPosition(cPos + Vector3(0, 0, -dist));
        cam->SetOrientation(M3Identity);
    }
    LOG_INFO(Core, "Viewer state reset (F6) — camera + orient + anim back to defaults");
}

void World::ReloadViewerTextures()
{
    if (!GEngine || !GEngine->TextBank())
    {
        LOG_WARN(Core, "World::ReloadViewerTextures: no engine/text-bank yet");
        return;
    }
    // ForceReloadAll is a virtual method on AbstractTextBank with a
    // default no-op; only the GL33 backend implements the destructive
    // drop. Tools that don't link PoseidonGL33 get the no-op.
    GEngine->TextBank()->ForceReloadAll();
    LOG_INFO(Core, "Viewer textures flushed (next bind will reload from disk)");
}
