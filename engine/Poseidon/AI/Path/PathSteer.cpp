#include <Poseidon/Core/Application.hpp>

#include <Poseidon/AI/Path/PathSteer.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/Game/OperMap.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/World/Scene/Scene.hpp>

namespace Poseidon
{
using namespace Foundation;

inline float Dist2(const OperInfoResult& info, Vector3Par pos)
{
    return info._pos.Distance2(pos);
}

int Path::FindNearest(Vector3Par pos) const
{
    // find nearest point on path
    int minI = -1;
    float minDist2 = 1e10;
    for (int i = 0; i < Size(); i++)
    {
        const OperInfoResult& info = Get(i);
        float dist2 = Dist2(info, pos);
        if (minDist2 > dist2)
        {
            minDist2 = dist2;
            minI = i;
        }
    }
    return minI;
}

float Path::Distance(int index, Vector3Par pos) const
{
    const OperInfoResult& curr = Get(index);
    const OperInfoResult& next = Get(index + 1);
    Vector3 b = curr._pos;
    Vector3 e = next._pos - curr._pos;
    Vector3 p = pos - curr._pos;
    float t = (e * p) / e.SquareSize();
    saturate(t, 0, 1);
    Vector3 nearest = b + t * e;
    float dist2Next = nearest.Distance2(pos);
    return dist2Next;
}

int Path::FindNext(Vector3Par pos) const
{
    // find index so that distance of line index-1,index to pos
    // is minimal
    int index = 0;
    float minDist = 1e10;
    for (int i = 0; i < Size() - 1; i++)
    {
        float dist = Distance(i, pos);
        if (minDist >= dist)
        {
            minDist = dist, index = i;
        }
    }
    return index + 1;
}

inline Vector3Val OperPos(const OperInfoResult& info)
{
    return info._pos;
}

Path::Path()
{
    _onRoad = false;
}

inline float NearestPointT(Vector3Par beg, Vector3Par end, Vector3Par pos)
{
    // point on line beg .. end nearest to pos
    Vector3Val eb = end - beg;
    Vector3Val pb = pos - beg;
    return (eb * pb) / eb.SquareSize();
}

inline Vector3 NearestPoint(Vector3Par beg, Vector3Par end, Vector3Par pos)
{
    // point on line beg .. end nearest to pos
    Vector3Val eb = end - beg;
    Vector3Val pb = pos - beg;
    float t = (eb * pb) / eb.SquareSize();
    saturate(t, 0, 1);
    return beg + eb * t;
}

inline Vector3 NearestPointInfinite(Vector3Par beg, Vector3Par end, Vector3Par pos)
{
    // point on line beg .. end nearest to pos
    Vector3Val eb = end - beg;
    Vector3Val pb = pos - beg;
    float t = (eb * pb) / eb.SquareSize();
    return beg + eb * t;
}

float Path::CostAtPos(Vector3Par pos) const
{
    if (Size() < 2)
    {
        return Get(Size() - 1)._cost;
    }
    int index = FindNext(pos);
    if (index < 1)
    {
        Fail("Bad next.");
        return Get(0)._cost;
    }
    const OperInfoResult& curr = Get(index);
    const OperInfoResult& prev = Get(index - 1);
    float tPrev = NearestPointT(curr._pos, prev._pos, pos);
    saturate(tPrev, 0, 1);
    return curr._cost + (prev._cost - curr._cost) * tPrev;
}

Vector3 Path::NearestPos(Vector3Par pos) const
{
    if (Size() < 2)
    {
        return Get(Size() - 1)._pos;
    }
    int index = FindNext(pos);
    if (index < 0)
    {
        Fail("Bad next.");
        return Get(Size() - 1)._pos;
    }
    const OperInfoResult& curr = Get(index);
    const OperInfoResult& prev = Get(index - 1);

    return NearestPoint(curr._pos, prev._pos, pos);
}

Vector3 Path::PosAtCost(float cost) const
{
    int next;
    int size = Size();
    for (next = 1; next < size; next++)
    {
        if (Get(next)._cost >= cost)
        {
            break;
        }
    }
    if (next >= size)
    {
        // return end of path
        return OperPos(Get(size - 1));
    }
    // interpolate
    const OperInfoResult& pInfo = Get(next - 1);
    const OperInfoResult& nInfo = Get(next);
    float denom = nInfo._cost - pInfo._cost;
    if (denom <= 0)
    {
        return OperPos(nInfo);
    }
    float factor = (cost - pInfo._cost) / denom;
    AI_ERROR(factor >= -0.001);
    AI_ERROR(factor <= +1.001);

    Vector3Val beg = pInfo._pos;
    Vector3Val end = nInfo._pos;

    return beg * (1 - factor) + end * factor;
}

bool Path::InHouseAtCost(float cost, Vector3Par pos) const
{
    int next;
    int size = Size();
    if (size <= 0)
    {
        return false;
    }
    for (next = 1; next < size; next++)
    {
        if (Get(next)._cost >= cost)
        {
            break;
        }
    }
    if (next >= size)
    {
        // return end of path
        const OperInfoResult& last = Get(size - 1);
        return last._house != nullptr;
    }
    // interpolate
    const OperInfoResult& pInfo = Get(next - 1);
    const OperInfoResult& nInfo = Get(next);

    return pInfo._house != nullptr && nInfo._house != nullptr;
}

Vector3 Path::PosAtCost(float cost, Vector3Par point) const
{
    int next;
    int size = Size();
    for (next = 1; next < size; next++)
    {
        if (Get(next)._cost >= cost)
        {
            break;
        }
    }
    if (next >= size)
    {
        // return end of path
        return OperPos(Get(size - 1));
    }
    // interpolate
    const OperInfoResult& pInfo = Get(next - 1);
    const OperInfoResult& nInfo = Get(next);
    float denom = nInfo._cost - pInfo._cost;
    if (denom <= 0)
    {
        return OperPos(nInfo);
    }
    float factor = (cost - pInfo._cost) / denom;
    AI_ERROR(factor >= -0.001);
    AI_ERROR(factor <= +1.001);

    Vector3Val beg = pInfo._pos;
    Vector3Val end = nInfo._pos;

    // ret is point on line corresponding to cost
    Vector3 ret = beg * (1 - factor) + end * factor;

    // the point must be somewhere above or below the path

    // containing point
    // i.e. everything should happen in plane containing beg,end and end+VUp
    // adjust direction is aside to beg..end

    Vector3Val eb = end - beg;

    if (eb.SquareSize() < 1e-6)
    {
        // singular case - between two identical points
        LOG_DEBUG(AI, "Singular path");
        return ret;
    }
    // calculate normal of vertical plane containing eb
    Vector3 planeNormal = VUp.CrossProduct(eb);
    // calculate most vertical perpendicular to eb
    Vector3 perpFromEB = planeNormal.CrossProduct(eb);

    // search for point ret..ret+aside nearest to point
    Vector3 retPoint = NearestPointInfinite(ret, ret + perpFromEB, point);

    return retPoint;
}

float Path::SpeedAtCost(float cost) const
{
    int next;
    int size = Size();
    for (next = 1; next < size; next++)
    {
        if (Get(next)._cost >= cost)
        {
            break;
        }
    }
    if (next >= size)
    {
        // end of path - no speed
        next = size - 1;
        if (next <= 0)
        {
            return 0; // no path - no speed
        }
    }
    // interpolate
    const OperInfoResult& pInfo = Get(next - 1);
    const OperInfoResult& nInfo = Get(next);
    float nom = OperPos(nInfo).Distance(OperPos(pInfo));
    float denom = nInfo._cost - pInfo._cost;
    if (nInfo._cost > GET_UNACCESSIBLE)
    {
        LOG_DEBUG(AI, "GET_UNACCESSIBLE cost in path N");
        return 1;
    }
    if (pInfo._cost > GET_UNACCESSIBLE)
    {
        LOG_DEBUG(AI, "GET_UNACCESSIBLE cost in path P");
        return 1;
    }
    if (denom <= 0)
    {
        if (nom > 1)
        {
            LOG_DEBUG(AI, "Singular cost in path");
            LOG_DEBUG(AI, "Path size {}, {:.1f}/{:.1f}", size, nom, denom);
        }
        return nom * 100;
    }
    return nom / denom;
}

Vector3 Path::Begin() const
{
    return OperPos(Get(0));
}

Vector3 Path::End() const
{
    return OperPos(Get(Size() - 1));
}

float Path::EndCost() const
{
    return Get(Size() - 1)._cost;
}

LSError OperInfoResult::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("pos", _pos, 1))
    PARAM_CHECK(ar.Serialize("cost", _cost, 1))
    return LSOK;
}

LSError Path::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("operIndex", _operIndex, 1, 1))
    PARAM_CHECK(ar.Serialize("maxIndex", _maxIndex, 1))
    PARAM_CHECK(ar.Serialize("searchTime", _searchTime, 1))
    PARAM_CHECK(ar.Serialize("onRoad", _onRoad, 1, false))
    PARAM_CHECK(ar.Serialize("Path", *((base*)this), 1))
    return LSOK;
}

#define PATH_POINT_MSG(XX) \
	XX(Vector3, pos, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Path node position"), IdxTransfer) \
	XX(float, cost, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Path node cost"), IdxTransfer)

DECLARE_NET_INDICES(PathPoint, PATH_POINT_MSG)
DEFINE_NET_INDICES(PathPoint, PATH_POINT_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(PathPoint)

namespace Poseidon
{

NetworkMessageFormat& OperInfoResult::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    PATH_POINT_MSG(MSG_FORMAT)
    return format;
}

TMError OperInfoResult::TransferMsg(NetworkMessageContext& ctx)
{
    AI_ERROR(dynamic_cast<const IndicesPathPoint*>(ctx.GetIndices()))
    const IndicesPathPoint* indices = static_cast<const IndicesPathPoint*>(ctx.GetIndices());

    ITRANSF(pos)
    ITRANSF(cost)
    return TMOK;
}

DEFINE_NET_INDICES(Path, PATH_MSG)
DEFINE_GET_INDICES(Path)

NetworkMessageFormat& Path::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    PATH_MSG(MSG_FORMAT)
    return format;
}

TMError Path::TransferMsg(NetworkMessageContext& ctx)
{
    AI_ERROR(dynamic_cast<const IndicesPath*>(ctx.GetIndices()))
    const IndicesPath* indices = static_cast<const IndicesPath*>(ctx.GetIndices());

    ITRANSF(operIndex)
    ITRANSF(maxIndex)
    ITRANSF(searchTime)
    ITRANSF(onRoad)
    TMCHECK(ctx.IdxTransferArray(indices->path, *((base*)this)))
    return TMOK;
}

float Path::CalculateError(NetworkMessageContext& ctx)
{
    AI_ERROR(dynamic_cast<const IndicesPath*>(ctx.GetIndices()))
    const IndicesPath* indices = static_cast<const IndicesPath*>(ctx.GetIndices());

    float error = 0;

    ICALCERR_NEQ(Time, searchTime, ERR_COEF_MODE)
    // ?? _operIndex

    return error;
}

#define DIAG 0

void Path::Optimize(VehicleWithAI* vehicle)
{
    Fail("Not used, optimization included in OperMap::ResultPath");
}

void Path::AvoidCollision(VehicleWithAI* vehicle)
{
    // change path to avoid collisions
}

} // namespace Poseidon
