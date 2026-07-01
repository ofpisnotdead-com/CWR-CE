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
#include <Poseidon/World/Terrain/Occlusion.hpp>

using namespace Poseidon;

#define DRAW_OBJS 1

namespace Poseidon
{
extern bool EnableObjOcc;
SRef<Occlusion>& GetOcclusions();
} // namespace Poseidon

typedef Ref<SortObject> SortObjectItem;

static int CmpRevDistObj(const SortObjectItem* p1, const SortObjectItem* p0)
{
    // the first object is the nearest one
    const SortObject* o0 = *p0;
    const SortObject* o1 = *p1;
    Coord dif = o0->distance2 - o1->distance2;
    if (dif < 0)
    {
        return -1;
    }
    if (dif > 0)
    {
        return +1;
    }
    return 0;
}

// far-to-near by camera-space depth, for the alpha pass (see AlphaSortOrder.hpp)
static int CmpRevAlphaSortObj(const SortObjectItem* p1, const SortObjectItem* p0)
{
    return AlphaSort::CompareAlphaDepth((*p1)->zCoord, (*p0)->zCoord);
}

static int CmpShapeObj(const SortObjectItem* p1, const SortObjectItem* p2)
{
    const SortObject* o1 = *p1;
    const SortObject* o2 = *p2;

    // first sort by pass
    int sDif = o1->passNum - o2->passNum;
    if (sDif)
    {
        return sDif;
    }

    LODShape* s1 = o1->object->GetShape();
    LODShape* s2 = o2->object->GetShape();
    sDif = (intptr_t)s2 - (intptr_t)s1;
    // no invisible LODs here
    if (sDif)
    {
        Shape* ss1 = s1->Level(0);
        Shape* ss2 = s2->Level(0);
        int complex1 = ss1->NFaces();
        int complex2 = ss2->NFaces();
        int cDiff = complex1 - complex2;
        // sort by shape complexity
        // first draw simple shapes
        if (cDiff)
        {
            return cDiff;
        }
        return sDif;
    }
    // sort by LOD - fine LODs (small LOD numbers) last
    sDif = o2->drawLOD - o1->drawLOD;
    if (sDif)
    {
        return sDif;
    }
    // first draw
    // same shape sort by distance, back first (helps to alpha transparency)
    Coord fDif = o2->distance2 - o1->distance2;
    if (fDif < 0)
    {
        return -1;
    }
    if (fDif > 0)
    {
        return +1;
    }
    return 0;
}

void Scene::EndObjects()
{
    // sort by screen size
}

void Scene::DrawFlare(ColorVal color, Vector3Par lightPos, bool secondary)
{
    Color lightColor = color * GEngine->GetAccomodateEye();
    float oldA = lightColor.A();
    // we can draw flares now
    // flare positions between posLight (0) and screen center (1)
    static const float flarePos[FlareLast + 1 - Flare0] = {0.0f,    -0.2f,   -0.1f,  +0.25f, +0.275f, +0.3f,
                                                           +0.4f,   +0.5f,   +0.65f, +0.7f,  +0.725f, +0.75f,
                                                           +0.875f, +0.885f, +1.0f,  +1.1f};
    // note - z is ignored when doing 2D draw
    // but is significant for decal draw
    float w = GLOB_ENGINE->Width();
    float h = GLOB_ENGINE->Height();
    // calculate screen position of light
    Vector3Val pos = ScaledInvTransform() * lightPos;
    // apply perspective
    Matrix4Val project = GetCamera()->Projection();
    const float thold = 0.6f;
    if (pos[2] < thold)
    {
        return; // no flares
    }
    float vis = (pos[2] - thold) * (1.0f / (1 - thold));
    saturateMin(vis, 1);
    float a = 0.9f * vis * vis;
    if (a > 0.01)
    {
        float invW = 1 / pos[2];
        Vector3 lightPos(project(0, 2) + project(0, 0) * pos[0] * invW, project(1, 2) + project(1, 1) * pos[1] * invW,
                         1);

        float size = 0.1f;
        float sizeX = +project(0, 0) * size * GetCamera()->InvLeft();
        float sizeY = -project(1, 1) * size * GetCamera()->InvTop();

        Vector3 center(0.5f * w, 0.5f * h, 1);
        const int special = NoZBuf | IsLight | ClampU | ClampV | IsAlphaFog;
        // Flare0 is handled specially
        {
            lightColor.SetA(oldA * a * 2);
            PackedColor color = PackedColor(lightColor);
            Texture* texture = Preloaded(PreloadedTexture(Flare0));
            if (texture)
            {
                MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
                if (mip.IsOK())
                {
                    float sizeCoef = texture->AWidth() * (1.0f / 64);
                    GEngine->DrawDecal(lightPos, 1, sizeX * sizeCoef, sizeY * sizeCoef, color, mip, special);
                }
            }
        }
        if (!secondary)
        {
            return;
        }
        lightColor.SetA(a * oldA);
        PackedColor color = PackedColor(lightColor);
        for (int s = Flare0 + 1; s <= FlareLast; s++)
        {
            float coef = flarePos[s - Flare0];
            Texture* texture = Preloaded(PreloadedTexture(s));
            if (texture)
            {
                MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
                if (mip.IsOK())
                {
                    Vector3 pos = lightPos + coef * (center - lightPos);
                    float sizeCoef = texture->AWidth() * (1.0f / 64);
                    GEngine->DrawDecal(pos, 1, sizeX * sizeCoef, sizeY * sizeCoef, color, mip, special);
                }
            }
        }
    }
}

void Scene::DrawFlares()
{
    if (!GetLandscape())
    {
        return;
    }
    float vis = GetLandscape()->SkyThrough();
    float night = MainLight()->NightEffect();

    Vector3Val cPos = GetCamera()->Position();

    if (vis > 0.05 && night < 0.95)
    {
        // draw sun flare
        CameraType camType = GWorld->GetCameraType();

        bool secondary = (camType != CamInternal);
        // check HasFlares of camera source
        Object* camObj = GWorld->CameraOn();
        if (camObj)
        {
            secondary = camObj->HasFlares(camType);
        }
        Vector3 lightDir = MainLight()->SunDirection();
        Color lightColor = MainLight()->SunColor() * (1 - night) * vis;

        // fictive sun position
        Vector3 sunPos = cPos - lightDir * ENGINE_CONFIG.horizontZ;
        float sunRadius = 0.02 * ENGINE_CONFIG.horizontZ;

        float visLand = 1;
        if (ENGINE_CONFIG.enableHWTL)
        {
            float t = GLandscape->IntersectWithGroundOrSea(nullptr, cPos, -lightDir, 0, ENGINE_CONFIG.horizontZ * 1.1);
            visLand = t >= ENGINE_CONFIG.horizontZ;
        }
        if (visLand > 0)
        {
            float a = visLand * 0.25;
            // check against occlusion buffer
            float occ = GetOcclusions()->TestSphereWSpace(sunPos, sunRadius);
            a *= occ;
            if (a >= 0.01)
            {
                // check line against view geometries
                CollisionBuffer col;
                Object* camOn = GWorld->CameraOn();
                if (camOn && !GWorld->GetCameraEffect())
                {
                    GLandscape->ObjectCollision(col, camOn, nullptr, cPos, sunPos, 0, ObjIntersectView);
                    // check if any of the objects is not considered
                    for (int i = 0; i < col.Size(); i++)
                    {
                        Object* obj = col[i].object;
                        if (obj && obj->GetShape() && !obj->GetShape()->CanOcclude())
                        {
                            // object is not included in occlusion buffer, check it now
                            a = 0;
                        }
                    }
                }
                if (a >= 0.01)
                {
                    saturateMin(a, 1);
                    lightColor.SetA(a);
                    DrawFlare(lightColor, GetCamera()->Position() - lightDir, secondary);
                }
            }
        }
    }
    if (night >= 0.2)
    {
        // draw flares from active lights
        Vector3Val camPos = GetCamera()->Position();
        Vector3Val camDir = GetCamera()->Direction();
        for (int i = 0; i < _aLights.Size(); i++)
        {
            Light* light = _aLights[i];
            Vector3 dir = camPos - light->Position();
            if (dir * GetCamera()->Direction() > 0)
            {
                continue; // this one has no flare
            }
            float intensity = light->FlareIntensity(camPos, camDir);
            if (intensity < 0.01)
            {
                continue;
            }
            // light

            // check if light position is visible from camera position
            float visLand = 1;
            if (ENGINE_CONFIG.enableHWTL)
            {
                float t = GLandscape->IntersectWithGroundOrSea(nullptr, cPos, dir, 0, 1.1);
                visLand = t > 1;
            }

            if (visLand > 0)
            {
                Color lightColor = light->GetObjectColor();
                float a = visLand * intensity * 2;
                float occ = GetOcclusions()->TestSphereWSpace(light->Position(), 0.4);
                a *= occ;
                saturateMin(a, 0.5);
                lightColor.SetA(a);
                DrawFlare(lightColor, light->Position(), false);
            }
        }
    }
}

void Scene::DrawRainLevel(float alpha, float yDensity, float xOffset, float yOffset, float z)
{
    Texture* texture = Preloaded(TextureRain);
    Color color(HWhite);
    color.SetA(alpha);
    Draw2DPars pars;
    pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
    pars.SetU(xOffset - z * 0.5f, xOffset + z * 0.5f);
    pars.SetV(yOffset - z * 0.5f * yDensity, yOffset + z * 0.5f * yDensity);
    pars.SetColor(PackedColor(color));
    pars.spec = NoZWrite | IsAlpha | NoClamp | IsAlphaFog;
    Rect2DAbs rect(0, 0, GLOB_ENGINE->Width(), GLOB_ENGINE->Height());
    GLOB_ENGINE->Draw2D(pars, rect);
}

void Scene::DrawRain()
{
    // start with single level rain
    if (!GetLandscape())
    {
        return;
    }
    float density = GetLandscape()->GetRainDensity();
    if (density >= 0.1)
    {
        Vector3Val dir = GetCamera()->Direction();
        float speed = fabs(GetCamera()->Speed() * dir) * 0.05f;
        saturate(speed, 0, 2);
        static float rainOffset;
        static Time rainT;
        float deltaRT = Glob.time - rainT;
        rainT += deltaRT;
        rainOffset += deltaRT;
        float yOffset = -fastFmod(rainOffset, 1);
        float xOffset = atan2(dir.X(), dir.Z()) * 0.3f;
        float yDensity = 0.3f + fabs(dir.Y()) + speed;
        saturate(yDensity, 0.1f, 1);
        // draw all levels
        density *= 0.2f;
        DrawRainLevel(density, yDensity, xOffset, yOffset, 8);
        DrawRainLevel(density, yDensity, xOffset, yOffset, 2);
    }
}

void Scene::ObjectsDrawn()
{
    // release all references
    DrawRain();

    DrawObjectsAndShadowsPass3();

    // clear working list
    _drawMergers.Resize(0);
    // keep drawObjects for next frame

    DrawFlares();
    _aLights.Resize(0);
}

int Scene::LevelFromDistance2(LODShape* shape, float distance2, float oScale, Vector3Par direction,
                              Vector3Par viewDirection)
{
    // if pixel size is lower than 2, object can be considered invisible
    // size in pixels is
    float scale = GetCamera()->Left();
    float diameter = shape->BoundingSphere() * 2 * oScale;

    if (distance2 > Square(ENGINE_CONFIG.objectsZ))
    {
        return LOD_INVISIBLE;
    }

    float pixelLimit = 0.125;
    float detail2 = distance2 * Square(_lodInvWidth);

    if (Square(diameter) < Square(pixelLimit * scale) * detail2)
    {
        return LOD_INVISIBLE;
    }

    if (shape->NLevels() < 2)
    {
        return 0;
    }

    float resol2 = detail2 * Square(scale);
    // disable decal LODs
    int level = shape->FindSqrtLevel(resol2, true);

    return level;
}

int Scene::LevelShadowFromDistance2(LODShape* shape, float distance2, float oScale, Vector3Par direction,
                                    Vector3Par viewDirection)
{
    distance2 *= 8;

    float scale = GetCamera()->Left();
    float diameter = shape->BoundingSphere() * 2 * oScale;

    float pixelLimit = 0.25;
    float detail2 = distance2 * Square(_lodInvWidth);

    if (Square(diameter) < Square(pixelLimit * scale) * detail2)
    {
        return LOD_INVISIBLE;
    }

    if (shape->NLevels() < 2)
    {
        return 0;
    }
    float limit = ENGINE_CONFIG.shadowLODLimit;
    saturateMax(detail2, Square(diameter * limit));
    float resol2 = detail2 * Square(scale);
    // disable decal LODs
    int level = shape->FindSqrtLevel(resol2, true);

    return level;
}

#define DO_STAT 0

#if DO_STAT

#include <Poseidon/Foundation/Math/Statistics.hpp>

NameStatistics Alpha;
NameStatistics Opaque;
NameStatistics Shadow;
#endif

#if _ENABLE_CHEATS

// advances diagnostics - via scripting
#include <Evaluator/express.hpp>

#define NOTHING GameValue()

#define DIAG_DRAW_MODE_ENUM(type, prefix, XX) \
    XX(type, prefix, Normal)                  \
    XX(type, prefix, Roadway)                 \
    XX(type, prefix, Geometry)                \
    XX(type, prefix, ViewGeometry)            \
    XX(type, prefix, FireGeometry)            \
    XX(type, prefix, Paths)

DECLARE_DEFINE_ENUM(DiagDrawMode, DDM, DIAG_DRAW_MODE_ENUM)

namespace Poseidon::Dev
{
DEFINE_ENUM(DiagEnable, DE, DIAG_ENABLE_ENUM)
}

DiagDrawMode DiagDrawModeState = DDMNormal;
namespace Poseidon::Dev
{
int DiagMode;
}

static GameValue SetDiagDrawMode(const GameState* state, GameValuePar oper)
{
    const char* modeStr = (RString)oper;
    DiagDrawMode mode = GetEnumValue<DiagDrawMode>(modeStr);
    if ((int)mode == -1)
        return NOTHING;
    DiagDrawModeState = mode;
    return NOTHING;
}

static GameValue SetDiagEnable(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    const char* modeStr = (RString)oper1;
    bool onOff = oper2;
    int modeMask = ~0;
    if (strcmpi(modeStr, "all"))
    {
        DiagEnable mode = GetEnumValue<DiagEnable>(modeStr);
        if ((int)mode == -1)
            return NOTHING;
        modeMask = 1 << mode;
    }

    if (onOff)
        DiagMode |= modeMask;
    else
        DiagMode &= ~modeMask;
    return NOTHING;
}

static GameValue SetDiagToggle(const GameState* state, GameValuePar oper1)
{
    const char* modeStr = (RString)oper1;
    DiagEnable mode = GetEnumValue<DiagEnable>(modeStr);
    if ((int)mode == -1)
        return NOTHING;
    int modeMask = 1 << mode;

    DiagMode ^= modeMask;
    return NOTHING;
}

#include <Poseidon/Foundation/Modules/Modules.hpp>

static const GameFunction ObjUnary[] = {
    GameFunction(GameNothing, "diag_drawmode", SetDiagDrawMode, GameString),
    GameFunction(GameNothing, "diag_toggle", SetDiagToggle, GameString),
};
static const GameOperator ObjBinary[] = {
    GameOperator(GameNothing, "diag_enable", function, SetDiagEnable, GameString, GameBool),
};

INIT_MODULE(GameStateObj, 3)
{
    GGameState.NewOperators(ObjBinary, sizeof(ObjBinary) / sizeof(*ObjBinary));
    GGameState.NewFunctions(ObjUnary, sizeof(ObjUnary) / sizeof(*ObjUnary));
};

#endif

int Scene::AdjustComplexity(SortObjectList& objs)
{
    int totalComplexity = 0;
    for (int i = 0; i < objs.Size(); i++)
    {
        SortObject* oi = objs[i];
        Object* obj = oi->object;
        if (!obj)
        {
            Fail("No obj in SortObject info");
            continue;
        }
        LODShape* shape = oi->shape;
        if (oi->forceDrawLOD >= 0)
        {
            oi->drawLOD = oi->forceDrawLOD;
        }
        else
        {
            int drawLevel =
                LevelFromDistance2(shape, oi->distance2, obj->Scale(), obj->Direction(), _camera->Direction());
            if (drawLevel != LOD_INVISIBLE)
            {
#if _ENABLE_CHEATS
                if (DiagDrawModeState != DDMNormal)
                {
                    int geom = -1;
                    switch (DiagDrawModeState)
                    {
                        case DDMGeometry:
                            geom = shape->FindGeometryLevel();
                            break;
                        case DDMViewGeometry:
                            geom = shape->FindViewGeometryLevel();
                            break;
                        case DDMFireGeometry:
                            geom = shape->FindFireGeometryLevel();
                            break;
                        case DDMRoadway:
                            geom = shape->FindRoadwayLevel();
                            break;
                        case DDMPaths:
                            geom = shape->FindPaths();
                            break;
                    }
                    if (geom >= 0)
                        drawLevel = geom;
                }
#endif
            }
            oi->drawLOD = drawLevel;
        }
        // check number of faces in given level
        if (oi->drawLOD != LOD_INVISIBLE)
        {
            oi->passNum = obj->PassNum(oi->drawLOD);
#if _ENABLE_CHEATS
            if (CHECK_DIAG(DETransparent))
            {
                // all geometries are drawn transparent
                if (oi->drawLOD == shape->FindGeometryLevel())
                {
                    if (oi->passNum < 2)
                        oi->passNum = 2;
                }
            }
#endif
            totalComplexity += obj->GetComplexity(oi->drawLOD, *obj);
        }
    }
    return totalComplexity;
}

static int ShadowFactor(Scene* scene)
{
    float addLightsFactor = scene->MainLight()->NightEffect();
    float skyCoef = floatMin(scene->GetLandscape()->SkyThrough(), 0.6f);
    float shadowFactor = skyCoef + 0.1f;
    return toIntFloor(shadowFactor * (1 - addLightsFactor) * 255);
}

int Scene::AdjustShadowComplexity(SortObjectList& objs)
{
#if 1
    int totalComplexity = 0;

    int shadowFactorI = ShadowFactor(this);

    for (int i = 0; i < objs.Size(); i++)
    {
        SortObject* oi = objs[i];
        Object* obj = oi->object;
        LODShape* shape = oi->shape;
        if (!shape)
        {
            continue;
        }
        if (!(shape->Special() & NoShadow) && oi->distance2 < Square(_shadowFogMaxRange) && shadowFactorI >= 12 &&
            obj->CastShadow())
        {
            int level = LevelShadowFromDistance2(shape, oi->distance2, obj->Scale(), obj->Direction(),
                                                 _mainLight->ShadowDirection());
            if (level != LOD_INVISIBLE)
            {
                level = shape->FindNearestWithoutProperty(level, "lodnoshadow");
                if (level < 0)
                {
                    level = LOD_INVISIBLE;
                }
            }
            oi->shadowLOD = level;
        }
        else
        {
            oi->shadowLOD = LOD_INVISIBLE;
        }
        if (oi->shadowLOD != LOD_INVISIBLE)
        {
            // check number of faces in given level
            Shape* level = shape->Level(oi->shadowLOD);
            totalComplexity += level->NFaces();
        }
    }
    return totalComplexity;
#else

    float addLightsFactor = MainLight()->NightEffect();
    float skyCoef = floatMin(GetLandscape()->SkyThrough(), 0.6f);
    float shadowFactor = skyCoef + 0.1f;
    int shadowFactorI = toIntFloor(shadowFactor * (1 - addLightsFactor) * 255);

    for (int i = 0; i < objs.Size(); i++)
    {
        SortObject* oi = objs[i];
        Object* obj = oi->object;
        LODShape* shape = oi->shape;
        if (!(shape->Special() & NoShadow) && oi->distance2 < Square(_shadowFogMaxRange) && shadowFactorI >= 12)
        {
            int level = 0;
            if (level != LOD_INVISIBLE)
            {
                level = shape->FindNearestWithoutProperty(level, "lodnoshadow");
                if (level < 0)
                    level = LOD_INVISIBLE;
            }
            oi->shadowLOD = level;
        }
        else
        {
            oi->shadowLOD = LOD_INVISIBLE;
        }
    }
    return 1000;
#endif
}

inline void CheckMinMaxIter(Vector3& min, Vector3& max, Vector3Par val)
{
#if __ICL
    if (min[0] > val[0])
        min[0] = val[0];
    if (max[0] < val[0])
        max[0] = val[0];
    if (min[1] > val[1])
        min[1] = val[1];
    if (max[1] < val[1])
        max[1] = val[1];
    if (min[2] > val[2])
        min[2] = val[2];
    if (max[2] < val[2])
        max[2] = val[2];
#else
    if (min[0] > val[0])
    {
        min[0] = val[0];
    }
    else if (max[0] < val[0])
    {
        max[0] = val[0];
    }
    if (min[1] > val[1])
    {
        min[1] = val[1];
    }
    else if (max[1] < val[1])
    {
        max[1] = val[1];
    }
    if (min[2] > val[2])
    {
        min[2] = val[2];
    }
    else if (max[2] < val[2])
    {
        max[2] = val[2];
    }
#endif
}

static bool FarEnoughForOcclusion(const SortObject* oi)
{
    // do not occlude things that are very near
    // the test would be very slow and is very like to fail
    const float occNearest = 20;
    if (oi->distance2 > Square(occNearest + 50))
    {
        return true;
    }
    else
    {
        float distNear = oi->distance2 * InvSqrt(oi->distance2) - oi->radius;
        if (distNear > occNearest)
        {
            return true;
        }
    }
    return false;
}

#if !DENSITY_LOD

void Scene::AdjustComplexity()
{
    float oldLodInvWidth = _lodInvWidth;

    // adjust lodInvWidth so we are on the line given by points
    // (MinTargetFrameDuration, minLodInvWidth), (MaxTargetFrameDuration, maxLodInvWidth):
    // l is _lodInvWidth, t is estimated time. Line equation lineL*l+lineT*t+lineC = 0.
    float lMin = _minLodInvWidth;
    float lMax = _maxLodInvWidth;

    float tMin = _minTargetFrameDuration;
    float tMax = _maxTargetFrameDuration;

    float lineL = tMin - tMax;
    float lineT = lMax - lMin;
    // normalize lineL +  lineT
    float invLineTLSize = InvSqrt(lineL * lineL + lineT * lineT);
    lineL *= invLineTLSize;
    lineT *= invLineTLSize;

    float lineC = -lineL * lMin - lineT * tMin;
#define DIAG_LOD 0
#if DIAG_LOD
    LOG_DEBUG(Graphics, "t range {:.1f}..{:.1f}", tMin, tMax);
    LOG_DEBUG(Graphics, "l range {:.3f}..{:.3f}", lMin, lMax);
#endif

    int targetGeom = ENGINE_CONFIG.maxObjects * 75;
    int complexity = 0;
    int treshold = 8;
    for (int iters = 5; --iters >= 0;)
    {
        // calculate complexity
        complexity = AdjustComplexity(_drawObjects);
        complexity += AdjustShadowComplexity(_drawObjects);
        // include landscape complexity
        complexity += toLargeInt(Square(ENGINE_CONFIG.horizontZ * (1.0f / 50)));

        float frameDuration = GEngine->GetAvgFrameDuration(4);

        // estimate duration of this frame
        float avgComplexity = 0;
        for (int i = 0; i < NStoreComplexities; i++)
        {
            avgComplexity += _lastComplexity[i];
        }
        avgComplexity *= 1.0f / NStoreComplexities;
        int estDuration = 200; // no estimate yet
        if (avgComplexity > 0)
        {
            estDuration = toLargeInt(frameDuration * complexity / avgComplexity);
        }
        // estimate some minimal complexity this computer is able to render
        float triCoef = 200; // good estimation for Voodoo2
        int maxDuration = toLargeInt(complexity / float(targetGeom) * triCoef);

        saturateMin(estDuration, maxDuration);

        // react to quick change in complexity
        // estimate frame rate based on previous complexity and time

        // calculate l and t
        // l is _lodInvWidth, t is estimated time
        // line normal is (lineL,lineT)
        float l = _lodInvWidth;
        float t = estDuration;
        float dist = lineL * l + lineT * t + lineC;
// check distance from line
// note: we might be in area out of tmin,tmax
// find nearest point on line
#if DIAG_LOD
        LOG_DEBUG(Graphics, "  t {:.1f}, l {:.3f}, dist {:.2f}", t, l, dist);
#endif
        float tDiff = dist * 200;

        if (tDiff > treshold)
        {
            if (_lodInvWidth >= _maxLodInvWidth)
            {
                break; // fast path - min reached
            }
            treshold = 4; // allow smoother change
            // scale - we are too slow
            float change = tDiff * -0.005f; // change is negative
            saturate(change, -0.2f, +0.2f);
            _lodInvWidth /= Square(1 + change);
            if (_lodInvWidth > _maxLodInvWidth)
            {
                // extreme reached - limit
                _lodInvWidth = _maxLodInvWidth;
                complexity = AdjustComplexity(_drawObjects);
                complexity += AdjustShadowComplexity(_drawObjects);
                complexity += toLargeInt(Square(ENGINE_CONFIG.horizontZ * (1.0f / 50)));
                break;
            }
        }
        else if (tDiff < -treshold)
        {
            if (_lodInvWidth <= _minLodInvWidth)
            {
                break; // fast path - max reached
            }
            treshold = 4; // allow smoother change
            // scale - we are too fast
            float change = tDiff * -0.005f; // change is possitive
            saturate(change, -0.2f, +0.2f);

            _lodInvWidth /= Square(1 + change);

            if (_lodInvWidth < _minLodInvWidth)
            {
                // extreme reached - limit
                _lodInvWidth = _minLodInvWidth;
                complexity = AdjustComplexity(_drawObjects);
                complexity += AdjustShadowComplexity(_drawObjects);
                complexity += toLargeInt(Square(ENGINE_CONFIG.horizontZ * (1.0f / 50)));
                break;
            }
        }
        else
        {
            // wanted state reached - terminate loop
            break;
        }
    }

    if (oldLodInvWidth > _lodInvWidth)
    {
        // going better - check for oscilation
        if (_lastScaleWorseTime > Glob.uiTime - 5)
        {
            // avoid change in this direction - restore state
            _lodInvWidth = oldLodInvWidth;
            complexity = AdjustComplexity(_drawObjects);
            complexity += AdjustShadowComplexity(_drawObjects);
        }
        _lastScaleBetterTime = Glob.uiTime;
    }
    if (oldLodInvWidth < _lodInvWidth)
    {
        // going worse - check for oscilation
        if (_lastScaleBetterTime > Glob.uiTime - 0.5)
        {
            // avoid change in this direction - restore state
            _lodInvWidth = oldLodInvWidth;
            complexity = AdjustComplexity(_drawObjects);
            complexity += AdjustShadowComplexity(_drawObjects);
        }
        _lastScaleWorseTime = Glob.uiTime;
    }

    // store complexity to complexity history
    for (int i = 1; i < NStoreComplexities; i++)
    {
        _lastComplexity[i - 1] = _lastComplexity[i];
    }
    _lastComplexity[NStoreComplexities - 1] = complexity;

    float avgComplexity = 0;
    for (int i = 0; i < NStoreComplexities; i++)
    {
        avgComplexity += _lastComplexity[i];
    }
    avgComplexity *= 1.0f / NStoreComplexities;
}
float Scene::GetSmokeGeneralization() const
{
    return _lodInvWidth * 0.1f;
}

#else

static inline float CoveredArea(Object* obj, float dist2, float oScale)
{
    LODShape* shape = obj->GetShape();
    if (!shape)
        return 0;
    float radius = shape->BoundingSphere() * oScale;

    Camera* cam = GScene->GetCamera();
    // return area in pixels
    float areaK = cam->InvLeft() * cam->InvTop() * GEngine->Width() * GEngine->Height();

    const float maxArea = 0.5f;
    // if (Square(radius)/dist2>maxArea)
    if (Square(radius) > dist2 * maxArea)
    {
        return maxArea * areaK;
    }
    return Square(radius) * areaK / dist2;
}

static int CmpCoveredAreaObj(const SortObjectItem* p1, const SortObjectItem* p0)
{
    // the first object is the nearest one
    const SortObject* o0 = *p0;
    const SortObject* o1 = *p1;
    float dif = (CoveredArea(o0->object, o0->distance2, o0->object->Scale()) -
                 CoveredArea(o1->object, o1->distance2, o1->object->Scale()));
    if (dif < 0)
        return -1;
    if (dif > 0)
        return +1;
    // make sure ordering is stable
    dif = o0->distance2 - o1->distance2;
    if (dif < 0)
        return +1;
    if (dif > 0)
        return -1;
    return CmpShapeObj(p1, p0);
}

static inline int Complexity(LODShape* lShape, int level)
{
    return lShape->Level(level)->NFaces();
}

static int FindLevelWithComplexity(LODShape* lShape, float complexity, float maxDif)
{
    PoseidonAssert(complexity >= 0);
    int bestI = -1;
    float bestDif = maxDif;
    for (int i = 0; i < lShape->NLevels(); i++)
    {
        float resol = lShape->Resolution(i);
        if (resol > 900)
            break;
        int lComplex = Complexity(lShape, i);
        float dif = fabs(complexity - lComplex);
        if (bestDif > dif)
        {
            bestDif = dif;
            bestI = i;
        }
    }
    return bestI;
}

static int FindShadowLevelWithComplexity(LODShape* lShape, float complexity, float maxDif)
{
    int i = FindLevelWithComplexity(lShape, complexity, maxDif);
    if (i < 0)
        return i;
    return lShape->FindNearestWithoutProperty(i, "lodnoshadow");
}

void Scene::AdjustComplexity()
{
#if _ENABLE_CHEATS
    static int displayComplexityLimit = 100000;
#endif

    // set _lodInvWidth in case any needs it
    // development only: it should not be used when Density Lod system is active
    _lodInvWidth = (_minLodInvWidth + _maxLodInvWidth) * 0.5f;

    int shadowFactorI = ShadowFactor(this);

#if _ENABLE_CHEATS
    static float maxDensity = 0.3; // max. 5 polygons per pixel wanted
    static float shadowAreaCoef = 1.0f / 32;
    // static float invShadowAreaCoef = 1/shadowAreaCoef;
    auto& input = InputSubsystem::Instance();
    if (input.GetCheat2ToDo(SDL_SCANCODE_LEFTBRACKET))
    {
        maxDensity /= 1.2f;
    }
    if (input.GetCheat2ToDo(SDL_SCANCODE_RIGHTBRACKET))
    {
        maxDensity *= 1.2f;
    }

    if (input.GetCheat1ToDo(SDL_SCANCODE_LEFTBRACKET))
    {
        shadowAreaCoef /= 1.5f;
        // invShadowAreaCoef = 1/shadowAreaCoef;
    }
    if (input.GetCheat1ToDo(SDL_SCANCODE_RIGHTBRACKET))
    {
        shadowAreaCoef *= 1.5f;
        // invShadowAreaCoef = 1/shadowAreaCoef;
    }
#else
    const float maxDensity = 0.3; // max. 5 polygons per pixel wanted
    const float shadowAreaCoef = 1.0f / 32;
// const float invShadowAreaCoef = 1/shadowAreaCoef;
#endif

    // calculate total covered area
    float totalArea = 0;
    // sum complexity of objects that are excluded from lod management
    int complexityUsed = 0;
    for (int i = 0; i < _drawObjects.Size(); i++)
    {
        SortObject* oi = _drawObjects[i];
        Object* obj = oi->object;
        if (oi->forceDrawLOD >= 0)
        {
            // count complexity as used
            if (oi->forceDrawLOD != LOD_INVISIBLE)
            {
                complexityUsed += obj->GetComplexity(oi->forceDrawLOD, *obj);
            }
        }
        else
        {
            float area = CoveredArea(obj, oi->distance2, obj->Scale());
            totalArea += area;
        }

        LODShape* shape = obj->GetShape();
        if (shape && !(shape->Special() & NoShadow) && oi->distance2 < Square(_shadowFogMaxRange) &&
            shadowFactorI >= 12 && obj->CastShadow())
        {
            float area = CoveredArea(obj, oi->distance2, obj->Scale());
            totalArea += area * shadowAreaCoef;
        }
        else
        {
            oi->shadowLOD = LOD_INVISIBLE;
        }
    }

    // if total area is zero, there are no controlled visible objects and density does not matter
    // we have total area, we know how much complexity we want - wa may calculate density now
    const int wantedComplexity = 30000;
    complexityUsed += toLargeInt(Square(ENGINE_CONFIG.horizontZ * (1.0f / 50)));

    int complexity = complexityUsed;

    float density = (wantedComplexity - complexity) / floatMax(totalArea, 1e-20);
    saturate(density, 0, maxDensity);

    float density0 = density;
    float totalArea0 = totalArea;
    // note: if too many polygons were already used, density may be negative

    float minArea = totalArea * 1e-4f;
    // first pass: select object which use lod 0
    // such objects often do not use available complexity
    for (int i = 0; i < _drawObjects.Size(); i++)
    {
        SortObject* oi = _drawObjects[i];
        Object* obj = oi->object;
        if (oi->forceDrawLOD < 0)
        {
            LODShape* lShape = obj->GetShape();
            float area = CoveredArea(obj, oi->distance2, obj->Scale());
            float oComplexity = area * density;
            int level = FindLevelWithComplexity(lShape, oComplexity, oComplexity * 1.5f + 200);
            if (level == 0)
            {
                int levelComplexity = Complexity(lShape, level);
#if _ENABLE_CHEATS
                if (abs(oComplexity - levelComplexity) > displayComplexityLimit)
                {
                    LOG_DEBUG(Graphics, "{} - complexity {}, wanted {:.0f}", (const char*)lShape->GetName(),
                              levelComplexity, oComplexity);
                }
#endif
                complexity += levelComplexity;
                oi->passNum = obj->PassNum(level);
                oi->drawLOD = level;

                totalArea -= area;
            }
        }
        else
        {
            if (oi->forceDrawLOD != LOD_INVISIBLE)
            {
                oi->passNum = obj->PassNum(oi->forceDrawLOD);
            }
            oi->drawLOD = oi->forceDrawLOD;
        }
        // similiar for shadows
        if (oi->shadowLOD < 0)
        {
            LODShape* lShape = obj->GetShape();
            float area = CoveredArea(obj, oi->distance2, obj->Scale());
            float oComplexity = area * density * shadowAreaCoef;
            int level = FindShadowLevelWithComplexity(lShape, oComplexity, oComplexity * 1.5f + 200);
            if (level == 0) // lShape->_minShadow should be used here instead
            {
                int levelComplexity = Complexity(lShape, level);
                complexity += levelComplexity;
                oi->shadowLOD = level;

                totalArea -= area * shadowAreaCoef;
            }
        }
    }

    // when we rendered almost everything, there is no need to update density any more
    if (totalArea > minArea)
    {
        density = (wantedComplexity - complexity) / floatMax(totalArea, 1e-20);
        saturate(density, 0, maxDensity);
    }

    // selects lods for all other objects
    for (int i = 0; i < _drawObjects.Size(); i++)
    {
        SortObject* oi = _drawObjects[i];
        Object* obj = oi->object;
        if (oi->drawLOD < 0)
        {
            LODShape* lShape = obj->GetShape();
            float area = CoveredArea(obj, oi->distance2, obj->Scale());
            float oComplexity = area * density;
            int level = FindLevelWithComplexity(lShape, oComplexity, oComplexity * 1.5f + 200);
            if (level < 0)
            {
                oi->drawLOD = LOD_INVISIBLE;
            }
            else
            {
                int levelComplexity = Complexity(lShape, level);
#if _ENABLE_CHEATS
                if (abs(oComplexity - levelComplexity) > displayComplexityLimit)
                {
                    LOG_DEBUG(Graphics, "{} - complexity {}, wanted {:.0f}", (const char*)lShape->GetName(),
                              levelComplexity, oComplexity);
                }
#endif
                complexity += levelComplexity;
                oi->passNum = obj->PassNum(level);
                oi->drawLOD = level;
            }
        }
        if (oi->shadowLOD < 0)
        {
            LODShape* lShape = obj->GetShape();
            float area = CoveredArea(obj, oi->distance2, obj->Scale());
            float oComplexity = area * density * shadowAreaCoef;
            int level = FindShadowLevelWithComplexity(lShape, oComplexity, oComplexity * 1.5f + 200);
            if (level < 0)
            {
                oi->shadowLOD = LOD_INVISIBLE;
            }
            else
            {
                int levelComplexity = Complexity(lShape, level);
#if _ENABLE_CHEATS
                if (abs(oComplexity - levelComplexity) > displayComplexityLimit)
                {
                    LOG_DEBUG(Graphics, "{} - complexity {}, wanted {:.0f}", (const char*)lShape->GetName(),
                              levelComplexity, oComplexity);
                }
#endif
                complexity += levelComplexity;
                oi->shadowLOD = level;
            }
        }
    }

    // store complexity to complexity history
    for (int i = 1; i < NStoreComplexities; i++)
    {
        _lastComplexity[i - 1] = _lastComplexity[i];
    }
    _lastComplexity[NStoreComplexities - 1] = complexity;

    float avgComplexity = 0;
    for (int i = 0; i < NStoreComplexities; i++)
    {
        avgComplexity += _lastComplexity[i];
    }
    avgComplexity *= 1.0f / NStoreComplexities;
#if 1
    GlobalShowMessage(
        500,
        "Complex %8d, ~ %8.0f, density %0.5f (%0.5f) < %0.5f, totArea %0.f (%0.f), check %0.1f, cUsed %d, shadC %.2f",
        complexity, avgComplexity, density, density0, maxDensity, totalArea, totalArea0, density0 * totalArea0,
        complexityUsed, 1 / shadowAreaCoef);
#endif
}
float Scene::GetSmokeGeneralization() const
{
    return (_minLodInvWidth + _maxLodInvWidth) * 0.5f * 0.1f;
}
#endif

// Cockpit pass routing — the one policy site.  The camera vehicle is
// queued at its inside-view LOD (World::Draw); only that queue ever
// selects an object's InsideLOD level, so equality here means "this is
// the first-person interior" — soldier hands/body, vehicle cockpit.
// Those draws carry PassKindHint::Cockpit so the descriptor build picks
// the Cockpit* pass family explicitly instead of inferring it from the
// shape's NoDropdown faces.
static void DrawSortObject(SortObject* oi)
{
    Object* obj = oi->object;
    const bool cockpit =
        GWorld && oi->drawLOD != LOD_INVISIBLE && oi->drawLOD == obj->InsideLOD(GWorld->GetCameraType());
    if (!cockpit)
    {
        obj->Draw(oi->drawLOD, oi->orClip, *obj);
        return;
    }
    const render::PassKindHint savedHint = GEngine->GetPassKindHint();
    GEngine->SetPassKindHint(render::PassKindHint::Cockpit);
    obj->Draw(oi->drawLOD, oi->orClip, *obj);
    GEngine->SetPassKindHint(savedHint);
}

void Scene::DrawObjectsAndShadowsPass1()
{
    // select first objects - those with highest visual priority

    int s = 0, t = 0;
    for (; s < _drawObjects.Size(); s++)
    {
        SortObject* sObj = _drawObjects[s];
        if (!sObj->notUsed)
        {
            if (s != t)
            {
                _drawObjects[t] = sObj;
            }
            t++;
        }
        else
        {
            sObj->object->SetInList(nullptr); // removed from the list
        }
    }
    _drawObjects.Resize(t);

#if DO_STAT
    Alpha.Clear();
    Opaque.Clear();
    Shadow.Clear();
#endif

    // remove all objects that should not be used

    AdjustComplexity();

    // copy objects to working list (mergers)
    // do not copy objects that are not drawn
    {
        // make smaller only when really necessary
        int objNeed = _drawObjects.Size();
        int objHave = _drawMergers.MaxSize();
        if (objNeed >= objHave)
        {
            _drawMergers.Reserve(objNeed, objNeed);
        }
        else if (objNeed * 2 < objHave && objHave > 1024)
        {
            // no need to keep it big now - make it smaller
            _drawMergers.Realloc(objNeed);
        }
        _drawMergers.Resize(0);
        for (int s = 0; s < _drawObjects.Size(); s++)
        {
            SortObject* sObj = _drawObjects[s];
            if (sObj->drawLOD == LOD_INVISIBLE && sObj->shadowLOD == LOD_INVISIBLE)
            {
                continue;
            }
            if (!sObj->object || !sObj->shape)
            {
                continue;
            }
            _drawMergers.Add(sObj);
        }
    }
    // One-shot shape census (perf campaign): triPerfDumpShapes arms this;
    // the next Pass1 logs the top repeated shapes — the instancing-candidate
    // histogram (how many draws are N copies of the same tree/fence/house).
    extern bool gPerfDumpShapesOnce;
    if (gPerfDumpShapesOnce)
    {
        gPerfDumpShapesOnce = false;
        struct Bucket
        {
            const char* name;
            int count;
        };
        std::map<std::string, int> hist;
        for (int s = 0; s < _drawMergers.Size(); s++)
        {
            SortObject* so = _drawMergers[s];
            if (so && so->shape)
                hist[std::string((const char*)so->shape->GetName())]++;
        }
        std::vector<std::pair<int, std::string>> top;
        for (auto& kv : hist)
            top.push_back({kv.second, kv.first});
        std::sort(top.rbegin(), top.rend());
        LOG_INFO(Graphics, "PERF shapes: {} objects, {} unique shapes", _drawMergers.Size(), (int)hist.size());
        for (size_t i = 0; i < top.size() && i < 20; i++)
            LOG_INFO(Graphics, "PERF shape[{}]: {} x{}", (int)i, top[i].second.c_str(), top[i].first);
    }

    // if we sort by shape, we group object with similiar textures
    // sort by distance

#if _ENABLE_CHEATS
    if (InputSubsystem::Instance().GetCheat2ToDo(SDL_SCANCODE_H))
    {
        EnableObjOcc = !EnableObjOcc;
        GlobalShowMessage(500, "Object occlusions %s", EnableObjOcc ? "On" : "Off");
    }
#endif

    if (EnableObjOcc)
    {
        // sort only what needs to checked/drawn for occlusion
        // this will remove especially cloudlets from occlusion testing
        // it also helps to maintain _drawMergers sorted
        // because sort is performed in different array
        // therefore _drawMergers sort may be performed incrementally
        AUTO_STATIC_ARRAY(Ref<SortObject>, occSort, 2048);
        for (int i = 0; i < _drawMergers.Size(); i++)
        {
            SortObject* oi = _drawMergers[i];
            if (oi->drawLOD == LOD_INVISIBLE)
            {
                continue;
            }
            Object* obj = oi->object;
            if (!obj)
            {
                continue;
            }
            LODShape* shape = oi->shape;
            if (!shape)
            {
                continue;
            }

            if (shape->CanBeOccluded() || shape->CanOcclude() && obj->OcclusionView())
            {
                occSort.Add(_drawMergers[i]);
            }
        }
        QSort(occSort.Data(), occSort.Size(), CmpRevDistObj);
// before drawing anything draw cockpit occlusion
// check if we are in internal view
#if 1
        if (GWorld->GetCameraType() == CamInternal && !GWorld->GetCameraEffect())
        {
            Object* obj = GWorld->CameraOn();
            if (obj)
            {
                int view = obj->InsideViewGeomLOD(CamInternal);
                if (view != LOD_INVISIBLE && view >= 0)
                {
                    // draw that particular view geometry
                    obj->AnimateComponentLevel(view);
                    // if object can occlude something, render its components

                    LODShape* lShape = obj->GetShape();

                    Shape* shape = lShape->Level(view);
                    const ConvexComponents* cc = lShape->GetConvexComponents(view);
                    if (cc)
                    {
                        GetOcclusions()->RenderShape(*obj, shape, *cc, ClipAll);
                    }
                    else
                    {
                        LOG_DEBUG(Graphics, "Inconsistent view geom in {}", (const char*)obj->GetDebugName());
                    }
                    obj->DeanimateComponentLevel(view);
                }
            }
        }
#endif

        // draw occlusions front to back
        for (int i = occSort.Size(); --i >= 0;)
        {
            SortObject* oi = occSort[i];
            Object* obj = oi->object;
            LODShape* shape = oi->shape;
            if (oi->drawLOD == LOD_INVISIBLE)
            {
                continue;
            }
            // select view geometry
            // no occlusions for or by camera vehicle
            if (obj == GWorld->CameraOn())
            {
                continue;
            }
            // if object is small and simple if should not be tested nor rendered
            bool occluded = false;
            // check object distance
            // if the objects is very near, do not check occlusion

            if (shape->CanBeOccluded())
            {
                // do not occlude things that are very near
                // the test would be very slow and is very like to fail
                if (FarEnoughForOcclusion(oi))
                {
                    Vector3 minMax[2];
                    obj->AnimatedMinMax(oi->drawLOD, minMax);
                    // check if object is occluded by objects
                    if (!GetOcclusions()->TestBBox(*oi->object, minMax, oi->orClip))
                    {
                        occluded = true;
                    }
                }
            }
            if (!occluded)
            {
                if (shape->CanOcclude() && obj->OcclusionView())
                {
                    Shape* view = shape->ViewGeometryLevel();
                    if (!view)
                    {
                        continue;
                    }
                    // test if object is near enough to make a some occlusion
                    float areaNom = Square(oi->radius * _camera->InvLeft());
                    float areaDenom = oi->distance2;
                    if (areaNom > 0.001 * areaDenom)
                    {
                        obj->AnimateViewGeometry();
                        // if object can occlude something, render its components
                        GetOcclusions()->RenderShape(*oi->object, view, shape->GetViewComponents(), oi->orClip);
                        obj->DeanimateViewGeometry();
                    }
                }
            }
            else
            {
                // occluded - do not draw, do not render into occlusion buffer
                oi->drawLOD = LOD_INVISIBLE;
            }
        }
    }

    QSort(_drawMergers.Data(), _drawMergers.Size(), CmpShapeObj);
    // first of all draw non-alpha objects

#if DRAW_OBJS
    {
        // Instanced runs (perf effort 08): _drawMergers is shape-sorted, so
        // identical static shapes arrive contiguously. A batchable run draws
        // the head once inside Begin/EndInstancedRun — every TL section then
        // renders all K instances from the WorldInstances matrix array. The
        // predicate keeps per-object state out of batches: static, proxy-free,
        // not OnSurface/IsColored, equal obj-special, no local lights in the
        // scene, and a tight distance band so the head's constant-fog value
        // is representative for the whole batch.
        const bool noLocalLights = NLights() == 0;
        for (int i = 0; i < _drawMergers.Size();)
        {
            SortObject* oi = _drawMergers[i];
            LODShape* shape = oi->object->GetShape();
            if (oi->drawLOD == LOD_INVISIBLE)
            {
                i++;
                continue;
            }
            PoseidonAssert(oi->drawLOD >= 0);
            Shape* sShape = shape->LevelOpaque(oi->drawLOD);
            if (!sShape)
            {
                i++;
                continue;
            }
            if (oi->passNum > 1)
            {
                i++;
                continue;
            }

            int runEnd = i + 1;
            const int headSpecial = sShape->Special() | oi->object->GetObjSpecial();
            const render::LegacySpec headSpec = render::SplitLegacy(headSpecial);
            const bool headBatchable = noLocalLights && oi->object->Static() && sShape->NProxies() == 0 &&
                                       !render::Has(headSpec.routing, render::Routing::OnSurface) &&
                                       !render::Has(headSpec.routing, render::Routing::IsColored) &&
                                       oi->object != GWorld->CameraOn();
            if (headBatchable)
            {
                GEngine->InstancedRunReset();
                if (GEngine->InstancedRunAdd(oi->object->Transform()))
                {
                    // Fog band: keep members within ~5% of the head's distance so
                    // the head's per-object constant fog approximates all of them.
                    const float d2lo = oi->distance2 * 0.90f;
                    const float d2hi = oi->distance2 * 1.10f;
                    while (runEnd < _drawMergers.Size())
                    {
                        SortObject* oj = _drawMergers[runEnd];
                        if (oj->object->GetShape() != shape || oj->drawLOD != oi->drawLOD ||
                            oj->passNum != oi->passNum || !oj->object->Static() ||
                            (sShape->Special() | oj->object->GetObjSpecial()) != headSpecial || oj->distance2 < d2lo ||
                            oj->distance2 > d2hi)
                        {
                            break;
                        }
                        if (!GEngine->InstancedRunAdd(oj->object->Transform()))
                        {
                            break;
                        }
                        runEnd++;
                    }
                }
            }

            const int runLen = runEnd - i;
            GSectionFilter = SectionClassFilter::OpaqueAndCutout;
            if (headBatchable && runLen >= 4)
            {
                GEngine->BeginInstancedRunUpload();
                DrawSortObject(oi);
                if (!GEngine->EndInstancedRun())
                {
                    // Vertex-soup sections can't instance — those drew only for the
                    // head; redraw the rest scalar (TL overdraw is z-equal opaque).
                    for (int k = i + 1; k < runEnd; k++)
                    {
                        DrawSortObject(_drawMergers[k]);
                    }
                }
            }
            else
            {
                for (int k = i; k < runEnd; k++)
                {
                    DrawSortObject(_drawMergers[k]);
                }
            }
            GSectionFilter = SectionClassFilter::All;
#if DO_STAT
            Opaque.Count(shape->Name());
#endif
            i = runEnd;
        }
    }
#endif
}

// A surface overlay (road / decal): OnSurface-routed geometry drawn as a
// polygon-offset decal over the terrain, detected from the draw-LOD level
// spec exactly as Object::PassNum classifies surface objects.
static bool IsSurfaceSortObject(const SortObject* oi)
{
    if (!oi->object)
        return false;
    LODShape* lShape = oi->object->GetShape();
    if (!lShape)
        return false;
    Shape* s = lShape->Level(oi->drawLOD);
    if (!s)
        return false;
    const int spec = s->Special() | oi->object->GetObjSpecial();
    return render::IsOnSurfaceSpec(spec);
}

// On-surface ordering: PassOrder (roads before decals), then shape, then
// back-to-front within a shape — see Scene/SurfaceDrawOrder.hpp.
static int CmpSurfaceObj(const SortObjectItem* p1, const SortObjectItem* p2)
{
    const SortObject* o1 = *p1;
    const SortObject* o2 = *p2;
    const Poseidon::SurfaceDraw::SurfaceDrawKey k1{
        o1->object ? o1->object->PassOrder(o1->drawLOD) : 0,
        o1->object ? static_cast<const void*>(o1->object->GetShape()) : nullptr, o1->distance2};
    const Poseidon::SurfaceDraw::SurfaceDrawKey k2{
        o2->object ? o2->object->PassOrder(o2->drawLOD) : 0,
        o2->object ? static_cast<const void*>(o2->object->GetShape()) : nullptr, o2->distance2};
    return Poseidon::SurfaceDraw::CompareSurfaceDraw(k1, k2);
}

void Scene::DrawObjectsAndShadowsPass2()
{
    // must be sorted by distance
    // then draw shadows
    // note: some polygons can not be ordered
    int nDraw = _drawMergers.Size();
#if DRAW_OBJS
    if (GetLandscape())
    {
        // BeginShadowPass / EndShadowPass bracket the projected shadow
        // loop; on GL33 they flush the triangle queues so the per-poly
        // shadows commit between the world geometry and the alpha pass.

        // Surface-overlay blend sections (roads/decals) commit BEFORE the
        // shadow pass so the projected shadows darken on top of the road
        // instead of the road's blend section repainting over the shadowed
        // road pixels.  Their opaque sections already drew in pass 1; only
        // the OnSurface blend section (the visible asphalt) is drawn here.
        GEngine->FlushQueues();
        GEngine->EnableReorderQueues(false);
        // PassOrder, not distance: a fresh decal carries distance2~=0
        // (SetAutoCenter(false)) and would tie the road tile under the vehicle,
        // letting the road repaint over it.  Re-sorted by distance below.
        QSort(_drawMergers.Data(), nDraw, CmpSurfaceObj);
        for (int i = 0; i < nDraw; i++)
        {
            SortObject* oi = _drawMergers[i];
            if (oi->drawLOD == LOD_INVISIBLE)
                continue;
            // Mirror exactly the post-shadow blend branch that skips these
            // objects (passNum<=1 && HasBlendSections && surface).  With grass
            // enabled a surface object is passNum==2 (whole-alpha) and is drawn
            // by the passNum==2 branch below; drawing it here too would
            // double-draw it and re-overwrite the shadow.  The cheap passNum
            // gate also avoids the per-object Special() probe for non-surface
            // objects.
            if (oi->passNum > 1)
                continue;
            if (!IsSurfaceSortObject(oi))
                continue;
            LODShape* shp = oi->object->GetShape();
            Shape* lvl = shp ? shp->LevelOpaque(oi->drawLOD) : nullptr;
            if (!lvl || !lvl->HasBlendSections())
                continue;
            GSectionFilter = SectionClassFilter::BlendOnly;
            DrawSortObject(oi);
            GSectionFilter = SectionClassFilter::All;
        }
        GEngine->FlushQueues();
        GEngine->EnableReorderQueues(true);

        // Shadow-map depth pass (durable shadow fix; off by default).  Render the
        // visible casters' geometry from the sun into the cascade depth maps.
        // Invisible on its own — the lit shaders sample it once enabled; the
        // projected accumulator still runs.  Body in SceneShadowPass.cpp (keeps
        // Scene.cpp under the file-size limit).
        if (GEngine->ShadowMapsEnabled())
        {
            // Sun shadows fade out at dusk and vanish at night (no sun above the
            // horizon = no sun shadow), as the projected path and OFP/ArmA/FP do.
            // NightEffect is 0 in daylight, ramps through twilight, 1 at night. The
            // lit shaders fade the darkness by this factor; skip the depth pass once
            // it is fully dark (nothing would be visible, and a stale map is hidden
            // because the darkness factor is 0).
            float sunFactor = 1.0f - _mainLight->NightEffect();
            sunFactor = floatMax(0.0f, floatMin(1.0f, sunFactor));
            GEngine->SetShadowMapSunFactor(sunFactor);
            if (sunFactor > 0.01f)
            {
                RenderShadowMapDepthPass(nDraw);
            }
        }

        // Projected accumulator shadows — the fallback path.  When shadow maps are
        // on, the lit shaders darken receivers from the depth map instead, so skip
        // this to avoid two overlapping shadows.
        if (!GEngine->ShadowMapsEnabled())
        {
            // Frozen-pose caster accounting for the projected-shadow cache: Object::PrepareShadow
            // bumps gShadowFrozenRouted each time a settled corpse / stopped vehicle is served from
            // the cache instead of re-projected.  Reset per pass, then publish the count the
            // triShadowAssertFrozenCasts verb reads.
            extern int gShadowFrozenCasters;
            extern int gShadowFrozenRouted;
            GEngine->BeginShadowPass();
            gShadowFrozenRouted = 0;
            for (int i = 0; i < nDraw; i++)
            {
                SortObject* oi = _drawMergers[i];
                if (oi->shadowLOD == LOD_INVISIBLE)
                {
                    continue;
                }
                DrawExShadow(oi);
            }
            GEngine->EndShadowPass();
            gShadowFrozenCasters = gShadowFrozenRouted;
        }
    }
#endif
    // draw alpha parts of roads
    GEngine->FlushQueues();
    GEngine->EnableReorderQueues(false);
    QSort(_drawMergers.Data(), _drawMergers.Size(), CmpRevAlphaSortObj);
    // last draw alpha objects (not roads - they are already drawn)
    for (int i = 0; i < nDraw; i++)
    {
        SortObject* oi = _drawMergers[i];
        LODShape* shape = oi->object->GetShape();
        if (oi->drawLOD == LOD_INVISIBLE)
        {
            continue;
        }
        PoseidonAssert(oi->drawLOD >= 0);
        Shape* sShape = shape->LevelOpaque(oi->drawLOD);
        if (!sShape)
        {
            continue;
        }
        if (oi->passNum == 2)
        {
            // whole-alpha object (cloudlets, <0xd0 objects): draw all of it here
            DrawSortObject(oi);
#if DO_STAT
            Alpha.Count(shape->Name());
#endif
        }
        else if (oi->passNum <= 1 && sShape->HasBlendSections())
        {
            // Surface overlays (roads/decals: asfaltka/cesta) own their visible
            // surface as a blend section; those drew before the shadow pass so
            // the shadow darkens on top of them — skip here (see the pre-shadow
            // surface-blend pass above).
            if (IsSurfaceSortObject(oi))
            {
                continue;
            }
            // opaque object that owns translucent (blend) sections — e.g. a vehicle's
            // glass. Its opaque+cutout sections drew in pass 1; revisit it here, in
            // back-to-front order, drawing only the blend sections so they blend over
            // the already-drawn scene behind them (depth-write stays on — original parity).
            GSectionFilter = SectionClassFilter::BlendOnly;
            DrawSortObject(oi);
            GSectionFilter = SectionClassFilter::All;
#if DO_STAT
            Alpha.Count(shape->Name());
#endif
        }
    }
    GEngine->EnableReorderQueues(true);
}

#include <Poseidon/Foundation/Common/Filenames.hpp>

void Scene::DrawObjectsAndShadowsPass3()
{
#if DRAW_OBJS
    int i, nDraw = _drawMergers.Size();
    // pass 3 - draw cockpits after all external alpha effects
    for (i = 0; i < nDraw; i++)
    {
        SortObject* oi = _drawMergers[i];
        LODShape* shape = oi->object->GetShape();
        if (oi->drawLOD != LOD_INVISIBLE)
        {
            PoseidonAssert(oi->drawLOD >= 0);
            Shape* sShape = shape->LevelOpaque(oi->drawLOD);
            if (!sShape)
            {
                continue;
            }
            if (oi->passNum == 3)
            {
                {
                    DrawSortObject(oi);
                }
            }
        }
    }
#endif

#if DO_STAT
    LOG_DEBUG(Graphics, "Alpha objects");
    Alpha.Report();
    LOG_DEBUG(Graphics, "Opaque objects");
    Opaque.Report();
    LOG_DEBUG(Graphics, "Shadow objects");
    Shadow.Report();
#endif
}

void Scene::DrawReflections(const WaterLevel& water) {}

void Scene::DrawCollisionStar(Vector3Par pos, float size, PackedColor color)
{
    if (_collisionStar.NotNull() && size >= 0.01)
    {
        Ref<Object> star = new ObjectColored(_collisionStar->GetShape(), -1);
        star->SetTransform(M4Identity);
        star->SetPosition(pos);
        star->SetScale(size * 4);
        star->SetConstantColor(color);
        GetLandscape()->ShowObject(star);
    }
}

void Scene::DrawDiagModel(Vector3Par pos, LODShapeWithShadow* shape, float size, PackedColor color)
{
    Ref<Object> star = new ObjectColored(shape, -1);
    star->SetTransform(M4Identity);
    star->SetPosition(pos);
    star->SetScale(size / shape->BoundingSphere());
    star->SetConstantColor(color);
    GetLandscape()->ShowObject(star);
}

void Scene::DrawVolumeLight(LODShapeWithShadow* shape, PackedColor color, const Frame& pos, float size)
{
    if (!shape)
    {
        static bool warned = false;
        if (!warned)
        {
            LOG_ERROR(Graphics, "Skipping visible light volume because its shape failed to load");
            warned = true;
        }
        return;
    }

    Ref<Object> object = new ObjectColored(shape, -1);
    // draw light shape
    object->SetTransform(pos.Transform());
    object->SetPosition(object->PositionModelToWorld(shape->BoundingCenter()));
    // ObjectForDrawing(object,-1,(ENGINE_CONFIG.reflections&REFL_OBJECT)!=0);

    object->SetConstantColor(color);
    object->SetScale(size);
    ObjectForDrawing(object);
}

void Scene::DrawExShadow(SortObject* oi)
{
    // calculate position of the shadow of the object top
    Object* obj = oi->object;
    if (!obj)
    {
        return;
    }
    int level = oi->shadowLOD;
    LODShapeWithShadow* shape = oi->shape;
    Vector3Val objPos = obj->Position();
    Point3 shadowPos = objPos;
    const FrameBase& pos = *obj;

    if (!ShadowPos(objPos, shadowPos, _mainLight))
    {
        return;
    }

    Ref<Shape> sShape = obj->PrepareShadow(level, shadowPos, pos);
    // check bbox clipping and occlusion

    // per object clipping possible - bounding sphere was recalculated
    if (!sShape)
    {
        return; // no shadow
    }
    if (sShape->NPos() <= 0)
    {
        return;
    }

    float bRadius = sShape->BSphereRadius() * pos.Scale();
    Vector3Val bCenterM = sShape->BSphereCenter();
    Vector3Val bCenter = pos.PositionModelToWorld(bCenterM);
    // try to clip bounding sphere
    // if whole bounding sphere is out, object is already skipped
    if (GScene->GetCamera()->IsClipped(bCenter, bRadius, 1))
    {
        return;
    }
    ClipFlags clipFlags = GScene->GetCamera()->MayBeClipped(bCenter, bRadius, 1);
    if (EnableObjOcc)
    {
        bool occluded = false;
        if (shape->CanBeOccluded())
        {
            if (FarEnoughForOcclusion(oi))
            {
                // check occlusion - bbox recalculation
                Vector3 minMax[2];
                Vector3Val pos0 = sShape->Pos(0);
                minMax[0] = pos0;
                minMax[1] = pos0;
                for (int i = 0; i < sShape->NPos(); i++)
                {
                    Vector3Val posI = sShape->Pos(i);
                    CheckMinMaxIter(minMax[0], minMax[1], posI);
                }

                // check if object is occluded by objects
                if (!GetOcclusions()->TestBBox(pos, minMax, clipFlags))
                {
                    occluded = true;
                }
            }
        }
        if (occluded)
        {
            return;
        }
    }

    // first draw shadows of some proxies?
    for (int i = 0; i < obj->GetProxyCount(level); i++)
    {
        if (!obj->CastProxyShadow(level, i))
        {
            continue;
        }
        Matrix4 trans = obj->Transform(), invTrans = obj->GetInvTransform();

        LODShapeWithShadow* pshape = nullptr;
        Object* proxy = obj->GetProxy(pshape, level, trans, invTrans, *obj, i);
        if (!proxy)
        {
            continue;
        }
        // note: it is not sure we need to draw shadows of all proxies
        // proxy shadow must be always calculated dynamically
        // we do not want to draw proxies of complex objects like vehicle crews

        if (!pshape)
        {
            continue;
        }
        float dist2 = trans.Position().Distance2(GetCamera()->Position());
        int plevel =
            LevelShadowFromDistance2(pshape, dist2, trans.Scale(), trans.Direction(), GetCamera()->Direction());
        if (plevel != LOD_INVISIBLE)
        {
            plevel = pshape->FindNearestWithoutProperty(plevel, "lodnoshadow");
            if (plevel < 0)
            {
                plevel = LOD_INVISIBLE;
            }
        }
        if (plevel == LOD_INVISIBLE)
        {
            continue;
        }

        proxy->Animate(plevel);

        FrameWithInverse pframe(trans, invTrans);
        if (proxy->RecalcShadow(plevel, pframe, true, pshape))
        {
            LODShape* shadowShape = pshape->Shadow();
            Ref<Shape> ret = shadowShape->LevelOpaque(plevel);
            // we can draw shape ret now as shadow
            proxy->DrawShadow(ret, shadowPos, ClipAll, pframe);
        }

        proxy->Deanimate(plevel);
    }
    /**/

    // obj->DrawShadow(shadow,shadowPos,ClipAll,pos);
    obj->DrawShadow(sShape, shadowPos, clipFlags, pos);

#if DO_STAT
    Shadow.Count(oi->shape->Name());
#endif
}

// different global variables

void ManCleanUp();

void ClearShapes()
{
    VehicleTypes.Clear();
    // WeaponInfos.Clear();
    RoadTypes.Clear();
    // log any shape still resident in the cache at shutdown (refs would
    // be from cache itself plus any holdouts on the entity side).
    Shapes.ForEach([](LODShapeWithShadow& shape)
                   { Log("Shape %s not released (%d refs).", shape.Name(), shape.RefCounter()); });
    Shapes.Clear();

    MagazineTypes.Clear();
    WeaponTypes.Clear();
}

bool Scene::ShadowPos(Vector3Par pos, Vector3& aprox, LightSun* light)
{ //
    Vector3Val dir = light->ShadowDirection();
    if (dir.Y() > 0)
    {
        return false; // shadow casted up - no real shadow
    }
    const float maxShadow = 300;
    float t = GetLandscape()->IntersectWithGround(&aprox, pos, dir, 0, maxShadow * 1.1);
    if (t > maxShadow)
    {
        // no intersection
        return false;
    }
    if (t <= 0.01f)
    {
        // up -> on surface required
        aprox[1] = GLandscape->SurfaceY(aprox[0], aprox[2]);
    }
    return true;
}
