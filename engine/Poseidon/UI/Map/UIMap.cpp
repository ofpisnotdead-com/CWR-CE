#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Input/CheatCode.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
// #include "win.h"
#include <SDL3/SDL_scancode.h>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>

#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <SDL3/SDL_keycode.h>

#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Terrain/Roads.hpp>

#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Detection/Detector.hpp>

#include <Poseidon/World/MapTypes.hpp>
#include <Evaluator/express.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>

#include <Poseidon/IO/Streams/QBStream.hpp>

#include <Random/randomGen.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>

#include <time.h>

#include <Poseidon/Foundation/Enums/EnumNames.hpp>

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/Chat.hpp>

#include <Poseidon/Graphics/Textures/TexturePreload.hpp>

#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>

#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <ctype.h>

#include <Poseidon/AI/AIRadio.hpp>

#include <Poseidon/Foundation/Strings/StrFormat.hpp>

#include <Poseidon/Game/UiActions.hpp>
#include <stdio.h>
#include <string.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Math/Interpol.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/platform.hpp>

// static const int daysInMonth[]={31,28,31,30,31,30,31,31,30,31,30,31};

namespace Poseidon
{
int GetDaysInMonth(int year, int month);
}
namespace Poseidon
{

extern const float CameraZoom;
extern const float InvCameraZoom;

LSError AbstractOptionsUI::Serialize(ParamArchive& ar)
{
    return LSOK;
}

// Map (generic static control)

#define TO_INT(coef, arg) coef > 0 ? toIntFloor(arg) : toIntCeil(arg)

static void AddChar(BString<32>& buffer, char c)
{
    int n = strlen(buffer);
    if (n < 32 - 1)
    {
        buffer[n] = c;
        buffer[n + 1] = 0;
    }
}

static int Modulo(int& i, int base)
{
    int d = i >= 0 ? i / base : (i - base + 1) / base;
    int m = i - d * base;
    i = d;
    return m;
}

void GridFormat(BString<32>& buffer, RString format, int i)
{
    const char* first = nullptr;
    for (const char* p = format; *p != 0; p++)
    {
        if (isalnum(*p))
        {
            first = p;
            break;
        }
    }
    if (!first)
    {
        return;
    }
    BString<32> revert;
    int cf = 0;
    const char* p;
    for (p = format + format.GetLength() - 1; p != first; p--)
    {
        if (*p >= '0' && *p <= '9')
        {
            int m = Modulo(i, 10) + cf + *p - '0';
            int cf = m;
            m = Modulo(cf, 10);
            AddChar(revert, '0' + m);
        }
        else if (*p >= 'A' && *p <= 'J')
        {
            int m = Modulo(i, 10) + cf + *p - 'A';
            int cf = m;
            m = Modulo(cf, 10);
            AddChar(revert, 'A' + m);
        }
        else if (*p >= 'a' && *p <= 'j')
        {
            int m = Modulo(i, 10) + cf + *p - 'a';
            int cf = m;
            m = Modulo(cf, 10);
            AddChar(revert, 'a' + m);
        }
        else
        {
            AddChar(revert, *p);
        }
    }
    if (*p >= '0' && *p <= '9')
    {
        int m = Modulo(i, 10) + cf + *p - '0';
        int cf = m;
        m = Modulo(cf, 10);
        AddChar(revert, '0' + m);
    }
    else if (*p >= 'A' && *p <= 'Z')
    {
        int m = Modulo(i, 26) + cf + *p - 'A';
        int cf = m;
        m = Modulo(cf, 26);
        AddChar(revert, 'A' + m);
    }
    else if (*p >= 'a' && *p <= 'z')
    {
        int m = Modulo(i, 26) + cf + *p - 'a';
        int cf = m;
        m = Modulo(cf, 26);
        AddChar(revert, 'a' + m);
    }
    p--;
    for (; p >= (const char*)format; p--)
    {
        AddChar(revert, *p);
    }

    for (const char* p = revert + strlen(revert) - 1; p >= (const char*)revert; p--)
    {
        AddChar(buffer, *p);
    }
}

static void AddChar(char* buffer, char c)
{
    int n = strlen(buffer);
    buffer[n] = c;
    buffer[n + 1] = 0;
}

void PositionToAA11(Vector3Val pos, char* buffer)
{
    float sizeLand = LandGrid * LandRange;

    const GridInfo* info = GWorld->GetGridInfo(0);
    if (!info)
    {
        return;
    }

    float offsetX = GWorld->GetGridOffsetX();
    float offsetY = GWorld->GetGridOffsetY();

    *buffer = 0;
    for (const char* p = info->format; *p != 0; p++)
    {
        if (*p == 'X' || *p == 'x')
        {
            float coefX = sizeLand * info->invStepX;
            int x = TO_INT(coefX, (pos.X() - offsetX) * info->invStepX);
            BString<32> buf;
            GridFormat(buf, info->formatX, coefX >= 0 ? x : x - 1);
            strcat(buffer, buf);
        }
        else if (*p == 'Y' || *p == 'y')
        {
            float coefY = sizeLand * info->invStepX;
            int y = TO_INT(coefY, (sizeLand - pos.Z() - offsetY) * info->invStepY);
            BString<32> buf;
            GridFormat(buf, info->formatY, coefY >= 0 ? y : y - 1);
            strcat(buffer, buf);
        }
        else
        {
            AddChar(buffer, *p);
        }
    }
}

void MapTypeInfo::Load(const ParamEntry& cls)
{
    icon = GlobLoadTexture(GetPictureName(cls >> "icon"));
    color = GetPackedColor(cls >> "color");
    size = cls >> "size";
}

CStatic* CreateStaticMap(ControlsContainer* parent, int idc, const ParamEntry& cls)
{
    CStaticMap* map = new CStaticMap(parent, idc, cls, cls >> "scaleMin", cls >> "scaleMax", cls >> "scaleDefault");
    map->SetVisibleRect(map->X(), map->Y(), map->W(), map->H());
    map->Center();
    return map;
}

CStaticMap::CStaticMap(ControlsContainer* parent, int idc, const ParamEntry& cls, float scaleMin, float scaleMax,
                       float scaleDefault)
    : CStatic(parent, idc, cls)
{
    _defaultCenter.Init();
    _defaultCenter[0] = (Pars >> "CfgWorlds" >> Glob.header.worldname >> "centerPosition")[0];
    _defaultCenter[1] = 0;
    _defaultCenter[2] = (Pars >> "CfgWorlds" >> Glob.header.worldname >> "centerPosition")[1];

    float sizeLand = LandGrid * LandRange;
    saturate(_defaultCenter[0], 0, sizeLand);
    saturate(_defaultCenter[2], 0, sizeLand);

    _colorSeaPacked = GetPackedColor(cls >> "colorSea");
    _colorSea = Color((ColorVal)_colorSeaPacked);
    _colorForest = GetPackedColor(cls >> "colorForest");

    _colorCountlines = GetPackedColor(cls >> "colorCountlines");
    _colorCountlinesWater = GetPackedColor(cls >> "colorCountlinesWater");
    _colorForestBorder = GetPackedColor(cls >> "colorForestBorder");
    _colorNames = GetPackedColor(cls >> "colorNames");

    _colorInactive = GetPackedColor(cls >> "colorInactive");

    _fontLabel = GLOB_ENGINE->LoadFont(GetFontID(cls >> "fontLabel"));
    const ParamEntry* entry = cls.FindEntry("sizeLabel");
    if (entry)
    {
        _sizeLabel = (float)(*entry) * _fontLabel->Height();
    }
    else
    {
        _sizeLabel = cls >> "sizeExLabel";
    }
    _fontGrid = GLOB_ENGINE->LoadFont(GetFontID(cls >> "fontGrid"));
    entry = cls.FindEntry("sizeGrid");
    if (entry)
    {
        _sizeGrid = (float)(*entry) * _fontGrid->Height();
    }
    else
    {
        _sizeGrid = cls >> "sizeExGrid";
    }
    _fontUnits = GLOB_ENGINE->LoadFont(GetFontID(cls >> "fontUnits"));
    entry = cls.FindEntry("sizeUnits");
    if (entry)
    {
        _sizeUnits = (float)(*entry) * _fontUnits->Height();
    }
    else
    {
        _sizeUnits = cls >> "sizeExUnits";
    }
    _fontNames = GLOB_ENGINE->LoadFont(GetFontID(cls >> "fontNames"));
    entry = cls.FindEntry("sizeNames");
    if (entry)
    {
        _sizeNames = (float)(*entry) * _fontNames->Height();
    }
    else
    {
        _sizeNames = cls >> "sizeExNames";
    }

    //	_moveOnEdges = cls>>"moveOnEdges";

    _infoTree.Load(cls >> "Tree");
    _infoSmallTree.Load(cls >> "SmallTree");
    _infoBush.Load(cls >> "Bush");
    _infoChurch.Load(cls >> "Church");
    _infoChapel.Load(cls >> "Chapel");
    _infoCross.Load(cls >> "Cross");
    _infoRock.Load(cls >> "Rock");
    _infoBunker.Load(cls >> "Bunker");
    _infoFortress.Load(cls >> "Fortress");
    _infoFountain.Load(cls >> "Fountain");
    _infoViewTower.Load(cls >> "ViewTower");
    _infoLighthouse.Load(cls >> "Lighthouse");
    _infoQuay.Load(cls >> "Quay");
    _infoFuelstation.Load(cls >> "Fuelstation");
    _infoHospital.Load(cls >> "Hospital");
    _infoBusStop.Load(cls >> "BusStop");

    _infoWaypoint.Load(cls >> "Waypoint");
    _infoWaypointCompleted.Load(cls >> "WaypointCompleted");

    EnableCtrl(true);

    const ParamEntry& cfgMap = Pars >> "CfgInGameUI" >> "IslandMap";
    _iconPlayer = GlobLoadTexture(GetPictureName(cfgMap >> "iconPlayer"));
    _iconSelect = GlobLoadTexture(GetPictureName(cfgMap >> "iconSelect"));
    _iconCamera = GlobLoadTexture(GetPictureName(cfgMap >> "iconCamera"));
    _iconSensor = GlobLoadTexture(GetPictureName(cfgMap >> "iconSensor"));
    _colorFriendly = GetPackedColor(cfgMap >> "colorFriendly");
    _colorEnemy = GetPackedColor(cfgMap >> "colorEnemy");
    _colorCivilian = GetPackedColor(cfgMap >> "colorCivilian");
    _colorNeutral = GetPackedColor(cfgMap >> "colorNeutral");
    _colorUnknown = GetPackedColor(cfgMap >> "colorUnknown");
    _colorMe = GetPackedColor(cfgMap >> "colorMe");
    _colorPlayable = GetPackedColor(cfgMap >> "colorPlayable");
    _colorSelect = GetPackedColor(cfgMap >> "colorSelect");
    _colorSensor = GetPackedColor(cfgMap >> "colorSensor");
    _colorDragging = GetPackedColor(cfgMap >> "colorDragging");

    _colorExposureEnemy = GetPackedColor(cfgMap >> "colorExposureEnemy");
    _colorExposureUnknown = GetPackedColor(cfgMap >> "colorExposureUnknown");
    _colorRoads = GetPackedColor(cfgMap >> "colorRoads");
    _colorGrid = GetPackedColor(cfgMap >> "colorGrid");
    _colorGridMap = GetPackedColor(cfgMap >> "colorGridMap");
    _colorCheckpoints = GetPackedColor(cfgMap >> "colorCheckpoints");
    _colorCamera = GetPackedColor(cfgMap >> "colorCamera");
    _colorMissions = GetPackedColor(cfgMap >> "colorMissions");
    _colorActiveMission = GetPackedColor(cfgMap >> "colorActiveMission");
    _colorPath = GetPackedColor(cfgMap >> "colorPath");
    _colorInfoMove = GetPackedColor(cfgMap >> "colorInfoMove");
    _colorGroups = GetPackedColor(cfgMap >> "colorGroups");
    _colorActiveGroup = GetPackedColor(cfgMap >> "colorActiveGroup");
    _colorSync = GetPackedColor(cfgMap >> "colorSync");
    _colorLabelBackground = GetPackedColor(cfgMap >> "colorLabelBackground");

#if _ENABLE_CHEATS
    _show = true;
    _showCost = false;
#endif

    _showIds = false;
    _showScale = true;

    _moveKey = 0;
    _mouseKey = 0;

    _scaleMin = scaleMin;
    _scaleMax = scaleMax;
    _scaleX = _scaleDefault = scaleDefault;

    Reset();

    Precalculate();

    SetVisibleRect(_x, _y, _w, _h);
}

void CStaticMap::Reset()
{
    _moving = false;
    ClearAnimation();
    _moveKey = 0;
    _mouseKey = 0;
    _dragging = false;
    _selecting = false;
    _infoClick._type = signNone;
    _infoClickCandidate._type = signNone;
    _infoMove._type = signNone;

    //	Center();
}

SignInfo CStaticMap::FindSign(float x, float y)
{
    struct SignInfo info;
    info._type = signNone;
    info._unit = nullptr;
    info._id = nullptr;
    return info;
}

void CStaticMap::Center()
{
    Center(_defaultCenter);
}

// colors conversion

static PackedColor Mult2p00(PackedColor color)
{
    DWORD val = color;
    val &= 0x7f7f7f7f;
    val <<= 1;
    return PackedColor(val);
}

static PackedColor Mult1p50(PackedColor color)
{
    DWORD val = color;
    val &= 0xfefefefe;
    val += (val >> 1);
    return PackedColor(val);
}

static PackedColor Mult0p75(PackedColor color)
{
    DWORD val = color;
    val &= 0xfcfcfcfc;
    val += (val >> 1);
    return PackedColor(val >> 1);
}

PackedColor Mult(PackedColor col1, PackedColor col2)
{
    int a = ((col1.A8() + 1) * (col2.A8() + 1) - 1) >> 8;
    int r = ((col1.R8() + 1) * (col2.R8() + 1) - 1) >> 8;
    int g = ((col1.G8() + 1) * (col2.G8() + 1) - 1) >> 8;
    int b = ((col1.B8() + 1) * (col2.B8() + 1) - 1) >> 8;
    return PackedColor(r, g, b, a);
}

PackedColor CStaticMap::InactiveColor(PackedColor color)
{
    return Mult(color, _colorInactive);
}

// coordinates conversion

DrawCoord CStaticMap::WorldToScreen(Vector3Val pt)
{
    float invSizeLand = 1.0 / (LandGrid * LandRange);
    float xMap = pt.X() * invSizeLand * _invScaleX;
    float yMap = (1.0 - pt.Z() * invSizeLand) * _invScaleY;
    return DrawCoord(xMap + _mapX + _x, yMap + _mapY + _y);
}

Point3 CStaticMap::ScreenToWorld(DrawCoord ptMap)
{
    float sizeLand = LandGrid * LandRange;
    float x = (ptMap.x - _mapX - _x) * _scaleX * sizeLand;
    float y = (1.0 - (ptMap.y - _mapY - _y) * _scaleY) * sizeLand;
    // !!! WARNING: result.Y() == 0
    return Point3(x, 0, y);
}

#define CX(x) (toInt((x) * _wScreen) + 0.5)
#define CY(y) (toInt((y) * _hScreen) + 0.5)
#define CW(w) toInt((w) * _wScreen)
#define CH(h) toInt((h) * _hScreen)

// drawing
bool CStaticMap::DrawSign(MapTypeInfo& info, Vector3Val pos)
{
    float size = (_invScaleX * 0.05) * info.size;
    return DrawSign(info.icon, info.color, pos, size, size, 0);
}

bool CStaticMap::DrawSign(Texture* texture, PackedColor color, Vector3Val pos, float w, float h, float azimut,
                          RString text)
{
    DrawCoord posMap = WorldToScreen(pos);
    return DrawSign(texture, color, posMap, w, h, azimut, text);
}

bool CStaticMap::DrawSign(Texture* texture, PackedColor color, DrawCoord posMap, float w, float h, float azimut,
                          RString text)
{
    // normalize w, h
    w *= 1.0 / 640;
    h *= 1.0 / 480;

    if (azimut == 0)
    {
        // draw via Draw2D

        // center sign
        posMap.x -= 0.5 * w;
        posMap.y -= 0.5 * h;

        if (posMap.x + w < _x)
        {
            return false;
        }
        if (posMap.y + h < _y)
        {
            return false;
        }
        if (posMap.x > _x + _w)
        {
            return false;
        }
        if (posMap.y > _y + _h)
        {
            return false;
        }

        MipInfo mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
        GEngine->Draw2D(
            mip, color,
            Rect2DPixel(CX(posMap.x), CY(posMap.y), CX(posMap.x + w) - CX(posMap.x), CY(posMap.y + h) - CY(posMap.y)),
            _clipRect);

        posMap.x += w;
        posMap.y += 0.5 * h;
    }
    else
    {
        // draw via DrawPoly
        float a = 0.5 * w;
        float b = 0.5 * h;

        float r = sqrt(Square(a) + Square(b));
        if (posMap.x + r < _x)
        {
            return false;
        }
        if (posMap.y + r < _y)
        {
            return false;
        }
        if (posMap.x - r > _x + _w)
        {
            return false;
        }
        if (posMap.y - r > _y + _h)
        {
            return false;
        }

        float s = sin(azimut);
        float c = cos(azimut);
        float cx = posMap.x * _wScreen;
        float cy = posMap.y * _hScreen;
        a *= _wScreen;
        b *= _hScreen;

        const int n = 4;
        Vertex2DPixel vs[n];
        // 0
        vs[0].x = cx + c * a + s * b;
        vs[0].y = cy + s * a - c * b;
        vs[0].u = 1;
        vs[0].v = 0;
        vs[0].color = color;
        // 1
        vs[1].x = cx + c * a + s * (-b);
        vs[1].y = cy + s * a - c * (-b);
        vs[1].u = 1;
        vs[1].v = 1;
        vs[1].color = color;
        // 2
        vs[2].x = cx + c * (-a) + s * (-b);
        vs[2].y = cy + s * (-a) - c * (-b);
        vs[2].u = 0;
        vs[2].v = 1;
        vs[2].color = color;
        // 3
        vs[3].x = cx + c * (-a) + s * b;
        vs[3].y = cy + s * (-a) - c * b;
        vs[3].u = 0;
        vs[3].v = 0;
        vs[3].color = color;

        MipInfo mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
        GEngine->DrawPoly(mip, vs, n, _clipRect);

        posMap.x += 0.5 * (fabs(c * w) + fabs(s * h));
    }

    // text
    if (text.GetLength() > 0)
    {
        float size = _sizeNames * (_invScaleX * 0.05);
        saturate(size, 0.5 * _fontNames->Height(), 1.0 * _fontNames->Height());
        float hText = size;
        GEngine->DrawText(Point2DFloat(posMap.x, posMap.y - 0.5 * hText), size, Rect2DFloat(_x, _y, _w, _h), _fontNames,
                          PackedColor(color), text);
    }

    return true;
}

static const float ptsPerSquareSea = 6;  // seas
static const float ptsPerSquareTxt = 8;  // textures
static const float ptsPerSquareCLn = 8;  // countlines
static const float ptsPerSquareExp = 8;  // exposure
static const float ptsPerSquareCost = 8; // cost

static const float ptsPerSquareFor = 6;    // forests
static const float ptsPerSquareForBor = 6; // forests' borders
static const float ptsPerSquareRoad = 2;   // roads
static const float ptsPerSquareObj = 10;   // other objects

// Object level-of-detail: the "other objects" layer (scattered trees, rocks,
// bushes, building footprints) is thousands of individual sub-pixel icons when
// zoomed out.  Once a land square drops below this many screen points the loop
// strides by toIntCeil(ptsPerSquareObjLod*invPtsLand) cells, thinning the icon
// count without a hard cutoff.  Chosen so the default in-mission zoom (ptsLand
// ~19.5) stays per-cell and the stride only grows once the user zooms out.
static const float ptsPerSquareObjLod = 15;

} // namespace Poseidon
#include <Poseidon/Foundation/Containers/QuadtreeCont.hpp>
namespace Poseidon
{

struct DetectCircle
{
    int _x;
    int _z;
    int _radius;

    DetectCircle(int x, int z, int radius) : _x(x), _z(z), _radius(radius) {}
    bool operator()(int x, int z, int size) const
    {
        // calculate distance of rectangle center from our center
        int centerX = x + (size >> 1);
        int centerZ = z + (size >> 1);

        float dist2 = Square(centerX - _x) + Square(centerZ - _z);
        // check if we can quickly reject or quickly accept
        if (dist2 < Square(_radius + size))
        {
            // part of the circle is certainly contained in the rectangle
            return true;
        }
        if (dist2 > Square(_radius + size) * 2)
        {
            // circle is certianly of of the rectangle
            return false;
        }
        // circle may or may not be in the rectangle
        // we can perform stricter check, or we can return true and provide
        // some superfluous items
        return true;
    }
};

struct MountainInfo : public Vector3
{
    int _index;
    bool operator==(const MountainInfo& with) const { return _index == with._index; }
    MountainInfo() { _index = -1; }
    MountainInfo(Vector3Par pos, int index) : Vector3(pos), _index(index) {}

    int GetX() const { return toLargeInt(X()); }
    int GetY() const { return toLargeInt(Z()); }
};

struct SkipNear
{
    MountainInfo _pos;
    // bool _skipped;
    float _minDist;
    SkipNear(MountainInfo pos, float minDist) : _pos(pos), _minDist(minDist) {}
    bool operator()(const MountainInfo& arg) const
    {
        // check if the mountain is near enough
        // check if the mountain is higher
        if (arg._index >= _pos._index)
        {
            return false;
        }
        float dist2 = _pos.DistanceXZ2(arg);
        return dist2 < Square(_minDist);
    }
};

struct MountainRegion
{
    // in
    float _xMin, _xMax;
    float _zMin, _zMax;

    bool operator()(int x, int y, int size) const
    {
        float xMin = x, xMax = x + size;
        float zMin = y, zMax = y + size;
        bool ret = true;
        if (_xMin > xMax)
        {
            ret = false;
        }
        if (_xMax < xMin)
        {
            ret = false;
        }
        if (_zMin > zMax)
        {
            ret = false;
        }
        if (_zMax < zMin)
        {
            ret = false;
        }
        // LOG_DEBUG(UI, "Testing {},{}:{}, result {}",x,y,size,ret);
        return ret;
    }
};

struct MountainList
{
    // out
    AutoArray<int> _indices;

    bool operator()(const MountainInfo& pos)
    {
        _indices.Add(pos._index);
        return false;
    }
};

void CStaticMap::DrawBackground()
{
    float invLandRange = 1.0 / LandRange;
    float ptsLand = 800.0 * _invScaleX * invLandRange; // avoid dependece on video resolution
    float invPtsLand = 1.0 / ptsLand;

    float invTerrainRange = 1.0 / TerrainRange;
    float ptsTerrain = 800.0 * _invScaleX * invTerrainRange; // avoid dependece on video resolution
    float invPtsTerrain = 1.0 / ptsTerrain;

    // GEngine->ShowMessage(1000, "Scale = %.4f, PtsPerFld = %.2f", _scaleX, ptsLand);

    if (_texture)
    {
        // draw paper
        MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(_texture, 0, 0);

        float xStep = (1.0 / 2.0) * _invScaleX;
        float yStep = (1.0 / 2.0) * _invScaleY;
        float xMin = _x - fmod(-_mapX, xStep);
        float yMin = _y - fmod(-_mapY, yStep);

        for (float y = yMin; y < _y + _h; y += yStep)
        {
            for (float x = xMin; x < _x + _w; x += xStep)
            {
                GEngine->Draw2D(mip, _bgColor,
                                Rect2DPixel(x * _wScreen, y * _hScreen, xStep * _wScreen, yStep * _hScreen), _clipRect);
            }
        }
    }

    {
        // draw sea
        int iStep = toIntCeil(ptsPerSquareSea * invPtsLand);
        float xStep = iStep * invLandRange * _invScaleX;
        int jStep = iStep;
        float yStep = jStep * invLandRange * _invScaleY;
        int iMin = iStep * toIntFloor(-_mapX / xStep);
        float xMin = _x - fastFmod(-_mapX, xStep);
        int jMin = LandRange - jStep * toIntFloor(-_mapY / yStep);
        float yMin = _y - fastFmod(-_mapY, yStep);

        float y = yMin;
        int j = jMin;
        while (y <= _y + _h)
        {
            float x = xMin;
            int i = iMin;
            while (x <= _x + _w)
            {
                DrawSea(x, y, xStep, yStep, i, j, iStep, -jStep);
                i += iStep;
                x += xStep;
            }
            j -= jStep;
            y += yStep;
        }
    }

    // #if _ENABLE_CHEATS
    if (!_showScale)
    {
        // draw fields (textures)
        int iStep = toIntCeil(ptsPerSquareTxt * invPtsLand);
        float xStep = iStep * invLandRange * _invScaleX;
        int jStep = iStep;
        float yStep = jStep * invLandRange * _invScaleY;
        int iMin = iStep * toIntFloor(-_mapX / xStep);
        float xMin = _x - fmod(-_mapX, xStep);
        int jMin = LandRange - jStep * toIntFloor(-_mapY / yStep);
        float yMin = _y - fmod(-_mapY, yStep);

        // calculate mipmap level
        static const float invLog2 = 1.0 / log(2.0);
        _mipmapLevel = 7 - toIntFloor(log(floatMax(xStep * _wScreen, yStep * _hScreen)) * invLog2);
        if (_mipmapLevel < 0)
        {
            _mipmapLevel = 0;
        }
        if (_mipmapLevel > 8)
        {
            _mipmapLevel = 8;
        }

        float y = yMin;
        float j = jMin;
        while (y <= _y + _h)
        {
            float x = xMin;
            int i = iMin;
            while (x <= _x + _w)
            {
                DrawField(x, y, xStep, yStep, i, (int)j, iStep, -jStep);
                i += iStep;
                x += xStep;
            }
            j -= jStep;
            y += yStep;
        }
    }
    // #endif

#if _ENABLE_CHEATS
    if (_show)
#endif
    {
        // draw countlines
        {
            int iStep = toIntCeil(ptsPerSquareCLn * invPtsTerrain);
            float xStep = iStep * invTerrainRange * _invScaleX;
            int jStep = iStep;
            float yStep = jStep * invTerrainRange * _invScaleY;
            int iMin = iStep * toIntFloor(-_mapX / xStep);
            float xMin = _x - fmod(-_mapX, xStep);
            int jMin = TerrainRange - jStep * toIntFloor(-_mapY / yStep);
            float yMin = _y - fmod(-_mapY, yStep);

            float step = 50.0 * _scaleX;
            int stepexp = toIntFloor(log10(step));
            float step10 = pow(10.0, stepexp);
            step /= step10;
            if (step <= 2.0)
            {
                step = 2.0;
            }
            else if (step <= 5.0)
            {
                step = 5.0;
            }
            else
            {
                step = 10.0;
            }
            step *= step10;
            float maxLevel = step * toIntCeil(10000.0 / step);

            float y = yMin;
            int j = jMin;
            while (j >= 0 && y <= _y + _h)
            {
                float x = xMin;
                int i = iMin;
                while (i < TerrainRange && x <= _x + _w)
                {
                    DrawCountlines(x, y, xStep, yStep, i, j, iStep, -jStep, step, maxLevel);
                    i += iStep;
                    x += xStep;
                }
                j -= jStep;
                y += yStep;
            }
        }
        // draw objects
        {
            float xStep = invLandRange * _invScaleX;
            float yStep = invLandRange * _invScaleY;

            int iMin = toIntFloor(-_mapX / xStep);
            float xMin = _x - fmod(-_mapX, xStep);
            int jMin = LandRange - toIntFloor(-_mapY / yStep);
            float yMin = _y - fmod(-_mapY, yStep);

            if (ptsLand >= ptsPerSquareFor)
            {
                // forests
                float y = yMin;
                int j = jMin;
                while (j >= 0 && y <= _y + _h)
                {
                    float x = xMin;
                    int i = iMin;
                    while (i < LandRange && x <= _x + _w)
                    {
                        DrawForests(i, j, x, y, xStep, yStep);
                        i++;
                        x += xStep;
                    }
                    j--;
                    y += yStep;
                }
            }
            if (ptsLand >= ptsPerSquareForBor)
            {
                // forest borders
                float y = yMin;
                int j = jMin;
                while (j >= 0 && y <= _y + _h)
                {
                    float x = xMin;
                    int i = iMin;
                    while (i < LandRange && x <= _x + _w)
                    {
                        DrawForestBorders(i, j, x, y, xStep, yStep);
                        i++;
                        x += xStep;
                    }
                    j--;
                    y += yStep;
                }
            }
            if (ptsLand >= ptsPerSquareRoad)
            {
                // roads
                float y = yMin;
                int j = jMin;
                while (j >= 0 && y <= _y + _h)
                {
                    float x = xMin;
                    int i = iMin;
                    while (i < LandRange && x <= _x + _w)
                    {
                        DrawRoads(i, j);
                        i++;
                        x += xStep;
                    }
                    j--;
                    y += yStep;
                }
            }
            if (ptsLand >= ptsPerSquareObj)
            {
                // other objects — stride coarsens at zoom-out, where the icons are
                // sub-pixel clutter and the per-object draw count is what hurts.
                // The grid is snapped to objStep land-cell multiples (like the
                // field loop) so the thinned set is fixed in world space and the
                // icons translate with the map instead of popping on pan.
                int objStep = toIntCeil(ptsPerSquareObjLod * invPtsLand);
                if (objStep < 1)
                {
                    objStep = 1;
                }
                float oxStep = objStep * xStep;
                float oyStep = objStep * yStep;
                int oiMin = objStep * toIntFloor(-_mapX / oxStep);
                float oxMin = _x - fmod(-_mapX, oxStep);
                int ojMin = LandRange - objStep * toIntFloor(-_mapY / oyStep);
                float oyMin = _y - fmod(-_mapY, oyStep);
                float y = oyMin;
                int j = ojMin;
                while (j >= 0 && y <= _y + _h)
                {
                    float x = oxMin;
                    int i = oiMin;
                    while (i < LandRange && x <= _x + _w)
                    {
                        DrawObjects(i, j);
                        i += objStep;
                        x += oxStep;
                    }
                    j -= objStep;
                    y += oyStep;
                }
            }
        }
    }

    // names
    const ParamEntry& world = Pars >> "CfgWorlds" >> Glob.header.worldname;
    const ParamEntry* cls = &(world >> "Names");
    if (cls)
    {
        AUTO_STATIC_ARRAY(Vector3, points, 32);
        float minDist2 = Square(1000 * _scaleX);
        int n = cls->GetEntryCount();
        points.Resize(n);
        for (int i = 0; i < n; i++)
        {
            const ParamEntry& item = cls->GetEntry(i);
            Vector3 pos;
            pos.Init();
            pos[0] = (item >> "position")[0];
            pos[2] = (item >> "position")[1];
            pos[1] = 0;
            points[i] = pos;
            bool skip = false;
            for (int j = 0; j < i; j++)
            {
                if (pos.DistanceXZ2(points[j]) < minDist2)
                {
                    skip = true;
                    break;
                }
            }
            if (!skip)
            {
                DrawName(item);
            }
        }
    }
    else
    {
        Fail("Class <Names> expected");
    }

#if 1
    {
        // mountains
        QuadTreeCont<MountainInfo> mountainPos;

        const AutoArray<Vector3>& mountains = GLandscape->GetMountains();
        for (int i = 0; i < mountains.Size(); i++)
        {
            const Vector3& pos = mountains[i];
            // check if given pos is in the visible range
            int x = toLargeInt(pos.X());
            int z = toLargeInt(pos.Z());

            mountainPos.Set(x, z, MountainInfo(pos, i));
        }

        MountainRegion region;
        Vector3 minPos = ScreenToWorld(Point2DFloat(_x, _y));
        Vector3 maxPos = ScreenToWorld(Point2DFloat(_x + _w, _y + _h));
        region._xMin = floatMin(minPos.X(), maxPos.X());
        region._zMin = floatMin(minPos.Z(), maxPos.Z());
        region._xMax = floatMax(minPos.X(), maxPos.X());
        region._zMax = floatMax(minPos.Z(), maxPos.Z());

        MountainList list;

        mountainPos.ForEachInRegion(region, list);

        float minDistF = 1000 * _scaleX;
        int minDist = toLargeInt(1000 * _scaleX) + 2;
        for (int ii = 0; ii < list._indices.Size(); ii++)
        {
            int i = list._indices[ii];
            // make space based query
            const Vector3& pos = mountains[i];

            int x = toLargeInt(pos.X());
            int z = toLargeInt(pos.Z());
            SkipNear ctx(MountainInfo(pos, i), minDistF);

            if (!mountainPos.ForEachInRegion(DetectCircle(x, z, minDist), ctx))
            {
                DrawMount(pos);
            }
        }
    }
#else
    // mountains
    const AutoArray<Vector3>& mountains = GLandscape->GetMountains();
    float minDist2 = Square(1000 * _scaleX);
    for (int i = 0; i < mountains.Size(); i++)
    {
        const Vector3& pos = mountains[i];
        bool skip = false;
        for (int j = 0; j < i; j++)
        {
            if (pos.DistanceXZ2(mountains[j]) < minDist2)
            {
                skip = true;
                break;
            }
        }
        if (!skip)
            DrawMount(pos);
    }
#endif
}

void CStaticMap::DrawLegend()
{
    float size = _w;
    float x = _x + 0.05 * size;
    float y = _y + _h - 0.15 * size;
    float w = 0.25 * size;
    float h = 0.1 * size;
    float cx = CX(x), cy = CY(y), cw = CW(w), ch = CH(h);

    Draw2DPars pars;
    pars.mip = GEngine->TextBank()->UseMipmap(nullptr, 0, 0);
    pars.spec = NoZBuf | IsAlpha | ClampU | ClampV | IsAlphaFog;
    pars.SetColor(PackedWhite);
    pars.SetU(0, 1);
    pars.SetV(0, 1);
    Rect2DPixel rect(cx, cy, cw, ch);
    GEngine->Draw2D(pars, rect, _clipRect);

    PackedColor color = PackedBlack;
    GEngine->DrawLine(Line2DPixel(cx, cy, cx + cw, cy), color, color, _clipRect);
    GEngine->DrawLine(Line2DPixel(cx + cw, cy, cx + cw, cy + ch), color, color, _clipRect);
    GEngine->DrawLine(Line2DPixel(cx + cw, cy + ch, cx, cy + ch), color, color, _clipRect);
    GEngine->DrawLine(Line2DPixel(cx, cy + ch, cx, cy), color, color, _clipRect);

    float top = y + 0.01 * size;
    float left = x + 0.01 * size;

    float step = 50.0 * _scaleX;
    int stepexp = toIntFloor(log10(step));
    float step10 = pow(10.0, stepexp);
    step /= step10;
    if (step <= 2.0)
        step = 2.0;
    else if (step <= 5.0)
        step = 5.0;
    else
        step = 10.0;
    step *= step10;

    Font* font = _fontGrid;
    // Legend strings live in `packages/Combined/BIN/STRINGTABLE_MAP.utf8.csv`
    // so the briefing-map scale picks up the active UI language.
    // The new key restores the correct CONTOUR spelling — the
    // original hardcoded literal had a typo on the first word.
    RString contour = LocalizeString("STR_MAP_CONTOUR_INTERVAL");
    RString elevations = LocalizeString("STR_MAP_ELEVATIONS_IN_METERS");
    GEngine->DrawTextF(Point2DFloat(left, top), size * 0.02, Rect2DFloat(x, y, w, h), font, color, contour, step);
    top += size * 0.02;
    GEngine->DrawText(Point2DFloat(left, top), size * 0.02, Rect2DFloat(x, y, w, h), font, color, elevations);
    top += size * 0.03;

    step *= 10;
    float mapStep = InvLandGrid * InvLandRange * _invScaleX * step;

    GEngine->DrawLine(Line2DPixel(CX(left), CY(top), CX(left), CY(top + size * 0.02)), color, color, _clipRect);
    GEngine->DrawLine(Line2DPixel(CX(left + mapStep), CY(top), CX(left + mapStep), CY(top + size * 0.02)), color, color,
                      _clipRect);
    GEngine->DrawLine(Line2DPixel(CX(left), CY(top + size * 0.01), CX(left + mapStep), CY(top + size * 0.01)), color,
                      color, _clipRect);
    GEngine->DrawTextF(Point2DFloat(left + mapStep + size * 0.01, top), size * 0.02, Rect2DFloat(x, y, w, h), font,
                       color, "%g m", step);
}

void CStaticMap::DrawName(const ParamEntry& cls)
{
    Vector3 pos;
    pos.Init();
    pos[0] = (cls >> "position")[0];
    pos[2] = (cls >> "position")[1];
    pos[1] = 0;
    DrawCoord pt = WorldToScreen(pos);

    float size = _sizeNames * (_invScaleX * 0.05);
    saturate(size, 0.5 * _fontNames->Height(), 2.0 * _fontNames->Height());

    RString text = cls >> "name";
    float h = size;
    float w = GEngine->GetTextWidth(size, _fontNames, text);

    if (pt.x + w < _x)
    {
        return;
    }
    if (pt.y + 0.5 * h < _y)
    {
        return;
    }
    if (pt.x > _x + _w)
    {
        return;
    }
    if (pt.y - 0.5 * h > _y + _h)
    {
        return;
    }

    GEngine->DrawText(Point2DFloat(pt.x, pt.y - 0.5 * h), size, Rect2DFloat(_x, _y, _w, _h), _fontNames, _colorNames,
                      text);
}

void CStaticMap::DrawMount(Vector3Par pos)
{
    DrawCoord pt = WorldToScreen(pos);
    float size = 0.02;

    char buffer[256];
    ::sprintf(buffer, "%.0f", pos.Y());

    float h = size;
    float w = GEngine->GetTextWidth(size, _fontNames, buffer);

    if (pt.x + w + 0.005 < _x)
    {
        return;
    }
    if (pt.y + 0.5 * h < _y)
    {
        return;
    }
    if (pt.x > _x + _w)
    {
        return;
    }
    if (pt.y - 0.5 * h > _y + _h)
    {
        return;
    }

    int x = toIntFloor(pt.x * _wScreen);
    int y = toInt(pt.y * _hScreen);
    GEngine->DrawLine(Line2DPixel(x, y - 1, x + 1, y - 1), _colorCountlines, _colorCountlines, _clipRect);
    GEngine->DrawLine(Line2DPixel(x + 1, y - 1, x + 1, y + 1), _colorCountlines, _colorCountlines, _clipRect);
    GEngine->DrawLine(Line2DPixel(x + 1, y + 1, x, y + 1), _colorCountlines, _colorCountlines, _clipRect);
    GEngine->DrawLine(Line2DPixel(x, y + 1, x, y - 1), _colorCountlines, _colorCountlines, _clipRect);
    GEngine->DrawText(Point2DFloat(pt.x + 0.005, pt.y - 0.5 * h), size, Rect2DFloat(_x, _y, _w, _h), _fontNames,
                      _colorNames, buffer);
}

#define HEIGHT_LOG (GLandscape->GetTerrainRangeLog() - GLandscape->GetLandRangeLog())

#define GetHeightNT(j, i) GLandscape->GetHeight((j) << HEIGHT_LOG, (i) << HEIGHT_LOG)

void CStaticMap::DrawField(float x, float y, float xStep, float yStep, int i, int j, int iStep, int jStep)
{
    if (InRange(i, j) || InRange(i + iStep, j + jStep))
    {
        // draw only when there is a chance to draw anything
        Draw2DPars pars;
        int maxAlpha8 = 0;

        float alpha = 0.5 + 0.1 * GetHeightNT(j, i);
        int alpha8TL = toIntFloor(alpha * 255);
        alpha = 0.5 + 0.1 * GetHeightNT(j, i + iStep);
        int alpha8TR = toIntFloor(alpha * 255);
        alpha = 0.5 + 0.1 * GetHeightNT(j + jStep, i);
        int alpha8BL = toIntFloor(alpha * 255);
        alpha = 0.5 + 0.1 * GetHeightNT(j + jStep, i + iStep);
        int alpha8BR = toIntFloor(alpha * 255);

        if (maxAlpha8 <= alpha8TL)
        {
            if (alpha8TL > 255)
            {
                alpha8TL = 255;
            }
            maxAlpha8 = alpha8TL;
        }
        else if (alpha8TL < 0)
        {
            alpha8TL = 0;
        }
        if (maxAlpha8 <= alpha8TR)
        {
            if (alpha8TR > 255)
            {
                alpha8TR = 255;
            }
            maxAlpha8 = alpha8TR;
        }
        else if (alpha8TR < 0)
        {
            alpha8TR = 0;
        }
        if (maxAlpha8 <= alpha8BL)
        {
            if (alpha8BL > 255)
            {
                alpha8BL = 255;
            }
            maxAlpha8 = alpha8BL;
        }
        else if (alpha8BL < 0)
        {
            alpha8BL = 0;
        }
        if (maxAlpha8 <= alpha8BR)
        {
            if (alpha8BR > 255)
            {
                alpha8BR = 255;
            }
            maxAlpha8 = alpha8BR;
        }
        else if (alpha8BR < 0)
        {
            alpha8BR = 0;
        }

        if (maxAlpha8 > 0)
        {
            Texture* texture = nullptr;

            if (iStep == 1 && jStep == -1)
            {
                // TL
                pars.colorTL = Mult1p50(GLOB_LAND->GetRandomColor(i, j, pars.uTL, pars.vTL));
                pars.colorTL.SetA8(alpha8TL);
                // TR
                pars.colorTR = Mult1p50(GLOB_LAND->GetRandomColor(i + iStep, j, pars.uTR, pars.vTR));
                pars.colorTR.SetA8(alpha8TR);
                // BL
                pars.colorBL = Mult1p50(GLOB_LAND->GetRandomColor(i, j + jStep, pars.uBL, pars.vBL));
                pars.colorBL.SetA8(alpha8BL);
                // BR
                pars.colorBR = Mult1p50(GLOB_LAND->GetRandomColor(i + iStep, j + jStep, pars.uBR, pars.vBR));
                pars.colorBR.SetA8(alpha8BR);

                int id = GLOB_LAND->GetTexture(j + jStep, i);
                texture = GLOB_LAND->GetTexture(id);
                pars.spec = NoZBuf | IsAlpha | IsAlphaFog | DetailTexture | GLOB_LAND->ClampFlags(id);
            }
            else
            {
                // TL
                int id = GLOB_LAND->GetTexture(j, i);
                pars.colorTL = Mult0p75(PackedColor(GLOB_LAND->GetTexture(id)->GetColor()));
                pars.colorTL.SetA8(alpha8TL);
                // TR
                id = GLOB_LAND->GetTexture(j, i + iStep);
                pars.colorTR = Mult0p75(PackedColor(GLOB_LAND->GetTexture(id)->GetColor()));
                pars.colorTR.SetA8(alpha8TR);
                // BL
                id = GLOB_LAND->GetTexture(j + jStep, i);
                pars.colorBL = Mult0p75(PackedColor(GLOB_LAND->GetTexture(id)->GetColor()));
                pars.colorBL.SetA8(alpha8BL);
                // BR
                id = GLOB_LAND->GetTexture(j + jStep, i + iStep);
                pars.colorBR = Mult0p75(PackedColor(GLOB_LAND->GetTexture(id)->GetColor()));
                pars.colorBR.SetA8(alpha8BR);

                pars.spec = NoZBuf | IsAlpha | IsAlphaFog | NoClamp;
            }

            pars.SetU(0, 0);
            pars.SetV(0, 0);
            pars.vTL += 1;
            pars.uTR += 1;
            pars.vTR += 1;
            pars.uBR += 1;

            Rect2DPixel rect(x * _wScreen, y * _hScreen, xStep * _wScreen, yStep * _hScreen);

            pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, _mipmapLevel, _mipmapLevel);
            GLOB_ENGINE->Draw2D(pars, rect, _clipRect);
        }
    }
}

void CStaticMap::DrawSea(float x, float y, float xStep, float yStep, int i, int j, int iStep, int jStep)
{
    const float waves = 5.0;
    const float invWaves = 1.0 / (2.0 * waves);

    float heightTL = GetHeightNT(j, i);
    float heightTR = GetHeightNT(j, i + iStep);
    float heightBL = GetHeightNT(j + jStep, i);
    float heightBR = GetHeightNT(j + jStep, i + iStep);
    if (heightTL >= waves && heightTR >= waves && heightBL >= waves && heightBR >= waves)
    {
        return;
    }

    Draw2DPars pars;

    if (heightTL <= -waves && heightTR <= -waves && heightBL <= -waves && heightBR <= -waves)
    {
        // simple case: sea only
        pars.colorTL = _colorSeaPacked;
        pars.colorTR = _colorSeaPacked;
        pars.colorBL = _colorSeaPacked;
        pars.colorBR = _colorSeaPacked;
    }
    else
    {
        heightTL += waves;
        heightTL *= invWaves;
        saturate(heightTL, 0, 1);
        heightTR += waves;
        heightTR *= invWaves;
        saturate(heightTR, 0, 1);
        heightBL += waves;
        heightBL *= invWaves;
        saturate(heightBL, 0, 1);
        heightBR += waves;
        heightBR *= invWaves;
        saturate(heightBR, 0, 1);

        Color color, colorLand;
        colorLand = _colorSea;
        colorLand.SetA(0);

        // TL
        color = colorLand * heightTL + _colorSea * (1 - heightTL);
        pars.colorTL = PackedColor(color);
        // TR
        color = colorLand * heightTR + _colorSea * (1 - heightTR);
        pars.colorTR = PackedColor(color);
        // BL
        color = colorLand * heightBL + _colorSea * (1 - heightBL);
        pars.colorBL = PackedColor(color);
        // BR
        color = colorLand * heightBR + _colorSea * (1 - heightBR);
        pars.colorBR = PackedColor(color);
    }

    Rect2DPixel rect(x * _wScreen, y * _hScreen, xStep * _wScreen, yStep * _hScreen);
    pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
    pars.spec = NoZBuf | IsAlpha | IsAlphaFog | NoClamp;

    pars.SetU(0, 0);
    pars.SetV(0, 0);
    pars.vTL += 1;
    pars.uTR += 1;
    pars.vTR += 1;
    pars.uBR += 1;

    GLOB_ENGINE->Draw2D(pars, rect, _clipRect);
}

void CStaticMap::DrawScale(float x, float y, float xStep, float yStep, int i, int j, int iStep, int jStep)
{
    const float invMaxHeight = 1.0 / 700.0;

    float heightTL = GetHeightNT(j, i) * invMaxHeight;
    saturate(heightTL, 0, 1);
    float heightTR = GetHeightNT(j, i + iStep) * invMaxHeight;
    saturate(heightTR, 0, 1);
    float heightBL = GetHeightNT(j + jStep, i) * invMaxHeight;
    saturate(heightBL, 0, 1);
    float heightBR = GetHeightNT(j + jStep, i + iStep) * invMaxHeight;
    saturate(heightBR, 0, 1);

    Color color, colorL(0, 0, 0, 0), colorH(0, 0, 0, 1);
    Draw2DPars pars;

    // TL
    color = colorH * heightTL + colorL * (1 - heightTL);
    pars.colorTL = PackedColor(color);
    // TR
    color = colorH * heightTR + colorL * (1 - heightTR);
    pars.colorTR = PackedColor(color);
    // BL
    color = colorH * heightBL + colorL * (1 - heightBL);
    pars.colorBL = PackedColor(color);
    // BR
    color = colorH * heightBR + colorL * (1 - heightBR);
    pars.colorBR = PackedColor(color);

    pars.SetU(0, 0);
    pars.SetV(0, 0);
    pars.vTL += 1;
    pars.uTR += 1;
    pars.vTR += 1;
    pars.uBR += 1;

    Rect2DPixel rect(x * _wScreen, y * _hScreen, xStep * _wScreen, yStep * _hScreen);

    pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
    pars.spec = NoZBuf | IsAlpha | IsAlphaFog | NoClamp;
    GLOB_ENGINE->Draw2D(pars, rect, _clipRect);
}

void CStaticMap::DrawForests(int i, int j, float x, float y, float w, float h)
{
    if (!InRange(i, j - 1))
    {
        return;
    }

    const ObjectList& list = GLOB_LAND->GetObjects(j - 1, i);
    for (int o = 0; o < list.Size(); o++)
    {
        Object* obj = list[o];
        if (obj == nullptr)
        {
            continue;
        }
        if (obj->GetType() == Primary)
        {
            switch (obj->GetShape()->GetMapType())
            {
                case MapForestTriangle:
                {
                    const int n = 3;
                    Vertex2DPixel vs[n];
                    vs[0].u = 0;
                    vs[0].v = 0;
                    vs[0].color = _colorForest;
                    vs[1].u = 0;
                    vs[1].v = 0;
                    vs[1].color = _colorForest;
                    vs[2].u = 0;
                    vs[2].v = 0;
                    vs[2].color = _colorForest;

                    Vector3 dir = obj->Direction();
                    float angle = atan2(dir.X(), dir.Z());
                    switch (toInt(angle * 2.0 / H_PI))
                    {
                        case -1:
                            vs[0].x = x * _wScreen;
                            vs[0].y = (y + h) * _hScreen;
                            vs[1].x = x * _wScreen;
                            vs[1].y = y * _hScreen;
                            vs[2].x = (x + w) * _wScreen;
                            vs[2].y = (y + h) * _hScreen;
                            break;
                        case 0:
                            vs[0].x = x * _wScreen;
                            vs[0].y = y * _hScreen;
                            vs[1].x = (x + w) * _wScreen;
                            vs[1].y = y * _hScreen;
                            vs[2].x = x * _wScreen;
                            vs[2].y = (y + h) * _hScreen;
                            break;
                        case 1:
                            vs[0].x = x * _wScreen;
                            vs[0].y = y * _hScreen;
                            vs[1].x = (x + w) * _wScreen;
                            vs[1].y = y * _hScreen;
                            vs[2].x = (x + w) * _wScreen;
                            vs[2].y = (y + h) * _hScreen;
                            break;
                        case -2:
                        case 2:
                            vs[0].x = x * _wScreen;
                            vs[0].y = (y + h) * _hScreen;
                            vs[1].x = (x + w) * _wScreen;
                            vs[1].y = y * _hScreen;
                            vs[2].x = (x + w) * _wScreen;
                            vs[2].y = (y + h) * _hScreen;
                            break;
                    }
                    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
                    GLOB_ENGINE->DrawPoly(mip, vs, n, _clipRect);
                }
                    return;
                case MapForestSquare:
                {
                    Draw2DPars pars;
                    pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
                    pars.spec = NoZBuf | IsAlpha | ClampU | ClampV | IsAlphaFog;
                    pars.SetColor(_colorForest);
                    pars.SetU(0, 1);
                    pars.SetV(0, 1);
                    Rect2DPixel rect(x * _wScreen, y * _hScreen, w * _wScreen, h * _hScreen);
                    GLOB_ENGINE->Draw2D(pars, rect, _clipRect);
                }
                    return; // only one forest in square is enabled
            }
        }
    }
}

void CStaticMap::DrawObjects(int i, int j)
{
    if (!InRange(i, j - 1))
    {
        return;
    }

    const ObjectList& list = GLOB_LAND->GetObjects(j - 1, i);
    for (int o = 0; o < list.Size(); o++)
    {
        Object* obj = list[o];
        if (obj == nullptr)
        {
            continue;
        }
        if (obj->GetType() == Primary)
        {
            switch (obj->GetShape()->GetMapType())
            {
                case MapBuilding:
                case MapHouse:
                case MapFence:
                case MapWall:
                {
                    PackedColor color = obj->GetShape()->Color();
                    const Point3* minmax = obj->GetShape()->MinMax();
                    Point3 ptTL(minmax[0].X(), 0, minmax[0].Z());
                    Point3 ptTR(minmax[1].X(), 0, minmax[0].Z());
                    Point3 ptBL(minmax[0].X(), 0, minmax[1].Z());
                    Point3 ptBR(minmax[1].X(), 0, minmax[1].Z());
                    DrawCoord mapTL = WorldToScreen(obj->PositionModelToWorld(ptTL));
                    DrawCoord mapTR = WorldToScreen(obj->PositionModelToWorld(ptTR));
                    DrawCoord mapBL = WorldToScreen(obj->PositionModelToWorld(ptBL));
                    DrawCoord mapBR = WorldToScreen(obj->PositionModelToWorld(ptBR));

                    const int n = 4;
                    Vertex2DPixel vs[n];
                    // 0
                    vs[0].x = mapTL.x * _wScreen;
                    vs[0].y = mapTL.y * _hScreen;
                    vs[0].u = 0;
                    vs[0].v = 0;
                    vs[0].color = color;
                    // 1
                    vs[1].x = mapBL.x * _wScreen;
                    vs[1].y = mapBL.y * _hScreen;
                    vs[1].u = 0;
                    vs[1].v = 0;
                    vs[1].color = color;
                    // 2
                    vs[2].x = mapBR.x * _wScreen;
                    vs[2].y = mapBR.y * _hScreen;
                    vs[2].u = 0;
                    vs[2].v = 0;
                    vs[2].color = color;
                    // 3
                    vs[3].x = mapTR.x * _wScreen;
                    vs[3].y = mapTR.y * _hScreen;
                    vs[3].u = 0;
                    vs[3].v = 0;
                    vs[3].color = color;

                    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
                    GLOB_ENGINE->DrawPoly(mip, vs, n, _clipRect);
                }
                break;
                case MapTree:
                    DrawSign(_infoTree, obj->Position());
                    break;
                case MapSmallTree:
                    DrawSign(_infoSmallTree, obj->Position());
                    break;
                case MapBush:
                    DrawSign(_infoBush, obj->Position());
                    break;
                case MapChurch:
                    DrawSign(_infoChurch, obj->Position());
                    break;
                case MapChapel:
                    DrawSign(_infoChapel, obj->Position());
                    break;
                case MapCross:
                    DrawSign(_infoCross, obj->Position());
                    break;
                case MapRock:
                    DrawSign(_infoRock, obj->Position());
                    break;
                case MapBunker:
                    DrawSign(_infoBunker, obj->Position());
                    break;
                case MapFortress:
                    DrawSign(_infoFortress, obj->Position());
                    break;
                case MapFountain:
                    DrawSign(_infoFountain, obj->Position());
                    break;
                case MapViewTower:
                    DrawSign(_infoViewTower, obj->Position());
                    break;
                case MapLighthouse:
                    DrawSign(_infoLighthouse, obj->Position());
                    break;
                case MapQuay:
                    DrawSign(_infoQuay, obj->Position());
                    break;
                case MapFuelstation:
                    DrawSign(_infoFuelstation, obj->Position());
                    break;
                case MapHospital:
                    DrawSign(_infoHospital, obj->Position());
                    break;
                case MapBusStop:
                    DrawSign(_infoBusStop, obj->Position());
                    break;
            }

            if (_showIds)
            {
                float invLandRange = 1.0 / LandRange;
                float ptsLand = 800.0 * _invScaleX * invLandRange; // avoid dependece on video resolution
                float invPtsLand = 1.0 / ptsLand;

                int iStep = toIntCeil(ptsPerSquareCost * invPtsLand);
                float xStep = iStep * invLandRange * _invScaleX;
                if (xStep > 0.15)
                {
                    // draw object ID
                    DrawCoord pos = WorldToScreen(obj->Position());
                    const float size = 0.02;
                    float width = GEngine->GetTextWidthF(size, _fontGrid, "%d", obj->ID());
                    float x = pos.x - 0.5 * width;
                    float y = pos.y - 0.5 * size;
                    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
                    GEngine->Draw2D(mip, PackedWhite,
                                    Rect2DPixel(x * _wScreen, y * _hScreen, width * _wScreen, size * _hScreen),
                                    Rect2DPixel(_x * _wScreen, _y * _hScreen, _w * _wScreen, _h * _hScreen));
                    GEngine->DrawTextF(Point2DFloat(x, y), size, Rect2DFloat(_x, _y, _w, _h), _fontGrid, PackedBlack,
                                       "%d", obj->ID());
                }
            }
        }
    }
}

void CStaticMap::DrawForestBorders(int i, int j, float x, float y, float w, float h)
{
    if (!InRange(i, j - 1))
    {
        return;
    }

    const ObjectList& list = GLOB_LAND->GetObjects(j - 1, i);
    for (int o = 0; o < list.Size(); o++)
    {
        Object* obj = list[o];
        if (obj == nullptr)
        {
            continue;
        }
        if (obj->GetType() == Primary)
        {
            switch (obj->GetShape()->GetMapType())
            {
                case MapForestTriangle:
                {
                    Vector3 dir = obj->Direction();
                    float angle = atan2(dir.X(), dir.Z());
                    switch (toInt(angle * 2.0 / H_PI))
                    {
                        case -1:
                        case 1:
                            GLOB_ENGINE->DrawLine(
                                Line2DPixel(x * _wScreen, y * _hScreen, (x + w) * _wScreen, (y + h) * _hScreen),
                                _colorForestBorder, _colorForestBorder, _clipRect);
                            break;
                        case -2:
                        case 0:
                        case 2:
                            GLOB_ENGINE->DrawLine(
                                Line2DPixel(x * _wScreen, (y + h) * _hScreen, (x + w) * _wScreen, y * _hScreen),
                                _colorForestBorder, _colorForestBorder, _clipRect);
                            break;
                    }
                }
                break;
                case MapForestSquare:
                {
                    GeographyInfo geogr = GLOB_LAND->GetGeography(i, j);
                    if (!geogr.u.forestInner && !geogr.u.forestOuter)
                    {
                        GLOB_ENGINE->DrawLine(Line2DPixel(x * _wScreen, y * _hScreen, (x + w) * _wScreen, y * _hScreen),
                                              _colorForestBorder, _colorForestBorder, _clipRect);
                    }
                    geogr = GLOB_LAND->GetGeography(i, j - 2);
                    if (!geogr.u.forestInner && !geogr.u.forestOuter)
                    {
                        GLOB_ENGINE->DrawLine(
                            Line2DPixel(x * _wScreen, (y + h) * _hScreen, (x + w) * _wScreen, (y + h) * _hScreen),
                            _colorForestBorder, _colorForestBorder, _clipRect);
                    }
                    geogr = GLOB_LAND->GetGeography(i - 1, j - 1);
                    if (!geogr.u.forestInner && !geogr.u.forestOuter)
                    {
                        GLOB_ENGINE->DrawLine(Line2DPixel(x * _wScreen, y * _hScreen, x * _wScreen, (y + h) * _hScreen),
                                              _colorForestBorder, _colorForestBorder, _clipRect);
                    }
                    geogr = GLOB_LAND->GetGeography(i + 1, j - 1);
                    if (!geogr.u.forestInner && !geogr.u.forestOuter)
                    {
                        GLOB_ENGINE->DrawLine(
                            Line2DPixel((x + w) * _wScreen, y * _hScreen, (x + w) * _wScreen, (y + h) * _hScreen),
                            _colorForestBorder, _colorForestBorder, _clipRect);
                    }
                }
            }
        }
    }
}

void CStaticMap::DrawRoads(int i, int j)
{
    if (!InRange(i, j - 1))
    {
        return;
    }

    const ObjectList& list = GLOB_LAND->GetObjects(j - 1, i);
    for (int o = 0; o < list.Size(); o++)
    {
        Object* obj = list[o];
        if (obj == nullptr)
        {
            continue;
        }
        if (obj->GetType() == Network)
        {
            LODShape* lShape = obj->GetShape();
            Point3 ptTL = lShape->MemoryPoint("LB");
            Point3 ptTR = lShape->MemoryPoint("PB");
            Point3 ptBL = lShape->MemoryPoint("LE");
            Point3 ptBR = lShape->MemoryPoint("PE");

            if (!lShape->MemoryLevel())
            {
                // no memory  - syntetic points required
                Shape* geom = lShape->GeometryLevel();
                if (geom)
                {
                    Vector3Val min = geom->Min();
                    Vector3Val max = geom->Max();

                    ptTL = Vector3(min.X(), max.Y(), min.Z());
                    ptTR = Vector3(max.X(), max.Y(), min.Z());
                    ptBL = Vector3(min.X(), max.Y(), max.Z());
                    ptBR = Vector3(max.X(), max.Y(), max.Z());
                }
            }

            DrawCoord mapTL = WorldToScreen(obj->PositionModelToWorld(ptTL));
            DrawCoord mapTR = WorldToScreen(obj->PositionModelToWorld(ptTR));
            DrawCoord mapBL = WorldToScreen(obj->PositionModelToWorld(ptBL));
            DrawCoord mapBR = WorldToScreen(obj->PositionModelToWorld(ptBR));
            GLOB_ENGINE->DrawLine(
                Line2DPixel(mapTL.x * _wScreen, mapTL.y * _hScreen, mapBL.x * _wScreen, mapBL.y * _hScreen),
                _colorRoads, _colorRoads, _clipRect);
            GLOB_ENGINE->DrawLine(
                Line2DPixel(mapTR.x * _wScreen, mapTR.y * _hScreen, mapBR.x * _wScreen, mapBR.y * _hScreen),
                _colorRoads, _colorRoads, _clipRect);

            if (_showIds)
            {
                float invLandRange = 1.0 / LandRange;
                float ptsLand = 800.0 * _invScaleX * invLandRange; // avoid dependece on video resolution
                float invPtsLand = 1.0 / ptsLand;

                int iStep = toIntCeil(ptsPerSquareCost * invPtsLand);
                float xStep = iStep * invLandRange * _invScaleX;
                if (xStep > 0.15)
                {
                    // draw object ID
                    const float size = 0.02;
                    float x = (mapTL.x + mapBR.x) * 0.5;
                    float y = (mapTL.y + mapBR.y) * 0.5;
                    float width = GEngine->GetTextWidthF(size, _fontGrid, "%d", obj->ID());
                    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
                    GEngine->Draw2D(mip, PackedWhite,
                                    Rect2DPixel(x * _wScreen, y * _hScreen, width * _wScreen, size * _hScreen),
                                    Rect2DPixel(_x * _wScreen, _y * _hScreen, _w * _wScreen, _h * _hScreen));
                    GEngine->DrawTextF(Point2DFloat(x, y), size, Rect2DFloat(_x, _y, _w, _h), _fontGrid, PackedBlack,
                                       "%d", obj->ID());
                }
            }
        }
    }
}

void CStaticMap::DrawLines(DrawCoord* pt, float* height, float step, float minLevel, float maxLevel, PackedColor color)
{
    float invStep = 1.0 / step;

    int n0, n1, n2, nt;
    float xs, ys, xe, ye;
    // original (wrong):
    // draw triangle <0, 1, 2>
    // draw triangle <1, 2, 3>
    // correct:
    // draw triangle <0, 1, 3>
    // draw triangle <0, 2, 3>
    for (int t = 0; t < 2; t++)
    {
        // t = 0, 1
        // draw triangel <t, t+	1, t+2>
        n0 = 0;
        n1 = t + 1;
        n2 = 3;
        // sort vertices by height
        if (height[n0] > height[n1])
        { // swap n0, n1
            nt = n0;
            n0 = n1;
            n1 = nt;
        }
        if (height[n0] > height[n2])
        { // swap n0, n2
            nt = n0;
            n0 = n2;
            n2 = nt;
        }
        if (height[n1] > height[n2])
        { // swap n1, n2
            nt = n1;
            n1 = n2;
            n2 = nt;
        }

        float level = step * toIntCeil(height[n0] * invStep);
        saturateMax(level, minLevel);
        float toLevel = floatMin(height[n2], maxLevel);
        for (; level < toLevel; level += step)
        {
            // draw one line (at level <level>)
            float coef = (level - height[n0]) * (1.0 / (height[n2] - height[n0]));
            xe = pt[n0].x + coef * (pt[n2].x - pt[n0].x);
            ye = pt[n0].y + coef * (pt[n2].y - pt[n0].y);
            if (level == height[n1])
            {
                xs = pt[n1].x;
                ys = pt[n1].y;
            }
            else if (level < height[n1])
            {
                float coef = (level - height[n0]) * (1.0 / (height[n1] - height[n0]));
                xs = pt[n0].x + coef * (pt[n1].x - pt[n0].x);
                ys = pt[n0].y + coef * (pt[n1].y - pt[n0].y);
            }
            else
            {
                float coef = (level - height[n1]) * (1.0 / (height[n2] - height[n1]));
                xs = pt[n1].x + coef * (pt[n2].x - pt[n1].x);
                ys = pt[n1].y + coef * (pt[n2].y - pt[n1].y);
            }
            // draw line from <xs, ys> to <xe, ye>
            GLOB_ENGINE->DrawLine(Line2DPixel(xs * _wScreen, ys * _hScreen, xe * _wScreen, ye * _hScreen), color, color,
                                  _clipRect);
        }
    }
}

void CStaticMap::DrawCountlines(float x, float y, float xStep, float yStep, int i, int j, int iStep, int jStep,
                                float step, float maxLevel)
{
    // insert bounds into fields
    DrawCoord pt[4];
    float height[4];
    pt[0].x = x;
    pt[0].y = y;
    pt[1].x = x + xStep;
    pt[1].y = y;
    pt[2].x = x;
    pt[2].y = y + yStep;
    pt[3].x = x + xStep;
    pt[3].y = y + yStep;
    height[0] = GLOB_LAND->GetHeight(j, i);
    height[1] = GLOB_LAND->GetHeight(j, i + iStep);
    height[2] = GLOB_LAND->GetHeight(j + jStep, i);
    height[3] = GLOB_LAND->GetHeight(j + jStep, i + iStep);

    DrawLines(pt, height, step, -maxLevel, 0.5 * step, _colorCountlinesWater);
    DrawLines(pt, height, step, step, maxLevel, _colorCountlines);
}

void CStaticMap::DrawGrid()
{
    float sizeLand = LandGrid * LandRange;
    float invSizeLand = 1.0f / sizeLand;

    const GridInfo* info = GWorld->GetGridInfo(_scaleX);
    if (!info)
    {
        return;
    }

    float offsetX = GWorld->GetGridOffsetX();
    float offsetY = GWorld->GetGridOffsetY();

    float coefX = sizeLand * info->invStepX;
    float invCoefX = invSizeLand * info->stepX;

    float coefY = sizeLand * info->invStepY;
    float invCoefY = invSizeLand * info->stepY;

    int xFrom = TO_INT(coefX, (-_mapX * _scaleX * sizeLand - offsetX) * info->invStepX);
    int zFrom = TO_INT(coefY, (-_mapY * _scaleY * sizeLand - offsetY) * info->invStepY);
    int xTo = TO_INT(coefX, ((_w - _mapX) * _scaleX * sizeLand - offsetX) * info->invStepX);
    int zTo = TO_INT(coefY, ((_h - _mapY) * _scaleY * sizeLand - offsetY) * info->invStepY);

    float xBeg = _x + _mapX + (xFrom * info->stepX + offsetX) * _invScaleX * invSizeLand;
    float zBeg = _y + _mapY + (zFrom * info->stepY + offsetY) * _invScaleY * invSizeLand;
    float xFld = fabs(_invScaleX * invCoefX);
    float zFld = fabs(_invScaleY * invCoefY);

    float h = _sizeGrid;

    float zAct = zBeg;
    int zSign = coefY > 0 ? 1 : -1;
    for (int z = zFrom; zSign * (zTo - z) >= 0; z += zSign)
    {
        if (zAct >= _y && zAct <= _y + _h)
        {
            GLOB_ENGINE->DrawLine(Line2DPixel(_clipRect.x, zAct * _hScreen, _clipRect.x + _clipRect.w, zAct * _hScreen),
                                  _colorGridMap, _colorGridMap);
        }
        BString<32> buffer;
        GridFormat(buffer, info->formatY, zSign == 1 ? z : z - 1);
        float width = GEngine->GetTextWidth(_sizeGrid, _fontGrid, buffer);
        float left = _x;
        float right = _x + _w - width;
        float top = zAct + 0.5 * (zFld - h);
        GLOB_ENGINE->DrawText(Point2DFloat(left, top), _sizeGrid, Rect2DFloat(_x, _y, _w, _h), _fontGrid, _colorGrid,
                              buffer);
        GLOB_ENGINE->DrawText(Point2DFloat(right, top), _sizeGrid, Rect2DFloat(_x, _y, _w, _h), _fontGrid, _colorGrid,
                              buffer);
        zAct += zFld;
    }
    float xAct = xBeg;
    int xSign = coefX > 0 ? 1 : -1;
    for (int x = xFrom; xSign * (xTo - x) >= 0; x += xSign)
    {
        if (xAct >= _x && xAct <= _x + _w)
        {
            GLOB_ENGINE->DrawLine(Line2DPixel(xAct * _wScreen, _clipRect.y, xAct * _wScreen, _clipRect.y + _clipRect.h),
                                  _colorGridMap, _colorGridMap);
        }
        BString<32> buffer;
        GridFormat(buffer, info->formatX, xSign == 1 ? x : x - 1);

        float left = xAct + 0.5 * (xFld - GEngine->GetTextWidth(_sizeGrid, _fontGrid, buffer));
        float top = _y;
        float bottom = _y + _h - _sizeGrid;
        GLOB_ENGINE->DrawText(Point2DFloat(left, top), _sizeGrid, Rect2DFloat(_x, _y, _w, _h), _fontGrid, _colorGrid,
                              buffer);
        GLOB_ENGINE->DrawText(Point2DFloat(left, bottom), _sizeGrid, Rect2DFloat(_x, _y, _w, _h), _fontGrid, _colorGrid,
                              buffer);
        xAct += xFld;
    }
}

void CStaticMap::OnDraw(float alpha)
{
    Precalculate();
    unsigned key = _mouseKey;
    if (key == 0)
    {
        key = _moveKey;
    }
    if (key != 0)
    {
        float dt = Glob.uiTime - _moveLast;
        _moveLast = Glob.uiTime;
        if (Glob.uiTime - _moveStart < 0.5)
        {
            dt *= 0.5;
        }

        switch (key)
        {
            case SDLK_KP_PLUS:
                SetScale(exp(-dt) * GetScale());
                break;
            case SDLK_KP_MINUS:
                SetScale(exp(dt) * GetScale());
                break;
            case SDLK_KP_1:
                _mapX += dt;
                SaturateX(_mapX);
                _mapY -= dt;
                SaturateY(_mapY);
                break;
            case SDLK_KP_2:
                _mapY -= dt;
                SaturateY(_mapY);
                break;
            case SDLK_KP_3:
                _mapX -= dt;
                SaturateX(_mapX);
                _mapY -= dt;
                SaturateY(_mapY);
                break;
            case SDLK_KP_4:
                _mapX += dt;
                SaturateX(_mapX);
                break;
            case SDLK_KP_6:
                _mapX -= dt;
                SaturateX(_mapX);
                break;
            case SDLK_KP_7:
                _mapX += dt;
                SaturateX(_mapX);
                _mapY += dt;
                SaturateY(_mapY);
                break;
            case SDLK_KP_8:
                _mapY += dt;
                SaturateY(_mapY);
                break;
            case SDLK_KP_9:
                _mapX -= dt;
                SaturateX(_mapX);
                _mapY += dt;
                SaturateY(_mapY);
                break;
        }
    }
    else if (_interpolator)
    {
        float t = Glob.uiTime - _animationStart;
        Vector3 pos = (*_interpolator)(t).Position();
        _scaleX = exp(pos[1]);
        pos[1] = 0;
        saturate(_scaleX, _scaleMin, _scaleMax);
        Precalculate();
        Center(pos);
        _animationLast = Glob.uiTime;
        if (t > _interpolator->MaxArg())
        {
            ClearAnimation();
        }
    }

    DrawBackground();

#if _ENABLE_CHEATS
    if (_show)
#endif
        DrawGrid();

    DrawExt(alpha);

    if (_parent->IsTop() && !GWorld->GetCameraEffect())
    {
        RString cursorName = _parent->GetCursorName();
        if (stricmp(cursorName, "Arrow") != 0)
        {
            int wScreen = GLOB_ENGINE->Width2D();
            int hScreen = GLOB_ENGINE->Height2D();
            const float mouseH = 16.0 / 600;
            const float mouseW = 16.0 / 800;
            float mouseX = 0.5 + InputSubsystem::Instance().GetCursorX() * 0.5;
            float mouseY = 0.5 + InputSubsystem::Instance().GetCursorY() * 0.5;
            PackedColor color = _parent->GetCursorColor();
            color.SetA8(color.A8() / 2);
            float pt = mouseX - mouseW;
            if (pt > _x)
            {
                GLOB_ENGINE->DrawLine(
                    Line2DPixel(wScreen * _x, toInt(hScreen * mouseY), wScreen * pt, toInt(hScreen * mouseY)), color,
                    color);
            }
            pt = mouseX + mouseW;
            if (pt < _x + _w)
            {
                GLOB_ENGINE->DrawLine(
                    Line2DPixel(wScreen * pt, toInt(hScreen * mouseY), wScreen * (_x + _w), toInt(hScreen * mouseY)),
                    color, color);
            }
            pt = mouseY - mouseH;
            if (pt > _y)
            {
                GLOB_ENGINE->DrawLine(
                    Line2DPixel(toInt(wScreen * mouseX), hScreen * _y, toInt(wScreen * mouseX), hScreen * pt), color,
                    color);
            }
            pt = mouseY + mouseH;
            if (pt < _y + _h)
            {
                GLOB_ENGINE->DrawLine(
                    Line2DPixel(toInt(wScreen * mouseX), hScreen * pt, toInt(wScreen * mouseX), hScreen * (_y + _h)),
                    color, color);
            }
        }
    }
}

// mouse / keyboard events
bool CStaticMap::OnKeyUp(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    switch (nChar)
    {
        case SDLK_KP_PLUS:
        case SDLK_KP_MINUS:
        case SDLK_KP_1:
        case SDLK_KP_2:
        case SDLK_KP_3:
        case SDLK_KP_4:
            //	case SDLK_KP_5:
        case SDLK_KP_6:
        case SDLK_KP_7:
        case SDLK_KP_8:
        case SDLK_KP_9:
            _moveKey = 0;
            return true;
        default:
            return false;
    }
}

bool CStaticMap::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    switch (nChar)
    {
        case SDLK_KP_PLUS:
        case SDLK_KP_MINUS:
        case SDLK_KP_1:
        case SDLK_KP_2:
        case SDLK_KP_3:
        case SDLK_KP_4:
        case SDLK_KP_6:
        case SDLK_KP_7:
        case SDLK_KP_8:
        case SDLK_KP_9:
            ClearAnimation();
            if (_moveKey != nChar)
            {
                _moveKey = nChar;
                _moveStart = Glob.uiTime;
                _moveLast = Glob.uiTime;
            }
            return true;
        case SDLK_KP_MULTIPLY:
            _moveKey = 0;
            _mouseKey = 0;
            float time;
            if (_scaleDefault > _scaleX)
            {
                time = log(_scaleDefault / _scaleX);
            }
            else
            {
                time = log(_scaleX / _scaleDefault);
            }
            ClearAnimation();
            AddAnimationPhase(time, _scaleDefault, GetCenter());
            CreateInterpolator();
            return true;
        default:
            return false;
    }
}

void ExportWMF(const char* name, bool grid);

void CStaticMap::ProcessCheats()
{
    auto& input = InputSubsystem::Instance();
    if (input.CheatActivated() == CheatExportMap)
    {
        char buffer[256];
        ::sprintf(buffer, "C:\\%s.emf", Glob.header.worldname);
        ExportWMF(buffer, true);
        input.CheatServed();
    }
#if _ENABLE_CHEATS
    if (input.GetCheat2ToDo(SDL_SCANCODE_D))
    {
        char buffer[256];
        sprintf(buffer, "C:\\%s.emf", Glob.header.worldname);
        ExportWMF(buffer, true);
    }
    if (input.GetCheat2ToDo(SDL_SCANCODE_F))
    {
        char buffer[256];
        sprintf(buffer, "C:\\%s.emf", Glob.header.worldname);
        ExportWMF(buffer, false);
    }
    if (input.GetCheat1ToDo(SDL_SCANCODE_N))
    {
        _show = !_show;
    }
    if (input.GetCheat1ToDo(SDL_SCANCODE_W))
    {
        switch (_infoClick._type)
        {
            case signUnit:
                if (_infoClick._unit)
                {
                    EntityAI* veh = _infoClick._unit->GetVehicle();
                    if (veh)
                    {
                        GLOB_WORLD->SwitchCameraTo(veh, GLOB_WORLD->GetCameraType());
                        GWorld->UI()->ResetHUD();
                    }
                }
                break;
            case signVehicle:
            case signStatic:
                if (_infoClick._id != nullptr)
                {
                    GLOB_WORLD->SwitchCameraTo(_infoClick._id, GLOB_WORLD->GetCameraType());
                    GWorld->UI()->ResetHUD();
                }
                break;
        }
    }
#endif
}

void CStaticMap::OnRButtonDown(float x, float y)
{
    _mouseWorld = ScreenToWorld(DrawCoord(x, y));
    _moving = true;
}

void CStaticMap::OnRButtonUp(float x, float y)
{
    _moving = false;
}

void CStaticMap::OnMouseHold(float x, float y, bool active)
{
    if (!_moving && IsInside(x, y))
    {
        _infoMove = FindSign(x, y);
    }
}

void CStaticMap::OnMouseMove(float x, float y, bool active)
{
    if (_moving)
    {
        DrawCoord mouseMap = WorldToScreen(_mouseWorld);
        _mapX += x - mouseMap.x;
        SaturateX(_mapX);
        _mapY += y - mouseMap.y;
        SaturateY(_mapY);
    }
    else
    {
        OnMouseHold(x, y);
        return;
    }
}

void CStaticMap::OnMouseZChanged(float dz)
{
    if (dz == 0)
    {
        return;
    }
    _moveKey = 0;
    float scale = exp(0.1 * dz) * _scaleX;
    saturate(scale, _scaleMin, _scaleMax);
    SetScale(scale);
}

void CStaticMap::ClearAnimation()
{
    _animation.Resize(0);
    _animMatrices.Free();
    _animTimes.Free();
    _interpolator = nullptr;
}

void CStaticMap::AddAnimationPhase(float dt, float scale, Vector3Par pos)
{
    int i = _animation.Add();
    MapAnimationPhase& phase = _animation[i];
    if (i == 0)
    {
        _animationStart = Glob.uiTime;
        _animationLast = Glob.uiTime;
        phase.time = Glob.uiTime + dt;
    }
    else
    {
        phase.time = _animation[i - 1].time + dt;
    }
    phase.scale = scale;
    phase.pos = pos;
}

void CStaticMap::CreateInterpolator()
{
    int n = _animation.Size();

    _animTimes.Realloc(n + 1);
    _animMatrices.Realloc(n + 1);
    Vector3 pos;

    pos = GetCenter();
    pos[1] = log(_scaleX);
    _animTimes[0] = 0;
    _animMatrices[0].SetPosition(pos);
    _animMatrices[0].SetOrientation(M3Identity);
    for (int i = 0; i < n; i++)
    {
        pos = _animation[i].pos;
        pos[1] = log(_animation[i].scale);
        _animTimes[i + 1] = _animation[i].time - _animationStart;
        _animMatrices[i + 1].SetPosition(pos);
        _animMatrices[i + 1].SetOrientation(M3Identity);
    }
    _interpolator = new InterpolatorLinear(_animMatrices, _animTimes, n + 1);
}

Vector3 CStaticMap::GetCenter()
{
    DrawCoord pt(_center.x + _x, _center.y + _y);
    return ScreenToWorld(pt);
}

void CStaticMap::Precalculate()
{
    // precalculates for increasing drawing speed
    _wScreen = GLOB_ENGINE->Width2D();
    _hScreen = GLOB_ENGINE->Height2D();

    float coef = _hScreen / _wScreen;
    _scaleY = coef * _scaleX;
    _invScaleX = 1.0 / _scaleX;
    _invScaleY = 1.0 / _scaleY;

    _clipRect.x = _x * _wScreen;
    _clipRect.y = _y * _hScreen;
    _clipRect.w = _w * _wScreen;
    _clipRect.h = _h * _hScreen;
}

void CStaticMap::SaturateX(float& x)
{
    if (_invScaleX < _w)
    {
        x = 0.5 * (_w - _invScaleX);
    }
    else
    {
        saturate(x, _w - _invScaleX, 0);
    }
}

void CStaticMap::SaturateY(float& y)
{
    if (_invScaleY < _h)
    {
        y = 0.5 * (_h - _invScaleY);
    }
    else
    {
        saturate(y, _h - _invScaleY, 0);
    }
}

void CStaticMap::ScrollX(float dif)
{
    _mapX += dif;
    SaturateX(_mapX);
}

void CStaticMap::ScrollY(float dif)
{
    _mapY += dif;
    SaturateY(_mapY);
}

void CStaticMap::SetVisibleRect(float x, float y, float w, float h)
{
    _center.x = x - _x + 0.5 * w; // offset + 0.5 * width
    _center.y = y - _y + 0.5 * h; // offset + 0.5 * height
}

void CStaticMap::Center(Vector3Val pt)
{
    float invSizeLand = 1.0 / (LandGrid * LandRange);
    float xMap = pt.X() * invSizeLand * _invScaleX;
    float yMap = (1.0 - pt.Z() * invSizeLand) * _invScaleY;
    _mapX = _center.x - xMap;
    _mapY = _center.y - yMap;
    SaturateX(_mapX);
    SaturateY(_mapY);
}

void CStaticMap::SetScale(float scale)
{
    if (scale < 0)
    {
        scale = _scaleDefault;
    }
    saturate(scale, _scaleMin, _scaleMax);

    float mouseX = 0.5 + InputSubsystem::Instance().GetCursorX() * 0.5;
    saturate(mouseX, _x, _x + _w);
    float mouseY = 0.5 + InputSubsystem::Instance().GetCursorY() * 0.5;
    saturate(mouseY, _y, _y + _h);

    DrawCoord ptMap(mouseX, mouseY);
    DrawCoord offset(mouseX - _x, mouseY - _y);
    Vector3 pt = ScreenToWorld(ptMap);

    _scaleX = scale;
    Precalculate();

    float invSizeLand = 1.0 / (LandGrid * LandRange);
    float xMap = pt.X() * invSizeLand * _invScaleX;
    float yMap = (1.0 - pt.Z() * invSizeLand) * _invScaleY;
    _mapX = offset.x - xMap;
    _mapY = offset.y - yMap;
    SaturateX(_mapX);
    SaturateY(_mapY);
}

// Main (ingame) map

CStatic* CreateStaticMapMain(ControlsContainer* parent, int idc, const ParamEntry& cls)
{
    CStaticMap* map = new CStaticMapMain(parent, idc, cls);
    map->SetVisibleRect(map->X(), map->Y(), map->W(), map->H());
    map->Center();
    return map;
}

} // namespace Poseidon
