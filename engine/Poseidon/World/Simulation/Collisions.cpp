#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <stdlib.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/Array2D.hpp>
#include <Poseidon/Foundation/Containers/StreamArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Memory/MemAlloc.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

using namespace Poseidon;
namespace Poseidon
{
using Poseidon::Foundation::MStorage;
} // namespace Poseidon
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Entities/Vehicles/Vehicle.hpp>
#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/World/Scene/Thing.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>
#define PROFILE_COLLISIONS 1

#if PROFILE_COLLISIONS
#else
#define START_PROFILE(name)
#define END_PROFILE(name)
#define PROFILE_SCOPE(name)
#endif

#if _ENABLE_CHEATS
#define STARS 1
#endif

void ObjRadiusRectangle(int& xMin, int& xMax, int& zMin, int& zMax, Vector3Par oPos, Vector3Par nPos, float radius)
{
    const float maxObjRadius = 25;
    radius += maxObjRadius;
    float xMinF = floatMin(nPos.X(), oPos.X()) - radius;
    float xMaxF = floatMax(nPos.X(), oPos.X()) + radius;
    float zMinF = floatMin(nPos.Z(), oPos.Z()) - radius;
    float zMaxF = floatMax(nPos.Z(), oPos.Z()) + radius;
    xMin = toIntFloor(xMinF * InvObjGrid);
    xMax = toIntFloor(xMaxF * InvObjGrid);
    zMin = toIntFloor(zMinF * InvObjGrid);
    zMax = toIntFloor(zMaxF * InvObjGrid);
    // make sure always at least one square is valid
    if (xMin < 0)
    {
        xMin = 0;
    }
    if (xMin > ObjRange - 1)
    {
        xMin = ObjRange - 1;
    }
    if (xMax < 0)
    {
        xMax = 0;
    }
    if (xMax > ObjRange - 1)
    {
        xMax = ObjRange - 1;
    }
    if (zMin < 0)
    {
        zMin = 0;
    }
    if (zMin > ObjRange - 1)
    {
        zMin = ObjRange - 1;
    }
    if (zMax < 0)
    {
        zMax = 0;
    }
    if (zMax > ObjRange - 1)
    {
        zMax = ObjRange - 1;
    }
}

void ObjRadiusRectangle(int& xMin, int& xMax, int& zMin, int& zMax, Vector3Par oPos, float radius)
{
    float xMinF = oPos.X() - radius;
    float xMaxF = oPos.X() + radius;
    float zMinF = oPos.Z() - radius;
    float zMaxF = oPos.Z() + radius;
    xMin = toIntFloor(xMinF * InvObjGrid);
    xMax = toIntFloor(xMaxF * InvObjGrid);
    zMin = toIntFloor(zMinF * InvObjGrid);
    zMax = toIntFloor(zMaxF * InvObjGrid);
    // make sure always at least one square is valid
    if (xMin < 0)
    {
        xMin = 0;
    }
    if (xMin > ObjRange - 1)
    {
        xMin = ObjRange - 1;
    }
    if (xMax < 0)
    {
        xMax = 0;
    }
    if (xMax > ObjRange - 1)
    {
        xMax = ObjRange - 1;
    }
    if (zMin < 0)
    {
        zMin = 0;
    }
    if (zMin > ObjRange - 1)
    {
        zMin = ObjRange - 1;
    }
    if (zMax < 0)
    {
        zMax = 0;
    }
    if (zMax > ObjRange - 1)
    {
        zMax = ObjRange - 1;
    }
}

extern Ref<Object> DrawDiagLine(Vector3Par from, Vector3Par to, PackedColor color);

#define DIAG 0
#define DIAG_LAND 0
#define DIAG_VISIBLE_THROUGH 0

void Landscape::ObjectCollision(CollisionBuffer& retVal, Object* with, const Frame& withPos, bool onlyVehicles) const
{
    if (!with->GetShape()->GeometryLevel())
    {
        return; // no geometry - no test
    }
    Vector3Val nPos = withPos.Position();
    Vector3Val oPos = with->Position();
    float radius = with->GetRadius();
    int xMin, xMax, zMin, zMax;
    ObjRadiusRectangle(xMin, xMax, zMin, zMax, oPos, nPos, radius);
    int x, z;
    for (x = xMin; x <= xMax; x++)
    {
        for (z = zMin; z <= zMax; z++)
        {
            const ObjectList& list = _objects(x, z);
            int n = list.Size();
            for (int i = 0; i < n; i++)
            {
                Object* obj = list[i];
                if (obj == with)
                {
                    continue;
                }
                if (obj->GetType() == Network && obj->GetShape()->FindGeometryLevel() < 0)
                {
                    continue; // no collisions with roads
                }
                if (obj->GetType() == Temporary)
                {
                    continue; // no collisions with temporary
                }
                if (obj->GetType() == TypeTempVehicle)
                {
                    continue;
                }
                if (onlyVehicles)
                {
                    if (obj->Static())
                    {
                        continue;
                    }
                }
                // do not collide with "soft" dead objects (man bodies, trees)
                if (!obj->HasGeometry())
                {
                    continue;
                }
                // ObjectCollision(retVal,obj,with,withPos,prec);
                obj->Intersect(retVal, with);
            }
        }
    }
}

struct CheckObjectCollisionContext
{
    Object *with, *ignore;
    Vector3 beg, end;
    float radius;
    ObjIntersect type;
};

void Landscape::CheckObjectCollision(int x, int z, CollisionBuffer& retVal, CheckObjectCollisionContext& context) const
{
    if (!this_InRange(x, z))
    {
        return;
    }
    const ObjectList& list = _objects(x, z);
    int n = list.Size();
    for (int i = 0; i < n; i++)
    {
        Object* obj = list[i];
        if (obj == context.with)
        {
            continue;
        }
        if (obj == context.ignore)
        {
            continue;
        }
        if (obj->GetType() == Network && obj->GetShape()->FindGeometryLevel() < 0)
        {
            continue; // no collisions with roads
        }
        if (obj->GetType() == Temporary)
        {
            continue; // no collisions with temporary
        }
        if (obj->GetType() == TypeTempVehicle)
        {
            continue;
        }
        obj->Intersect(retVal, context.beg, context.end, context.radius, context.type);
    }
}

void Landscape::ObjectCollision(CollisionBuffer& retVal, Object* with, Object* ignore, Vector3Par beg, Vector3Par end,
                                float radius, ObjIntersect type) const
{
    int xMin, xMax, zMin, zMax;
    ObjRadiusRectangle(xMin, xMax, zMin, zMax, beg, end, radius);
    int x, z;
    Vector3 boundingCenter = (end + beg) * 0.5;
    float boundingSphere = end.Distance(beg) * 0.5;

#define DDA_OPTIMIZED 0
#if DDA_OPTIMIZED
    if (boundingSphere < 50)
#endif
    {
        for (x = xMin; x <= xMax; x++)
        {
            for (z = zMin; z <= zMax; z++)
            {
                const ObjectList& list = _objects(x, z);
                int n = list.Size();
                for (int i = 0; i < n; i++)
                {
                    Object* obj = list[i];
                    if (obj == with)
                    {
                        continue;
                    }
                    if (obj == ignore)
                    {
                        continue;
                    }
                    if (obj->GetType() == Network && obj->GetShape()->FindGeometryLevel() < 0)
                    {
                        continue; // no collisions with roads
                    }
                    if (obj->GetType() == Temporary)
                    {
                        continue; // no collisions with temporary
                    }
                    if (obj->GetType() == TypeTempVehicle)
                    {
                        continue;
                    }
                    float dist2 = obj->Position().Distance2Inline(boundingCenter);
                    float maxDist2 = Square(boundingSphere + obj->GetRadius());
                    if (dist2 > maxDist2)
                    {
                        continue;
                    }
                    obj->Intersect(retVal, beg, end, radius, type);
                }
            }
        }
    }
#if DDA_OPTIMIZED
    else
    {
        CheckObjectCollisionContext context;
        context.beg = beg;
        context.end = end;
        context.ignore = ignore;
        context.with = with;
        context.radius = radius;
        context.type = type;

        // assume max. 50 m objects present
        int border = toIntFloor(1.5 + 50 * _invLandGrid);

        int ox = toIntFloor(end.X() * _invLandGrid);
        int oz = toIntFloor(end.Z() * _invLandGrid);
        int sx = toIntFloor(beg.X() * _invLandGrid);
        int sz = toIntFloor(beg.Z() * _invLandGrid);
        int aox = abs(ox - sx), aoz = abs(oz - sz);
        const float sBorder = 1.5;

        if (aox > aoz)
        {
            // mainly horizontal - primary coordinate is x, derived is z
            float dd = float(oz - sz) / aox;
            int pd = ox > sx ? +1 : -1;
            int p = sx - pd * border;
            float d = sz - dd * border;
            int plen = aox + 2 * border + 1;
            for (int i = plen; --i >= 0;)
            {
                int dis = toIntFloor(d - sBorder);
                int die = toIntFloor(d + sBorder);
                for (int di = dis; di <= die; di++)
                {
                    CheckObjectCollision(p, di, retVal, context);
                }
                d += dd;
                p += pd;
            }
        }
        else if (aoz != 0)
        {
            // mainly vertical - primary coordinate is z, derived is x
            float dd = float(ox - sx) / aoz;
            int pd = oz > sz ? +1 : -1;
            int p = sz - pd * border;
            PoseidonAssert(abs(p) < 2000);
            float d = sx - dd * border;
            int plen = aoz + 2 * border + 1;
            for (int i = plen; --i >= 0;)
            {
                int dis = toIntFloor(d - sBorder);
                int die = toIntFloor(d + sBorder);
                for (int di = dis; di <= die; di++)
                {
                    CheckObjectCollision(p, di, retVal, context);
                }
                d += dd;
                p += pd;
            }
        }
        else
        {
            int ssx, ssz;
            for (ssx = -border; ssx <= border; ssx++)
                for (ssz = -border; ssz <= border; ssz++)
                {
                    CheckObjectCollision(ox + ssx, oz + ssz, retVal, context);
                }
        }
    }
#endif
}

float Landscape::UnderRoadSurface(const Object* obj, Vector3Par modelPos, float bumpy, float* dX, float* dZ,
                                  Texture** texture) const
{
    // if we are on road, return road surface parameters
    LODShape* lShape = obj->GetShape();
    Shape* shape = lShape->RoadwayLevel();
    PoseidonAssert(shape);
    if (!(shape->GetOrHints() & ClipLandOn))
    {
        float maxY = -1e10;
        float maxDX = 0, maxDZ = 0;
        Texture* maxTexture = nullptr;
        int i = 0;
        shape->InitPlanes();
        for (Offset f = shape->BeginFaces(), e = shape->EndFaces(); f < e; shape->NextFace(f), i++)
        {
            const Poly& face = shape->Face(f);
            const Plane& plane = shape->GetPlane(i);
            if (plane.Normal().Y() > -0.2f)
            {
                // ignore roadways that are too steep
                continue;
            }
            float y, dX, dZ;
            float under = plane.Distance(modelPos);
// max 1 m step allowed
#if STARS
            if (CHECK_DIAG(DECollision))
            {
                Vector3 sPos;
                LOG_DEBUG(Physics, "ModelPos {:.1f},{:.1f},{:.1f}", modelPos[0], modelPos[1], modelPos[2]);
                sPos = modelPos;
                obj->PositionModelToWorld(sPos, sPos);
                LOG_DEBUG(Physics, "      -- world {:.1f},{:.1f},{:.1f}", sPos[0], sPos[1], sPos[2]);
                _world->GetScene()->DrawCollisionStar(sPos, 0.05, PackedColor(Color(0, 0.5, 0)));
                // calculate nearest point on the plane
                sPos = modelPos - plane.Normal() * under;
                LOG_DEBUG(Physics, "ModelPos+under {:.1f},{:.1f},{:.1f}", sPos[0], sPos[1], sPos[2]);
                obj->PositionModelToWorld(sPos, sPos);
                LOG_DEBUG(Physics, "      -- world {:.1f},{:.1f},{:.1f}", sPos[0], sPos[1], sPos[2]);
                _world->GetScene()->DrawCollisionStar(sPos, 0.035, PackedColor(Color(0, 0.5, 0.5)));
            }
#endif
            if (under <= -0.5f || under > 1.0f)
            {
                continue;
            }
            if (face.InsideFromTop(*shape, plane, modelPos, &y, &dX, &dZ))
            {
#if STARS
                if (CHECK_DIAG(DECollision))
                {
                    Vector3 sPos = modelPos;
                    sPos[1] = y;
                    LOG_DEBUG(Physics, "result     {:.1f},{:.1f},{:.1f}", sPos[0], sPos[1], sPos[2]);
                    obj->PositionModelToWorld(sPos, sPos);
                    LOG_DEBUG(Physics, "  -- world {:.1f},{:.1f},{:.1f}", sPos[0], sPos[1], sPos[2]);
                    _world->GetScene()->DrawCollisionStar(sPos, 0.025, PackedColor(Color(0.5, 0.5, 0)));
                }
#endif
                if (maxY < y)
                {
                    maxY = y, maxDX = dX, maxDZ = dZ;
                    maxTexture = face.GetTexture();
                }
            }
        }
        if (maxY >= -1e3)
        {
            if (dX || dZ)
            {
                Vector3 normal(-maxDX, 1, -maxDZ);
                obj->DirectionModelToWorld(normal, normal);
                if (fabs(normal.Y()) > 1e-2)
                {
                    maxDX = -normal.X() / normal.Y();
                    maxDZ = -normal.Z() / normal.Y();
                }
                else
                {
                    maxDX = 0;
                    maxDZ = 0;
                }

                if (dX)
                {
                    *dX = maxDX;
                }
                if (dZ)
                {
                    *dZ = maxDZ;
                }
            }
            if (texture)
            {
                *texture = maxTexture;
            }
            return maxY - modelPos.Y();
        }
    }
    else
    {
        float minUnder = 1e10;
        float maxDX = 0, maxDZ = 0;
        Texture* maxTexture = nullptr;
        int i = 0;
        shape->InitPlanes();
        for (Offset f = shape->BeginFaces(), e = shape->EndFaces(); f < e; shape->NextFace(f), i++)
        {
            const Poly& face = shape->Face(f);
            const Plane& plane = shape->GetPlane(i);
            float y, dX, dZ;
            if (face.InsideFromTop(*shape, plane, modelPos, &y, &dX, &dZ))
            {
                Vector3 wPos = obj->PositionModelToWorld(modelPos);
                float y = SurfaceY(wPos[0], wPos[2], &dX, &dZ);
                float under = y - wPos.Y();
#if STARS
                if (CHECK_DIAG(DECollision))
                {
                    Vector3 spos = wPos;
                    spos[1] = y;
                    _world->GetScene()->DrawCollisionStar(spos, 0.025, PackedColor(Color(0.5, 0.5, 0)));
                }
#endif
                // Log("under %f",under);
                if (under < -1.0f)
                {
                    continue; // avoid checking landscape right above roadway
                }
                // the face is not sure to be a triangle
                if (minUnder > under)
                {
                    minUnder = under, maxDX = dX, maxDZ = dZ;
                    maxTexture = face.GetTexture();
                }
            }
        }
        if (minUnder < 1e3)
        {
            if (dX)
            {
                *dX = maxDX;
            }
            if (dZ)
            {
                *dZ = maxDZ;
            }
            if (texture)
            {
                *texture = maxTexture;
            }
            return minUnder;
        }
    }
    // otherwise return no collision
    return -10;
}

void Landscape::GroundCollision(GroundCollisionBuffer& retVal, Object* with, const Frame& withPos, float above,
                                float bumpy, bool enableLandcontact, bool soldier) const
{
    Vector3Val wPos = withPos.Position();

    // for normal orientation use LandContact LOD level
    int level = with->GetShape()->FindLandContactLevel();
    if (!soldier)
    {
        // soldier uses always landcontact
        if (!enableLandcontact || level < 0)
        {
            level = with->GetShape()->FindGeometryLevel();
        }
        else
        {
            float dx, dz;
            float posY = SurfaceY(wPos.X(), wPos.Z(), &dx, &dz);
            (void)posY;

            Vector3 normal(-dx, 1, -dz);
            if (posY < GetSeaLevel())
            {
                normal = VUp;
            }

            float surfaceAngle = normal.CosAngle(withPos.DirectionUp());
            if (surfaceAngle < 0.86)
            {
                level = with->GetShape()->FindGeometryLevel();
            }
        }
    }
    if (level < 0)
    {
        return;
    }
    Shape* withShape = with->GetShape()->LevelOpaque(level);
    if (!withShape)
    {
        Fail("!withShape");
        return;
    }

    int xMin, xMax, zMin, zMax;
    float wRad = with->GetRadius();
    ObjRadiusRectangle(xMin, xMax, zMin, zMax, wPos, wPos, wRad);
    int x, z;
    with->Animate(level);
    Matrix4Val withTrans = withPos.Transform();
    for (int j = 0; j < withShape->NPos(); j++)
    {
        Point3 pos(VFastTransform, withTrans, withShape->Pos(j));
        float dX, dZ;

        Texture* texture = nullptr;
        float surfaceY;
        float bump = 0;
        if (bumpy > 0)
        {
            surfaceY = BumpySurfaceY(pos[0], pos[2], dX, dZ, texture, bumpy, bump);
        }
        else
        {
            surfaceY = SurfaceY(pos[0], pos[2], &dX, &dZ, &texture);
        }
        float underLevel = surfaceY + above - pos[1];
        if (surfaceY < _seaLevelWave)
        {
            // simulate water "bump" waves
            float bumpySea = _seaLevelWave;
            if (bumpy)
            {
                float bumpX = pos[0] * 0.3;
                float bumpZ = pos[2] * 0.3;
                int iBumpX = toIntFloor(bumpX);
                int iBumpZ = toIntFloor(bumpZ);
                float bump00 = _randGen.RandomValue(_randGen.GetSeed(iBumpX, iBumpZ));
                float bump01 = _randGen.RandomValue(_randGen.GetSeed(iBumpX + 1, iBumpZ));
                float bump10 = _randGen.RandomValue(_randGen.GetSeed(iBumpX, iBumpZ + 1));
                float bump11 = _randGen.RandomValue(_randGen.GetSeed(iBumpX + 1, iBumpZ + 1));

                bumpX -= iBumpX; // relative in-bump coordinates
                bumpZ -= iBumpZ;
                // bilinear interpolation of bumpY
                float bump0 = bump00 + (bump01 - bump00) * bumpX;
                float bump1 = bump10 + (bump11 - bump10) * bumpX;
                float bump = bump0 + (bump1 - bump0) * bumpZ; // result in range 0 .. 1
                bumpySea -= bump * 0.5;
            }

            // water on level bumpySea
            float underWater = above + bumpySea - pos[1];
            if (underWater <= 0)
            {
                continue; // above water, must be above ground
            }
            UndergroundInfo& info = retVal.Append();
            info.texture = texture;
            info.obj = nullptr;
            info.under = underWater;
            info.pos = pos;
            info.pos[1] = bumpySea; // applied directly on surface
            info.dX = 0;
            info.dZ = 0;
            info.vertex = j;
            info.level = level;
            info.type = GroundWater;
        }

        // Now we know we are in contact with the ground. Take bump into account.
        underLevel += bump;
        if (underLevel <= 0)
        {
            continue;
        }
        UndergroundInfo& info = retVal.Append();
        info.texture = texture;
        info.obj = nullptr;
        info.under = underLevel;
        info.pos = pos;
        info.dX = dX;
        info.dZ = dZ;
        info.vertex = j;
        info.level = level;
        info.type = GroundSolid;
    }
    int nLands = retVal.Size();
    for (z = zMin; z <= zMax; z++)
    {
        for (x = xMin; x <= xMax; x++)
        {
            // Point3 pos(xC,0,zC);
            const ObjectList& list = _objects(x, z);
            int n = list.Size();
            for (int i = 0; i < n; i++)
            {
                Object* obj = list[i];
                LODShape* objShape = obj->GetShape();
                if (!objShape)
                {
                    continue;
                }
                int roadwayLevel = objShape->FindRoadwayLevel();

                if (roadwayLevel < 0)
                {
                    continue;
                }
                if (obj == with)
                {
                    continue;
                }

                float oRad = obj->GetRadius();
                if (obj->Position().Distance2Inline(wPos) > Square(oRad + wRad))
                {
                    continue;
                }

                obj->Animate(roadwayLevel);
                objShape->RoadwayLevel()->RecalculateNormalsAsNeeded();

                Matrix4Val fromWithToObj = obj->GetInvTransform() * withTrans;

                for (int j = 0; j < withShape->NPos(); j++)
                {
                    Vector3Val wPos = withShape->Pos(j);
                    // check against topplane equation
                    // if( wTopPlane.Distance(wPos)<0 ) continue; // no colision is possible
                    Point3 pos(VFastTransform, fromWithToObj, wPos - Vector3(0, above, 0));
                    float dX, dZ; // plane differential in obj model space
                    Texture* texture = nullptr;
                    float underLevel = UnderRoadSurface(obj, pos, bumpy, &dX, &dZ, &texture);
                    if (underLevel <= -1.5f)
                    {
                        continue;
                    }
                    if (!soldier && underLevel < -0.15)
                    {
                        continue;
                    }
                    UndergroundInfo info;
                    info.texture = texture;
                    info.obj = obj;
                    info.under = underLevel;
                    info.pos = obj->PositionModelToWorld(pos);
                    info.dX = dX;
                    info.dZ = dZ;

                    info.vertex = j;
                    info.level = level;
                    info.type = GroundSolid;
                    // prefer road to deal with landscape bumps
                    float preferRoad = bumpy * 0.5;
                    bool noAdd = false;
                    for (int p = 0; p < nLands; p++)
                    {
                        UndergroundInfo& pInfo = retVal[p];
                        if (pInfo.vertex != j)
                        {
                            continue;
                        }
                        // prefer road: delete land info already present if road is higher
                        if (pInfo.under <= underLevel + preferRoad)
                        {
                            retVal.Delete(p);
                            nLands--;
                        }
                        else
                        {
                            noAdd = true;
                        }
                        break;
                    }
                    for (int p = nLands; p < retVal.Size(); p++)
                    {
                        UndergroundInfo& pInfo = retVal[p];
                        if (pInfo.vertex != j)
                        {
                            continue;
                        }
                        if (pInfo.under <= underLevel)
                        {
                            retVal.Delete(p);
                        }
                        else
                        {
                            noAdd = true;
                        }
                        break;
                    }
                    if (!noAdd)
                    {
                        retVal.Add(info);
                    }
                }
                obj->Deanimate(roadwayLevel);
            }
        }
    }
    with->Deanimate(level);

#if STARS
    if (CHECK_DIAG(DECollision))
    {
        int i = 0;
        for (; i < nLands; i++)
        {
            UndergroundInfo& info = retVal[i];
            _world->GetScene()->DrawCollisionStar(info.pos, 0.05, PackedColor(Color(1, 1, 0)));
        }
        for (; i < retVal.Size(); i++)
        {
            UndergroundInfo& info = retVal[i];
            _world->GetScene()->DrawCollisionStar(info.pos, 0.05, PackedColor(Color(1, 0, 0)));
        }
    }
#endif
}

void Landscape::GroundCollisionPlane(GroundCollisionBuffer& retVal, Object* with, const Frame& withPos, float above,
                                     float bumpy, bool enableLandcontact)
{
    // for normal orientation use LandContact LOD level
    int level = with->GetShape()->FindLandContactLevel();
    if (level < 0 || withPos.DirectionUp().Y() < 0.7 || !enableLandcontact)
    {
        level = with->GetShape()->FindGeometryLevel();
    }
    if (level < 0)
    {
        return;
    }
    Shape* withShape = with->GetShape()->LevelOpaque(level);
    PoseidonAssert(withShape);

    with->Animate(level);

    float dX, dZ;
    Texture* texture = nullptr;
    Vector3 comDown = with->GetCenterOfMass();
    comDown[1] = withShape->Min().Y() + above;
    Vector3 pos = withPos.Transform().FastTransform(comDown);
    pos[1] += 0.5; // max. 0.5 m above lowest object point
    Object* obj = nullptr;
    float surfaceY = RoadSurfaceY(pos, &dX, &dZ, &texture, &obj);
    Vector3 normal(-dX, 1, -dZ);
    Vector3 point(pos.X(), surfaceY + above, pos.Z());
    normal.Normalize();
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DECollision))
    {
        GScene->DrawCollisionStar(pos, 0.06, PackedColor(Color(1, 0, 0)));
        GScene->DrawCollisionStar(point, 0.1, PackedColor(Color(1, 0.5, 0)));
    }
#endif

    Plane plane(normal, point);

    float waterY = GetSeaLevel();

    Matrix4Val withTrans = withPos.Transform();
    for (int j = 0; j < withShape->NPos(); j++)
    {
        Vector3 jpos = withTrans * withShape->Pos(j);
        if (jpos.Y() < waterY)
        {
            UndergroundInfo& info = retVal.Append();
            info.texture = texture;
            info.obj = nullptr;
            info.under = waterY - jpos.Y();
            info.pos = jpos;
            info.pos[1] = waterY;
            info.dX = 0;
            info.dZ = 0;
            info.vertex = j;
            info.level = level;
            info.type = GroundWater;
        }

        float underLevel = -plane.Distance(jpos);

        underLevel += CalculateBump(jpos.X(), jpos.Z(), texture, bumpy);
        if (underLevel <= 0)
        {
            continue;
        }
        UndergroundInfo& info = retVal.Append();
        info.texture = texture;
        info.obj = obj;
        info.under = underLevel;
        info.pos = jpos;
        info.dX = dX;
        info.dZ = dZ;
        info.vertex = j;
        info.level = level;
        info.type = GroundSolid;
    }
    with->Deanimate(level);
}

void Landscape::ExplosionDammageEffects(EntityAI* owner, Shot* shot, Object* directHit, Vector3Par pos, Vector3Par dir,
                                        const AmmoType* type, bool enemyDammage)
{
    if (!type)
    {
        return;
    }

    AIUnit* ownerUnit = owner ? owner->CommanderUnit() : nullptr;
    AIGroup* ownerGroup = ownerUnit ? ownerUnit->GetGroup() : nullptr;
    AICenter* ownerCenter = ownerGroup ? ownerGroup->GetCenter() : nullptr;

    // perform hit sound
    float rndFreq = GRandGen.RandomValue() * 0.1 + 0.95;
    const RandomSound* hitSounds = &type->_hitGround;
    if (directHit)
    {
        hitSounds = &type->_hitBuilding;
        // different hit types for different objects
        EntityAI* hitAI = dyn_cast<EntityAI>(directHit);
        if (hitAI)
        {
            const VehicleType* typeAI = hitAI->GetType();
            if (typeAI->IsKindOf(GWorld->Preloaded(VTypeMan)))
            {
                hitSounds = &type->_hitMan;
            }
            else if (typeAI->IsKindOf(GWorld->Preloaded(VTypeAllVehicles)))
            {
                hitSounds = &type->_hitArmor;
            }
        }
    }
    const SoundPars& pars = hitSounds->SelectSound(GRandGen.RandomValue());
    if (pars.name.GetLength() > 0)
    {
        IWave* sound = GSoundScene->OpenAndPlayOnce(pars.name, pos, VZero, pars.vol, pars.freq * rndFreq);
        if (sound)
        {
            GSoundScene->SimulateSpeedOfSound(sound);
            GSoundScene->AddSound(sound);
        }
    }

    // small/big explosion
    bool smallExplosion = !type->explosive;
    bool water = pos.Y() < _seaLevelWave + 0.01;
    const float craterTimeCoef = 5;
    if (smallExplosion)
    {
        // crater visible - draw it
        float landAlpha = floatMin(type->hit * (1.0 / 100), 1);
        float scale = landAlpha * 0.2 * (GRandGen.RandomValue() * 0.3 + 0.8);
        float alpha = 10 * landAlpha * (GRandGen.RandomValue() * 0.3 + 0.7);
        saturateMin(alpha, 1);
        saturateMin(scale, 0.05);

        float timeToLive = 60 * scale;
        timeToLive *= craterTimeCoef;
        saturate(timeToLive, 10, 30);

        if (directHit)
        {
#if 1
            LODShapeWithShadow* shape = nullptr;
            // directHit may be a part of a hierarchy
            Vehicle* vehicleHit = dyn_cast<Vehicle>(directHit);
            Matrix4Val toModel = directHit->WorldInvTransform();
            Vector3 rPos = toModel.FastTransform(pos);
            Vector3 rDir = dir;
            Crater* crater = nullptr;
            if (vehicleHit)
            {
                CraterOnVehicle* cov = new CraterOnVehicle(shape, VehicleTypes.New("crateronvehicle"), timeToLive,
                                                           scale * 0.5, directHit, rPos, rDir);
                crater = cov;
            }
            else
            {
                crater = new Crater(shape, VehicleTypes.New("crater"), timeToLive, scale * 0.5, false, false, water);
                Matrix4 transform;
                Matrix4 toWorld = directHit->WorldTransform();
                Vector3Val wDir = toWorld.Rotate(rDir);
                Vector3 pDir = fabs(wDir.Y()) < 0.9 ? VUp : VForward;
                transform.SetDirectionAndUp(wDir, pDir);
                transform.SetScale(scale * 0.5);
                Vector3 wPos = toWorld.FastTransform(rPos) - wDir * 0.05;
                transform.SetPosition(wPos);
                crater->SetTransform(transform);
            }
            crater->SetAlpha(alpha);

            GLOB_WORLD->AddAnimal(crater);
#endif
        }
        else
        {
            LODShapeWithShadow* shape = GLOB_SCENE->Preloaded(CraterShell);
            float azimut = GRandGen.RandomValue() * H_PI * 2;

            Matrix4 transform(MIdentity);
            transform.SetOrientation(Matrix3(MRotationY, azimut));
            transform.SetScale(scale * 0.5);

            transform.SetPosition(pos);

            Crater* crater =
                new Crater(shape, VehicleTypes.New("crater"), timeToLive, scale * 0.5, false, false, water);
            crater->SetTransform(transform);
            crater->SetAlpha(alpha);
            GLOB_WORLD->AddAnimal(crater);
        }
    }
    else
    {
        float maxY = floatMax(type->indirectHitRange * 2, 0.5);
        float surfY = RoadSurfaceY(pos);
        float y = pos[1] - surfY;
        if (y < maxY)
        {
            // place explosion crater on the ground
            float hit = floatMax(type->indirectHit * type->indirectHitRange, 5);
            float landAlpha = (1 - y / maxY) * hit * (1.0 / 200);
            // if( landAlpha>0.05 )
            if (landAlpha > 0.001)
            {
                // crater visible - draw it
                saturateMin(landAlpha, 2);
                // use smoke simulation for drawing
                float scale = landAlpha * (GRandGen.RandomValue() * 0.3 + 0.8);
                float alpha = 0.5 * landAlpha * (GRandGen.RandomValue() * 0.3 + 0.7);
                float azimut = GRandGen.RandomValue() * H_PI * 2;
                Matrix4 transform(MIdentity);
                transform.SetOrientation(Matrix3(MRotationY, azimut));
                transform.SetScale(scale * 0.5);
                LODShapeWithShadow* shape = GLOB_SCENE->Preloaded(CraterShell);
                // small/big explosion

                // Preloaded() returns null if CfgScenePreload's CraterShell model failed to
                // load (ScenePreloader.cpp logs the error but doesn't exit in non-strict mode)
                // -- skip the visual offset rather than dereference a null shape.
                Vector3 offset = shape ? transform.Rotate(shape->BoundingCenter()) : VZero;
                transform.SetPosition(Vector3(pos[0], surfY, pos[2]) + offset);

                float timeToLive = scale * 60;
                timeToLive *= craterTimeCoef;
                saturateMin(timeToLive, 120);

                // create vehicle
                Crater* crater =
                    new Crater(shape, VehicleTypes.New("crater"), timeToLive, scale * 0.5, true, false, water);
                crater->SetTransform(transform);
                crater->SetAlpha(alpha);
                GLOB_WORLD->AddAnimal(crater);

#if 1
                float distance2ToCamera = GScene->GetCamera()->Position().Distance2(transform.Position());
                if (distance2ToCamera < Square(500))
                {
                    int count = toIntFloor(landAlpha * 10);
                    while (--count >= 0)
                    {
                        // some ground effects flying out
                        Vector3 vel((GRandGen.RandomValue() * 15 - 7.5) * landAlpha,
                                    (GRandGen.RandomValue() * 10) * landAlpha,
                                    (GRandGen.RandomValue() * 15 - 7.5) * landAlpha);
                        Vector3 posOffset((GRandGen.RandomValue() * 2 - 1) * scale,
                                          (GRandGen.RandomValue() * 1) * scale,
                                          (GRandGen.RandomValue() * 2 - 1) * scale);
                        Matrix4 fxTrans = transform;
                        // random orientation
                        fxTrans.SetOrientation(Matrix3(MRotationX, GRandGen.RandomValue() * (2 * H_PI)) *
                                               Matrix3(MRotationY, GRandGen.RandomValue() * (2 * H_PI)));
                        fxTrans.SetPosition(transform.Position() + posOffset);

                        ThingEffectKind kind = dyn_cast<Transport>(directHit) ? TEArmor : TEGround;
                        Entity* fx = CreateThingEffect(kind, fxTrans, vel);
                        (void)fx;
                    }
                }
#endif
            }
        }
    }

    if (ownerCenter)
    {
        float maxDist = type->hit * 0.5f + type->indirectHit * type->indirectHitRange * 0.5f;
        float maxRange = floatMax(300, TACTICAL_VISIBILITY);
        saturate(maxDist, 3, maxRange);
        Disclose(owner, pos, maxDist, enemyDammage, type->visibleFire > 1);
    }
}

void Landscape::ExplosionDammage(EntityAI* owner, Shot* shot, Object* directHit, Vector3Par pos, Vector3Par dir,
                                 const AmmoType* type)
{
    bool enemyDammage = false;

    AIUnit* ownerUnit = owner ? owner->CommanderUnit() : nullptr;
    AIGroup* ownerGroup = ownerUnit ? ownerUnit->GetGroup() : nullptr;
    AICenter* ownerCenter = ownerGroup ? ownerGroup->GetCenter() : nullptr;

    const float impulseKinScale = 1;
    const float impulseExplScale = 600;
    if (directHit)
    {
        float hitVal = type->hit - type->indirectHit;

        directHit->DirectDammage(shot, owner, pos, hitVal);
        // if directHit is vehicle, add force impulse
        Vehicle* veh = dyn_cast<Vehicle>(directHit);
        if (veh && shot)
        {
            if (ownerCenter && ownerCenter->IsEnemy(veh->GetTargetSide()))
            {
                enemyDammage = true;
            }
            float factor = veh->GetShape()->GeometrySphere() * (1.0 / 6);
            Vector3 relPos = pos - veh->COMPosition();
            Vector3 forcePos = relPos + Vector3(GRandGen.RandomValue() * 2 * factor - factor,
                                                GRandGen.RandomValue() * 2 * factor - factor,
                                                GRandGen.RandomValue() * 2 * factor - factor);
            // calculate force
            // direct hit - transfer all intertia plus direct explosion
            Vector3 force = shot->Speed() * (shot->GetMass() * impulseKinScale);
            if (type->explosive)
            {
                force += relPos.Normalized() * (hitVal * factor * -impulseExplScale);
            }
            veh->AddImpulseNetAware(force, forcePos.CrossProduct(force));
        }
    }
    const float maxRadius = type->indirectHitRange * 4;
    const float hit = type->indirectHit;
    int xMin, xMax, zMin, zMax;
    ObjRadiusRectangle(xMin, xMax, zMin, zMax, pos, pos, maxRadius);
    int x, z;
    for (x = xMin; x <= xMax; x++)
    {
        for (z = zMin; z <= zMax; z++)
        {
            const ObjectList& list = _objects(x, z);
            int n = list.Size();
            for (int i = 0; i < n; i++)
            {
                Object* obj = list[i];
                if (!obj->GetShape())
                {
                    continue;
                }
                if (obj->GetType() == Temporary)
                {
                    continue;
                }
                if (obj->GetType() == TypeTempVehicle)
                {
                    continue;
                }
                if (obj == shot)
                {
                    continue;
                }
                float dist2 = obj->Position().Distance2(pos);
                float maxDist2 = Square(maxRadius + obj->GetRadius());
                if (dist2 > maxDist2)
                {
                    continue;
                }
                float unshielded = 1;
                if (obj != directHit)
                {
                    float objectSize = obj->VisibleSizeRequired() * 0.8;
                    unshielded = Visible(pos, obj->Position(), objectSize, directHit, obj, ObjIntersectIFire);
                }
                float unshieldedHit = hit * unshielded;

                obj->IndirectDammage(shot, owner, pos, unshieldedHit, type->indirectHitRange);
                Vehicle* veh = dyn_cast<Vehicle>(obj);
                if (veh && shot)
                {
                    if (ownerCenter && ownerCenter->IsEnemy(veh->GetTargetSide()))
                    {
                        enemyDammage = true;
                    }
                    float factor = veh->GetShape()->GeometrySphere() * (1.0 / 6);
                    Vector3 relPos = pos - veh->COMPosition();
                    Vector3 forcePos =
                        relPos + Vector3(GRandGen.RandomValue() * 2 * factor - factor,
                                         floatMin(0, -GRandGen.RandomValue() * 2 * factor - factor), // upward bias
                                         GRandGen.RandomValue() * 2 * factor - factor);
                    float relDist = relPos.SquareSize();
                    float minRelDist = Square(type->indirectHitRange);
                    float hitFactor = relDist > minRelDist ? minRelDist / relDist : 1;
                    float hitVal = unshieldedHit * hitFactor;
                    Vector3 force = relPos.Normalized() * (hitVal * factor * -impulseExplScale);
                    veh->AddImpulseNetAware(force, forcePos.CrossProduct(force));
                }
            }
        }
    }

    ExplosionDammageEffects(owner, shot, directHit, pos, dir, type, enemyDammage);
    GetNetworkManager().ExplosionDammageEffects(owner, shot, directHit, pos, dir, type, enemyDammage);
}

#include <Poseidon/Foundation/Containers/StaticArray.hpp>
void Landscape::Disclose(EntityAI* owner, Vector3Par pos, float maxDist, bool discloseSide, bool disclosePosition)
{
    AUTO_STATIC_ARRAY(Ref<AIGroup>, disclose, 32);

    // tell all near enemy vehicles they are disclosed
    if (!owner)
    {
        LOG_DEBUG(Physics, "No owner");
        return;
    }
    AIUnit* ownerUnit = owner->CommanderUnit();
    if (!ownerUnit)
    {
        return;
    }
    AIGroup* ownerGroup = ownerUnit->GetGroup();
    if (!ownerGroup)
    {
        return;
    }

    AICenter* myCenter = ownerGroup->GetCenter();
    int xMin, xMax, zMin, zMax;
    ObjRadiusRectangle(xMin, xMax, zMin, zMax, pos, pos, maxDist);
    for (int x = xMin; x <= xMax; x++)
    {
        for (int z = zMin; z <= zMax; z++)
        {
            const ObjectList& list = _objects(x, z);
            int n = list.Size();
            for (int i = 0; i < n; i++)
            {
                Object* obj = list[i];
                if (obj->GetType() != TypeVehicle)
                {
                    continue;
                }
                EntityAI* veh = dyn_cast<EntityAI>(obj);
                if (!veh)
                {
                    continue;
                }
                if (veh->Position().Distance2(pos) > Square(maxDist))
                {
                    continue;
                }
                AIUnit* vehBrain = veh->CommanderUnit();
                if (vehBrain && vehBrain->GetLifeState() == AIUnit::LSAlive)
                {
                    AIGroup* vehGroup = vehBrain->GetGroup();
                    if (vehGroup && myCenter != vehGroup->GetCenter())
                    {
                        if (myCenter->IsEnemy(veh->GetTargetSide()))
                        {
                            disclose.Add(vehGroup);
                            vehBrain->Disclose(false);
                            if (veh->PilotUnit() && veh->PilotUnit() != vehBrain)
                            {
                                veh->PilotUnit()->Disclose(false);
                            }
                            if (veh->GunnerUnit() && veh->GunnerUnit() != vehBrain)
                            {
                                veh->GunnerUnit()->Disclose(false);
                            }
                        }
                        // add owner to group sensor list
                        float delay = 3.0f * veh->GetInvAbility();
                        if (vehBrain->GetTargetAssigned())
                        {
                            // vehicle already busy
                            delay *= 2;
                        }

                        float accuracy = 0.1f;
                        float sideAcc = discloseSide ? 1.5f : 0.1f;
                        if (disclosePosition)
                        {
                            vehGroup->AddTarget(owner, accuracy, sideAcc, delay);
                        }
                        else
                        {
                            // assume owner is at explosion position
                            vehGroup->AddTarget(owner, accuracy, sideAcc, delay, &pos);
                        }
                    }
                }
            }
        }
    }
    // disclose units before groups: group Disclose sets Combat mode, which
    // prevents units from executing the Danger reaction triggered by unit Disclose
    for (int i = 0; i < disclose.Size(); i++)
    {
        AIGroup* vehGroup = disclose[i];
        vehGroup->Disclose(nullptr);
    }
}

static void NearestPoint(float& t1, float& t2, Vector3Val b1, Vector3Val d1, Vector3Val b2, Vector3Val d2)
{
    // Finds t1,t2 such that (b1+d1*t1 - b2-d2*t2) is perpendicular to both directions.
    // Note: untested; results may be unreliable on degenerate inputs.
    Vector3 b = b1 - b2;
    float d2d2 = d2 * d2;
    float d1d1 = d1 * d1;
    float d1d2 = d1 * d2;
    float denom = d1d1 * d2d2 - d1d2 * d1d2;
    float bd2 = b * d2;
    float bd1 = b * d1;
    t1 = t2 = 0;
    if (fabs(denom) > 1e-5) // for parallel any t1 will do
    {                       // not parallel
        t1 = (d1d2 * bd2 - bd1 * d2d2) / denom;
    }
    if (d2d2 > 1e-5)
    { // t2 can be solved
        t2 = (bd2 + d1d2 * t1) / d2d2;
    }
}

static float NearestPoint(Vector3Val b1, Vector3Val d1, Vector3Val b2, Vector3Val d2)
{
    // Returns t minimising |b1-b2 + (d1-d2)*t|. Solved via f'(t)=0.
    Vector3 b = b1 - b2;
    Vector3 d = d1 - d2;
    float denom = d * d;
    if (denom > 1e-5)
    {
        return -(b * d) / denom;
    }
    return 0;
}

void Landscape::PredictCollision(VehicleCollisionBuffer& ret, const Vehicle* vehicle, float maxTime, float gap,
                                 float maxDistance) const
{
    // Note: allow exclusions (e.g. bridges, gas-stations)
    float vRadius = vehicle->CollisionSize() * 0.8;
    float maxDist2 = vehicle->Speed().SquareSize() * Square(maxTime);
    saturateMax(maxDist2, Square(maxDistance));
    float radius = maxDist2 * InvSqrt(maxDist2);
    int xMin, xMax, zMin, zMax;
    Vector3Val vehPos = vehicle->Position();
    ::ObjRadiusRectangle(xMin, xMax, zMin, zMax, vehPos, radius);
    for (int x = xMin; x <= xMax; x++)
    {
        for (int z = zMin; z <= zMax; z++)
        {
            const ObjectListFull* list = _objects(x, z).GetList();
            if (!list || list->GetNonStaticCount() <= 0)
            {
                continue; // no vehicles in this list
            }
            for (int i = 0; i < list->Size(); i++)
            {
                const Object* obj = list->Get(i);
                if (obj->GetType() != TypeVehicle)
                {
                    continue;
                }
                if (obj->Position().Distance2Inline(vehPos) > maxDist2)
                {
                    continue;
                }
                const EntityAI* ai = dyn_cast<const EntityAI>(obj);
                if (!ai)
                {
                    continue;
                }
                if (!obj->GetShape())
                {
                    continue;
                }
                if (!obj->GetShape()->GeometryLevel())
                {
                    continue;
                }
                if (!obj->HasGeometry())
                {
                    continue;
                }
                if (obj == vehicle)
                {
                    continue;
                }
                float minTime =
                    NearestPoint(vehicle->Position(), vehicle->Speed(), obj->Position(), obj->ObjectSpeed());
                saturate(minTime, 0, maxTime);
                Vector3 pt1 = vehicle->Position() + vehicle->Speed() * minTime;
                Vector3 pt2 = obj->Position() + obj->ObjectSpeed() * minTime;
                float minDist2 = pt1.Distance2Inline(pt2);

                if (minDist2 < Square(maxDistance))
                {
                    float cRadius = obj->CollisionSize();
                    Vector3Val norm = (obj->Position() - vehicle->Position());
                    Vector3 relDir = norm.Normalized();
                    float relSpeed = relDir * (vehicle->Speed() - obj->ObjectSpeed());
                    // if relative speed is small, there is no need to have big gap?
                    float iGap = Interpolativ(relSpeed, 0, 10, gap * 0.2, gap);
                    float sumR = vRadius + cRadius + iGap;
                    float minDist = sqrt(minDist2);

                    if (minDist < sumR)
                    {
                        VehicleCollision& info = ret.Append();
                        info.pos = pt2;
                        info.who = ai;
                        info.distance = minDist - vRadius - cRadius;
                        info.time = minTime;
                    }
                }
            }
        }
    }
}

inline float InterpolativC(float control, const float cMin, const float cMax, const float vMin, float vMax)
{
    if (control < cMin)
    {
        return vMin;
    }
    if (control > cMax)
    {
        return vMax;
    }
    return (control - cMin) * (1 / (cMax - cMin)) * (vMax - vMin) + vMin;
}

struct VisCheckContext
{
    const Object *skip1, *target;
    Vector3 sensorPos;
    Vector3 objectPos;
    Vector3 lineDist;
    Vector3 lineDir;
    float lineSize;
    float objectSize;
    float objectSeem;
    float objVis;
};

bool Landscape::CheckVisibility(int x, int z, VisCheckContext& context, ObjIntersect isect) const
{
    if (!this_InRange(x, z))
    {
        return false;
    }
    const ObjectListFull* list = _objects(x, z).GetList();
    if (!list)
    {
        return false;
    }
    int no = list->Size();
    for (int oi = 0; oi < no; oi++)
    {
        Object* obstacle = list->Get(oi);
        LODShape* obstacleShape = obstacle->GetShape();
        if (!obstacleShape)
        {
            continue;
        }
        if (isect == ObjIntersectFire || isect == ObjIntersectIFire)
        {
            if (!obstacle->OcclusionFire())
            {
                continue;
            }
        }
        else
        {
            if (!obstacle->OcclusionView())
            {
                continue;
            }
        }
        if (obstacle == context.skip1 || obstacle == context.target)
        {
            continue;
        }
        if (obstacle->GetType() == TypeTempVehicle)
        {
            continue;
        }
        if (obstacle->GetType() == Network && obstacleShape->FindGeometryLevel() < 0)
        {
            continue;
        }
        if (obstacle->GetDestructType() == DestructTree && obstacle->IsDestroyed())
        {
            continue;
        }
        Vector3Val obstaclePos = obstacle->VisiblePosition();
        float t = context.lineDir * (obstaclePos - context.sensorPos);
        saturate(t, 0.01, context.lineSize);
        Vector3 nearest = t * context.lineDir + context.sensorPos;
        float dist2 = obstaclePos.Distance2Inline(nearest);
        float obstacleSize = obstacle->VisibleSize();
        if (dist2 > Square(obstacleSize))
        {
            continue;
        }
        if (context.target && context.target->IgnoreObstacle(obstacle, isect))
        {
            continue;
        }
        CollisionBuffer result;
        obstacle->Intersect(result, context.objectPos, context.sensorPos, 0, isect);
        if (result.Size() > 0)
        {
            float visibleThrough = 1;
            if (isect == ObjIntersectIFire)
            {
                float distanceThrough = 0;
                for (int i = 0; i < result.Size(); i++)
                {
                    distanceThrough += result[i].dirOut.Size();
                }
                float obstacleLogArmor = obstacle->GetLogArmor();
                // exp(-logArmor * dist): armor protection decays exponentially with thickness
                float unshielded = exp(-obstacleLogArmor * distanceThrough) * 3;
                visibleThrough = floatMin(unshielded, 1);
            }
            else if (isect == ObjIntersectFire)
            {
                // direct fire dammage
                visibleThrough = 0;
#if DIAG_VISIBLE_THROUGH
                float distanceThrough = 0;
                for (int i = 0; i < result.Size(); i++)
                {
                    distanceThrough += result[i].dirOut.Size();
                }
                LOG_DEBUG(Physics, "  fire obstacle, through {:.2f}", distanceThrough);
#endif
            }
            else
            {
                float obstacleAlpha = obstacleShape->Color().A8() * (1.0f / 255);
                if (obstacleAlpha > 0.975f * 0.5f)
                {
                    visibleThrough = 0;
#if DIAG_VISIBLE_THROUGH
                    if (context.skip1 && context.target)
                    {
                        float distanceThrough = 0;
                        for (int i = 0; i < result.Size(); i++)
                        {
                            distanceThrough += result[i].dirOut.Size();
                        }
                        LOG_DEBUG(Physics, "  {} to {}: {}, solid, obstacleAlpha {:.3f}, through {:.2f}",
                                  (const char*)context.skip1->GetDebugName(),
                                  (const char*)context.target->GetDebugName(), (const char*)obstacleShape->Name(),
                                  obstacleAlpha, distanceThrough);
                    }
#endif
                }
                else
                {
                    float distanceThrough = 0;
                    for (int i = 0; i < result.Size(); i++)
                    {
                        distanceThrough += result[i].dirOut.Size();
                    }
                    if (distanceThrough > 0 && obstacleAlpha > 0)
                    {
                        float viewDensity = obstacle->ViewDensity();
                        float vtLog = viewDensity * distanceThrough;
                        if (vtLog <= -4.6) // -4.6 gives 0.01 visibility
                        {
                            visibleThrough = 0;
#if DIAG_VISIBLE_THROUGH
                            if (context.skip1 && context.target)
                            {
                                LOG_DEBUG(Physics, "  {} to {}: {}, opaque, distance {:.1f}, viewDensity {:.2f}",
                                          (const char*)context.skip1->GetDebugName(),
                                          (const char*)context.target->GetDebugName(),
                                          (const char*)obstacleShape->Name(), distanceThrough, viewDensity);
                            }
#endif
                        }
                        else
                        {
                            visibleThrough = exp(vtLog);
                            saturateMin(visibleThrough, 1);
#if DIAG_VISIBLE_THROUGH
                            if (context.skip1 && context.target)
                            {
                                LOG_DEBUG(
                                    Physics, "  %s to %s: %s, distance %.1f, visibleThrough %.2f, dens %.2f, a %.2f",
                                    (const char*)context.skip1->GetDebugName(),
                                    (const char*)context.target->GetDebugName(), (const char*)obstacleShape->Name(),
                                    distanceThrough, visibleThrough, viewDensity, obstacleAlpha);
                            }
#endif
                        }
                    }
                }
            }
            context.objVis *= visibleThrough;
            if (context.objVis <= 0.025)
            {
                context.objVis = 0;
                return true; // invisible
            }
        }
    }
    return false;
}

float Landscape::CheckUnderLand(Vector3Par beg, Vector3Par dir, float tMin, float tMax, int x, int z) const
{
    // beg+tMin*dir and beg+tMax*dir must lie within square (x,z)
    float y00, y01, y10, y11;
#if !USE_SWIZZLED_ARRAYS
    if (!this_TerrainInRange(z, x) || !this_TerrainInRange(z + 1, x + 1))
    {
        y00 = y01 = y10 = y11 = -100;
    }
    else
    {
        y00 = GetData(x, z);
        y01 = GetData(x + 1, z);
        y10 = GetData(x, z + 1);
        y11 = GetData(x + 1, z + 1);
    }
#else
    if (!this_TerrainInRange(z, x) || !this_TerrainInRange(z + 1, x + 1))
        return false; // no bump outside landscape
    RawType y4[2][2];
    _data.GetFour(y4, x, z);
    y00 = RawToHeight(y4[0][0]);
    y01 = RawToHeight(y4[0][1]);
    y10 = RawToHeight(y4[1][0]);
    y11 = RawToHeight(y4[1][1]);
#endif

    Vector3 begPoint = beg + tMin * dir;
    Vector3 endPoint = beg + tMax * dir;

#if DIAG

    Ref<Object> obj = DrawDiagLine(begPoint, endPoint, PackedColor(Color(1, 1, 0, 1)));
    const_cast<Landscape*>(this)->ShowObject(obj);

#endif

#if DIAG_LAND
    LOG_DEBUG(Physics, "Check {:.1f},{:.1f},{:.1f} to {:.1f},{:.1f},{:.1f} (t = {:.1f},{:.1f})", begPoint[0],
              begPoint[1], begPoint[2], endPoint[0], endPoint[1], endPoint[2], tMin, tMax);
#endif
    float maxSurfY = floatMax(floatMax(y00, y01), floatMax(y10, y11));
    float minLineY = floatMin(begPoint.Y(), endPoint.Y());
    if (minLineY > maxSurfY)
    {
#if DIAG_LAND
        LOG_DEBUG(Physics, "  minmax failed");
#endif
        return 0;
    }
    // each face is split into two triangles; determine which contains each endpoint
    Vector3 normal1;
    Vector3 normal2;
    // triangle 00,01,10
    normal1.Init();
    normal2.Init();
    normal1[0] = (y01 - y00) * -_invTerrainGrid;
    normal1[1] = 1;
    normal1[2] = (y10 - y00) * -_invTerrainGrid;
    // triangle 01,10,11
    normal2[0] = (y11 - y10) * -_invTerrainGrid;
    normal2[1] = 1;
    normal2[2] = (y11 - y01) * -_invTerrainGrid;
    //
    Vector3 point((x + 1) * _terrainGrid, y01, z * _terrainGrid);
    Plane plane1(normal1, point);
    Plane plane2(normal2, point);

    Plane *begPlane, *endPlane;

    {
        float xIn = begPoint.X() * _invTerrainGrid - x; // relative 0..1 in square
        float zIn = begPoint.Z() * _invTerrainGrid - z;
        begPlane = xIn <= 1 - zIn ? &plane1 : &plane2;
    }

    {
        float xIn = endPoint.X() * _invTerrainGrid - x; // relative 0..1 in square
        float zIn = endPoint.Z() * _invTerrainGrid - z;
        endPlane = xIn <= 1 - zIn ? &plane1 : &plane2;
    }

    float dist1 = -begPlane->Distance(begPoint);
    float dist2 = -endPlane->Distance(endPoint);
#if DIAG_LAND
    LOG_DEBUG(Physics, "  Dist {:.3f},{:.3f}", dist1, dist2);
#endif
    float under1 = tMin > 0 && dist1 > 0 ? dist1 / tMin : 0;
    float under2 = tMax > 0 && dist2 > 0 ? dist2 / tMax : 0;
    float under = floatMax(under1, under2);
#if DIAG_LAND
    LOG_DEBUG(Physics, "  under {:.3f},{:.3f}", under1, under2);
#endif
    float squareX = x * _terrainGrid, squareZ = z * _terrainGrid;
    float diagNom = _terrainGrid - beg.X() - beg.Z() + squareX + squareZ;
    float diagDenom = dir.X() + dir.Z();
    // diagDenom may be negative; normalise sign to avoid division in the check below
    if (diagDenom < 0)
    {
        diagNom = -diagNom, diagDenom = -diagDenom;
    }
    if (diagNom >= tMin * diagDenom && diagNom <= tMax * diagDenom) // avoids division
    {
        float diagT = diagNom / diagDenom;
        // calculate position on diagonal
        Vector3 diagPoint = beg + diagT * dir;
        float distD1 = -plane1.Distance(diagPoint);
#if DIAG_LAND
        // distance from both planes should be nearly the same
        float distD2 = -plane2.Distance(diagPoint);
        if (fabs(distD1 - distD2) > 1e-3)
        {
            LOG_DEBUG(Physics, "  Dist diag {:.3f} vs {:.3f}", distD1, distD2);
        }
        // verify it is on diagonal
        float diagX = diagPoint.X() - squareX;
        float diagZ = diagPoint.Z() - squareZ;
        if (fabs(diagX + diagZ - _terrainGrid) > 1e-3)
        {
            LOG_DEBUG(Physics, "  Diag {:.3f},{:.3f}, sum {:.3f}", diagX, diagZ, diagX + diagZ);
        }
#endif
        float underD = diagT > 0 ? distD1 / diagT : 0;
        saturateMax(under, underD);
#if DIAG_LAND
        LOG_DEBUG(Physics, "  distD {:.3f}", distD1);
        LOG_DEBUG(Physics, "  underD {:.3f}", underD);
#endif
    }
    return under;
}

float Landscape::Visible(Vector3Par from, Vector3Par to, float toRadius, const Object* skip1, const Object* target,
                         ObjIntersect isect) const
{
#if DIAG
    LOG_DEBUG(Physics, "***From {} to {}, type {}", skip1 ? (const char*)skip1->GetDebugName() : "<null>",
              target ? (const char*)target->GetDebugName() : "<null>", isect);
#endif
    float objectSize = toRadius;
    Vector3Val objectPos = to;
    Vector3Val sensorPos = from;

    Vector3 objDir = objectPos - sensorPos;
    float dist2 = objDir.SquareSize();
    float invDist = InvSqrt(dist2);
    float dist = dist2 * invDist;
    objDir *= invDist;

    Vector3 pos = sensorPos;
    Vector3 tgt = objectPos + Vector3(0, -objectSize, 0);
    Vector3 norm = (tgt - pos);
    Vector3 dNorm = norm.Normalized();
    float deltaX = dNorm.X();
    float deltaZ = dNorm.Z();

    float minVis = 1;

    {
        int xInt = toIntFloor(pos.X() * _invTerrainGrid);
        int zInt = toIntFloor(pos.Z() * _invTerrainGrid);

        float invDeltaX = fabs(deltaX) < 1e-10 ? 1e10 : 1 / deltaX;
        float invDeltaZ = fabs(deltaZ) < 1e-10 ? 1e10 : 1 / deltaZ;
        int ddx = deltaX >= 0 ? 1 : -1;
        int ddz = deltaZ >= 0 ? 1 : -1;
        float dnx = deltaX >= 0 ? _terrainGrid : 0;
        float dnz = deltaZ >= 0 ? _terrainGrid : 0;

        float tRest = dist;
        int maxIter = toInt(ENGINE_CONFIG.horizontZ * _invTerrainGrid * 15);
        float tMin = 0, tMax = 0;
        float maxNUnder = 0;
        while (tRest > 0)
        {
            if (--maxIter < 0)
            {
                LOG_DEBUG(Physics, "From {} to {}, type {}", skip1 ? (const char*)skip1->GetDebugName() : "<null>",
                          target ? (const char*)target->GetDebugName() : "<null>", (int)isect);
                LOG_DEBUG(Physics, "Max iters failed (dist {:.1f}), from {:.1f},{:.1f},{:.1f}, to {:.1f},{:.1f},{:.1f}",
                          dist, sensorPos[0], sensorPos[1], sensorPos[2], objectPos[0], objectPos[1], objectPos[2]);
                LOG_DEBUG(Physics, "hz {:.0f}, hz*5 {:.0f}", ENGINE_CONFIG.horizontZ, ENGINE_CONFIG.horizontZ * 5);
                break;
            }
            tMin = tMax;

            int xio = xInt, zio = zInt;
            Vector3 tPoint = pos + dNorm * tMin;
            float tx = (xInt * _terrainGrid + dnx - tPoint.X()) * invDeltaX;
            float tz = (zInt * _terrainGrid + dnz - tPoint.Z()) * invDeltaZ;
            if (tx <= tz)
            {
                saturateMin(tx, tRest);
                xInt += ddx;
                tMax += tx;
                tRest -= tx;
            }
            else
            {
                saturateMin(tz, tRest);
                zInt += ddz;
                tRest -= tz;
                tMax += tz;
            }

            float maxUnder = CheckUnderLand(sensorPos, dNorm, tMin, tMax, xio, zio);
            saturateMax(maxNUnder, maxUnder);
            if ((objectSize * 2 * invDist) <= maxNUnder)
            {
#if DIAG
                LOG_DEBUG(Physics, "  fully covered by ground");
#endif
                return 0;
            }
        }
        minVis = 1 - maxNUnder / (objectSize * 2 * invDist);

        if (minVis < 1e-3)
        {
            return minVis;
        }
    }

    saturate(minVis, 0, 1);

    VisCheckContext context;
    context.lineDist = objectPos - sensorPos;
    context.lineDir = objDir;
    context.lineSize = dist;
    context.objectSeem = objectSize * invDist;
    context.objectPos = objectPos;
    context.sensorPos = sensorPos;
    context.skip1 = skip1;
    context.target = target;
    context.objectSize = objectSize;
    context.objVis = 1;

    int border = toIntFloor(1.5 + objectSize * _invLandGrid);

    int ox = toIntFloor(objectPos.X() * _invLandGrid);
    int oz = toIntFloor(objectPos.Z() * _invLandGrid);
    int sx = toIntFloor(sensorPos.X() * _invLandGrid);
    int sz = toIntFloor(sensorPos.Z() * _invLandGrid);
    int aox = abs(ox - sx), aoz = abs(oz - sz);
    const float sBorder = 1.5;
    if (aox > aoz)
    {
        // mainly horizontal - primary coordinate is x, derived is z
        float dd = float(oz - sz) / aox;
        int pd = ox > sx ? +1 : -1;
        int p = sx - pd * border;
        float d = sz - dd * border;
        int plen = aox + 2 * border + 1;
        for (int i = plen; --i >= 0;)
        {
            int dis = toIntFloor(d - sBorder);
            int die = toIntFloor(d + sBorder);
            for (int di = dis; di <= die; di++)
            {
                CheckVisibility(p, di, context, isect);
                if (context.objVis <= 0)
                {
                    return 0;
                }
            }
            d += dd;
            p += pd;
        }
    }
    else if (aoz != 0)
    {
        // mainly vertical - primary coordinate is z, derived is x
        float dd = float(ox - sx) / aoz;
        int pd = oz > sz ? +1 : -1;
        int p = sz - pd * border;
        PoseidonAssert(abs(p) < 2000);
        float d = sx - dd * border;
        int plen = aoz + 2 * border + 1;
        for (int i = plen; --i >= 0;)
        {
            int dis = toIntFloor(d - sBorder);
            int die = toIntFloor(d + sBorder);
            for (int di = dis; di <= die; di++)
            {
                CheckVisibility(di, p, context, isect);
                if (context.objVis <= 0)
                {
                    return 0;
                }
            }
            d += dd;
            p += pd;
        }
    }
    else
    {
        int ssx, ssz;
        for (ssx = -border; ssx <= border; ssx++)
        {
            for (ssz = -border; ssz <= border; ssz++)
            {
                CheckVisibility(sx + ssx, sz + ssz, context, isect);
                if (context.objVis <= 0)
                {
                    return 0;
                }
            }
        }
    }

    return minVis * context.objVis;
}

void Landscape::IsInside(StaticArrayAuto<OLink<Object>>& objects, Object* ignore, Vector3Par pos, ObjIntersect isect)
{
    objects.Clear();
    int xMin, xMax, zMin, zMax;
    ::ObjRadiusRectangle(xMin, xMax, zMin, zMax, pos, 25);
    int x, z;
    for (x = xMin; x <= xMax; x++)
    {
        for (z = zMin; z <= zMax; z++)
        {
            const ObjectList& list = _objects(x, z);
            int n = list.Size();
            for (int i = 0; i < n; i++)
            {
                Object* obj = list[i];
                if (obj == ignore)
                {
                    continue;
                }
                if (obj->IsInside(pos, isect))
                {
                    objects.Add(obj);
                }
            }
        }
    }
}

static RString ShapeName(const Object* obj)
{
    if (!obj)
    {
        return RString();
    }
    if (!obj->GetShape())
    {
        return RString();
    }
    return obj->GetShape()->GetName();
}

static RString ShapeDebugName(const Object* obj)
{
    if (!obj)
    {
        return RString("<nullptr>");
    }
    return ShapeName(obj) + RString(" ") + obj->GetDebugName();
}

float Landscape::Visible(Vector3Par sensorPos, const Object* sensor, const Object* object, float reserve,
                         ObjIntersect isect) const
{
    LODShape* objectShape = object->GetShape();
    if (!objectShape)
    {
        return 0;
    }
    float objectSize = object->VisibleSizeRequired() * reserve * 0.8;
    saturateMax(objectSize, 0.01); // area-based: must not be zero

    Vector3Val objectPos = object->AimingPosition();

    float dist2 = objectPos.Distance2(sensorPos);
    if (dist2 < Square(ENGINE_CONFIG.horizontZ * 8))
    {
        return Visible(sensorPos, objectPos, objectSize, sensor, object, isect);
    }
    RptF("Dist %.1f, between %s (%.1f,%.1f,%.1f) and %s (%.1f,%.1f,%.1f)", sqrt(dist2),
         (const char*)ShapeDebugName(sensor), sensorPos.X(), sensorPos.Y(), sensorPos.Z(),
         (const char*)ShapeDebugName(object), objectPos.X(), objectPos.Y(), objectPos.Z());
    return 0;
}

float Landscape::Visible(const Object* sensor, const Object* object, float reserve, ObjIntersect isect) const
{
    return Visible(sensor->CameraPosition(), sensor, object, reserve, isect);
}

float Landscape::VisibleStrategic(int xs, int zs, int xe, int ze) const
{
    Fail("Not implemented.");
    return 1;
}

float Landscape::VisibleStrategic(Vector3Par from, Vector3Par to) const
{
#if 1
    float dist = to.Distance(from);
    int samples = toIntFloor(dist * _invTerrainGrid * 2);
    if (samples <= 0)
    {
        return 1;
    }
    float invSamples = 1.0 / samples;
    Vector3 step = (to - from) * invSamples;
    Point3 pt;
    int i;
    int tLog = GetTerrainRangeLog() - GetLandRangeLog();
    for (i = samples, pt = from; --i >= 0; pt += step)
    {
        int ix = toIntFloor(pt.X() * _invTerrainGrid);
        int iz = toIntFloor(pt.Z() * _invTerrainGrid);
        if (!this_TerrainInRange(ix, iz))
        {
            continue;
        }
        if (!this_TerrainInRange(ix + 1, iz + 1))
        {
            continue;
        }
        float y00 = GetData(ix, iz);
        float y01 = GetData(ix, iz + 1);
        float y10 = GetData(ix + 1, iz);
        float y11 = GetData(ix + 1, iz + 1);
        float y = y00;
        saturateMin(y, y01);
        saturateMin(y, y10);
        saturateMin(y, y11);
        saturateMax(y, 0);
        y -= 3; // slight pessimism for GeographyInfo coverage
        int ixt = ix >> tLog;
        int izt = iz >> tLog;
        GeographyInfo g = GetGeography(ixt, izt);
        if (g.u.full)
        {
            y += 15;
        }
        static const float objectsHeight[] = {0, 0, 2, 5};
        y += objectsHeight[g.u.howManyObjects];
        if (y > pt[1])
        {
            return 0;
        }
    }
#endif
    return 1;
}

bool Landscape::CheckIntersection(Vector3Par beg, Vector3Par end, int x, int z, float& tRet) const
{
    // beg and end must be in square (x,z)
    float y00, y01, y10, y11;
#if !USE_SWIZZLED_ARRAYS
    if (!this_TerrainInRange(z, x) || !this_TerrainInRange(z + 1, x + 1))
    {
        y00 = y01 = y10 = y11 = -100;
    }
    else
    {
        y00 = GetData(x, z);
        y01 = GetData(x + 1, z);
        y10 = GetData(x, z + 1);
        y11 = GetData(x + 1, z + 1);
    }
#else
    if (!this_TerrainInRange(z, x) || !this_TerrainInRange(z + 1, x + 1))
        return false; // no bump outside landscape
    RawType y4[2][2];
    _data.GetFour(y4, x, z);
    y00 = RawToHeight(y4[0][0]);
    y01 = RawToHeight(y4[0][1]);
    y10 = RawToHeight(y4[1][0]);
    y11 = RawToHeight(y4[1][1]);
#endif

    float maxSurfY = floatMax(floatMax(y00, y01), floatMax(y10, y11));
    float minLineY = floatMin(beg.Y(), end.Y());
    if (minLineY > maxSurfY)
    {
        return false;
    }
    Vector3 normal1;
    Vector3 normal2;
    normal1.Init();
    normal2.Init();
    // triangle 00,01,10
    normal1[0] = (y01 - y00) * -_invTerrainGrid;
    normal1[1] = 1;
    normal1[2] = (y10 - y00) * -_invTerrainGrid;
    // triangle 01,10,11
    normal2[0] = (y11 - y10) * -_invTerrainGrid;
    normal2[1] = 1;
    normal2[2] = (y11 - y01) * -_invTerrainGrid;
    //
    Vector3 point((x + 1) * _terrainGrid, y01, z * _terrainGrid);
    Plane plane1(normal1, point);
    Plane plane2(normal2, point);

    const float inEps = _invTerrainGrid * 0.01;

    Vector3 direction = end - beg;
    float dist1 = plane1.Distance(beg);
    if (dist1 < 0)
    {
        float xIn = beg.X() * _invTerrainGrid - x; // relative 0..1 in square
        float zIn = beg.Z() * _invTerrainGrid - z;
        if (xIn <= 1 - zIn + inEps)
        {
            tRet = 0;
            return true;
        }
    }
    float dist2 = plane2.Distance(beg);
    if (dist2 < 0)
    {
        float xIn = beg.X() * _invTerrainGrid - x; // relative 0..1 in square
        float zIn = beg.Z() * _invTerrainGrid - z;
        if (xIn >= 1 - zIn - inEps)
        {
            tRet = 0;
            return true;
        }
    }

    float denom1 = -plane1.Normal() * direction;
    if (fabs(denom1) > 1e-10)
    // if( denom1>1e-10 )
    {
        float t = dist1 / denom1;
        if (t >= 0 && t <= 1)
        {
            Vector3 nPos = beg + t * direction;

            float xIn = nPos.X() * _invTerrainGrid - x; // relative 0..1 in square
            float zIn = nPos.Z() * _invTerrainGrid - z;
            if (xIn <= 1 - zIn + inEps)
            {
                tRet = t;
                return true;
            }
        }
    }

    float denom2 = -plane2.Normal() * direction;
    if (denom2 > 1e-10)
    {
        float t = dist2 / denom2;
        if (t >= 0 && t <= 1)
        {
            Vector3 nPos = beg + t * direction;

            float xIn = nPos.X() * _invTerrainGrid - x; // relative 0..1 in square
            float zIn = nPos.Z() * _invTerrainGrid - z;
            if (xIn >= 1 - zIn - inEps)
            {
                tRet = t;
                return true;
            }
        }
    }
    return false;
}

inline float FloatSign(float x)
{
    if (x >= 0)
    {
        return 1;
    }
    return -1;
}

float Landscape::IntersectWithGround(Vector3* ret, Vector3Par from, Vector3Par dir, float minDist, float maxDist) const
{
    float maxDist0 = maxDist * 1.1f;
    saturateMin(maxDist, ENGINE_CONFIG.horizontZ);
    Vector3 pos = from;

    Vector3 dNorm = dir.Normalized();
    float deltaX = dNorm.X();
    float deltaZ = dNorm.Z();

    int xInt = toIntFloor(pos.X() * _invTerrainGrid);
    int zInt = toIntFloor(pos.Z() * _invTerrainGrid);

    float invDeltaX = fabs(deltaX) < 1e-10 ? FloatSign(deltaX) * 1e10 : 1 / deltaX;
    float invDeltaZ = fabs(deltaZ) < 1e-10 ? FloatSign(deltaZ) * 1e10 : 1 / deltaZ;
    int ddx = deltaX >= 0 ? 1 : -1;
    int ddz = deltaZ >= 0 ? 1 : -1;
    float dnx = deltaX >= 0 ? _terrainGrid : 0;
    float dnz = deltaZ >= 0 ? _terrainGrid : 0;

    float tRest = maxDist;
    int maxIter = toInt(ENGINE_CONFIG.horizontZ * _invTerrainGrid * 15);
    while (tRest > 0)
    {
        if (--maxIter < 0)
        {
            LOG_DEBUG(Physics, "IntersectWithGround: Max iters failed (dist {:.1f})", maxDist);
            break;
        }
        Vector3 beg = pos;

        int xio = xInt, zio = zInt;
        float tx = (xInt * _terrainGrid + dnx - pos.X()) * invDeltaX;
        float tz = (zInt * _terrainGrid + dnz - pos.Z()) * invDeltaZ;
        // Clamp to 0: when direction component is near-zero, floating-point
        // imprecision in (gridEdge - pos) * hugeInvDelta can produce small negatives
        if (tx < 0)
            tx = 0;
        if (tz < 0)
            tz = 0;
        if (tx <= tz)
        {
            saturateMin(tx, tRest);
            xInt += ddx;
            tRest -= tx;
            pos += dNorm * tx;
        }
        else
        {
            saturateMin(tz, tRest);
            zInt += ddz;
            tRest -= tz;
            pos += dNorm * tz;
        }

        float t;
        bool col = CheckIntersection(beg, pos, xio, zio, t);
        if (col)
        {
            if (ret)
            {
                *ret = beg * (1 - t) + pos * t;
            }
            return t;
        }
    }
    if (ret)
    {
        *ret = from + dNorm * maxDist0;
    }
    return maxDist0;
}

float Landscape::IntersectWithGroundOrSea(Vector3* ret, bool& sea, Vector3Par from, Vector3Par dir, float minDist,
                                          float maxDist) const
{
    sea = false;
    float seaLevel = _seaLevelWave;
    float tSea = maxDist * 2;
    float denom = dir[1];
    float nom = seaLevel - from[1];
    if (nom > 0)
    {
        // already under sea
        if (ret)
        {
            *ret = from + minDist * dir;
        }
        sea = true;
        return minDist;
    }
    if (denom < -1e-6)
    {
        // check only when dir goes down
        tSea = nom / denom;

        if (tSea < minDist)
        {
            if (ret)
            {
                *ret = from + minDist * dir;
            }
            sea = true;
            return minDist;
        }
    }
    saturateMin(maxDist, tSea);

    float t = IntersectWithGround(ret, from, dir, minDist, maxDist);
    if (t > tSea)
    {
        if (ret)
        {
            *ret = from + dir * tSea;
        }
        sea = true;
        return tSea;
    }
    return t;
}

float Landscape::IntersectWithGroundOrSea(Vector3* ret, Vector3Par from, Vector3Par dir, float minDist,
                                          float maxDist) const
{
    bool sea;
    return IntersectWithGroundOrSea(ret, sea, from, dir, minDist, maxDist);
}

Vector3 Landscape::IntersectWithGround(Vector3Par from, Vector3Par dir, float minDist, float maxDist) const
{
    Vector3 ret;
    IntersectWithGround(&ret, from, dir, minDist, maxDist);
    return ret;
}

Vector3 Landscape::IntersectWithGroundOrSea(Vector3Par from, Vector3Par dir, float minDist, float maxDist) const
{
    Vector3 ret;
    IntersectWithGroundOrSea(&ret, from, dir, minDist, maxDist);
    return ret;
}

Object* Landscape::PreviewFire(const Object* ignore, Vector3Par from, Vector3Par speed, Vector3 accel,
                               float timeToLive) const
{
    // Note: implement trajectory preview
    return nullptr;
}

CollisionBuffer::CollisionBuffer()
{
    static StaticStorage<CollisionInfo> storage;
    SetStorage(storage.Init(128));
}

CollisionBuffer::~CollisionBuffer() = default;

VehicleCollisionBuffer::VehicleCollisionBuffer()
{
    static StaticStorage<VehicleCollision> storage;
    SetStorage(storage.Init(128));
}

VehicleCollisionBuffer::~VehicleCollisionBuffer() = default;

GroundCollisionBuffer::GroundCollisionBuffer()
{
    static StaticStorage<UndergroundInfo> storage;
    SetStorage(storage.Init(512));
}

GroundCollisionBuffer::~GroundCollisionBuffer() = default;
