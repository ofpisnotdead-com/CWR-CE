
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>
#include <utility>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Memory/FastAlloc.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

// Defined at global scope in World/Object code (unwrapped subsystems).
namespace Poseidon
{
class Object;
}
namespace Poseidon
{
Object* NewObject(Poseidon::Foundation::RString typeName, Poseidon::Foundation::RString shapeName);
}
Poseidon::Object* NewProxyObject(Poseidon::Foundation::RString shapeName);

namespace Poseidon
{

#if defined(_M_X64) || defined(_M_AMD64)
} // namespace Poseidon
#include <intrin.h>
namespace Poseidon
{
#elif defined(__x86_64__)
} // namespace Poseidon
#include <x86intrin.h>
namespace Poseidon
{
#endif
} // namespace Poseidon
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/World/Simulation/Animation/Animation.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/IO/FileServer.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>

#include <Poseidon/IO/Streams/SerializeBin.hpp>

#include <Poseidon/Graphics/Rendering/Primitives/Edges.hpp>
#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>

#include <Poseidon/Foundation/Common/Filenames.hpp>

#include <Poseidon/Core/Data3D.h>

#include <Poseidon/World/MapTypes.hpp>

#include <Poseidon/Graphics/Rendering/Shape/ShapeShared.hpp>
namespace Poseidon
{

const char* LevelName(float resolution)
{
    static char buf[256];
    if (IsSpec(resolution, GEOMETRY_SPEC))
    {
        return "geometry";
    }
    else if (IsSpec(resolution, MEMORY_SPEC))
    {
        return "memory";
    }
    else if (IsSpec(resolution, LANDCONTACT_SPEC))
    {
        return "landContact";
    }
    else if (IsSpec(resolution, ROADWAY_SPEC))
    {
        return "roadway";
    }
    else if (IsSpec(resolution, PATHS_SPEC))
    {
        return "paths";
    }
    else if (IsSpec(resolution, HITPOINTS_SPEC))
    {
        return "hitpoints";
    }
    else if (IsSpec(resolution, VIEW_GEOM_SPEC))
    {
        return "geometryView";
    }
    else if (IsSpec(resolution, FIRE_GEOM_SPEC))
    {
        return "geometryFire";
    }
    else if (IsSpec(resolution, VIEW_PILOT_GEOM_SPEC))
    {
        return "geometryViewPilot";
    }
    else if (IsSpec(resolution, VIEW_GUNNER_GEOM_SPEC))
    {
        return "geometryViewGunner";
    }
    else if (IsSpec(resolution, VIEW_COMMANDER_GEOM_SPEC))
    {
        return "geometryViewCommander";
    }
    else if (IsSpec(resolution, VIEW_CARGO_GEOM_SPEC))
    {
        return "geometryViewCargo";
    }
    snprintf(buf, sizeof(buf), "%g", resolution);
    return buf;
}

static const char* LevelName(LODShape* shape, Shape* level)
{
    for (int i = 0; i < shape->NLevels(); i++)
    {
        if (shape->Level(i) == level)
        {
            return LevelName(shape->Resolution(i));
        }
    }
    return "Error";
}

void LODShape::CalculateHints()
{
    _andHints = ~0;
    _orHints = 0;
    for_each_alpha for (int level = 0; level < NLevels(); level++)
    {
        Shape* shape = Level(level);
        if (!shape)
        {
            continue;
        }
        _andHints &= shape->GetAndHints();
        _orHints |= shape->GetOrHints();
    }
#if ALPHA_SPLIT
    Shape* oShape = LevelOpaque(0);
    Shape* aShape = LevelAlpha(0);
    if (!aShape || oShape->NFaces() >= aShape->NFaces())
        aShape = oShape;
#else
    Shape* aShape = LevelOpaque(0);
#endif
    _color = aShape->_color, _colorTop = aShape->_colorTop;
    float alpha = _color.A8() * (1.0 / 255);
    float transparency = 1 - alpha * 1.5;

    if (transparency >= 0.99)
    {
        _viewDensity = 0;
    }
    if (transparency > 0.01)
    {
        _viewDensity = log(transparency) * 6;
    }
    else
    {
        _viewDensity = -10;
    }
}

void LODShape::DefineMinMax(int level)
{
    Shape* oShape = LevelOpaque(level);
    Shape* aShape = LevelOpaque(level);
    _minMax[0] = oShape->Min();
    _minMax[1] = oShape->Max();
    if (aShape)
    {
        SaturateMin(_minMax[0], aShape->Min());
        SaturateMin(_minMax[1], aShape->Max());
    }
    CalculateBoundingSphere();
}

void LODShape::CalculateBoundingSphereRadius()
{
    float maxSphere2 = 0;
    for (int level = 0; level < _nLods; level++)
    {
        Shape* shape = Level(level);
        if (!shape)
        {
            continue;
        }
        for (int i = 0; i < shape->_pos.Size(); i++)
        {
            const V3& pos = shape->_pos[i];
            float sphere2 = pos.SquareSize();
            saturateMax(maxSphere2, sphere2);
        }
        for (int p = 0; p < shape->_phase.Size(); p++)
        {
            for (int i = 0; i < shape->_phase[p].Size(); i++)
            {
                const Vector3& pos = shape->_phase[p][i];
                float sphere2 = pos.SquareSize();
                saturateMax(maxSphere2, sphere2);
            }
        }
    }
    _boundingSphere = sqrt(maxSphere2);
}

void LODShape::CalculateBoundingSphere()
{
    // -_boundingCenter center is original zero positioned in new coordinate system
    // when you add _boundingCenter to object coordinates, you will get original position

    // calculate bounding sphere from min-max information
    Vector3 oldBoundingCenter = _boundingCenter;
    Vector3 changeBoundingCenter = VZero;
    if (!_lockAutoCenter)
    {
        if (_autoCenter && NLevels() > 0)
        {
            // consider only clipflags of
            changeBoundingCenter = (_minMax[0] + _minMax[1]) * 0.5;
            // for OnSurface shapes do not change y component
            Shape* level0 = Level(0);
            if ((level0->GetAndHints() & ClipLandOn) && (level0->Special() & OnSurface))
            {
                changeBoundingCenter[1] = 0;
            }
        }
        else
        {
            // we might need to reset bounding center to zero
            changeBoundingCenter = -_boundingCenter;
        }

        if (changeBoundingCenter.SquareSize() < 1e-10 && _boundingSphere > 0)
        {
            // note: bounding sphere may be changed even when center is not
            CalculateBoundingSphereRadius();
            return; // no change
        }
    }
    _boundingCenter += changeBoundingCenter;
    _minMax[0] -= changeBoundingCenter;
    _minMax[1] -= changeBoundingCenter;
    _aimingCenter -= changeBoundingCenter;

    float maxSphere2 = 0;
    for_each_alpha for (int level = 0; level < _nLods; level++)
    {
        Shape* shape = Level(level);
        if (!shape)
        {
            continue;
        }
        for (int i = 0; i < shape->_pos.Size(); i++)
        {
            V3& pos = shape->_pos[i];
            pos -= changeBoundingCenter;
            float sphere2 = pos.SquareSize();
            saturateMax(maxSphere2, sphere2);
        }
        for (int p = 0; p < shape->_phase.Size(); p++)
        {
            for (int i = 0; i < shape->_phase[p].Size(); i++)
            {
                Vector3& pos = shape->_phase[p][i];
                pos -= changeBoundingCenter;
                float sphere2 = pos.SquareSize();
                saturateMax(maxSphere2, sphere2);
            }
        }
        // change min-max offsets of all lods
        shape->_minMax[0] -= changeBoundingCenter;
        shape->_minMax[1] -= changeBoundingCenter;
        shape->_bCenter -= changeBoundingCenter;
        shape->_minMaxOrig[0] -= changeBoundingCenter;
        shape->_minMaxOrig[1] -= changeBoundingCenter;
        shape->_bCenterOrig -= changeBoundingCenter;
        // change proxy positions
        for (int i = 0; i < shape->_proxy.Size(); i++)
        {
            ProxyObject* proxy = shape->_proxy[i];
            Object* pobj = proxy->obj;
            pobj->SetPosition(pobj->Position() - changeBoundingCenter);
            // recalc. inverse transform
            proxy->invTransform = pobj->InverseScaled();
        }
    }
    _boundingSphere = sqrt(maxSphere2);
    // d coefs of planes are changed
    if (changeBoundingCenter.SquareSize() > 0.01)
    {
        for_each_alpha for (int level = 0; level < _nLods; level++)
        {
            Shape* shape = Level(level);
            if (shape)
            {
                shape->RecalculateNormals(false);
            }
        }
    }
}

void LODShape::CalculateMinMax(bool recalcLevels)
{
    //  calculate only for first LOD-level
    //  we assume this will be valid for the other too
    //  scan through all vertices of all levels
    if (recalcLevels)
    {
        for_each_alpha for (int level = 0; level < NLevels(); level++)
        {
            Shape* shape = Level(level);
            if (!shape)
            {
                continue;
            }
            shape->CalculateMinMax();
            shape->StoreOriginalMinMax();
        }
    }

    // calculate min-max of all levels
    _minMax[0] = Vector3(1e10, 1e10, 1e10);    // min
    _minMax[1] = Vector3(-1e10, -1e10, -1e10); // max
    //_minMax[0]=Vector3(COORD_MAX,COORD_MAX,COORD_MAX); // min
    //_minMax[1]=Vector3(COORD_MIN,COORD_MIN,COORD_MIN); // max
    for_each_alpha for (int level = 0; level < NLevels(); level++)
    {
        Shape* shape = Level(level);
        if (!shape)
        {
            continue;
        }
        SaturateMin(_minMax[0], shape->Min());
        SaturateMax(_minMax[1], shape->Max());
    }

    CalculateBoundingSphere();
}

void LODShape::CheckForcedProperties()
{
    const ParamEntry& notes = Pars >> "CfgModels";
    char shortName[256];
    GetFilename(shortName, GetName());
    while (strpbrk(shortName, " -/()"))
    {
        *strpbrk(shortName, " -/()") = '_';
    }
    const ParamEntry* modelNotes = notes.FindEntry(shortName);
    if (modelNotes)
    {
        // check if some properties are defined
        const ParamEntry* props = modelNotes->FindEntry("properties");
        if (props && NLevels() > 0)
        {
            // scan properties and add them into geometry or topmost level
            Shape* geom = GeometryLevel();
            if (!geom)
            {
                geom = Level(0);
            }
            for (int i = 0; i < props->GetSize() - 1; i += 2)
            {
                RStringB propName = (*props)[i];
                RStringB propVal = (*props)[i + 1];
                geom->SetProperty(propName, propVal);
            }
            // some force properties may change meaning of some shapes
            ScanShapes();
        }
    }
}

void LODShape::ScanProperties()
{
    const char* armor = PropertyValue("armor");
    if (armor && *armor)
    {
        _armor = atof(armor);
    }
    else
    {
        _armor = 200;
    }
    if (_armor > 1e-10)
    {
        _invArmor = 1 / _armor;
        _logArmor = log(_armor);
    }
    else
    {
        _invArmor = 1e10;
        _logArmor = 25;
    }
}

void LODShape::CalculateMass()
{
    // mass should always be stored in geometry
    Shape* shape = GeometryLevel();
    if (!shape)
    {
        return;
    }
    if (_massArray.Size() == 0)
    {
        return;
    }
    double totalMass = 0;
    int i;
    // calculate position of center of mass
    Vector3 sum(VZero);
    for (i = 0; i < shape->_pointToVertex.Size(); i++)
    {
        Vector3Val r = shape->Pos(shape->_pointToVertex[i]);
        float m = _massArray[i];
        totalMass += m;
        sum += r * m;
    }
    if (totalMass > 0.0)
    {
        _centerOfMass = sum * (1 / totalMass);
    }
    else
    {
        _centerOfMass = VZero;
    }

    Matrix3 totalInertia(M3Zero);
    for (i = 0; i < shape->_pointToVertex.Size(); i++)
    {
        //{{FIX inertia
        Vector3Val r = shape->Pos(shape->_pointToVertex[i]) - _centerOfMass;
        //}}FIX inertia
        float m = _massArray[i];
        Matrix3Val rTilda = r.Tilda();
        totalInertia -= rTilda * rTilda * m;
    }
    _mass = totalMass;
    if (totalMass > 0)
    {
        _invInertia = totalInertia.InverseGeneral();
        _invMass = 1 / totalMass;
    }
    else
    {
        _invInertia = M3Identity;
        _invMass = 1;
    }
}

// LOD implementation

void LODShape::DoClear()
{                        // forget everything but name
    _canOcclude = false; // do not use for occlusions unless told otherwise
    _canBeOccluded = false;
    _autoCenter = true;
    _allowAnimation = false;
    _lockAutoCenter = false;
    _special = 0;
    _andHints = _orHints = 0; // default: no hints
    _nLods = 0;
    _remarks = 0;
    _boundingCenter = VZero;
    _geometryCenter = VZero;
    _boundingSphere = 0;
    _minMax[0] = VZero;
    _minMax[1] = VZero;
    _geometry = _memory = _landContact = _roadway = _hitpoints = _paths = -1;
    _geometryFire = _geometryView = -1;

    _geometryViewPilot = -1;
    _geometryViewGunner = -1;
    _geometryViewCommander = -1;
    _geometryViewCargo = -1;

    _invInertia.SetIdentity();
    _centerOfMass = VZero;
    _mass = 0;
    _invMass = 1e10;
    _aimingCenter = VZero;
    _geomComponents = new ConvexComponents();
    _viewComponents = new ConvexComponents();
    _fireComponents = new ConvexComponents();
    _massArray.Clear();
    _viewDensity = -100;
    _color = PackedBlack;
    _colorTop = PackedBlack;
}

void LODShape::DoConstruct()
{
    _name = "";
    DoClear();
}

void LODShape::DoConstruct(const LODShape& src, bool copyAnimations)
{
    // copy shapes, not only references
    for_each_alpha for (int i = 0; i < src._nLods; i++)
    {
        if (src._lods[i])
        {
            _lods[i] = new Shape(*src._lods[i], copyAnimations);
        }
        else
        {
            _lods[i] = nullptr;
        }
        _resolutions[i] = src._resolutions[i];
    }
    _nLods = src._nLods;

    _autoCenter = src._autoCenter;
    _lockAutoCenter = src._lockAutoCenter;
    _allowAnimation = src._allowAnimation;
    _minMax[0] = src._minMax[0], _minMax[1] = src._minMax[1];
    _boundingCenter = src._boundingCenter;
    _boundingSphere = src._boundingSphere;
    _special = src._special;
    _remarks = src._remarks;
    _andHints = src._andHints;
    _orHints = src._orHints;
    _geometry = src._geometry;
    _geometryFire = src._geometryFire;
    _geometryView = src._geometryView;

    _geometryViewPilot = src._geometryViewPilot;
    _geometryViewCommander = src._geometryViewCommander;
    _geometryViewGunner = src._geometryViewGunner;
    _geometryViewCargo = src._geometryViewCargo;

    _memory = src._memory;
    _landContact = src._landContact;
    _roadway = src._roadway;
    _paths = src._paths;
    _hitpoints = src._hitpoints;
    _name = ""; // copy name is empty
    _invInertia = src._invInertia;
    _mass = src._mass;
    _invMass = src._invMass;
    _centerOfMass = src._centerOfMass;
    _aimingCenter = src._aimingCenter;
    if (copyAnimations)
    {
        _geomComponents = new ConvexComponents(*src._geomComponents);
        _viewComponents = new ConvexComponents(*src._viewComponents);
        _fireComponents = new ConvexComponents(*src._fireComponents);
        if (src._massArray.Size() > 0 && GeometryLevel())
        {
            _massArray = src._massArray;
        }
        else
        {
            _massArray.Clear();
        }
    }
    else
    {
        _geomComponents = new ConvexComponents();
        _viewComponents = new ConvexComponents();
        _fireComponents = new ConvexComponents();
        _massArray.Clear();
    }
}

void LODShape::DoDestruct()
{
#if VERBOSE
    if (_name[0])
    {
        LOG_DEBUG(Graphics, "Destruct shape {}", (const char*)_name);
    }
#endif
    int i;
    for (i = 0; i < _nLods; i++)
    {
        _lods[i].Free();
    }
    _nLods = 0;
}

void LODShape::OptimizeShapes()
{
    // Skip LOD optimization when no Application is available (tools/studio context)
    if (!GApp)
        return;

    // delete LODs that are too complex
    // this reduces memory usage
    int i = 0;
    // scan normal LODs
    for (; i < _nLods; i++)
    {
        if (_resolutions[i] >= 900)
        {
            break;
        }
        float relRes = _resolutions[i] / _boundingSphere;
        // LOG_DEBUG(Graphics, "  {}: Relative resolution: {:.3f}",i,relRes);
        if (relRes > ENGINE_CONFIG.objectLODLimit)
        {
            break; // this LOD is neccessary
        }
        int needed = atoi(_lods[i]->PropertyValue("lodneeded"));
        if (needed >= 2)
        {
            break;
        }
    }
    int n = i - 1; // last LOD is always neccesary

    for (i = 0; i < n; i++)
    {
        // delete too complex LODs
        _lods[i] = nullptr;
        LOG_DEBUG(Graphics, "Dropped {}: {} ({})", (const char*)_name, i, _resolutions[i]);
    }

    // remove any nullptr LODs
    int d = 0;
    for (int s = 0; s < _nLods; s++)
    {
        if (_lods[s])
        {
            // delete any vdecal lods (used for some trees)
            if (s != 0 && (_lods[s]->GetAndHints() & ClipDecalMask) != ClipDecalNone && _lods[s]->NPos() > 0)
            {
                LOG_DEBUG(Graphics, "VDecal  {}: {} ({})", (const char*)_name, s, _resolutions[s]);
            }
            else if (ENGINE_CONFIG.enableHWTL && atoi(_lods[s]->PropertyValue("notl")) > 0)
            {
                LOG_DEBUG(Graphics, "TL dropped {}: {} ({})", (const char*)_name, s, _resolutions[s]);
            }
            else
            {
                _resolutions[d] = _resolutions[s];
                _lods[d] = _lods[s];
                _lods[d]->SetLevel(d);
                d++;
            }
        }
    }
    for (int s = d; s < _nLods; s++)
    {
        _lods[s] = nullptr;
    }
    _nLods = d;

    // scan shadow LODs
    for (i = 0; i < _nLods; i++)
    {
        if (_resolutions[i] >= 900)
        {
            break;
        }
        float relRes = _resolutions[i] / _boundingSphere;
        if (relRes > ENGINE_CONFIG.shadowLODLimit)
        {
            break; // this LOD is neccessary
        }
    }
    n = i - 1; // last LOD is always neccesary

    // optimize shadow LODs
    // if some lod is disabled for shadowing
    // all lods before it should be disabled to
    for (i = 0; i < n; i++)
    {
        // disable shadows of too complex LODs
        if (_lods[i]->FindProperty("lodnoshadow") < 0)
        {
            _lods[i]->_prop.Add(NamedProperty("lodnoshadow", "1"));
        }
    }
    ScanShapes();
}

} // namespace Poseidon
#include <Poseidon/World/Terrain/Landscape.hpp>
namespace Poseidon
{

DEFINE_FAST_ALLOCATOR(ProxyObject)

static RString ReplacesChars(RString shapeName, char c, char w)
{
    if (!strchr(shapeName, c))
    {
        return shapeName;
    }
    shapeName.MakeMutable();
    char* str = shapeName.MutableData();
    while (*str)
    {
        if (*str == c)
        {
            *str = w;
        }
        str++;
    }
    return shapeName;
}

bool GReplaceProxies = true;

void LODShape::Load(QIStream& f, bool reversed)
{
    DoClear();

#if VERBOSE
    LOG_DEBUG(Graphics, "Load {}, {}", (const char*)_name, reversed ? "Reversed" : "");
#endif

    // check optimized load
    int seekStart = f.tellg();
    if (LoadOptimized(f))
    {
        // reverse all vector data if necessary
        if (reversed)
        {
            Reverse();
            _remarks |= REM_REVERSED;
        }
        if (!CheckLegalCreator())
        {
            RptF("Bad file format (%s).", Name());
        }
        return;
    }
    f.seekg(seekStart, QIOS::beg);
    // check magic

    _special = 0;
    _remarks = 0;

    if (reversed)
    {
        _remarks |= REM_REVERSED;
    }

    // load all LODs and their setups respectivelly
    // check for LOD header
    char magic[4];
    int nLods = 1;
    bool tagged = false;
    int ver = 0;

    _mass = 0;
    _invMass = 1e10;
    _invInertia.SetZero();
    _centerOfMass = Vector3(VZero);

    f.read(magic, sizeof(magic));
    if (f.fail() || f.eof())
    {
        WarningMessage("Error loading %s (Magic)", (const char*)_name);
        goto Error;
    }
    if (!strncmp(magic, "NLOD", sizeof(magic)))
    {
        // LOD header - we will load multiple LODs
        Log("Warning: NLOD format in object %s", (const char*)_name);
        tagged = true;
        f.read((char*)&nLods, sizeof(nLods));
        if (f.fail() || f.eof())
        {
            WarningMessage("Error loading %s (nLods)", (const char*)_name);
            goto Error;
        }
    }
    else if (!strncmp(magic, "MLOD", sizeof(magic)))
    {
        // LOD header - we will load multiple LODs
        tagged = true;
        f.read((char*)&ver, sizeof(ver));
        f.read((char*)&nLods, sizeof(nLods));
        if (f.fail() || f.eof())
        {
            WarningMessage("Error loading %s (nLods)", (const char*)_name);
            goto Error;
        }
    }
    else
    {
        ErrorMessage("Warning: preNLOD format in object %s", (const char*)_name);
        f.seekg(-(int)sizeof(magic), QIOS::cur);
        Fail("Very old object loaded.");
    }
    int major, minor;
    major = ver >> 8;
    minor = ver & 0xff;
    if (major > 1) // only format versions 1.xx supported
    {
        WarningMessage("%s: Unsupported version %d.%02d", (const char*)_name, major, minor);
        goto Error;
    }

    for (int i = 0; i < nLods; i++)
    {
#if VERBOSE > 1
        LOG_DEBUG(Graphics, "  Load level {}", i);
#endif
        Ref<Shape> shape = new Shape();
        float resolution = 0;

        int startShapeInStream = f.tellg();

        bool wasMassArray = _massArray.Size() > 0;
        resolution = shape->LoadTagged(f, reversed, ver, false, _massArray, tagged);

        bool geometryOnly = ResolGeometryOnly(resolution);
        if (geometryOnly && shape->NFaces() > 0)
        {
            // to avoid unnecessary vertex sharing,
            // reload geometry with no normals
            int endShapeInStream = f.tellg();
            f.seekg(startShapeInStream, QIOS::beg);

            // revert state - massArray might be set during LoadTagged
            if (!wasMassArray)
            {
                _massArray.Clear();
            }

            shape = new Shape();
            shape->LoadTagged(f, reversed, ver, false, _massArray, tagged);
            // revert to new position
            f.seekg(endShapeInStream, QIOS::beg);
        }

        if (shape->_loadWarning)
        {
            LOG_DEBUG(Graphics, "Warnings in {}:{}", (const char*)_name, resolution);
        }
        AddShape(shape, resolution);

        // scan selections for proxy objects

        // scan for proxies
        for (int i = 0; i < shape->_sel.Size(); i++)
        {
            static const char proxyName[] = "proxy:";
            static int proxyNameLen = strlen(proxyName);

            const NamedSelection& sel = shape->_sel[i];
            const char* selName = sel.Name();

            if (strncmp(selName, proxyName, proxyNameLen))
            {
                continue;
            }
            selName += proxyNameLen;

            if (sel.Size() != 3)
            {
                RptF("%s:%s: Bad proxy object definition %s", (const char*)_name, LevelName(this, shape), sel.Name());
                continue;
            }

            // check if proxy selection is hidden
            if (sel.Faces().Size() != 1)
            {
                RptF("%s: Proxy object should be single face %s", (const char*)_name, sel.Name());
                if (sel.Faces().Size() < 1)
                {
                    continue;
                }
            }

            Poly& face = shape->FaceIndexed(sel.Faces()[0]);
            face.OrSpecial(IsHiddenProxy | NoTexMerger);
            face.SetTexture(nullptr);
            if ((face.Special() & (IsHidden | IsHiddenProxy)) == 0)
            {
                LOG_DEBUG(Graphics, "{}:{}: Proxy face should be hidden {}", (const char*)_name, LevelName(this, shape),
                          sel.Name());
            }

            char shapeName[256];
            snprintf(shapeName, sizeof(shapeName), "%s", (const char*)selName);
            char* ext = strchr(shapeName, '.');
            int id = -1;
            if (ext)
            {
                *(ext++) = 0;
                id = atoi(ext);
            }
            // it is proxy: define it

            Ref<ProxyObject> obj = new ProxyObject;
            // create object from shape name
            // shape name may be name of some vehicle class
            //  replace any spaces with '_';
            if (GReplaceProxies)
            {
                RString proxyType = RString("Proxy") + ReplacesChars(shapeName, ' ', '_');
                RString proxyShape = GetShapeName(shapeName);
                Ref<Object> pobj = NewObject(proxyType, proxyShape);
                if (!pobj)
                {
                    RptF("Cannot create proxy object %s", shapeName);
                    continue;
                }
                obj->obj = pobj;
            }
            else
            {
                obj->obj = NewProxyObject(shapeName);
            }
            obj->name = shapeName;
            obj->id = id;
            // get proxy object coordinates
            // scan selection
            int pi0 = sel[0], pi1 = sel[1], pi2 = sel[2];

            const V3* p0 = &shape->Pos(pi0);
            const V3* p1 = &shape->Pos(pi1);
            const V3* p2 = &shape->Pos(pi2);

            float dist01 = p0->Distance2(*p1);
            float dist02 = p0->Distance2(*p2);
            float dist12 = p1->Distance2(*p2);

            // p0,p1 should be the shortest distance
            if (dist01 > dist02)
            {
                swap(p1, p2), swap(dist01, dist02); // swap points 1,2
            }
            if (dist01 > dist12)
            {
                swap(p0, p2), swap(dist01, dist12); // swap points 0,2
            }
            // p0,p2 should be the second shortest distance
            if (dist02 > dist12)
            {
                swap(p0, p1), swap(dist02, dist12);
            }
            //  verify results

            Matrix4 trans;
            trans.SetPosition(*p0);
            trans.SetDirectionAndUp((*p1 - *p0), (*p2 - *p0));
            LODShapeWithShadow* pshape = obj->obj->GetShape();
            if (pshape)
            {
                trans.SetPosition(trans.FastTransform(pshape->BoundingCenter()));
            }
            obj->obj->SetTransform(trans);
            obj->invTransform = trans.InverseScaled();
            obj->selection = i;
            obj->obj->SetDestructType(DestructNo);
            shape->_proxy.Add(obj);
        }

        if (shape->_sel.Size() <= 0)
        {
            // no selections: we may safely optimize
            shape->Optimize();
        }

        // scan for sections
        if (Pars.FindEntry("CfgModels"))
        {
            const ParamEntry& notes = Pars >> "CfgModels";
            // note: shape name may contain spaces
            char shortName[256];
            GetFilename(shortName, GetName());
            while (strpbrk(shortName, " -/()"))
            {
                *strpbrk(shortName, " -/()") = '_';
            }
            if (notes.FindEntry(shortName))
            {
                shape->DefineSections(notes >> shortName);
            }
            else
            {
                shape->DefineSections(notes >> "default");
            }
            if (!geometryOnly)
            {
                shape->FindSections();
            }
        }
    }
    PoseidonAssert(_nLods >= 1);

    // map type
    {
        const char* mapType = PropertyValue("map");
        if (!*mapType)
        {
            // LOG_DEBUG(Graphics, "{}: No map type {}",(const char *)(const char *)_name,mapType);
            _mapType = MapHide;
        }
        else if (stricmp(mapType, "TREE") == 0)
        {
            _mapType = MapTree;
        }
        else if (stricmp(mapType, "SMALL TREE") == 0)
        {
            _mapType = MapSmallTree;
        }
        else if (stricmp(mapType, "BUSH") == 0)
        {
            _mapType = MapBush;
        }
        else if (stricmp(mapType, "BUILDING") == 0)
        {
            _mapType = MapBuilding;
        }
        else if (stricmp(mapType, "HOUSE") == 0)
        {
            _mapType = MapHouse;
        }
        else if (stricmp(mapType, "FOREST BORDER") == 0)
        {
            _mapType = MapForestBorder;
        }
        else if (stricmp(mapType, "FOREST TRIANGLE") == 0)
        {
            _mapType = MapForestTriangle;
        }
        else if (stricmp(mapType, "FOREST SQUARE") == 0)
        {
            _mapType = MapForestSquare;
        }
        else if (stricmp(mapType, "CHURCH") == 0)
        {
            _mapType = MapChurch;
        }
        else if (stricmp(mapType, "CHAPEL") == 0)
        {
            _mapType = MapChapel;
        }
        else if (stricmp(mapType, "CROSS") == 0)
        {
            _mapType = MapCross;
        }
        else if (stricmp(mapType, "ROCK") == 0)
        {
            _mapType = MapRock;
        }
        else if (stricmp(mapType, "BUNKER") == 0)
        {
            _mapType = MapBunker;
        }
        else if (stricmp(mapType, "FORTRESS") == 0)
        {
            _mapType = MapFortress;
        }
        else if (stricmp(mapType, "FOUNTAIN") == 0)
        {
            _mapType = MapFountain;
        }
        else if (stricmp(mapType, "VIEW-TOWER") == 0)
        {
            _mapType = MapViewTower;
        }
        else if (stricmp(mapType, "LIGHTHOUSE") == 0)
        {
            _mapType = MapLighthouse;
        }
        else if (stricmp(mapType, "QUAY") == 0)
        {
            _mapType = MapQuay;
        }
        else if (stricmp(mapType, "FUELSTATION") == 0)
        {
            _mapType = MapFuelstation;
        }
        else if (stricmp(mapType, "HOSPITAL") == 0)
        {
            _mapType = MapHospital;
        }
        else if (stricmp(mapType, "FENCE") == 0)
        {
            _mapType = MapFence;
        }
        else if (stricmp(mapType, "WALL") == 0)
        {
            _mapType = MapWall;
        }
        else if (stricmp(mapType, "HIDE") == 0)
        {
            _mapType = MapHide;
        }
        else if (stricmp(mapType, "BUSSTOP") == 0)
        {
            _mapType = MapBusStop;
        }
        else
        {
            RptF("%s: Unknown map type %s", (const char*)_name, mapType);
            _mapType = MapHide;
        }
    }
    // move (0,0,0) to the geometry center
    {
        const char* autoCenter = PropertyValue("autocenter");
        if (*autoCenter && atoi(autoCenter) == 0)
        {
            _autoCenter = false;
        }
        CalculateMinMax();
        CalculateHints();
        // calculate mass, center of mass and inertia
        if (_massArray.Size() > 0)
        {
            CalculateMass();
        }
    }
    // autodetect if animation should be allowed
    if (_orHints & (ClipLandMask | ClipDecalMask))
    {
        _allowAnimation = true;
    }
    if ((_orHints & ClipLightMask) == (_andHints & ClipLightMask))
    {
        ClipFlags light = _orHints & ClipLightMask;
        switch (light)
        {
            case ClipLightSky:
            case ClipLightCloud:
            case ClipLightStars:
            case ClipLightLine:
                _allowAnimation = true;
                break;
        }
    }

    {
        const char* animated = PropertyValue("animated");
        if (*animated)
        {
            _allowAnimation = atoi(animated) != 0;
        }
    }

    for (int i = 0; i < _nLods; i++)
    {
        if (_resolutions[i] < 900)
        {
            saturateMin(_resolutions[i], BoundingSphere() * 2);
        }
    }
    // remove too complex LODs
    OptimizeShapes();
    // scan only normal LODs

    for (int i = 0; i < _nLods; i++)
    {
        if (_resolutions[i] < 900 && _lods[i])
        {
            Shape* oShape = _lods[i];
            if (oShape)
            {
                _special |= oShape->Special();
            }
        }
    }
    CalculateMinMax();
    if (_massArray.Size() > 0)
    {
        CalculateMass();
    }

    CheckForcedProperties();
    InitConvexComponents();
    ScanProperties();
    CalculateHints();

    _propertyClass = PropertyValue("class");
    _propertyDammage = PropertyValue("dammage");

    {
        int complexity = LevelOpaque(0)->NFaces();
        float size = BoundingSphere();
        Shape* view = ViewGeometryLevel();
        int viewComplexity = view ? view->NFaces() : 0;
        // check size and complexity
        // large or simple objects may occlude
        if (viewComplexity <= 0)
        {
            _canOcclude = false; // view geometry empty
        }
        else if (size > 5)
        {
            _canOcclude = true;
        }
        else if (size > 2 && viewComplexity <= 6)
        {
            _canOcclude = true;
        }
        // large or complex objects may be occluded
        if (complexity >= 6)
        {
            _canBeOccluded = true;
        }
        if (size > 5)
        {
            _canBeOccluded = true;
        }
        // allow override with property
        const char* expCanOcclude = PropertyValue("canocclude");
        const char* expCanBeOccluded = PropertyValue("canbeoccluded");
        if (*expCanOcclude)
        {
            _canOcclude = atoi(expCanOcclude) != 0;
        }
        if (*expCanBeOccluded)
        {
            _canBeOccluded = atoi(expCanOcclude) != 0;
        }
    }

    if (GUseFileBanks && !CheckLegalCreator())
    {
        RptF("Bad file format (%s).", Name());
    }

    return;
Error:
    // create empty shape
    _lods[0] = new Shape();
    _lods[0]->SetLevel(0);
    _resolutions[0] = 0.0f;
    _nLods = 1;
}

void LODShape::PrepareProperties(const ParamEntry& cfg)
{
    // read names of selections that must not split across sections
}

void LODShape::Reload(QIStream& f, bool reversed)
{
    Log("Reload shape %s", (const char*)_name);

    Load(f, reversed);
}

void LODShape::Load(const char* name, bool reversed)
{
    // convert Data.. structures into Poly
    // load external file, convert ...
    // if applied outside constructor, Unload should be performed first
    // on error return empty object
    char nameTemp[1024];
    if (strlen(name) > sizeof(nameTemp))
    {
        WarningMessage("Name '%s' too long.");
    }
    strncpy(nameTemp, name, sizeof(nameTemp)); // save name
    nameTemp[sizeof(nameTemp) - 1] = 0;
    strlwr(nameTemp);
    _name = nameTemp;

    QIFStream f;
    if (GFileServer)
    {
        GFileServer->Open(f, _name);
    }
    else
    {
        f.open(_name);
    }
    if (f.fail())
    {
        WarningMessage("Cannot open object %s", (const char*)_name);
        return;
    }
    Load(f, reversed);
}

ConvexComponents* LODShape::GetConvexComponents(int level) const
{
    if (level == FindGeometryLevel())
    {
        return _geomComponents;
    }
    else if (level == FindFireGeometryLevel())
    {
        return _fireComponents;
    }
    else if (level == FindViewGeometryLevel())
    {
        return _viewComponents;
    }
    else if (level == FindViewPilotGeometryLevel())
    {
        return _viewPilotComponents;
    }
    else if (level == FindViewGunnerGeometryLevel())
    {
        return _viewGunnerComponents;
    }
    else if (level == FindViewCommanderGeometryLevel())
    {
        return _viewCommanderComponents;
    }
    else if (level == FindViewCargoGeometryLevel())
    {
        return _viewCargoComponents;
    }
    else
    {
        return nullptr;
    }
}

void LODShape::FindHitComponents(FindArray<int>& hits, const char* name) const
{
    // scan firegeometry components
    hits.Resize(0);
    Shape* geom = FireGeometryLevel();
    if (!geom)
    {
        return;
    }
    int sel = geom->FindNamedSel(name);
    if (sel < 0)
    {
        return;
    }
    // check if it is in some Component%02d selection
    const NamedSelection& namedSel = geom->NamedSel(sel);

    for (int i = 1; i < 1000; i++)
    {
        char name[64];
        snprintf(name, sizeof(name), "Component%02d", i);
        int selIndex = geom->FindNamedSel(name);
        if (selIndex < 0)
        {
            break;
        }
        const NamedSelection& sel = geom->NamedSel(selIndex);
        if (sel.IsSubset(namedSel) || namedSel.IsSubset(sel))
        {
            // selection in component or component in selection
            hits.AddUnique(i - 1);
        }
    }
}

void LODShape::InvalidateConvexComponents(int level)
{
    ConvexComponents* cc = GetConvexComponents(level);
    if (cc)
    {
        cc->Invalidate();
    }
}

void LODShape::RecalculateConvexComponentsAsNeeded(int level)
{
    ConvexComponents* cc = GetConvexComponents(level);
    if (cc)
    {
        cc->RecalculateAsNeeded(Level(level));
    }
}

void LODShape::InitConvexComponents(ConvexComponents& cc, Shape* geom)
{
    geom->RecalculateNormalsAsNeeded();
    for (int i = 1; i < 1000; i++)
    {
        char name[64];
        snprintf(name, sizeof(name), "Component%02d", i);
        int selIndex = geom->FindNamedSel(name);
        if (selIndex < 0)
        {
            break;
        }
        const NamedSelection& sel = geom->NamedSel(selIndex);
        if (sel.Faces().Size() < 4)
        {
            if (sel.Faces().Size() > 0)
            {
                RptF("Strange convex component %s in %s:%s", (const char*)_name, (const char*)name,
                     LevelName(this, geom));
            }
            continue;
        }
        ConvexComponent* component = new ConvexComponent;
        cc.Add(component);
        component->Init(geom, name);
    }
    cc.Validate();
}

void LODShape::InitCC(Ref<ConvexComponents>& cc, Shape* shape)
{
    if (shape)
    {
        cc = new ConvexComponents();
        InitConvexComponents(*cc, shape);
        if (cc->RecalculateEdges(shape))
        {
            RptF("Shape %s:%s - bad components", (const char*)Name(), LevelName(this, shape));
        }
    }
}

void LODShape::InitConvexComponents()
{
    Shape* geom = GeometryLevel();
    Shape* fire = FireGeometryLevel();
    Shape* view = ViewGeometryLevel();
    if (geom)
    {
        InitConvexComponents(*_geomComponents, geom);
    }
    if (fire)
    {
        if (fire == geom)
        {
            _fireComponents = _geomComponents;
        }
        else
        {
            InitConvexComponents(*_fireComponents, fire);
        }
    }
    if (view)
    {
        if (view == geom)
        {
            _viewComponents = _geomComponents;
        }
        else if (view == fire)
        {
            _viewComponents = _fireComponents;
        }
        else
        {
            InitConvexComponents(*_viewComponents, view);
        }

        const char* expCanOcclude = PropertyValue("canocclude");
        if (*expCanOcclude && atoi(expCanOcclude) == 0)
        {
            // occlusion disabled - no edges
        }
        else
        {
            if (_viewComponents->RecalculateEdges(view))
            {
                RptF("Shape %s:%s - bad components", (const char*)Name(), LevelName(this, view));
            }
        }
    }

    if (geom)
    {
        _geometryCenter = (geom->Max() + geom->Min()) * 0.5;
        _geometrySphere = geom->Max().Distance(geom->Min()) * 0.5;
        _aimingCenter = _geometryCenter;
    }
    else
    {
        _geometrySphere = _boundingSphere;
        _geometryCenter = _boundingCenter;
        _aimingCenter = _boundingCenter;
    }
    // override aiming point if necessary
    const char* aimName = "zamerny";
    if (MemoryPointExists(aimName))
    {
        _aimingCenter = MemoryPoint(aimName);
    }

    InitCC(_viewPilotComponents, ViewPilotGeometryLevel());
    InitCC(_viewGunnerComponents, ViewGunnerGeometryLevel());
    InitCC(_viewCommanderComponents, ViewCommanderGeometryLevel());
    InitCC(_viewCargoComponents, ViewCargoGeometryLevel());
}

const RStringB& LODShape::PropertyValue(const char* name) const
{
    Shape* level = GeometryLevel();
    if (level)
    {
        const RStringB& value = level->PropertyValue(name);
        if (value.GetLength() > 0)
        {
            return value;
        }
    }
    level = _lods[0];
    if (!level)
    {
        Fail("No shape");
        return Foundation::RStringBEmpty;
    }
    return level->PropertyValue(name);
}

int LODShape::FindLevel(float resolution, bool noDecal) const
{
    // find first suitable LOD level
    PoseidonAssert(_nLods >= 1);
    if (resolution < 900)
    {
        // find normal LOD
        int i = 1;
        for (; i < _nLods; i++)
        {
            float aRes = _resolutions[i];
            if (aRes > resolution)
            {
                break; // this one is too rough
            }
            if (aRes >= 900)
            {
                break; // memory or special LOD
            }
        }
        i--;
        while (i > 0 && !_lods[i])
        {
            i--;
        }
        if (noDecal)
        {
            while (i > 0 && _lods[i] && _lods[i - 1] && (_lods[i]->GetAndHints() & ClipDecalMask) != ClipDecalNone)
            {
                i--;
            }
        }
        return _lods[i] ? i : -1;
    }
    else
    {
        // find special LOD
        float minDiff = 1e20;
        int minI = -1;
        for (int i = 0; i < _nLods; i++)
        {
            if (!_lods[i])
            {
                continue;
            }
            float diff = fabs(_resolutions[i] - resolution);
            if (minDiff > diff)
            {
                minDiff = diff, minI = i;
            }
        }
        return minI;
    }
}

bool LODShape::IsSpecLevel(int level, float spec) const
{
    return (level >= 0 && _resolutions[level] > spec * 0.99 && _resolutions[level] < spec * 1.01);
}

int LODShape::FindSpecLevel(float spec) const
{
    int level = FindLevel(spec);
    if (level >= 0 && _resolutions[level] > spec * 0.99 && _resolutions[level] < spec * 1.01)
    {
        return level;
    }
    return -1;
}

int LODShape::FindSqrtLevel(float resolution2, bool noDecal) const
{
    // find first suitable LOD level
    PoseidonAssert(_nLods >= 1);
    // only normal LOD searched
    int i = 1;
    for (; i < _nLods; i++)
    {
        float aRes = _resolutions[i];
        if (aRes * aRes > resolution2)
        {
            break; // this one is too rough
        }
        if (aRes >= 900)
        {
            break; // memory LOD
        }
    }
    i--;
    if (noDecal)
    {
        while (i > 0 && _lods[i - 1] && (_lods[i]->GetAndHints() & ClipDecalMask) != ClipDecalNone)
        {
            i--;
        }
    }
    return i;
}

int LODShape::FindNearestWithoutProperty(int i, const char* property) const
{
    if (i == LOD_INVISIBLE)
    {
        return i;
    }
    PoseidonAssert(i < _nLods);
    PoseidonAssert(i >= 0);
    while (i > 0 && _lods[i] && _lods[i]->FindProperty(property) >= 0)
    {
        i--;
    }
    while (i < _nLods && _lods[i] && _resolutions[i] < 900 && _lods[i]->FindProperty(property) >= 0)
    {
        i++;
    }
    if (i >= _nLods)
    {
        return -1;
    }
    return i;
}

int Shape::PointIndex(const char* name) const
{
    int index = FindNamedSel(name);
    if (index < 0)
    {
        return -1;
    }
    const Selection& sel = NamedSel(index);
    // selection should be one point only
    if (sel.Size() < 1)
    {
        LOG_ERROR(Graphics, "No point in selection {}.", name);
        return -1;
    }
    return sel[0];
}

const V3& Shape::NamedPosition(const char* name, const char* altName) const
{
    int pIndex = PointIndex(name);
    if (pIndex >= 0)
    {
        return Pos(pIndex);
    }
    if (altName)
    {
        int pIndex = PointIndex(altName);
        if (pIndex >= 0)
        {
            return Pos(pIndex);
        }
    }
    return V3Zero;
}

void LODShape::ScanProxies(bool modifyFaces)
{
    extern bool GReplaceProxies;
    for (int l = 0; l < _nLods; l++)
    {
        Shape* shape = _lods[l];
        if (!shape)
            continue;
        for (int i = 0; i < shape->_sel.Size(); i++)
        {
            static const char proxyName[] = "proxy:";
            static int proxyNameLen = strlen(proxyName);
            const NamedSelection& sel = shape->_sel[i];
            const char* selName = sel.Name();
            if (strncmp(selName, proxyName, proxyNameLen))
                continue;
            selName += proxyNameLen;
            if (sel.Size() != 3)
            {
                RptF("%s: Bad proxy object definition %s", (const char*)_name, sel.Name());
                continue;
            }
            if (sel.Faces().Size() < 1)
                continue;
            if (modifyFaces)
            {
                Poly& face = shape->FaceIndexed(sel.Faces()[0]);
                face.OrSpecial(IsHiddenProxy | NoTexMerger);
                face.SetTexture(nullptr);
            }
            char shapeName[256];
            snprintf(shapeName, sizeof(shapeName), "%s", (const char*)selName);
            char* ext = strchr(shapeName, '.');
            int id = -1;
            if (ext)
            {
                *(ext++) = 0;
                id = atoi(ext);
            }
            Ref<ProxyObject> obj = new ProxyObject;
            if (GReplaceProxies)
            {
                RString proxyType = RString("Proxy") + ReplacesChars(shapeName, ' ', '_');
                RString proxyShape = GetShapeName(shapeName);
                Ref<Object> pobj = NewObject(proxyType, proxyShape);
                if (!pobj)
                {
                    RptF("Cannot create proxy object %s", shapeName);
                    continue;
                }
                obj->obj = pobj;
            }
            else
            {
                obj->obj = NewProxyObject(shapeName);
            }
            obj->name = shapeName;
            obj->id = id;
            int pi0 = sel[0], pi1 = sel[1], pi2 = sel[2];
            const V3* p0 = &shape->Pos(pi0);
            const V3* p1 = &shape->Pos(pi1);
            const V3* p2 = &shape->Pos(pi2);
            float dist01 = p0->Distance2(*p1);
            float dist02 = p0->Distance2(*p2);
            float dist12 = p1->Distance2(*p2);
            if (dist01 > dist02)
            {
                swap(p1, p2);
                swap(dist01, dist02);
            }
            if (dist01 > dist12)
            {
                swap(p0, p2);
                swap(dist01, dist12);
            }
            if (dist02 > dist12)
            {
                swap(p0, p1);
                swap(dist02, dist12);
            }
            Matrix4 trans;
            trans.SetPosition(*p0);
            trans.SetDirectionAndUp((*p1 - *p0), (*p2 - *p0));
            LODShapeWithShadow* pshape = obj->obj->GetShape();
            if (pshape)
                trans.SetPosition(trans.FastTransform(pshape->BoundingCenter()));
            obj->obj->SetTransform(trans);
            obj->invTransform = trans.InverseScaled();
            obj->selection = i;
            obj->obj->SetDestructType(DestructNo);
            shape->_proxy.Add(obj);
        }
    }
}

DEFINE_FAST_ALLOCATOR(LODShapeWithShadow)
LODShapeWithShadow::LODShapeWithShadow() = default;

LODShapeWithShadow::LODShapeWithShadow(const char* name, bool reversed) : LODShape(name, reversed) {}

LODShapeWithShadow::LODShapeWithShadow(QIStream& f, bool reversed) : LODShape(f, reversed) {}

LODShape::LODShape()
{
    DoConstruct();
}
LODShape::LODShape(const LODShape& src, bool copyAnimations)
{
    DoConstruct(src, copyAnimations);
    ScanProperties();
}
void LODShape::operator=(const LODShape& src)
{
    DoDestruct();
    DoConstruct(src, true);
}
LODShape::~LODShape()
{
    DoDestruct();
}

LODShape::LODShape(const char* name, bool reversed)
{
    DoConstruct();
    Load(name, reversed);
}
LODShape::LODShape(QIStream& f, bool reversed)
{
    DoConstruct();
    Load(f, reversed);
}

const V3& LODShape::NamedPoint(int level, const char* name, const char* altName) const
{
    Shape* shape = Level(level);
    int pIndex = shape->PointIndex(name);
    if (pIndex >= 0)
    {
        return shape->Pos(pIndex);
    }
    if (altName)
    {
        int pIndex = shape->PointIndex(altName);
        if (pIndex >= 0)
        {
            return shape->Pos(pIndex);
        }
    }
    return V3Zero;
}

const V3& LODShape::MemoryPoint(const char* name, const char* altName) const
{
    Shape* memory = MemoryLevel();
    if (!memory)
    {
        return V3Zero;
    }
    return memory->NamedPosition(name, altName);
}

bool LODShape::MemoryPointExists(const char* name) const
{
    Shape* memory = MemoryLevel();
    if (!memory)
    {
        return false;
    }
    int pIndex = memory->PointIndex(name);
    return pIndex >= 0;
}

bool LODShape::IsInside(Vector3Par pos) const
{
    // make sure there is well defined geometry LOD
    if (_geomComponents->Size() <= 0)
    {
        return false;
    }

    GeometryLevel()->RecalculateNormalsAsNeeded();
    RecalculateGeomComponentsAsNeeded();
    // all calculation will be performed in model space
    for (int iThis = 0; iThis < _geomComponents->Size(); iThis++)
    {
        const ConvexComponent& cThis = *(*_geomComponents)[iThis];
        // check intersection will all convex components
        if (cThis.IsInside(pos))
        {
            return true;
        }
    }
    return false;
}

void LODShape::OrSpecial(int special)
{
    _special |= special;
    for_each_alpha for (int i = 0; i < _nLods; i++)
    {
        if (_lods[i])
        {
            _lods[i]->OrSpecial(special);
        }
    }
}
void LODShape::AndSpecial(int special)
{
    _special &= special;
    for_each_alpha for (int i = 0; i < _nLods; i++)
    {
        if (_lods[i])
        {
            _lods[i]->AndSpecial(special);
        }
    }
}
void LODShape::SetSpecial(int special)
{
    _special = special;
    for_each_alpha for (int i = 0; i < _nLods; i++)
    {
        if (_lods[i])
        {
            _lods[i]->SetSpecial(special);
        }
    }
}
void LODShape::RescanSpecial()
{
    _special = 0;
    for_each_alpha for (int i = 0; i < _nLods; i++)
    {
        if (_resolutions[i] > 900)
        {
            continue;
        }
        if (_lods[i])
        {
            _special |= _lods[i]->Special();
        }
    }
}

void LODShape::ScanShapes()
{
    _geometry = -1;
    _geometryFire = -1;
    _geometryView = -1;

    _geometryViewPilot = -1;
    _geometryViewGunner = -1;
    _geometryViewCommander = -1;
    _geometryViewCargo = -1;

    _memory = -1;
    _landContact = -1;
    _roadway = -1;
    _hitpoints = -1;
    _paths = -1;
    for (int i = 0; i < _nLods; i++)
    {
        float resolution = _resolutions[i];
        if (resolution > 900)
        {
            if (IsSpec(resolution, GEOMETRY_SPEC))
            {
                _geometry = i;
            }
            else if (IsSpec(resolution, MEMORY_SPEC))
            {
                _memory = i;
            }
            else if (IsSpec(resolution, LANDCONTACT_SPEC))
            {
                _landContact = i;
            }
            else if (IsSpec(resolution, ROADWAY_SPEC))
            {
                _roadway = i;
            }
            else if (IsSpec(resolution, PATHS_SPEC))
            {
                _paths = i;
            }
            else if (IsSpec(resolution, HITPOINTS_SPEC))
            {
                _hitpoints = i;
            }
            else if (IsSpec(resolution, VIEW_GEOM_SPEC))
            {
                _geometryView = i;
            }
            else if (IsSpec(resolution, FIRE_GEOM_SPEC))
            {
                _geometryFire = i;
            }
            else if (IsSpec(resolution, VIEW_PILOT_GEOM_SPEC))
            {
                _geometryViewPilot = i;
            }
            else if (IsSpec(resolution, VIEW_GUNNER_GEOM_SPEC))
            {
                _geometryViewGunner = i;
            }
            else if (IsSpec(resolution, VIEW_COMMANDER_GEOM_SPEC))
            {
                _geometryViewCommander = i;
            }
            else if (IsSpec(resolution, VIEW_CARGO_GEOM_SPEC))
            {
                _geometryViewCargo = i;
            }
            else if (resolution < 2000) // cockpit LOD
            {
            }
            else if (resolution < 10000) // spec cockpit LOD
            {
            }
            else
            {
                LOG_DEBUG(Graphics, "{}: Uknown spec lod ({})", (const char*)Name(), resolution);
            }
        }
    }
    if (_geometry >= 0)
    {
        // geometry is made alpha transparent
        _lods[_geometry]->OrSpecial(IsAlpha | IsAlphaFog | IsColored);
    }
    if (_geometryView >= 0)
    {
        // geometry is made alpha transparent
        _lods[_geometryView]->OrSpecial(IsAlpha | IsAlphaFog | IsColored);
    }
    if (_geometryFire >= 0)
    {
        // geometry is made alpha transparent
        _lods[_geometryFire]->OrSpecial(IsAlpha | IsAlphaFog | IsColored);
    }
    if (_geometryView < 0)
    {
        _geometryView = _geometry;
    }
    if (_geometryFire < 0)
    {
        _geometryFire = _geometryView;
    }
    if (_geometry >= 0)
    {
        const char* fireGeom = _lods[_geometry]->PropertyValue("firegeometry");
        if (atoi(fireGeom) > 0)
        {
            _geometryFire = _geometry;
        }
        const char* viewGeom = _lods[_geometry]->PropertyValue("viewgeometry");
        if (atoi(viewGeom) > 0)
        {
            _geometryView = _geometry;
        }
    }
}

void LODShape::AddShape(Shape* shape, float resolution)
{
    // added to the end of the LOD list
    PoseidonAssert(_nLods < MAX_LOD_LEVELS);
    _lods[_nLods] = shape;
    _resolutions[_nLods] = resolution;
    shape->SetLevel(_nLods);
    _nLods++;
    ScanShapes();
}

void LODShape::ChangeShape(int level, Shape* shape)
{
    PoseidonAssert(level < _nLods);
    _lods[level] = shape;
    if (shape)
    {
        shape->SetLevel(level);
    }
}

} // namespace Poseidon
