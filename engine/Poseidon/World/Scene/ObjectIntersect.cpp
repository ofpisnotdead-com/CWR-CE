#include <Poseidon/Core/Application.hpp>

#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/UI/Settings/AspectRatio.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>
#include <Poseidon/World/Simulation/FrameInv.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>
#include <Poseidon/Foundation/Common/Filenames.hpp>

#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/AI/AI.hpp>

#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <stdint.h>
#include <stdio.h>
#include <cmath>
#include <Poseidon/Foundation/Containers/StreamArray.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>

#if !_ENABLE_CHEATS
#define STARS 0
#define SHOT_STARS 0
#else
#define STARS 0
#define SHOT_STARS 1
#endif

#define PERF_ISECT 0

#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/World/MapTypes.hpp>
#include <Poseidon/World/Scene/ObjLine.hpp>
#include <Poseidon/World/Terrain/Occlusion.hpp>

namespace Poseidon
{
inline Vector3 MinMaxCorner(const Vector3* minMax, int x, int y, int z)
{
    return Vector3(minMax[x][0], minMax[y][1], minMax[z][2]);
}

// check if two shapes are in collision state

Ref<Object> DrawDiagLine(Vector3Par from, Vector3Par to, PackedColor color)
{
    Ref<LODShapeWithShadow> shape = ObjectLine::CreateShape();
    Ref<Object> lineObj = new ObjectLineDiag(shape);
    lineObj->SetConstantColor(color);

    lineObj->SetPosition(from);
    ObjectLine::SetPos(shape, VZero, to - from);
    return lineObj;
}

static void DiagBBox(const Vector3* minMax, const Frame& pos, PackedColor color)
{
    LODShapeWithShadow* shape = GScene->Preloaded(SphereModel);

    for (int lr = 0; lr < 2; lr++)
    {
        for (int ud = 0; ud < 2; ud++)
        {
            for (int fb = 0; fb < 2; fb++)
            {
                Vector3 wCorner = MinMaxCorner(minMax, lr, ud, fb);
                Ref<Object> obj = new ObjectColored(shape, -1);
                obj->SetPosition(pos.PositionModelToWorld(wCorner));
                obj->SetScale(0.1);
                obj->SetConstantColor(color);
                GLandscape->ShowObject(obj);
            }
        }
    }

    // draw 12 lines
    static const int lines[12][2][3] = {
        {{0, 0, 0}, {0, 0, 1}}, // diff in Z
        {{0, 1, 0}, {0, 1, 1}}, {{1, 0, 0}, {1, 0, 1}}, {{1, 1, 0}, {1, 1, 1}},

        {{0, 0, 0}, {0, 1, 0}}, // diff in Y
        {{0, 0, 1}, {0, 1, 1}}, {{1, 0, 0}, {1, 1, 0}}, {{1, 0, 1}, {1, 1, 1}},

        {{0, 0, 0}, {1, 0, 0}}, // diff in X
        {{0, 0, 1}, {1, 0, 1}}, {{0, 1, 0}, {1, 1, 0}}, {{0, 1, 1}, {1, 1, 1}},
    };
    for (int l = 0; l < 12; l++)
    {
        // get from
        Vector3 from = MinMaxCorner(minMax, lines[l][0][0], lines[l][0][1], lines[l][0][2]);
        Vector3 to = MinMaxCorner(minMax, lines[l][1][0], lines[l][1][1], lines[l][1][2]);
        Ref<Object> obj = DrawDiagLine(pos.PositionModelToWorld(from), pos.PositionModelToWorld(to), color);
        GLandscape->ShowObject(obj);
    }
}

static bool IntersectBBox(Vector3* isect, const Vector3 tMinMax[2], const Vector3 wMinMax[2], const Matrix4& withToThis)
{
#if 1
    // calculate bbox of transformed bbox
    Vector3 wtMin(+1e10, +1e10, +1e10), wtMax(-1e10, -1e10, -1e10); // in this coordinates

    Vector3 wCorners[2][2][2];
    for (int lr = 0; lr < 2; lr++)
    {
        for (int ud = 0; ud < 2; ud++)
        {
            for (int fb = 0; fb < 2; fb++)
            {
                Vector3 wCorner = MinMaxCorner(wMinMax, lr, ud, fb);
                Vector3 wtCorner;
                wtCorner.SetFastTransform(withToThis, wCorner);
                wCorners[lr][ud][fb] = wtCorner;
                CheckMinMax(wtMin, wtMax, wtCorner);
            }
        }
    }

    // clip wtMinMax against tMinMax

    if (wtMin[0] >= tMinMax[1][0])
    {
        return false;
    }
    if (wtMin[1] >= tMinMax[1][1])
    {
        return false;
    }
    if (wtMin[2] >= tMinMax[1][2])
    {
        return false;
    }
    if (wtMax[0] <= tMinMax[0][0])
    {
        return false;
    }
    if (wtMax[1] <= tMinMax[0][1])
    {
        return false;
    }
    if (wtMax[2] <= tMinMax[0][2])
    {
        return false;
    }

    if (isect)
    {
        // the caller requires intersection result
        isect[0] = VectorMax(wtMin, tMinMax[0]);
        isect[1] = VectorMin(wtMax, tMinMax[1]);
    }

#else

    // use planes with normals (1,0,0), (0,1,0) and (0,0,1)
    isect[0] = tMinMax[0];
    isect[1] = tMinMax[1];

    Vector3 wCorners[2][2][2];
    for (int lr = 0; lr < 2; lr++)
        for (int ud = 0; ud < 2; ud++)
            for (int fb = 0; fb < 2; fb++)
            {
                Vector3 wCorner = MinMaxCorner(wMinMax, lr, ud, fb);
                Vector3 wtCorner;
                wtCorner.SetFastTransform(withToThis, wCorner);
                wCorners[lr][ud][fb] = wtCorner;
            }

    for (int c = 0; c < 3; c++)
    {
        // boundbox of intersections between two planes and all edges
        // search for maximal point above minimum
        Vector3 cMin(+1e10, +1e10, +1e10);
        Vector3 cMax(-1e10, -1e10, -1e10);
        for (int diff = 0; diff < 3; diff++)
            for (int same1 = 0; same1 < 2; same1++)
                for (int same2 = 0; same2 < 2; same2++)
                {
                    int x1, y1, z1;
                    int x2, y2, z2;
                    if (diff == 0)
                        x1 = 0, y1 = same1, z1 = same2, x2 = 1, y2 = same1, z2 = same2;
                    else if (diff == 1)
                        x1 = same1, y1 = 0, z1 = same2, x2 = same1, y2 = 1, z2 = same2;
                    else
                        x1 = same1, y1 = same2, z1 = 0, x2 = same1, y2 = same2, z2 = 1; // diff==2
                    // calculate intersection between a,b and all six planes of cThis

                    Vector3Val rA = wCorners[x1][y1][z1];
                    Vector3Val rB = wCorners[x2][y2][z2];

                    // t=(nA-nP)/(nA-nB)
                    float aC = rA[c], bC = rB[c];
                    // min plane - inX<0 => x is out
                    float inMinA = aC - tMinMax[0][c];
                    float inMinB = bC - tMinMax[0][c];
                    bool isInMinA = (inMinA > 0);
                    bool isInMinB = (inMinB > 0);
                    // max plane - inX>0 => x is out
                    float inMaxA = aC - tMinMax[1][c];
                    float inMaxB = bC - tMinMax[1][c];
                    bool isInMaxA = (inMaxA < 0);
                    bool isInMaxB = (inMaxB < 0);
                    if (isInMinA != isInMinB)
                    {
                        float t = inMinA / (aC - bC);
                        PoseidonAssert(t >= -1e5 && t <= 1 + 1e5);
                        Point3 iSect = (rB - rA) * t + rA;
                        CheckMinMax(cMin, cMax, iSect);
                    }
                    if (isInMaxA != isInMaxB)
                    {
                        float t = inMaxA / (aC - bC);
                        PoseidonAssert(t >= -1e5 && t <= 1 + 1e5);
                        Point3 iSect = (rB - rA) * t + rA;
                        CheckMinMax(cMin, cMax, iSect);
                    }
                    if (isInMinA && isInMaxA)
                        CheckMinMax(cMin, cMax, rA);
                    if (isInMinB && isInMaxB)
                        CheckMinMax(cMin, cMax, rB);
                }
        // limit min-max box of this to the newly acquired region
        SaturateMax(isect[0], cMin);
        SaturateMin(isect[1], cMax);

        if (isect[0][0] >= isect[1][0])
            return false;
        if (isect[0][1] >= isect[1][1])
            return false;
        if (isect[0][2] >= isect[1][2])
            return false;
    }
#endif
    return true;
}

// need only OcclusionPoly

static bool CalculateIntersectionsExact(CollisionBuffer& result, const Object* thisObj, const ConvexComponent& cThis,
                                        const ConvexComponent& cWith, Matrix4Val thisToWith, Matrix4Val withToThis,
                                        Matrix4Val thisToWorld, int hierLevel)
{
    Vector3 direction;
    float under;
    // all calculation done in this space
    // clip all faces of cThis with cWith
    const Shape* sThis = cThis.GetShape();
    sThis->BuildFaceIndexToOffset();

#if 1
    AUTO_STATIC_ARRAY(Vector3, thisVertexResult, 256);
#endif

    // calculate direction
    Vector3 thisNormal = VZero;
    bool someThisLeft = false;
    OcclusionPoly poly;
    OcclusionPoly resClip;
    for (int i = 0; i < cThis.NPlanes(); i++)
    {
        const Poly& face = cThis.GetFace(i);
        // construct analytical poly
        poly.Clear();
        // face of this in with space
        for (int f = 0; f < face.N(); f++)
        {
            Vector3Val pos = sThis->Pos(face.GetVertex(f));
            // verify vertex is in plane
            poly.Add(thisToWith.FastTransform(pos));
        }
        // clip it with all planes of with
        for (int w = 0; w < cWith.NPlanes(); w++)
        {
            const Plane& wPlane = cWith.GetPlane(w);
            resClip.Clear();
            if (poly.Clip(resClip, wPlane.Normal(), wPlane.D()))
            {
                poly = resClip;
                if (poly.N() < 3)
                {
                    break;
                }
            }
        }
        // check if something is rest after clipping
        if (poly.N() >= 3)
        {
            // transform poly to this space
            poly.Transform(withToThis);
            // calculate oriented area
            poly.SumCrossProducts(thisNormal);
            someThisLeft = true;

            for (int v = 0; v < poly.N(); v++)
            {
                // add only unique vertices
                Vector3Val pos = poly.Get(v);
                bool alreadyIn = false;
                for (int i = 0; i < thisVertexResult.Size(); i++)
                {
                    if (thisVertexResult[i].Distance2(pos) < Square(1e-4))
                    {
                        alreadyIn = true;
                        break;
                    }
                }
                if (!alreadyIn)
                {
                    thisVertexResult.Add(poly.Get(v));
                }
            }
        }
    }

    if (!someThisLeft)
    {
        // nothing left from this
        // no intersection
        return false;
    }

    direction = thisNormal.Normalized();
    // we may calculate max. d of this
    float maxDThis = -1e10;
    float minDWith = +1e10;
    for (int i = 0; i < thisVertexResult.Size(); i++)
    {
        float d = direction * thisVertexResult[i];
        saturateMax(maxDThis, d);
    }

    bool someWithLeft = false;
    // clip all faces of cWith with cThis
    // scan all points of cWith

    const Shape* sWith = cWith.GetShape();
    sWith->BuildFaceIndexToOffset();

#if 1
    for (int i = 0; i < cWith.NPlanes(); i++)
    {
        const Poly& face = cWith.GetFace(i);
        // construct analytical poly
        poly.Clear();
        // face of this in with space
        for (int f = 0; f < face.N(); f++)
        {
            Vector3Val wPos = sWith->Pos(face.GetVertex(f));
            Vector3Val pos = withToThis.FastTransform(wPos);

            float under = maxDThis - pos * direction;

            if (under > 0)
            {
                // add only unique vertices
                bool alreadyIn = false;
                for (int i = 0; i < result.Size(); i++)
                {
                    if (result[i].pos.Distance2(pos) < Square(1e-4))
                    {
                        alreadyIn = true;
                    }
                }

                if (!alreadyIn)
                {
                    CollisionInfo& ret = result.Append();
                    // calculate average of bounding boxes in this space
                    ret.texture = nullptr;
                    ret.pos = pos;
                    ret.dirOut = direction;
                    ret.underVolume = 0;
                    ret.under = under;
                    ret.hierLevel = hierLevel;
                    ret.object = const_cast<Object*>(thisObj);
                    ret.component = -1; // no check
#if STARS
                    GLOB_SCENE->DrawCollisionStar(thisToWorld.FastTransform(ret.pos));
#endif
                }
            }
        }
    }
#else
    Vector3 withCenter = VZero, withNormal = VZero;
    OcclusionPoly poly;
    OcclusionPoly resClip;
    for (int i = 0; i < cWith.NPlanes(); i++)
    {
        const Poly& face = cWith.GetFace(i);
        // contsruct analytical poly
        poly.Clear();
        // face of this in with space
        for (int f = 0; f < face.N(); f++)
        {
            Vector3Val pos = sWith->Pos(face.GetVertex(f));
            poly.Add(withToThis.FastTransform(pos));
        }
        // clip it with all planes of with
        for (int w = 0; w < cThis.NPlanes(); w++)
        {
            const Plane& wPlane = cThis.GetPlane(w);
            resClip.Clear();
            if (poly.Clip(resClip, wPlane.Normal(), wPlane.D()))
            {
                poly = resClip;
                if (poly.N() < 3)
                    break;
            }
        }
        // check if something is rest after clipping
        if (poly.N() >= 3)
        {
            // calculate volume and center of volume
            // poly.SumXYVolume(volume,cov);
            someWithLeft = true;

            for (int v = 0; v < poly.N(); v++)
            {
                Vector3Val pos = poly.Get(v);
                /*
                LOG_DEBUG(Graphics, "w {}: {:.2f},{:.2f},{:.2f}",v,pos[0],pos[1],pos[2]);
                float d = direction*pos;
                saturateMin(minDWith,d);
                */

                float under = maxDThis - pos * direction;

                if (under > 0.001)
                {
                    // LOG_DEBUG(Graphics, "  {}: w under {:.2f} (pos
                    // {:.3f},{:.3f},{:.3f},)",i,under,pos[0],pos[1],pos[2]);
                    //  add only unique vertices
                    bool alreadyIn = false;
                    for (int i = 0; i < result.Size(); i++)
                    {
                        if (result[i].pos.Distance2(pos) < Square(1e-4))
                            alreadyIn = true;
                    }

                    if (!alreadyIn)
                    {
                        CollisionInfo& ret = result.Append();
                        // calculate average of bounding boxes in this space
                        ret.texture = nullptr;
                        ret.pos = pos;
                        ret.dirOut = direction;
                        ret.hierLevel = hierLevel;
                        ret.underVolume = 0;
                        ret.under = under;
                        ret.component = -1;
                        ret.object = const_cast<Object*>(thisObj);
#if STARS
                        GLOB_SCENE->DrawCollisionStar(thisToWorld.FastTransform(ret.pos));
#endif
                    }
                }
            }
        }
    }
#endif

    if (!someWithLeft)
    {
        // this is fully contained in with — special case.
        // actual volume calculation would be exact here; we provide
        // "large" values instead
        direction = VUp;
        under = 0.5;
        return true; // special handling required for this situation
    }
    else
    {
        under = maxDThis - minDWith;
    }

    return true;
}

#if _ENABLE_CHEATS
#define DIAG_INTERSECT 1
#endif

void Object::Intersect(CollisionBuffer& result, Object* with, const FrameBase& thisPos, const FrameBase& withPos,
                       int hierLevel) const
{
    // typical usage:
    // this is object in landscape
    // with is vehicle we test on collisions
    // with inverse transformation is typically cheap
    LODShape* withShape = with->GetShape();
    LODShape* thisShape = this->GetShape();
    if (!withShape || !thisShape)
    {
        return;
    }
    // check all possible collisions between components of this and with
    // create transformation between with and this
    // first of all check object bounding spheres
    float dist2 = thisPos.Position().Distance2(withPos.Position());
    float sumRadius2 = Square(this->GetRadius() + with->GetRadius());
    if (dist2 > sumRadius2)
    {
        return; // no collision possible
    }
    if (thisShape->GetGeomComponents().Size() <= 0)
    {
        return;
    }
    if (withShape->GetGeomComponents().Size() <= 0)
    {
        return;
    }
    Shape* withGeom = withShape->GeometryLevel();
    Shape* thisGeom = thisShape->GeometryLevel();

    if (!withGeom || !thisGeom)
    {
        return;
    }
    // make sure both objects have well defined geometry level
    // note: we will deanimate it again
    const_cast<Object*>(this)->AnimateGeometry();
    const_cast<Object*>(with)->AnimateGeometry();

    // minMax may be invalid now
    thisGeom->RecalculateNormalsAsNeeded();
    withGeom->RecalculateNormalsAsNeeded();

#if PERF_ISECT
#endif

    Matrix4Val withToThis = thisPos.GetInvTransform() * withPos;

// draw min-max box positions
#if DIAG_INTERSECT
    if (CHECK_DIAG(DECollision))
    {
        // draw bbox of this and with
        DiagBBox(thisGeom->MinMax(), thisPos, PackedColor(Color(0, 0, 1, 0.5)));
        DiagBBox(withGeom->MinMax(), withPos, PackedColor(Color(0, 1, 0, 0.5)));
    }
#endif
    Vector3 isect[2];
    if (IntersectBBox(isect, thisGeom->MinMax(), withGeom->MinMax(), withToThis))
    {
#if DIAG_INTERSECT
        if (CHECK_DIAG(DECollision))
        {
            DiagBBox(isect, thisPos, PackedColor(Color(0, 0, 0, 0.5)));
        }
#endif
        Matrix4Val thisToWith = withPos.GetInvTransform() * thisPos;

        thisShape->RecalculateGeomComponentsAsNeeded();
        withShape->RecalculateGeomComponentsAsNeeded();

        for (int iThis = 0; iThis < thisShape->GetGeomComponents().Size(); iThis++)
        {
            const ConvexComponent& cThis = *thisShape->GetGeomComponents()[iThis];
// check cThis with "with" bsphere
#if PERF_ISECT
#endif

            Vector3 thisCenter = thisPos.FastTransform(cThis.GetCenter());
            float dist2 = thisCenter.Distance2(withPos.Position());
            float sumRadius2 = Square(cThis.GetRadius() + with->GetRadius());
            if (dist2 > sumRadius2)
            {
                continue;
            }

            for (int iWith = 0; iWith < withShape->GetGeomComponents().Size(); iWith++)
            {
                const ConvexComponent& cWith = *withShape->GetGeomComponents()[iWith];
                // check this and with bounding sphere

#if PERF_ISECT
#endif

                float sphereDist2 = (thisToWith * cThis.GetCenter()).Distance2(cWith.GetCenter());
                if (sphereDist2 > Square(cThis.GetRadius() + cWith.GetRadius()))
                {
                    continue;
                }
                // calculate in this space
                // edges have always one component different
                Vector3 tMinMax[2];

#if PERF_ISECT
#endif

                if (!IntersectBBox(tMinMax, cThis.MinMax(), cWith.MinMax(), withToThis))
                {
                    continue;
                }

#if DIAG_INTERSECT
                if (CHECK_DIAG(DECollision))
                {
                    DiagBBox(tMinMax, thisPos, PackedColor(Color(0.5, 0.5, 0.5, 1)));
                }
#endif

#if PERF_ISECT
#endif

                CalculateIntersectionsExact(result, this, cThis, cWith, thisToWith, withToThis, *this, hierLevel);

            } // for( iWith )
        } // for( iThis )
    }

    // scan all proxies in geometry level
    int geomLevel = thisShape->FindGeometryLevel();
    int nProxies = GetProxyCount(geomLevel);
    if (nProxies > 0)
    {
        Matrix4 trans0 = Transform();
        Matrix4 invTrans0 = GetInvTransform();
        for (int p = 0; p < nProxies; p++)
        {
            Matrix4 trans = trans0;
            Matrix4 invTrans = invTrans0;
            // note: shape is ignored
            LODShapeWithShadow* shape = nullptr;
            Object* obj = GetProxy(shape, geomLevel, trans, invTrans, *this, p);
            if (!obj)
            {
                continue;
            }
            // pass frame
            FrameWithInverse objFrame(trans, invTrans);
            // add intersection with given proxy
            obj->Intersect(result, with, objFrame, withPos, hierLevel + 1);
        }
    }

    const_cast<Object*>(this)->DeanimateGeometry();
    const_cast<Object*>(with)->DeanimateGeometry();
}

void Object::Intersect(CollisionBuffer& result, Object* with) const
{
    Intersect(result, with, *this, *with);
}

inline Vector3 NearestPoint(Vector3Par beg, Vector3Par end, Vector3Par pos)
{
    // point on line beg .. end nearest to pos
    Vector3Val eb = end - beg;
    Vector3Val pb = pos - beg;
    float t = (eb * pb) / eb.SquareSizeInline();
    saturate(t, 0, 1);
    return beg + eb * t;
}

void Object::Intersect(CollisionBuffer& result, Vector3Par beg, Vector3Par end, float radius, ObjIntersect type) const
{
    Intersect(*this, result, beg, end, radius, type);
}

#define LOG_ISECT 0

void Object::Intersect(const FrameBase& pos, CollisionBuffer& result, Vector3Par beg, Vector3Par end, float radius,
                       ObjIntersect type, int hierLevel) const
{
    if (!pos.Transform().IsFinite() || !std::isfinite(pos.Scale()) || pos.Scale() == 0)
    {
        return;
    }
    LODShape* thisShape = this->GetShape();
    if (!thisShape)
    {
        return;
    }
    // make sure there is well defined geometry LOD

    ConvexComponents* cc = nullptr;
    int geomLevel;
    if (type == ObjIntersectFire || type == ObjIntersectIFire)
    {
        cc = thisShape->GetConvexComponents(thisShape->FindFireGeometryLevel()),
        geomLevel = thisShape->FindFireGeometryLevel();
    }
    else if (type == ObjIntersectView)
    {
        cc = thisShape->GetConvexComponents(thisShape->FindViewGeometryLevel()),
        geomLevel = thisShape->FindViewGeometryLevel();
    }
    else
    {
        cc = thisShape->GetConvexComponents(thisShape->FindGeometryLevel()), geomLevel = thisShape->FindGeometryLevel();
    }
    if (geomLevel < 0)
    {
        return;
    }

    if (cc->Size() <= 0)
    {
        return;
    }

    if (_isDestroyed)
    {
        if ((DestructType)_destrType == DestructTree)
        {
            return;
        }
        if ((DestructType)_destrType == DestructTent)
        {
            return;
        }
    }

    // note: we will deanimate it again
    if (geomLevel < 0)
    {
        return;
    }

    Shape* geomShape = thisShape->LevelOpaque(geomLevel);

    // check if distance of line beg..end from boundingsphere is low enough
    // distance of line and point:
    // find nearest point on line
    Vector3 rp = NearestPoint(beg, end, pos.Position());

    float scale = Scale();

    float rDistFromLine2 = rp.Distance2(pos.Position());
    float factor = IsAnimated(geomLevel) ? 2 : 1;
    float rDistFromLine2Max = Square(factor * scale * thisShape->BoundingSphere());
    // quick rejection of very far objects
    if (rDistFromLine2 > rDistFromLine2Max)
    {
        // collision not possible
        return;
    }

    const_cast<Object*>(this)->AnimateComponentLevel(geomLevel);

    // update normals, min-max and bSphere
    geomShape->RecalculateNormalsAsNeeded();

    Vector3 bCenter = pos.PositionModelToWorld(geomShape->BSphereCenter());
    Vector3 p = NearestPoint(beg, end, bCenter);
    float bRadius = geomShape->BSphereRadius();
    float distFromLine2 = p.Distance2(bCenter);
    float distFromLine2Max = Square(bRadius * scale);
    if (distFromLine2 > distFromLine2Max)
    {
        const_cast<Object*>(this)->DeanimateComponentLevel(geomLevel);
        return;
    }

    cc->RecalculateAsNeeded(geomShape);
    // all calculation will be performed in this space
    // convert beg and end

    Matrix4Val invTransform = pos.GetInvTransform();
    Vector3Val begThis = invTransform.FastTransform(beg);
    Vector3Val endThis = invTransform.FastTransform(end);

#if LOG_ISECT
    // if (type==ObjIntersectFire)
    {
        LOG_DEBUG(Graphics, "begThis {:.1f},{:.1f},{:.1f},", begThis.X(), begThis.Y(), begThis.Z());
        LOG_DEBUG(Graphics, "endThis {:.1f},{:.1f},{:.1f},", endThis.X(), endThis.Y(), endThis.Z());
    }
#endif

    for (int iThis = 0; iThis < cc->Size(); iThis++)
    {
        const ConvexComponent& cThis = *cc->Get(iThis);
        // check intersection will all convex components
        // check line beg..end with all cThis faces
        Vector3 b = begThis, e = endThis;
        float bt = 0, et = 1;
        // clip the b..e line with all planes of cThis
        int i;
        PoseidonAssert(cThis.NPlanes() >= 4);
        Vector3 dirOut;

#if LOG_ISECT
        LOG_DEBUG(Graphics, "{} ({}): component {}", (const char*)GetDebugName(), (const char*)GetShape()->Name(),
                  iThis);
#endif

        for (i = 0; i < cThis.NPlanes(); i++)
        {
            const Plane& plane = cThis.GetPlane(i);
            float distE = plane.Distance(e);
            float distB = plane.Distance(b);
            // dist<0 means point is in outer space
            if (distB < 0 && distE < 0)
            {
#if LOG_ISECT
                LOG_DEBUG(Graphics, "  NotInside");
#endif
                goto NotInside;
            }
            // dist>=0 means point is in inner space
            if (distB >= 0 && distE >= 0)
            {
                continue; // no clip
            }
            Vector3Val normal = plane.Normal();
            Vector3 bme = b - e;
            float denom = bme * normal;
            if (fabs(denom) < 1e-6)
            {
                if (bme.Normalized() * normal < 1e-6)
                {
                    RptF("Object::Intersect bme %.2f,%.2f,%.2f normal %.2f,%.2f,%.2f", bme.X(), bme.Y(), bme.Z(),
                         normal.X(), normal.Y(), normal.Z());
                }
#if LOG_ISECT
                LOG_DEBUG(Graphics, "  Parallel");
#endif
                continue; // parallel - no clip
            }
            float t = (plane.Normal() * b + plane.D()) / denom;

            // there must be some intersection
            PoseidonAssert(t >= -1e-3);
            PoseidonAssert(t <= 1 + 1e-3);
            Vector3 pt = (e - b) * t + b;
            PoseidonAssert(plane.Distance(pt) < 1e-3);
            // note: t is between bt and et
            float tt = bt + t * (et - bt);

            if (distB < 0)
            {
                // b is outside
                b = pt, bt = tt;
#if LOG_ISECT
                LOG_DEBUG(Graphics, "  setb {:.3f}", tt);
#endif
            }
            else
            {
                // e is outside
                PoseidonAssert(distE < 0);
                e = pt, et = tt;
#if LOG_ISECT
                LOG_DEBUG(Graphics, "  sete {:.3f}", tt);
#endif
            }
            dirOut = normal;
        }
        {
            // b,e interval is intersection
            if (bt > et)
            {
// there must be some degenerate planes? probably non-convex component?
#if LOG_ISECT
                LOG_DEBUG(Graphics, "  t {:.3f}..{:.3f} no hit", bt, et);
#endif
            }

            // return collision information
            CollisionInfo& ret = result.Append();
            // check nearest point of intersections
            Vector3 tPoint = b;
            float t = bt;
#if 1
            float maxDist = _shape->GeometrySphere() * 2;
            if (tPoint.SquareSize() > Square(maxDist))
            {
#if LOG_ISECT
                LOG_DEBUG(Graphics, "Impossible hit {:.3f},{:.3f},{:.3f}", tPoint[0], tPoint[1], tPoint[2]);
#endif
                tPoint = tPoint.Normalized() * maxDist;
            }
#endif
            ret.pos = tPoint;

            // get texture from any face of the component

            ret.texture = cThis.GetTexture();
            ret.dirOut = e - b; // note: dir out is nonsense
            ret.under = t;
            ret.hierLevel = hierLevel;
            ret.object = const_cast<Object*>(this);
            ret.component = iThis;
        }

    NotInside:;
    }

    // scan all proxies in geometry level
    int nProxies = GetProxyCount(geomLevel);
    if (nProxies > 0)
    {
        Matrix4 trans0 = Transform();
        Matrix4 invTrans0 = GetInvTransform();
        for (int p = 0; p < nProxies; p++)
        {
            // note: shape is ignored
            LODShapeWithShadow* shape = nullptr;
            Matrix4 trans = trans0;
            Matrix4 invTrans = invTrans0;
            Object* obj = GetProxy(shape, geomLevel, trans, invTrans, *this, p);
            if (!obj)
            {
                continue;
            }
            // pass frame
            FrameWithInverse objFrame(trans, invTrans);
            // add intersection with given proxy
            obj->Intersect(objFrame, result, beg, end, radius, type, hierLevel + 1);
        }
    }

    const_cast<Object*>(this)->Deanimate(geomLevel);
}
} // namespace Poseidon
