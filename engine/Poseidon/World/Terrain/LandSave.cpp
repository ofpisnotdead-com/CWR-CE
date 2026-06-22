#include <Poseidon/Core/Application.hpp>

#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/FileServer.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Scene/ObjectClasses.hpp>

#include <Poseidon/Graphics/Rendering/Primitives/Poly.hpp>
#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/Core/Progress.hpp>

#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/Foundation/Math/Math3DP.hpp>

#include <Poseidon/World/Terrain/LandFile.hpp>
#include <Poseidon/World/Terrain/WrpReader.hpp>
#include <Poseidon/IO/Serialization/SerializeBinExt.hpp>
#include <Poseidon/World/Terrain/TerrainProfile.hpp>

#include <time.h>
#include <string>
#include <cstdio>
#include <stdint.h>
#include <string.h>
#include <utility>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/Array2D.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Memory/CheckMem.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/Types/RemoveLinks.hpp>
#include <Poseidon/Foundation/platform.hpp>
#include <Random/randomGen.hpp>

// #define LAND_TEXTURES_MAX 256
#define LAND_TEXTURES_MAX 512

namespace Poseidon
{
LSError Landscape::LoadData(QIStream& f, float landGrid)
{
    ProgressReset();
    ProgressStart(LocalizeString(IDS_LOAD_WORLD));

    Log("Begin landscape load (%d KB).", Foundation::MemoryUsed() / 1024);

    int i;
    int x, z;

    Init(); // clear the landscape

    _segCache.Clear();

    WorldHeader header;
    f.read((char*)&header, sizeof(header));

    if (f.fail())
    {
        return LSUnknownError;
    }
    int version = 0;
    if (header.magic == FILE_MAGIC_4)
    {
        version = 4;
    }
    else if (header.magic == FILE_MAGIC_3)
    {
        version = 3;
    }
    else if (header.magic == FILE_MAGIC)
    {
        version = 2;
    }
    else
    {
        return LSUnknownError;
    }

    Dim(header.xRange, header.zRange, header.xRange, header.zRange, landGrid);

    FlushCache();

    for (z = 0; z < _landRange; z++)
    {
        for (x = 0; x < _landRange; x++)
        {
            SetTex(x, z, 0);
            SetData(x, z, 0);
        }
    }
    for (z = 0; z < header.zRange; z++)
    {
        for (x = 0; x < header.xRange; x++)
        {
            short val;
            f.read((char*)&val, sizeof(val));
            float data = val * LANDDATA_SCALE;
            if ((x | z) < _landRange)
            {
                SetData(x, z, data);
            }
        }
    }
    for (z = 0; z < header.zRange; z++)
    {
        for (x = 0; x < header.xRange; x++)
        {
            short val;
            f.read((char*)&val, sizeof(val));
            if ((x | z) < _landRange)
            {
                if (val < 0 || val > LAND_TEXTURES_MAX)
                {
                    Fail("Bad texture on landscape");
                    val = 0;
                }
                SetTex(x, z, val);
            }
        }
    }

    ProgressAdd(f.rest());
    ProgressFrame();

    for (i = 0; i < LAND_TEXTURES_MAX; i++)
    {
        char texName[33] = {}; // 32 wire bytes + guaranteed NUL for the strstr / SetTexture C-string use
        f.read(texName, 32);
        if (*texName)
        {
            PoseidonAssert(!strstr(texName, "$$")); // SetTexture(i,"landText\\pi.pac");
            SetTexture(i, texName);
        }
        ProgressSetRest(f.rest());
        ProgressRefresh();
    }

    if (f.fail())
    {
        return LSUnknownError;
    }

    const int maxID = 65536;
    bool isID[maxID];
    memset(isID, 0, sizeof(isID));

    // load all objects
    // bool someForest=false;
    int countObjects = 0;
    int maxObjID = -1;
    for (;;)
    {
        countObjects++;
        if (version < 3)
        {
            SingleObject so;
            f.read(&so, sizeof(so));
            if (f.fail() || f.eof())
            {
                break;
            }
            if (!*so.name)
            {
                break; // object list terminated
            }
            float x = so.x * _landGrid;
            float z = so.z * _landGrid;
            float y = SurfaceY(x, z) + so.y;

            Vector3 oPos(x, y, z);
            AddObject(x, y, z, so.heading, so.name);
        }
        else if (version == 3)
        {
            SingleObject3 so;
            f.read(&so, sizeof(so));
            if (f.fail() || f.eof())
            {
                break;
            }
            if (!*so.name)
            {
                break; // object list terminated
            }
            int id = NewObjectID();
            ObjectCreate(id, so.name, ConvertToM(so.matrix));
        }
        else
        {
            SingleObject4 so;
            f.read(&so, sizeof(so));
            if (f.fail() || f.eof())
            {
                break;
            }
            if (!*so.name)
            {
                break; // object list terminated
            }
            saturateMax(maxObjID, so.id);
            if (so.id < maxID && isID[so.id])
            {
                LOG_DEBUG(World, "Conflict id {}", so.id);
                Object* dup = FindObjectNC(so.id);
                if (dup)
                {
                    Vector3P pos = so.matrix.Position();
                    Fail("Duplicite object id.");
                    RptF("Duplicate1 %s (%.1f,%.1f,%.1f)", so.name, pos.X(), pos.Y(), pos.Z());
                    RptF("Duplicate2 %s (%.1f,%.1f,%.1f)", (const char*)dup->GetShape()->Name(), dup->Position().X(),
                         dup->Position().Y(), dup->Position().Z());
                }
            }
            if (so.id < maxID)
            {
                isID[so.id] = true;
            }
            PoseidonAssert(so.id >= 0);
            ObjectCreate(so.id, so.name, ConvertToM(so.matrix));
        }

        ProgressSetRest(f.rest());
        ProgressRefresh();
    }
    LOG_DEBUG(World, "Total {} objects, max. id {}", countObjects, maxObjID);

    ProgressFrame();

    Log("Landscape loaded. (%d KB).", Foundation::MemoryUsed() / 1024);

    InitGeography();

    ProgressFinish();

    return LSOK;
}

int Landscape::LoadData(const char* name, float landGrid)
{
    DWORD start = Poseidon::Foundation::GlobalTickCount();
    LOG_DEBUG(World, "Landscape::LoadData start");
    Log("Load landscape %s", name);
    _name = name; // remmember name
    LSError ret = LSUnknownError;
    // FIX - allow worlds in banks
    QIFStreamB f;
    f.AutoOpen(name);
    int seekStart = f.tellg();
    if (f.fail())
    {
    }
    else if (LoadOptimized(f, landGrid))
    {
        ret = LSOK;
    }
    else
    {
        f.seekg(seekStart, QIOS::beg);
        ret = LoadData(f, landGrid);
    }
    if (ret != LSOK)
    {
        Poseidon::Foundation::ErrorMessage("Cannot load world '%s'", name);
    }
    LOG_DEBUG(World, "Landscape::LoadData time {}", Poseidon::Foundation::GlobalTickCount() - start);
    return ret;
}

void Landscape::SaveData(QOStream& f) const
{
    int i;
    int x, z;

    // f.read((char *)&temp,sizeof(temp));
    WorldHeader header;

    header.magic = FILE_MAGIC_4;
    header.xRange = _landRange;
    header.zRange = _landRange;
    f.write((char*)&header, sizeof(header));

    for (z = 0; z < _landRange; z++)
    {
        for (x = 0; x < _landRange; x++)
        {
            // short val=toIntFloor(GetData(x,z)/LANDDATA_SCALE);
            short val = HeightToShort(GetData(x, z));
            f.write((char*)&val, sizeof(val));
        }
    }
    for (z = 0; z < _landRange; z++)
    {
        for (x = 0; x < _landRange; x++)
        {
            short val = GetTex(x, z);
            f.write((char*)&val, sizeof(val));
        }
    }

    // save all texture names
    for (i = 0; i < LAND_TEXTURES_MAX; i++)
    {
        char texName[32];
        if (_texture.Size() > i && _texture[i])
        {
            const char* name = _texture[i]->Name();
            if (i == 0)
            {
                name = "LandText\\mo.pac";
            }
            strncpy(texName, name, sizeof(texName));
        }
        else
        {
            *texName = 0;
        }
        f.write(texName, sizeof(texName));
    }

// save all objects
// check for ID duplicate
#define CHECK_ID 1
#if CHECK_ID
    const int maxID = 65536;
    bool isID[maxID];
    memset(isID, 0, sizeof(isID));
#endif
    for (z = 0; z < _landRange; z++)
    {
        for (x = 0; x < _landRange; x++)
        {
            const ObjectList& list = _objects(x, z);
            int i, n = list.Size();
            for (i = 0; i < n; i++)
            {
                Object* o = list[i];
                if (!o)
                {
                    continue;
                }
                if (o->GetType() != Primary && o->GetType() != Network)
                {
                    continue;
                }
                SingleObject4 so;
                so.matrix = ConvertToP(o->Transform());
                strncpy(so.name, o->GetShape()->Name(), sizeof(so.name));
                so.id = o->ID();
#if CHECK_ID
                if (so.id < maxID && isID[so.id])
                {
                    LOG_DEBUG(World, "Conflict id {}", so.id);
                    Object* dup = FindObjectNC(so.id);
                    if (dup)
                    {
                        Vector3P pos = so.matrix.Position();
                        Fail("Duplicite object id.");
                        RptF("Duplicate1 %s (%.1f,%.1f,%.1f)", so.name, pos.X(), pos.Y(), pos.Z());
                        RptF("Duplicate2 %s (%.1f,%.1f,%.1f)", (const char*)dup->GetShape()->Name(),
                             dup->Position().X(), dup->Position().Y(), dup->Position().Z());
                    }
                }
                if (so.id < maxID)
                {
                    isID[so.id] = true;
                }
#endif
                if (*so.name)
                {
                    f.write((char*)&so, sizeof(so));
                }
            }
        }
    }
    {
        SingleObject4 so;
        so.name[0] = 0;
        f.write((char*)&so, sizeof(so));
    }
}
int Landscape::SaveData(const char* name)
{
    QOFStream f(name);
    SaveData(f);
    f.close();
    if (f.fail())
    {
        Poseidon::Foundation::ErrorMessage("Cannot save world '%s'", name);
        return -1;
    }
    return 0;
}

#define PROGRESS       \
    if (f.IsLoading()) \
        ProgressSetRest(f.GetRest()), ProgressRefresh();

} // namespace Poseidon
#include <Poseidon/Foundation/Algorithms/SplineEqD.hpp>
namespace Poseidon
{

// bilinear interpolation, yxz, xf and zf = <0,1>

__forceinline float Bilint(float y00, float y01, float y10, float y11, float xf, float zf)
{
    float y0z = y00 * (1 - zf) + y01 * zf;
    float y1z = y10 * (1 - zf) + y11 * zf;
    return y0z * (1 - xf) + y1z * xf;
}

void Landscape::MakeObjectsTerrainRelative()
{
    // make Y of all objects relative to terrain level
    for (int z = 0; z < _landRange; z++)
    {
        for (int x = 0; x < _landRange; x++)
        {
            const ObjectList& list = _objects(x, z);
            for (int i = 0; i < list.Size(); i++)
            {
                Object* obj = list[i];
                if (!obj)
                {
                    continue;
                }
                if (dyn_cast<ForestPlain>(obj))
                {
                    continue;
                }
                LODShape* shape = obj->GetShape();
                Vector3 pos = obj->Position();
                Vector3 bcWorld = (shape ? obj->PositionModelToWorld(-shape->BoundingCenter()) : pos);
                float surfY = SurfaceY(bcWorld.X(), bcWorld.Z());
                pos[1] -= surfY;
                obj->SetPosition(pos);
                // note: list bounding box may change - we will take care of it later
            }
        }
    }
}
void Landscape::MakeObjectsTerrainAbsolute()
{
    // make Y of all objects back to terrain level
    for (int z = 0; z < _landRange; z++)
    {
        for (int x = 0; x < _landRange; x++)
        {
            const ObjectList& list = _objects(x, z);
            if (list.GetList())
            {
                for (int i = 0; i < list.Size(); i++)
                {
                    Object* obj = list[i];
                    if (!obj)
                    {
                        continue;
                    }
                    if (dyn_cast<ForestPlain>(obj))
                    {
                        continue;
                    }
                    LODShape* shape = obj->GetShape();
                    Vector3 pos = obj->Position();
                    Vector3 bcWorld = (shape ? obj->PositionModelToWorld(-shape->BoundingCenter()) : pos);
                    float surfY = SurfaceY(bcWorld.X(), bcWorld.Z());
                    pos[1] += surfY;
                    obj->SetPosition(pos);
                    // note: we will need to recalc. bbox - see StaticChanged()
                }
                list.GetList()->StaticChanged();
            }
        }
    }
}

void Landscape::ResampleTerrain(int sampleStepLog)
{
    int maxResample = _terrainRangeLog - _landRangeLog;
    if (maxResample <= 0)
    {
        return;
    }
    saturateMin(sampleStepLog, maxResample);
    MakeObjectsTerrainRelative();

    const int sampleStep = 1 << sampleStepLog;
    const int resultX = _terrainRange >> sampleStepLog;
    const int resultZ = _terrainRange >> sampleStepLog;
    Array2D<float> result;
    result.Dim(resultX, resultZ);

    for (int x = 0; x < resultX; x++)
    {
        for (int z = 0; z < resultZ; z++)
        {
            result(x, z) = _data(x * sampleStep, z * sampleStep);
        }
    }
    _data = result;
    _terrainRange >>= sampleStepLog;
    _terrainRangeMask = _terrainRange - 1;
    _terrainRangeLog -= sampleStepLog;
    _terrainGrid *= sampleStep;
    _invTerrainGrid = 1 / _terrainGrid;

    MakeObjectsTerrainAbsolute();

    _segCache.Clear();
    if (GScene)
    {
        GScene->GetShadowCache().Clear();
    }
}

template <class Type>
Type GetClipped(Array2D<Type>& array, int x, int y, const Type& defValue)
{
    if (x < 0 || y < 0 || x >= array.GetXRange() || y >= array.GetYRange())
    {
        return defValue;
    }
    return array.Get(x, y);
}

#define DIAG_PROBLEM 0

#if DIAG_PROBLEM
// there are some subdivision problems with road near 5052.75,6708.5

const float ProblemX = 5055.85;
const float ProblemZ = 6707.61;
#endif

inline bool ObjectInside(Object* obj, float xMin, float zMin, float xMax, float zMax, bool diag = false)
{
    LODShape* lShape = obj->GetShape();
    float bRadius = lShape->BoundingSphere();
    Vector3Val pos = obj->Position();
    float xMinO = pos.X() - bRadius, xMaxO = pos.X() + bRadius;
    float zMinO = pos.Z() - bRadius, zMaxO = pos.Z() + bRadius;

    bool ret = (xMaxO >= xMin && xMinO <= xMax && zMaxO >= zMin && zMinO <= zMax);
    if (diag)
    {
        LOG_DEBUG(World, "  {} ({:.1f},{:.1f} - {:.1f}) {}", (const char*)obj->GetDebugName(), pos.X(), pos.Z(),
                  bRadius, ret ? "inside" : "outside");
        LOG_DEBUG(World, "  -- {:.1f},{:.1f} .. {:.1f},{:.1f}   <><> {:.1f},{:.1f} .. {:.1f},{:.1f}", xMin, zMin, xMax,
                  zMax, xMinO, zMinO, xMaxO, zMaxO);
    }
    return ret;
}

static void CheckMinMax(float& min, float& max, float val)
{
    saturateMin(min, val), saturateMax(max, val);
}

void Landscape::SubdivideTerrainOneStep()
{
    // get config class name
    const ParamEntry& cls = Pars >> "CfgWorlds" >> Glob.header.worldname;

    struct Factors
    {
        float rougness;
        float maxRoad;
        float maxTrack;
        float maxSlopeFactor;
    };
    Factors whiteNoise;
    Factors fractal;

    const ParamEntry& subdiv = cls >> "Subdivision";
    const ParamEntry& fractalCls = subdiv >> "Fractal";
    const ParamEntry& whiteNoiseCls = subdiv >> "Fractal";

    whiteNoise.rougness = whiteNoiseCls >> "rougness";
    whiteNoise.maxRoad = whiteNoiseCls >> "maxRoad";
    whiteNoise.maxTrack = whiteNoiseCls >> "maxTrack";
    whiteNoise.maxSlopeFactor = whiteNoiseCls >> "maxSlopeFactor";

    fractal.rougness = fractalCls >> "rougness";
    fractal.maxRoad = fractalCls >> "maxRoad";
    fractal.maxTrack = fractalCls >> "maxTrack";
    fractal.maxSlopeFactor = fractalCls >> "maxSlopeFactor";

    const float minSubdivideY = subdiv >> "minY";
    const float minSubdivideSlope = subdiv >> "minSlope";

    // make relief data more dense
    const int resultX = _terrainRange * 2;
    const int resultZ = _terrainRange * 2;

    // we create result in separate array
    Array2D<float> result;
    // subdivision may be disabled on some rectangles
    Array2D<bool> enableSubdiv;
    // randomness may be disabled on some rectangles
    Array2D<float> enableRandom;

    result.Dim(resultX, resultZ);
    enableSubdiv.Dim(resultX, resultZ);
    enableRandom.Dim(resultX, resultZ);

    int terrainLog = _terrainRangeLog - _landRangeLog;
    int terrainLandStep = (1 << terrainLog);
    int terrainLandStepMask = terrainLandStep - 1;

    // check where is subdivision enabled and where not
    for (int x = 0; x < _terrainRange; x++)
    {
        for (int z = 0; z < _terrainRange; z++)
        {
            // source values
            // float y00 = ClippedData(z,  x);
            // float y01 = ClippedData(z+1,x);
            // float y10 = ClippedData(z,  x+1);
            // float y11 = ClippedData(z+1,x+1);

            int xl = x & ~terrainLandStepMask;
            int zl = z & ~terrainLandStepMask;

            // values in the corners of the landgrid
            float yl00 = ClippedData(zl, xl);
            float yl01 = ClippedData(zl + terrainLandStep, xl);
            float yl10 = ClippedData(zl, xl + terrainLandStep);
            float yl11 = ClippedData(zl + terrainLandStep, xl + terrainLandStep);

            float ylMin = floatMin(floatMin(yl00, yl01), floatMin(yl10, yl11));
            float ylMax = floatMax(floatMax(yl00, yl01), floatMax(yl10, yl11));

            int xg = xl >> terrainLog;
            int zg = zl >> terrainLog;
            GeographyInfo g = _geography(xg, zg);

            bool enable = true;
            float random = 1;

            if (ylMax <= ylMin + minSubdivideSlope * _terrainGrid || ylMin < minSubdivideY || g.u.forestInner ||
                g.u.forestOuter || g.u.waterDepth >= 1)
            {
                enable = false;
                random = 0;
            }
            else
            {
#if DIAG_PROBLEM
                // check if we are near given place
                float xx = x * _terrainGrid, zz = z * _terrainGrid;
                float xe = xx + _terrainGrid, ze = zz + _terrainGrid;

                float dist = sqrt(Square(xx - ProblemX) + Square(zz - ProblemZ));
                bool diag = false;
                if (xx <= ProblemX && xe >= ProblemX && zz <= ProblemZ && ze >= ProblemZ)
                {
                    LOG_DEBUG(World, "Grid {},{} ({:.2f},{:.2f}) *** {:.2f},{:.2f}", x, z, xx, zz, ProblemX, ProblemZ);
                }
                else if (dist < _terrainGrid * 1.5)
                {
                    LOG_DEBUG(World, "Grid {},{} ({:.2f},{:.2f})", x, z, xx, zz);
                    diag = true;
                }
#endif
                bool house = false;
                bool road = false;
                // check if there is some object in (x,z)..(x+1,z+1) terrain grid range
                // that would prevent smoothing or randomness
                float xMin = x * _terrainGrid, xMax = xMin + _terrainGrid;
                float zMin = z * _terrainGrid, zMax = zMin + _terrainGrid;
                for (int xxg = xg - 1; xxg <= xg + 1; xxg++)
                {
                    for (int zzg = zg - 1; zzg <= zg + 1; zzg++)
                    {
                        if (!this_InRange(xxg, zzg))
                        {
                            continue;
                        }
                        const ObjectList& ol = GetObjects(zzg, xxg);
                        if (ol.Size() > 0)
                        {
                            for (int i = 0; i < ol.SizeNotEmpty(); i++)
                            {
                                Object* obj = ol[i];
                                // check if object is in (x,z)..(x+1,z+1)
                                // check if object class makes some problems
                                if (obj->GetType() != Primary && obj->GetType() != Network)
                                {
                                    continue;
                                }
                                LODShape* lShape = obj->GetShape();
                                if (!lShape)
                                {
                                    continue;
                                }
                                // check object type
                                if (dyn_cast<Building>(obj))
                                {
                                    if (!ObjectInside(obj, xMin, zMin, xMax, zMax))
                                    {
                                        continue;
                                    }
                                    house = true;
                                }
                                else if (dyn_cast<Road>(obj))
                                {
#if DIAG_PROBLEM
                                    if (!ObjectInside(obj, xMin, zMin, xMax, zMax, diag))
#else
                                    if (!ObjectInside(obj, xMin, zMin, xMax, zMax))
#endif
                                    {
                                        continue;
                                    }
                                    // no random subdivision under roads
                                    road = true;
                                }
                            }
                        }
                    }
                }
                if (house)
                {
                    enable = false;
                }
                if (road)
                {
                    random = 0;
                    // check for wild terrain, if detected, disable subdivision completely
                    float maxDX = -1e10, maxDZ = -1e10;
                    float minDX = +1e10, minDZ = +1e10;
                    // scan for max and min differentials
                    CheckMinMax(minDZ, maxDZ, ClippedData(z, x) - ClippedData(z - 1, x));
                    CheckMinMax(minDZ, maxDZ, ClippedData(z + 1, x) - ClippedData(z + 1, x));
                    CheckMinMax(minDZ, maxDZ, ClippedData(z + 2, x) - ClippedData(z + 1, x));

                    CheckMinMax(minDZ, maxDZ, ClippedData(z, x + 1) - ClippedData(z - 1, x + 1));
                    CheckMinMax(minDZ, maxDZ, ClippedData(z + 1, x + 1) - ClippedData(z + 1, x + 1));
                    CheckMinMax(minDZ, maxDZ, ClippedData(z + 2, x + 1) - ClippedData(z + 1, x + 1));

                    CheckMinMax(minDX, maxDX, ClippedData(z, x) - ClippedData(z, x - 1));
                    CheckMinMax(minDX, maxDX, ClippedData(z, x + 1) - ClippedData(z, x));
                    CheckMinMax(minDX, maxDX, ClippedData(z, x + 2) - ClippedData(z, x + 1));

                    CheckMinMax(minDX, maxDX, ClippedData(z + 1, x) - ClippedData(z + 1, x - 1));
                    CheckMinMax(minDX, maxDX, ClippedData(z + 1, x + 1) - ClippedData(z + 1, x));
                    CheckMinMax(minDX, maxDX, ClippedData(z + 1, x + 2) - ClippedData(z + 1, x + 1));

                    maxDX *= _invTerrainGrid;
                    maxDZ *= _invTerrainGrid;
                    minDX *= _invTerrainGrid;
                    minDZ *= _invTerrainGrid;

                    if (maxDX - minDX > 1.0f || maxDZ - minDZ > 1.0f)
                    {
                        enable = false;
                    }
                }
            }
            enableSubdiv(x, z) = enable;
            enableRandom(x, z) = random;
        }
    }

    for (int x = 0; x < _terrainRange; x++)
    {
        for (int z = 0; z < _terrainRange; z++)
        {
            // source values
            float ymm = ClippedData(z - 1, x - 1);
            float ym0 = ClippedData(z, x - 1);
            float ym1 = ClippedData(z + 1, x - 1);
            float ym2 = ClippedData(z + 2, x - 1);

            float y0m = ClippedData(z - 1, x);
            float y00 = ClippedData(z, x);
            float y01 = ClippedData(z + 1, x);
            float y02 = ClippedData(z + 2, x);

            float y1m = ClippedData(z - 1, x + 1);
            float y10 = ClippedData(z, x + 1);
            float y11 = ClippedData(z + 1, x + 1);
            float y12 = ClippedData(z + 2, x + 1);

            float y2m = ClippedData(z - 1, x + 2);
            float y20 = ClippedData(z, x + 2);
            float y21 = ClippedData(z + 1, x + 2);
            float y22 = ClippedData(z + 2, x + 2);

            // destination indices
            int xd = x * 2;
            int zd = z * 2;
            // bilinear interpolation

            int xl = x & ~terrainLandStepMask;
            int zl = z & ~terrainLandStepMask;

            float yl00 = ClippedData(zl, xl);
            float yl01 = ClippedData(zl + terrainLandStep, xl);
            float yl10 = ClippedData(zl, xl + terrainLandStep);
            float yl11 = ClippedData(zl + terrainLandStep, xl + terrainLandStep);

            float ylMin = floatMin(floatMin(yl00, yl01), floatMin(yl10, yl11));
            float ylMax = floatMax(floatMax(yl00, yl01), floatMax(yl10, yl11));

            GeographyInfo g = _geography(xl >> terrainLog, zl >> terrainLog);

            const float omega = 1.0f;

            if (!enableSubdiv(x, z))
            {
                // note: bilinear interpolation is not what we want

                result(xd, zd) = y00;
                result(xd + 1, zd) = (y00 + y10) * 0.5f;
                result(xd, zd + 1) = (y00 + y01) * 0.5f;
                // result(xd+1,zd+1) = (y00+y01+y10+y11)*0.25f;
                result(xd + 1, zd + 1) = (y01 + y10) * 0.5f;
                continue;
            }

            // omega can be controlled depending on source geography info
            // omega == 0 is bilinear interpolation
            // omega == 1 is similiar to b-spline interpolation

            const float alpha = -omega * (1.0f / 16);
            const float beta = (8 + omega) * (1.0f / 16);
            const float sigma = alpha * alpha;
            const float mi = alpha * beta;
            const float ni = beta * beta;

            result(xd, zd) = y00;
            // randomize three new points
            // check surface roughness
            Texture* tex = GetTexture(GetTexture(zl >> terrainLog, xl >> terrainLog));
            float fractalRandomness = 1;
            float whiteNoiseRandomness = 1;

            float slope = ylMax - ylMin;
            saturateMax(slope, _terrainGrid * 0.04f);

            if (tex)
            {
                saturateMin(fractalRandomness, tex->Roughness() * fractal.rougness);
                saturateMin(whiteNoiseRandomness, tex->Roughness() * whiteNoise.rougness);
            }
            if (g.u.road)
            {
                saturateMin(fractalRandomness, fractal.maxRoad);
                saturateMin(whiteNoiseRandomness, whiteNoise.maxRoad);
            }
            if (g.u.track)
            {
                saturateMin(fractalRandomness, fractal.maxTrack);
                saturateMin(whiteNoiseRandomness, whiteNoise.maxTrack);
            }

            fractalRandomness *= _terrainGrid / 50;

            saturateMin(fractalRandomness, slope * fractal.maxSlopeFactor);
            saturateMin(whiteNoiseRandomness, slope * whiteNoise.maxSlopeFactor);

            float randomF = enableRandom(x, z);
            float randomness = (fractalRandomness + whiteNoiseRandomness) * randomF;

            // check if neighbourh square allows randomness
            float randomness10 = randomness;
            float randomness01 = randomness;

            saturateMin(randomness10, GetClipped(enableRandom, x, z - 1, 0.0f));
            saturateMin(randomness01, GetClipped(enableRandom, x - 1, z, 0.0f));

            float random10 = (_randGen.RandomValue(xd + 1, zd) - 0.5f) * (2 * randomness10);
            float random01 = (_randGen.RandomValue(xd, zd + 1) - 0.5f) * (2 * randomness01);
            float random11 = (_randGen.RandomValue(xd + 1, zd + 1) - 0.5f) * (2 * randomness);

            // check if each edge can be interpolated
            // this edge is between x,z and x,z-1
            bool neighbourgh0M = GetClipped(enableSubdiv, x, z - 1, true);
            if (neighbourgh0M)
            {
                result(xd + 1, zd) = (y00 + y10) * beta + (y20 + ym0) * alpha + random10;
            }
            else
            {
                result(xd + 1, zd) = (y00 + y10) * 0.5f;
            }
            // this edge is between x,z and x-1,z
            bool neighbourghM0 = GetClipped(enableSubdiv, x - 1, z, true);
            if (neighbourghM0)
            {
                result(xd, zd + 1) = (y00 + y01) * beta + (y02 + y0m) * alpha + random01;
            }
            else
            {
                result(xd, zd + 1) = (y00 + y01) * 0.5f;
            }
            result(xd + 1, zd + 1) =
                ((y00 + y01 + y10 + y11) * ni + (y20 + ym0 + y21 + ym1 + y02 + y0m + y12 + y1m) * mi +
                 (ymm + ym2 + y2m + y22) * sigma + random11);
        }
    }

    // change landscape attributes accordingly
    _data = result;
    _terrainRange <<= 1;
    _terrainRangeMask = _terrainRange - 1;
    _terrainRangeLog += 1;
    _terrainGrid /= 2;
    _invTerrainGrid = 1 / _terrainGrid;
}

void Landscape::SubdivideTerrain(int subdivStepLog)
{
    auto st0 = TerrainProfile::Now();

    MakeObjectsTerrainRelative();

    while (subdivStepLog > 0)
    {
        SubdivideTerrainOneStep();
        subdivStepLog--;
    }

    MakeObjectsTerrainAbsolute();

    _segCache.Clear();
    if (GScene)
    {
        GScene->GetShadowCache().Clear();
    }

    auto elapsed = TerrainProfile::Now() - st0;
    // ~3GHz: cycles/3e6 = ms
    RptF("SubdivideTerrain: %.1f ms, grid=%d range=%d", elapsed / 3e6, _terrainRange, _landRange);
    LOG_DEBUG(Core, "LOAD: SubdivideTerrain {}ms grid={} range={}", elapsed / 3e6, _terrainRange, _landRange);
}

// Subdivision cache file format: magic + version + params + raw heightmap data
static const uint32_t SUBDIV_CACHE_MAGIC = 0x53444356; // "SDCV"
static const uint32_t SUBDIV_CACHE_VERSION = 1;

static std::string GetSubdivCachePath(const char* wrpName, int targetSubdivLog)
{
    // Build cache path from WRP name
    std::string base(wrpName ? wrpName : "unknown");
    // Replace path separators and dots
    for (auto& c : base)
    {
        if (c == '\\' || c == '/' || c == ':')
            c = '_';
    }
    char buf[512];
    const auto& cacheDir = GamePaths::Instance().CacheDir();
    snprintf(buf, sizeof(buf), "%s/cwr_subdiv_%s_L%d.cache", cacheDir.c_str(), base.c_str(), targetSubdivLog);
    return std::string(buf);
}

bool Landscape::LoadSubdivCache(int targetSubdivLog)
{
    std::string path = GetSubdivCachePath(_name, targetSubdivLog);
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;

    uint32_t magic, version;
    int cachedLandRange, cachedLandRangeLog, cachedTerrainRange, cachedTerrainRangeLog;
    float cachedLandGrid, cachedTerrainGrid;

    bool ok = fread(&magic, 4, 1, f) == 1 && magic == SUBDIV_CACHE_MAGIC && fread(&version, 4, 1, f) == 1 &&
              version == SUBDIV_CACHE_VERSION && fread(&cachedLandRange, 4, 1, f) == 1 &&
              fread(&cachedLandRangeLog, 4, 1, f) == 1 && fread(&cachedLandGrid, 4, 1, f) == 1 &&
              fread(&cachedTerrainRange, 4, 1, f) == 1 && fread(&cachedTerrainRangeLog, 4, 1, f) == 1 &&
              fread(&cachedTerrainGrid, 4, 1, f) == 1;

    if (!ok || cachedLandRange != _landRange || cachedLandRangeLog != _landRangeLog || cachedLandGrid != _landGrid)
    {
        fclose(f);
        return false;
    }

    // Adjust objects to be relative before changing terrain
    MakeObjectsTerrainRelative();

    // Resize _data to match cached terrain dimensions
    _data.Dim(cachedTerrainRange, cachedTerrainRange);
    size_t dataBytes = cachedTerrainRange * cachedTerrainRange * sizeof(RawType);
    ok = fread(_data.RawData(), 1, dataBytes, f) == dataBytes;
    fclose(f);

    if (!ok)
    {
        MakeObjectsTerrainAbsolute();
        return false;
    }

    _terrainRange = cachedTerrainRange;
    _terrainRangeMask = _terrainRange - 1;
    _terrainRangeLog = cachedTerrainRangeLog;
    _terrainGrid = cachedTerrainGrid;
    _invTerrainGrid = 1.0f / _terrainGrid;

    // Adjust objects back to absolute with new terrain heights
    MakeObjectsTerrainAbsolute();

    _segCache.Clear();
    if (GScene)
        GScene->GetShadowCache().Clear();

    LOG_DEBUG(Core, "LOAD: SubdivCache HIT from {}", path);
    return true;
}

void Landscape::SaveSubdivCache(int targetSubdivLog)
{
    std::string path = GetSubdivCachePath(_name, targetSubdivLog);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f)
    {
        LOG_WARN(Core, "LOAD: SubdivCache save failed: {}", path);
        return;
    }

    uint32_t magic = SUBDIV_CACHE_MAGIC, version = SUBDIV_CACHE_VERSION;
    fwrite(&magic, 4, 1, f);
    fwrite(&version, 4, 1, f);
    fwrite(&_landRange, 4, 1, f);
    fwrite(&_landRangeLog, 4, 1, f);
    fwrite(&_landGrid, 4, 1, f);
    fwrite(&_terrainRange, 4, 1, f);
    fwrite(&_terrainRangeLog, 4, 1, f);
    fwrite(&_terrainGrid, 4, 1, f);

    size_t dataBytes = _terrainRange * _terrainRange * sizeof(RawType);
    size_t written = fwrite(_data.RawData(), 1, dataBytes, f);
    fclose(f);
    if (written != dataBytes)
    {
        LOG_WARN(Core, "LOAD: SubdivCache write failed ({}/{} bytes), removing {}", written, dataBytes, path);
        remove(path.c_str());
        return;
    }

    LOG_DEBUG(Core, "LOAD: SubdivCache saved ({} bytes) to {}", dataBytes + 32, path);
}

class ObjectRectIndex
{
    FindArray<RStringB> _objectNames;
    Array2D<int> _offset;
    int _endOffset;

  public:
    ObjectRectIndex();
    ~ObjectRectIndex();

    void Dim(int x, int z);
    void SetOffset(int x, int z, int offset) { _offset(x, z) = offset; }
    void SetEndOffset(int offset) { _endOffset = offset; }

    const RStringB& GetObjectName(int i) { return _objectNames[i]; }

    int GetBegOffset(int x, int z) const { return _offset(x, z); }
    int GetEndOffset(int x, int z) const;
    void Reset();
    void Monotonize();
    void TransferNames(SerializeBinStream& f);
};

int ObjectRectIndex::GetEndOffset(int x, int z) const
{
    z++;
    if (z < _offset.GetYRange())
    {
        return _offset(x, z);
    }
    else
    {
        z = 0;
        x++;
        if (x < _offset.GetXRange())
        {
            return _offset(x, z);
        }
        return _endOffset;
    }
}

ObjectRectIndex::ObjectRectIndex() = default;
ObjectRectIndex::~ObjectRectIndex() = default;

void ObjectRectIndex::Dim(int x, int z)
{
    _offset.Dim(x, z);
    // scan
}

void ObjectRectIndex::Reset()
{
    for (int z = 0; z < _offset.GetYRange(); z++)
    {
        for (int x = 0; x < _offset.GetXRange(); x++)
        {
            _offset(x, z) = 0;
        }
    }
}

void ObjectRectIndex::TransferNames(SerializeBinStream& f)
{
    f.TransferBasicArray(_objectNames);
    // check christmas
    time_t t;
    time(&t);
    struct tm* lt = localtime(&t);
    bool christmas = lt->tm_mon == 11 && (lt->tm_mday == 24 || lt->tm_mday == 25);
    if (christmas)
    {
        for (int j = 0; j < _objectNames.Size(); j++)
        {
            if (stricmp(_objectNames[j], "data3d\\str_smrcicicek.p3d") == 0)
            {
                _objectNames[j] = "data3d\\pa_sx.p3d";
            }
        }
    }
}

void ObjectRectIndex::Monotonize()
{
    int lastOffset = 0;
    for (int x = 0; x < _offset.GetXRange(); x++)
    {
        for (int z = 0; z < _offset.GetYRange(); z++)
        {
            int& off = _offset(x, z);
            if (off == 0)
            {
                off = lastOffset;
            }
            else if (off > lastOffset)
            {
                RptF("Object offset not monotone (%d,%d)", x, z);
            }
            else
            {
                lastOffset = off;
            }
        }
    }
    if (lastOffset >= _endOffset)
    {
        RptF("Object offset not monotone - end offset");
    }
}

#define ENABLE_OBJECT_INDEX 0

#if ENABLE_OBJECT_INDEX
ObjectRectIndex index;

void Landscape::LoadObjects(int x, int z)
{
    QIFStreamB in;
    in.AutoOpen(_name);
    SerializeBinStream f(&in);
    int beg = index.GetBegOffset(x, z);
    int end = index.GetEndOffset(x, z);
    if (end == beg)
    {
        // list is empty - no change required
        return;
    }
    f.SeekG(beg);
    ObjectList& ol = _objects(x, z);
    // remove all static objects from the list
    for (int oi = 0; oi < ol.Size(); oi++)
    {
        Object* obj = ol[oi];
        if (obj->GetType() == Primary || obj->GetType() == Network)
        {
            // list is already loaded - no need to load it
            return;
        }
    }
    while (f.TellG() < end && !f.GetError())
    {
        // load object info
        int id = f.LoadInt();
        if (id < 0)
        {
            LOG_ERROR(World, "Terminator reached");
            break;
        }
        int nameIndex = f.LoadInt();
        const RStringB& name = index.GetObjectName(nameIndex);
        Matrix4 trans;
        f.Transfer(trans);
        int xr, zr;
        ObjectCreate(id, name, trans, &xr, &zr);
        DoAssert(x == xr);
        DoAssert(z == zr);
    }
}

void Landscape::ReleaseObjects(int x, int z)
{
    ObjectList& ol = _objects(x, z);
    // remove all static objects from the list
    for (int oi = 0; oi < ol.Size();)
    {
        Object* obj = ol[oi];
        if (obj->GetType() == Primary || obj->GetType() == Network)
        {
            ol.Delete(oi);
        }
        else
        {
            oi++;
        }
    }
}

#endif

template <class T, T default_value = 0>
class auto_init
{
  public:
    typedef T value_type;

  private:
    value_type value;

  public:
    // This is the point of the exercise: provide a default constructor
    __forceinline auto_init() : value(default_value) {}

    // These make it close to interchangeable with T
    __forceinline auto_init(const auto_init& other) : value(other.value) {}
    __forceinline explicit auto_init(const value_type& initial_value) : value(initial_value) {}
    __forceinline auto_init& operator=(const auto_init& other)
    {
        value = other.value;
        return *this;
    }
    __forceinline auto_init& operator=(value_type new_value)
    {
        value = new_value;
        return *this;
    }
    __forceinline operator const value_type&() const { return value; }
    __forceinline operator value_type&() { return value; }

    // And these are useful sometimes
    __forceinline const value_type& get() const { return value; }
    __forceinline void set(const value_type& new_value) { value = new_value; }
};

void Landscape::SerializeBin(SerializeBinStream& f, float landGrid)
{
    if (f.IsLoading())
    {
        ProgressReset();
        ProgressStart(LocalizeString(IDS_LOAD_WORLD));
        ProgressAdd(f.GetRest());
        ProgressFrame();
        Init(); // clear the landscape
    }

    Log("Begin landscape load (%d KB).", Foundation::MemoryUsed() / 1024);
#ifdef _MSC_VER
    if (!f.Version('WRPO'))
#else
    if (!f.Version(StrToInt("OPRW")))
#endif
    {
        f.SetError(f.EBadFileType);
        return;
    }
    int version = 3;
    f.Transfer(version);
    if (version < 2 || version > 3)
    {
        Poseidon::Foundation::WarningMessage("Bad version %d in landscape", version);
        f.SetError(f.EBadVersion);
        return;
    }
    if (f.IsLoading())
    {
        if (version >= 3)
        {
            int lx = f.LoadInt();
            int ly = f.LoadInt();
            int rx = f.LoadInt(); // terrain x,y
            int ry = f.LoadInt();
            Dim(lx, ly, rx, ry, landGrid);
        }
        else
        {
            Dim(256, 256, 256, 256, landGrid);
        }
        // set default grid size
        SetLandGrid(50);
        FlushCache();
    }
    else
    {
        if (version >= 3)
        {
            int lx = GetLandRange();
            int ly = GetLandRange();
            int rx = GetLandRange(); // terrain x,y
            int ry = GetLandRange();
            f.SaveInt(lx);
            f.SaveInt(ly);
            f.SaveInt(rx);
            f.SaveInt(ry);
        }
    }
    // transfer all relevant data
    f.TransferBinaryCompressed(_geography.RawData(), _geography.RawSize());
    PROGRESS;
    f.TransferBinaryCompressed(_soundMap.RawData(), _soundMap.RawSize());
    PROGRESS;
    f.TransferBasicArray(_mountains);
    PROGRESS;
    f.TransferBinaryCompressed(_tex.RawData(), _tex.RawSize());
    PROGRESS;
    f.TransferBinaryCompressed(_random.RawData(), _random.RawSize());
    PROGRESS;
    f.TransferBinaryCompressed(_data.RawData(), _data.RawSize());
    PROGRESS;
    // transfer texture index
    if (f.IsLoading())
    {
#if ENABLE_OBJECT_INDEX
        index.Dim(GetLandRange(), GetLandRange());
        index.Reset();
#else
        ObjectRectIndex index;
#endif

        int texCount = f.LoadInt();
        // count off the wire; each entry reads at least a 1-byte (NUL) name below — reject a
        // count the stream cannot back before a huge Resize.
        if (texCount < 0 || texCount > f.GetRest())
        {
            f.SetError(f.EFileStructure);
            return;
        }
        _texture.Resize(texCount);
        for (int i = 0; i < _texture.Size(); i++)
        {
            RStringB name;
            bool enableUV = false;
            f.Transfer(name);
            f.Transfer(enableUV);
            // note: enableUV is not used
            SetTexture(i, name);
        }
        PROGRESS;
        // FindArray<RStringB> objectNames;
        index.TransferNames(f);
        // f.TransferBasicArray(objectNames);
        PROGRESS;

// LOG_DEBUG(World, "Start objects {}",f.GetRest());
//  request all object files to be loaded
#if _ENABLE_CHEATS
        // remmember file position
        int startObjects = f.TellG();

        for (;;)
        {
            Matrix4 trans;
            int id = f.LoadInt();
            if (id < 0)
                break;
            int nameIndex = f.LoadInt();
            const RStringB& name = index.GetObjectName(nameIndex);
            GFileServer->Request(name, 1.0f);
            f.Transfer(trans);
        }

        // rewind file, process requests
        f.SeekG(startObjects);
#endif

#if ENABLE_OBJECT_INDEX
        int lastX = -1, lastZ = -1;
        int terminatorOffset = -1;
#endif

        // AutoArray< auto_init<char> > idUsed;
        AutoArray<InitPtr<Object>> idCache;
        AutoArray<AutoArray<InitPtr<Object>>> conflicts;

        for (;;)
        {
            Matrix4 trans;
#if ENABLE_OBJECT_INDEX
            terminatorOffset = f.TellG();
#endif
            int id = f.LoadInt();
            // id indexes idCache via Access(id), which grows it to id+1 — a huge id off the
            // wire is a multi-GB allocation. Treat an out-of-range id as the terminator.
            if (id < 0 || id > (16 << 20))
            {
                break;
            }

            int nameIndex = f.LoadInt();
            const RStringB& name = index.GetObjectName(nameIndex);
            f.Transfer(trans);
            int x, z;
            Object* obj = ObjectCreate(id, name, trans, &x, &z);

            idCache.Access(id);
            if (idCache[id])
            {
                // conflict detected
                conflicts.Access(id);
                conflicts[id].Add(obj);
            }
            else
            {
                idCache[id] = obj;
            }

            PROGRESS;

#if ENABLE_OBJECT_INDEX
            int offset = f.TellG();

            if (lastX != x || lastZ != z)
            {
                if (lastX > x || lastX == x && lastZ > z)
                {
                    RptF("x,z going back from %d,%d to %d,%d", lastX, lastZ, x, z);
                }
                if (index.GetBegOffset(x, z) != 0)
                {
                    RptF("Bad object segmentation - %d:%s", id, (const char*)name);
                }
                else
                {
                    Log("Start slot %d,%d", x, z);
                    index.SetOffset(x, z, offset);
                }
                lastX = x;
                lastZ = z;
            }
#endif
        }

        int newID = idCache.Size();
        for (int i = 0; i < conflicts.Size(); i++)
        {
            if (conflicts[i].Size() == 0)
            {
                continue;
            }
            Object* o1 = idCache[i];
            for (int j = 0; j < conflicts[i].Size(); j++)
            {
                Object* o2 = conflicts[i][j];
                DoAssert(o2->ID() == o1->ID());
                // swap so that O1 contains valid id
                if (dyn_cast<ForestPlain>(o1))
                {
                    swap(o1, o2);
                }
                // check new ID
                o2->SetID(newID++);
            }
        }
#if ENABLE_OBJECT_INDEX
        index.SetEndOffset(terminatorOffset);
        index.Monotonize();
#endif
        // LOG_DEBUG(World, "End   objects {}",f.GetRest());
    }
    else
    {
        // save texture info
        f.SaveInt(_texture.Size());
        for (int i = 0; i < _texture.Size(); i++)
        {
            Texture* txt = _texture[i].texture;
            RStringB name = txt ? txt->GetName() : "";
            bool enableUV = _texture[i].offsetUV;
            f.Transfer(name);
            f.Transfer(enableUV);
        }
        // transfer object lists
        // consider two pass - object name list and index into this list
        FindArray<RStringB> objectNames;
        for (int x = 0; x < _landRange; x++)
        {
            for (int z = 0; z < _landRange; z++)
            {
                ObjectList& ol = _objects(x, z);
                for (int i = 0; i < ol.Size(); i++)
                {
                    Object* obj = ol[i];
                    if (obj->GetType() != Primary && obj->GetType() != Network)
                    {
                        continue;
                    }
                    RStringB name = obj->GetShape()->GetName();
                    objectNames.AddUnique(name);
                }
            }
        }
        f.TransferBasicArray(objectNames);

        for (int x = 0; x < _landRange; x++)
        {
            for (int z = 0; z < _landRange; z++)
            {
                ObjectList& ol = _objects(x, z);
                for (int i = 0; i < ol.Size(); i++)
                {
                    Object* obj = ol[i];
                    if (obj->GetType() != Primary && obj->GetType() != Network)
                    {
                        continue;
                    }
                    RStringB name = obj->GetShape()->GetName();
                    Matrix4 pos = obj->Transform();
                    f.SaveInt(obj->ID());
                    f.SaveInt(objectNames.Find(name));
                    f.Transfer(pos);
                }
            }
        }
        f.SaveInt(-1); // terminator
    }
    if (f.IsLoading())
    {
        ProgressFinish();
    }
}

bool Landscape::LoadOptimized(QIStream& f, float landGrid)
{
    WrpReader reader;
    if (!reader.Load(f))
    {
        RptF("WrpReader: %s", reader.GetError() ? reader.GetError() : "unknown error");
        return false;
    }

    if (reader.GetFormat() != WrpReader::OPRW_V2 && reader.GetFormat() != WrpReader::OPRW_V3)
    {
        RptF("LoadOptimized: unexpected format %s", reader.GetFormatName());
        return false;
    }

    ProgressReset();
    ProgressStart(LocalizeString(IDS_LOAD_WORLD));
    Init();

    int lx = reader.GetGridX();
    int ly = reader.GetGridZ();
    int rx = reader.GetTerrainX();
    int ry = reader.GetTerrainZ();
    Dim(lx, ly, rx, ry, landGrid);
    SetLandGrid(50);
    FlushCache();

    // Copy raw arrays into Array2D members
    memcpy(_geography.RawData(), reader.GetGeography().Data(), reader.GetGeography().Size());
    memcpy(_soundMap.RawData(), reader.GetSoundMap().Data(), reader.GetSoundMap().Size());
    memcpy(_tex.RawData(), reader.GetTexIndices().Data(), reader.GetTexIndices().Size());
    memcpy(_random.RawData(), reader.GetRandom().Data(), reader.GetRandom().Size());

    // Copy heightmap into _data
    const float* heights = reader.GetHeightmapData();
    for (int z = 0; z < ly; z++)
        for (int x = 0; x < lx; x++)
            SetData(x, z, heights[z * lx + x]);

    // Copy mountains
    _mountains = reader.GetMountains();

    ProgressRefresh();

    // Set up textures
    _texture.Resize(reader.GetTextureCount());
    for (int i = 0; i < reader.GetTextureCount(); i++)
        SetTexture(i, reader.GetTextureName(i));

    ProgressRefresh();

    // Create objects from reader data (names already resolved by WrpReader)
#if _ENABLE_CHEATS
    for (int i = 0; i < reader.GetObjectCount(); i++)
    {
        const auto& obj = reader.GetObject(i);
        GFileServer->Request(obj.name, 1.0f);
    }
#endif

    AutoArray<InitPtr<Object>> idCache;
    AutoArray<AutoArray<InitPtr<Object>>> conflicts;

    for (int i = 0; i < reader.GetObjectCount(); i++)
    {
        const auto& obj = reader.GetObject(i);
        int x, z;
        Object* o = ObjectCreate(obj.id, obj.name, obj.transform, &x, &z);

        idCache.Access(obj.id);
        if (idCache[obj.id])
        {
            conflicts.Access(obj.id);
            conflicts[obj.id].Add(o);
        }
        else
        {
            idCache[obj.id] = o;
        }

        ProgressRefresh();
    }

    // Resolve ID conflicts
    int newID = idCache.Size();
    for (int i = 0; i < conflicts.Size(); i++)
    {
        if (conflicts[i].Size() == 0)
            continue;
        Object* o1 = idCache[i];
        for (int j = 0; j < conflicts[i].Size(); j++)
        {
            Object* o2 = conflicts[i][j];
            DoAssert(o2->ID() == o1->ID());
            if (dyn_cast<ForestPlain>(o1))
                swap(o1, o2);
            o2->SetID(newID++);
        }
    }

    ProgressFinish();
    return true;
}

void Landscape::SaveOptimized(QOStream& f)
{
    SerializeBinStream str(&f);
    SerializeBin(str, _landGrid);
}

void Landscape::SaveOptimized(const char* name)
{
    // optional - just to make sure all is converted well
    QOFStream f;
    f.open(name);
    SaveOptimized(f);
    f.close();
}

// load/save current status (no terrain/object data save here)
LSError Landscape::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("lastObjectID", _objectId, 1))
    if (ar.IsSaving())
    {
        RefArray<Object> objects;
        for (int x = 0; x < _landRange; x++)
        {
            for (int z = 0; z < _landRange; z++)
            {
                const ObjectList& list = _objects(x, z);
                for (int o = 0; o < list.Size(); o++)
                {
                    Object* obj = list[o];
                    // do not save temporaries
                    if (obj->GetType() != Primary && obj->GetType() != Network)
                    {
                        continue;
                    }
                    PoseidonAssert(obj->GetShape());
                    PoseidonAssert(obj->GetShape()->Name());
                    if (obj->ID() == 0)
                    {
                        Log("Object id %d %s", obj->ID(), (const char*)obj->GetShape()->Name());
                    }
                    if (!obj->MustBeSaved())
                    {
                        continue; // save only non-default states
                    }
                    objects.Add(obj);
                }
            }
        }
        PARAM_CHECK(ar.Serialize("Objects", objects, 1))
    }
    else if (ar.GetPass() == ParamArchive::PassSecond)
    {
        ParamArchive arCls;
        if (!ar.OpenSubclass("Objects", arCls))
        {
            return LSOK; // no objects saved
        }
        arCls.FirstPass();
        int n;
        PARAM_CHECK(arCls.Serialize("items", n, 1))
        for (int i = 0; i < n; i++)
        {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "Item%d", i);
            ParamArchive arRef;
            if (!arCls.OpenSubclass(buffer, arRef))
            {
                continue;
            }
            Object* obj = Object::CreateObject(arRef); // Find object in landscape
            if (!obj)
            {
                continue;
            }
            arRef.SecondPass();
            PARAM_CHECK(obj->Serialize(arRef))
            arCls.CloseSubclass(arRef);
        }
    }
    else
    {
        RefArray<Object> objects;
        PARAM_CHECK(ar.Serialize("Objects", objects, 1))
    }
    return LSOK;
}

void Landscape::ResetObjectIDs()
{
    Log("ResetObjectIDs");
    ClearIDCache();
    // reset id of all vehicles - use with caution
    int maxId = -1;
    int x, z;
    for (x = 0; x < _landRange; x++)
    {
        for (z = 0; z < _landRange; z++)
        {
            ObjectList& list = _objects(x, z);
            for (int o = 0; o < list.Size(); o++)
            {
                Object* obj = list[o];
                // never change primary object ID
                if (obj->GetType() != Primary && obj->GetType() != Network)
                {
                    obj->SetID(-1);
                }
                else
                {
                    // PoseidonAssert( obj->ID()>=0 );
                }
                if (maxId < obj->ID())
                {
                    maxId = obj->ID();
                }
            }
        }
    }
    SetLastObjectID(maxId);
    // vehicles will have id assigned by world
}

bool Landscape::CheckObjectStructure() const
{
#if DO_LINK_DIAGS
#endif
    return true;
}

void Landscape::ResetState()
{
    // repair all objects, remove any non-primaries
    for (int x = 0; x < _landRange; x++)
    {
        for (int z = 0; z < _landRange; z++)
        {
            ObjectList& list = _objects(x, z);
            int maxRetry = 10000;
        Retry:
            if (--maxRetry <= 0)
            {
                Fail("Too much Retry attempts");
            }
            else
            {
                for (int o = 0; o < list.Size();)
                {
                    int oldSize = list.Size();
                    Object* obj = list[o];
                    if (obj->GetType() != Primary && obj->GetType() != Network)
                    {
                        // delete non-primary
                        list.Delete(o);
                        if (oldSize - 1 != list.Size())
                        {
                            goto Retry;
                        }
                        continue;
                    }
                    Vector3 oldPos = obj->Position();
                    obj->ResetStatus(); // full repair
                    // note: obj may move during ResetStatus
                    // check if it still belongs to the same list

                    int xl, zl;
                    SelectObjectList(xl, zl, obj->Position().X(), obj->Position().Z());
                    if (xl != x || zl != z)
                    {
                        if (oldSize - 1 != list.Size())
                        {
                            if (oldSize == list.Size())
                            {
                                // LOG_DEBUG(World, "Object list unchanged");
                                o++;
                                continue;
                            }
                            goto Retry;
                        }

                        // object moved to another list and deleted from this one
                        continue;
                    }

                    if (oldSize != list.Size())
                    {
                        goto Retry;
                    }

                    o++;
                }
            }
            list.Compact();
        }
    }
    for (int x = 0; x < _landRange; x++)
    {
        for (int z = 0; z < _landRange; z++)
        {
            ObjectList& list = _objects(x, z);
            for (int o = 0; o < list.Size();)
            {
                Object* obj = list[o];
                if (obj->GetType() != Primary && obj->GetType() != Network)
                {
                    // delete non-primary
                    LOG_ERROR(World, "Non-primary object {:x}:{},{} present", (uintptr_t)obj,
                              (const char*)obj->GetDebugName(),
                              obj->GetShape() ? (const char*)obj->GetShape()->Name() : "<null>");
                }
                o++;
            }
        }
    }
    RebuildIDCache();
}

void Landscape::OnTimeSkipped()
{
    // let all objects react to time change
    for (int x = 0; x < _landRange; x++)
    {
        for (int z = 0; z < _landRange; z++)
        {
            ObjectList& list = _objects(x, z);
            for (int o = 0; o < list.Size(); o++)
            {
                Object* obj = list[o];
                obj->OnTimeSkipped();
            }
        }
    }
}

void Landscape::ClearIDCache()
{
    Log("Landscape::ClearIDCache");
    _objectIds.Clear();
}

void Landscape::RebuildIDCache()
{
    Log("Landscape::RebuildIDCache");
    int x, z;
    int maxId = -1;
    for (x = 0; x < _landRange; x++)
    {
        for (z = 0; z < _landRange; z++)
        {
            const ObjectList& list = _objects(x, z);
            for (int i = 0; i < list.Size(); i++)
            {
                Object* obj = list[i];
                int id = obj->ID();
                if (id >= 0)
                {
                    _objectIds.Access(id);
                    _objectIds[id] = obj;
                    if (id > maxId)
                    {
                        id = maxId;
                    }
                }
                else
                {
                    PoseidonAssert(obj->GetType() != Primary);
                    PoseidonAssert(obj->GetType() != Network);
                }
            }
        }
    }
    _objectIds.Compact();
    SetLastObjectID(maxId);
}

void Landscape::AddToIDCache(Object* object)
{
    int id = object->ID();
    _objectIds.Access(id);
    _objectIds[id] = object;
}

Object* Landscape::FindObject(int id) const
{
    if (id < 0)
    {
        return nullptr; // id<0 means nullptr
    }
    // this function is much slower than GetObject(id)
    // try id cache first
    if (id < _objectIds.Size())
    {
        Object* ret = _objectIds[id];
        if (ret)
        {
            PoseidonAssert(ret->ID() == id);
            return ret;
        }
    }
    else
    {
        Log("No ID cache.");
    }
    return FindObjectNC(id);
}

Object* Landscape::FindObjectNC(int id) const
{
    // check cached square
    int x, z, xx, zz;
    // check last successfull square
    if (this_InRange(_lastFindObjectX, _lastFindObjectZ))
    {
        // check neighhbourghs
        for (xx = -1; xx <= +1; xx++)
        {
            for (zz = -1; zz <= +1; zz++)
            {
                x = _lastFindObjectX + xx;
                z = _lastFindObjectX + zz;
                if (!this_InRange(x, z))
                {
                    continue;
                }
                const ObjectList& list = _objects(x, z);
                for (int o = 0; o < list.Size(); o++)
                {
                    if (list[o]->ID() == id)
                    {
                        _lastFindObjectX = x;
                        _lastFindObjectZ = z;
                        Log("ID Cache qsearch %d OK", id);
                        return list[o];
                    }
                }
            }
        }
    }
    // check all cells
    for (x = 0; x < _landRange; x++)
    {
        for (z = 0; z < _landRange; z++)
        {
            const ObjectList& list = _objects(x, z);
            for (int o = 0; o < list.Size(); o++)
            {
                if (list[o]->ID() == id)
                {
                    _lastFindObjectX = x;
                    _lastFindObjectZ = z;
                    Log("ID Non-Cached search %d OK", id);
                    return list[o];
                }
            }
        }
    }
    Log("ID Non-Cached search %d failed", id);
    return nullptr;
}
} // namespace Poseidon
