
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/World/Entities/Vehicles/Vehicle.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

using namespace Poseidon;
namespace Poseidon
{

static const Color BackgroundColor(0.55, 0.6, 0.8);
static const Color FullSunColor(0.85, 0.75, 0.4);

static const Color SunsetColor(0.8, 0.30, 0.23); // color of sun light before sunset
static const Color MoonColor(HBlack);

static const Color SunsetSkyColor(1.0, 0.2, 0.1);        // color of sky around sun before sunset
static const Color SunsetObjectColor(1.0, 0.8, 0.1);     // color of sun before sunset
static const Color SunsetHaloObjectColor(0.9, 0.4, 0.2); // color of sun helo before sunset
static const Color SunObjectColor(1, 1, 0.9);            // color of full moon
static const Color SunHaloObjectColor(0.9, 0.9, 0.7);    // color of full sun halo

static const Color MoonObjectColor(0.9, 0.9, 1.0, 0.7);      // color of full moon
static const Color MoonHaloObjectColor(0.9, 0.9, 1.0, 0.05); // color of full moon halo
static const Color MoonsetObjectColor(0.9, 0.75, 0.4);       // color of setting moon
static const Color MoonsetHaloObjectColor(0.9, 0.5, 0.2);    // color of setting moon halo

LightSun::LightSun()
{
    _direction = VForward;
    _shadowDirection = VForward;
    _sunDirection = VForward;
    _moonDirection = VForward;
    _moonDirectionUp = VUp;
    _starsOrientation = M3Identity;

    _moonPhase = 0;
    _nightEffect = 0;
    _starsVisible = 0;

    _colorFull = FullSunColor;
    _diffuse = _colorFull; // default - full sun
    _diffuse.SaturateMinMax();
    _ambient = BackgroundColor;
    _sunColor = _colorFull;
    _sunObjectColor = ::SunObjectColor;
    _sunHaloObjectColor = ::SunHaloObjectColor;
    _moonObjectColor = ::MoonObjectColor;
    _moonHaloObjectColor = ::MoonHaloObjectColor;
    _skyColor = BackgroundColor + _colorFull * 0.5f;
    _sunSkyColor = _colorFull;
    _ambientPrecalc = _ambient;
    _diffusePrecalc = _diffuse;
}

Color LightSun::AmbientResult() const
{
    return _ambientPrecalc;
}

Color LightSun::FullResult(float diffuse) const
{
    return _diffusePrecalc * diffuse + _ambientPrecalc;
}

#define MIN_BACK_INTENSITY 0.05
#define MIN_SKY_INTENSITY 0.03

const float sunSunset = 20 * (H_PI / 180); // sunset object limit
const float begSunset = 25 * (H_PI / 180); // sunset light
const float endSunset = 10 * (H_PI / 180); // UHEL_VYCHODU sunrise angle range
const float nightAngle = 5 * (H_PI / 180); // UHEL_NEF night effect angle

const float sinSunSunset = sin(sunSunset), invSinSunSunset = 1 / sinSunSunset;
const float sinBegSunset = sin(begSunset), invSinBegSunset = 1 / sinBegSunset;
const float sinEndSunset = sin(endSunset), invSinEndSunset = 1 / sinEndSunset;
const float sinNightAngle = sin(nightAngle), invSinNightAngle = 1 / sinNightAngle;

const float sunsetRamp = 0.7; // SUNSET_RAMP

inline float ConvertSunAngle(float sunAngle)
{
    return AngleDifference(H_PI, sunAngle);
}

// create a coordinate system simulating Earth and Moon movement relative to sun
void LightSun::Recalculate(World* world)
{
    if (!world)
    {
        world = GWorld;
    }
    float latitudeCoord = world ? world->GetLatitude() : -40 * H_PI / 180;
    static const Matrix3 moonOrbitAngle(MRotationZ, 5 * H_PI / 180);
    static const Matrix3 earthAxis = Matrix3(MRotationX, 23 * H_PI / 180);
    Matrix3 latitude(MRotationX, latitudeCoord); // -40 - Croatia, -90 - north pole

    const float initMoonOnOrbitPos = 0.5;
    const float day = 1.0 / 365;
    const float lunarMonth = 28 * day;

    float timeInYear = Glob.clock.GetTimeInYear();
    float timeOfDay = Glob.clock.GetTimeOfDay();
    float moonOnOrbitPos = initMoonOnOrbitPos + timeInYear * (1.0 / lunarMonth);
    Matrix3Val moonOnOrbit = moonOrbitAngle * Matrix3(MRotationY, moonOnOrbitPos * (H_PI * 2));
    Matrix3Val earthOnOrbit = Matrix3(MRotationY, timeInYear * (H_PI * 2));
    // note - midnight is on the point furthest from the sun
    Matrix3Val midnightToCurrent = Matrix3(MRotationY, timeOfDay * (H_PI * 2));
    // calculate sun and moon position relative to current postion
    Matrix3Val cameraToCosmos = earthAxis * midnightToCurrent * earthOnOrbit * latitude;
    Matrix3Val cameraToStars = midnightToCurrent * earthOnOrbit * latitude;
    Matrix3Val cosmosToCamera = cameraToCosmos.InverseRotation();
    Matrix3Val starsToCamera = cameraToStars.InverseRotation();
    // use rotation of PI/2 to achieve this
    static const Matrix3 normalDirection(MRotationX, -H_PI / 2);
    Matrix3Val convert = normalDirection * cosmosToCamera;
    _direction = convert * earthOnOrbit.Direction();
    _moonDirection = convert * moonOnOrbit.Direction();

    _starsOrientation = normalDirection * starsToCamera;

    // reverse N-S, W-W
    _direction[0] = -_direction[0];
    _direction[2] = -_direction[2];
    _moonDirection[0] = -_moonDirection[0];
    _moonDirection[2] = -_moonDirection[2];
    _starsOrientation(0, 0) = -_starsOrientation(0, 0);
    _starsOrientation(2, 0) = -_starsOrientation(2, 0);
    _starsOrientation(0, 1) = -_starsOrientation(0, 1);
    _starsOrientation(2, 1) = -_starsOrientation(2, 1);
    _starsOrientation(0, 2) = -_starsOrientation(0, 2);
    _starsOrientation(2, 2) = -_starsOrientation(2, 2);

    // calculate _moonDirectionUp so that moon is always facing the sun
    _moonDirectionUp = _moonDirection - _direction;
    // both _direction and _moonDirection are normalized
    // moon phase is determined by position of moon relative to sun
    float cosMoonPhase = -(_direction * _moonDirection);
    float moonHaloIntensity = (cosMoonPhase + 1) * 0.5;
    float moonIntensity = floatMin(cosMoonPhase + 1, 1);
    // cos==0 -> full moon, cos==-1 -> dark moon
    _moonPhase = 0.5 + acos(cosMoonPhase) / (2 * H_PI); // D moon

    float sinSun = -_direction.Y();
    float absSinSun = fabs(sinSun);

    if (sinSun < 0)
    {
        // night, early morning or late evening
        if (absSinSun > sinEndSunset)
        {
            _sunSkyColor = Color(HBlack);
            _colorFull = MoonColor;
        }
        else
        {
            // sunset or sunrise
            // interpolate between moon and sunset colors
            float sunset = 1 - absSinSun * invSinEndSunset;
            _colorFull = MoonColor + (SunsetColor - MoonColor) * sunset;
            _sunSkyColor = SunsetSkyColor * sunset;
        }
        if (absSinSun > sinEndSunset)
        {
            _starsVisible = 1;
        }
        else
        {
            _starsVisible = absSinSun * invSinEndSunset;
        }
        _nightEffect = 1;
        _sunColor = SunsetColor;
        _sunObjectColor = SunsetObjectColor;
        _sunHaloObjectColor = SunsetHaloObjectColor;
    }
    else
    {
        // day
        if (absSinSun > sinBegSunset)
        {
            _colorFull = FullSunColor;
            _sunSkyColor = _colorFull;
        }
        else
        {
            // evening or morning
            float sunset = 1 - absSinSun * invSinBegSunset;
            _colorFull = FullSunColor + (SunsetColor - FullSunColor) * sunset;
            _sunSkyColor = _colorFull + (SunsetSkyColor - _colorFull) * sunset;
        }

        if (absSinSun > sinSunSunset)
        {
            _sunObjectColor = ::SunObjectColor;
            _sunHaloObjectColor = ::SunHaloObjectColor;
        }
        else
        {
            float sunset = 1 - absSinSun * invSinSunSunset;
            _sunHaloObjectColor = ::SunHaloObjectColor + (::SunsetHaloObjectColor - ::SunHaloObjectColor) * sunset;
            _sunObjectColor = ::SunObjectColor + (::SunsetObjectColor - ::SunObjectColor) * sunset;
        }

        if (absSinSun > sinNightAngle)
        {
            _nightEffect = 0;
        }
        else
        {
            _nightEffect = 1 - absSinSun * invSinNightAngle;
        }
        _starsVisible = 0;

        _sunColor = _colorFull;
    }

    float sinMoon = -_moonDirection.Y();
    if (sinMoon < 0) // moon below horizont
    {
        _moonObjectColor = ::MoonsetObjectColor;
        _moonHaloObjectColor = ::MoonsetHaloObjectColor;
    }
    else
    {
        if (sinMoon > sinSunSunset)
        {
            _moonObjectColor = ::MoonObjectColor;
            _moonHaloObjectColor = ::MoonHaloObjectColor;
        }
        else
        {
            float sunset = 1 - sinMoon * invSinSunSunset;
            _moonObjectColor = ::MoonHaloObjectColor + (::MoonsetHaloObjectColor - ::MoonHaloObjectColor) * sunset;
            _moonHaloObjectColor = ::MoonObjectColor + (::MoonsetObjectColor - ::MoonObjectColor) * sunset;
        }
    }
    _moonObjectColor.SetA(::MoonObjectColor.A() * moonIntensity);
    _moonHaloObjectColor.SetA(::MoonHaloObjectColor.A() * moonHaloIntensity);

    float ambientI = sinSun * 1.5;
    float backgroundI = sinSun * 2.0;
    saturate(ambientI, MIN_BACK_INTENSITY, 1);
    saturate(backgroundI, MIN_SKY_INTENSITY, 1);
    _ambient = BackgroundColor * ambientI;
    _skyColor = BackgroundColor * backgroundI + _colorFull * 0.5;

    // enable lights when night effects are on

    // consider how much sun is visible
    _sunColor.SaturateMinMax();
    _skyColor.SaturateMinMax();
    _sunSkyColor.SaturateMinMax();
    _ambient.SaturateMinMax();
    _direction.Normalize();

    _shadowDirection = _direction;
    _sunDirection = _direction;

    const float maxShadowDer = -0.2;
    if (_shadowDirection[1] > maxShadowDer)
    {
        _shadowDirection[1] = maxShadowDer;
        _shadowDirection.Normalize();
    }
}

// additional lights
Light::Light()
{
    _on = true;
}

Light::~Light()
{
    // unregister light with D3D
}

int Light::Compare(const Light& with, const LightContext& context) const
{
    // compare distance relative to given context
    Vector3Val camPos = context.position;
    Vector3 relThisPos = camPos - Position();
    Vector3 relWithPos = camPos - with.Position();
    float distThis2 = relThisPos.SquareSize();
    float distWith2 = relWithPos.SquareSize();
    float diff = distWith2 * SortBrightness() - distThis2 * with.SortBrightness();
    if (diff > 0)
    {
        return +1;
    }
    if (diff < 0)
    {
        return -1;
    }
    return 0;
}

int Light::Compare(const Light& with) const
{
    // compare distance relative to global camera
    Vector3Val camPos = GLOB_SCENE->GetCamera()->Position();
    Vector3 relThisPos = camPos - Position();
    Vector3 relWithPos = camPos - with.Position();
    float distThis2 = relThisPos.SquareSize();
    float distWith2 = relWithPos.SquareSize();
    float diff = distWith2 * SortBrightness() - distThis2 * with.SortBrightness();
    if (diff > 0)
    {
        return +1;
    }
    if (diff < 0)
    {
        return -1;
    }
    return 0;
}

float Light::SortBrightness() const
{
    return Brightness();
}

bool Light::Visible(const Object* obj) const
{
    // default implementation: check only distance
    Vector3Val position = obj->Position() - Position();
    return Brightness() > 0.1 * SquareDistance(position);
}

LightPositioned::LightPositioned() = default;

void LightPositioned::Prepare(const Matrix4& worldToModel)
{
    _modelPos = worldToModel.FastTransform(Position());
    _modelDir = worldToModel.Rotate(Direction());
    _modelDir.Normalize();
}

LightPositionedColored::LightPositionedColored() : _ambient(HWhite), _diffuse(HWhite) {}

LightPositionedColored::LightPositionedColored(ColorVal diffuse, ColorVal ambient)
    : _diffuse(diffuse), _ambient(ambient)
{
}

void LightSun::SetMaterial(const TLMaterial& mat)
{
    _ambientPrecalc = _ambient * mat.ambient + _diffuse * mat.forcedDiffuse;
    _diffusePrecalc = _diffuse * mat.diffuse;
}

void LightSun::GetDescription(LightDescription& desc) const
{
    // used for HW T&L
    desc.type = LTDirectional;
    desc.dir = Direction();
    desc.pos = VZero;       // ignored for directional
    desc.startAtten = 1e10; // ignored for directional
    desc.ambient = Ambient();
    desc.diffuse = Diffuse();
    desc.phi = 0;
    desc.theta = 0;
}

void LightPositionedColored::SetMaterial(const TLMaterial& mat)
{
    _ambientPrecalc = _ambient * mat.ambient + _diffuse * mat.forcedDiffuse;
    _diffusePrecalc = _diffuse * mat.diffuse;
}

LightPoint::LightPoint(ColorVal color, ColorVal ambient) : base(color, ambient), _startAtten(50) {}

LightPoint::LightPoint() = default;

float LightPoint::FlareIntensity(Vector3Par camPos, Vector3Par camDir) const
{
    Vector3Val relPos = camPos - Position();
    // calculate surface lighting factor
    float startAtten = Square(_startAtten);
    float endAtten = startAtten * 100;
    float size2 = relPos.SquareSize();
    if (size2 >= endAtten)
    {
        return 0;
    }
    float invSize = InvSqrt(size2);
    float atten = 1;
    float cosFi = -camDir * relPos * invSize;
    if (size2 >= startAtten)
    {
        atten = startAtten * invSize * invSize;
    }
    return atten * cosFi;
}

Color LightPoint::Apply(Vector3Par point, Vector3Par normal)
{
    // normal and point is given in model space
    // calculate distance from pointlight
    Vector3Val relPos = point - _modelPos;
    // calculate surface lighting factor
    float startAtten = Square(_startAtten);
    float endAtten = startAtten * 100;
    float size2 = relPos.SquareSize();
    if (size2 >= endAtten)
    {
        return Color(HBlack);
    }
    float invSize = InvSqrt(size2);
    float atten = 1;
    if (size2 >= startAtten)
    {
        atten = startAtten * invSize * invSize;
    }
    // not cosFi is actualy cosFi*size
    float cosFi = relPos * normal;
    if (cosFi > 0)
    {
        cosFi *= invSize;
        return (_diffusePrecalc * cosFi + _ambientPrecalc) * atten;
    }
    else
    {
        return _ambientPrecalc * atten;
    }
}

void LightPoint::GetDescription(LightDescription& desc) const
{
    // used for HW T&L
    desc.type = LTPoint;
    desc.dir = Direction();
    desc.pos = Position();         // ignored for directional
    desc.startAtten = _startAtten; // ignored for directional
    desc.ambient = Ambient();
    desc.diffuse = GetDiffuse();
    desc.phi = 0;
    desc.theta = 0;
}

LightReflector::LightReflector(LODShapeWithShadow* shape, ColorVal color, ColorVal ambient, float angle, float size)
    : _shape(shape), base(color, ambient), _angle(angle), _startAtten(200), _size(size)
{
}

bool LightReflector::Visible(const Object* obj) const
{
    // default implementation: check only distance
    float dist2 = obj->Position().Distance2(Position());
    return Brightness() > 0.1 * dist2;
}

#define MIN_INSIDE 0.97814760073 // 12 degree
#define MAX_INSIDE 0.99026806874 // 8 degree

float LightReflector::FlareIntensity(Vector3Par camPos, Vector3Par camDir) const
{
    // flare only if camera is in light cone
    // check distance
    // check "inside cone" value
    Vector3Val relPos = camPos - Position();
    float inside = relPos * Direction();
    if (inside <= 0)
    {
        return 0;
    }
    float size2 = relPos.SquareSize();

    float startAtten = Square(_startAtten);
    float endAtten = startAtten * 100;
    if (size2 >= endAtten)
    {
        return 0;
    }

    float minInside2 = size2 * (MIN_INSIDE * MIN_INSIDE);
    float inside2 = inside * inside;
    if (inside2 < minInside2)
    {
        return 0;
    }

    float atten = 1;
    float invSize = InvSqrt(size2);
    if (size2 >= startAtten)
    {
        atten = startAtten * invSize * invSize;
    }
    float cosFi = -camDir * relPos * invSize;
    if (cosFi > 0)
    {
        float maxInside2 = size2 * (MAX_INSIDE * MAX_INSIDE);
        // note: distance normalization is VERY slow
        // it takes usually one division and one square root
        // we need some approximation for this
        cosFi *= invSize;
        if (inside2 > maxInside2)
        {
            inside = 1;
        }
        else
        {
            inside = (inside2 - minInside2) * (1 / (maxInside2 - minInside2));
        }
        atten *= inside;
        return atten * cosFi;
    }
    else
    {
        return 0;
    }
}

Color LightReflector::Apply(Vector3Par point, Vector3Par normal)
{
    // calculate distance from pointlight
    Vector3Val relPos = point - _modelPos;
    // calculate surface lighting factor
    float startAtten = Square(_startAtten);
    float endAtten = startAtten * 100;
    float size2 = relPos.SquareSize();
    if (size2 >= endAtten)
    {
        return Color(HBlack);
    }
    // determine if the point is inside the light cone
    // cos(coneangle)=relPosNorm*direction
    // if point is inside, then cos(coneangle)>cos(_angle)
    float inside = relPos * _modelDir;
    if (inside <= 0)
    {
        return Color(HBlack);
    }
    float minInside2 = size2 * (MIN_INSIDE * MIN_INSIDE);
    float inside2 = inside * inside;
    if (inside2 < minInside2)
    {
        return Color(HBlack);
    }
    // not cosFi is actualy cosFi*size
    float cosFi = relPos * normal;
    float atten = 1;
    float invSize = InvSqrt(size2);
    if (size2 >= startAtten)
    {
        atten = startAtten * invSize * invSize;
    }
    if (cosFi > 0)
    {
        float maxInside2 = size2 * (MAX_INSIDE * MAX_INSIDE);
        // note: distance normalization is VERY slow
        // it takes usually one division and one square root
        // we need some approximation for this
        cosFi *= invSize;
        if (inside2 > maxInside2)
        {
            inside = 1;
        }
        else
        {
            inside = (inside2 - minInside2) * (1 / (maxInside2 - minInside2));
        }
        atten *= inside;
        return (_ambientPrecalc + _diffusePrecalc * cosFi) * atten;
    }
    else
    {
        return _ambientPrecalc * atten;
    }
}

void LightReflector::GetDescription(LightDescription& desc) const
{
    // used for HW T&L
    desc.type = LTSpotLight;
    desc.dir = Direction();
    desc.pos = Position();         // ignored for directional
    desc.startAtten = _startAtten; // ignored for directional
    desc.ambient = Ambient();
    desc.diffuse = GetDiffuse();
    desc.phi = H_PI * 0.20;
    desc.theta = H_PI * 0.12;
}

float LightPoint::Brightness() const
{
    return _diffuse.Brightness() * _startAtten * _startAtten;
}

float LightPoint::SortBrightness() const
{
    // point lights have bigger chance of affecting result
    // increase their brightness for sorting purposes
    return Brightness() * 5;
}

Color LightPoint::GetObjectColor() const
{
    return _diffuse;
}

void LightPoint::ToDraw(ClipFlags clipFlags, bool dimmed)
{
    // note: most point lights are invisible
}

void LightPoint::Load(const ParamEntry& cls)
{
    _diffuse = GetColor(cls >> "color");
    _ambient = GetColor(cls >> "ambient");
    float brightness = cls >> "brightness";
    SetBrightness(brightness);
}

float LightReflector::Brightness() const
{
    return _diffuse.Brightness() * _startAtten * _startAtten;
}

void LightReflector::SetBrightness(float coef)
{
    _startAtten = 200 * InvSqrt(_diffuse.Brightness() / coef);
}

Color LightReflector::GetObjectColor() const
{
    return _diffuse;
}

void LightReflector::ToDraw(ClipFlags clipFlags, bool dimmed)
{
    // reflector: draw volumetrical light object
    if (GScene->MainLight()->NightEffect() < 0.01)
    {
        return;
    }
    if (dimmed)
    {
        return;
    }
    Color c = GetObjectColor();
    float invSize = InvSqrt(c.R() * c.R() + c.G() * c.G() + c.B() * c.B());
    c = c * invSize;
    GScene->DrawVolumeLight(_shape, PackedColor(c), *this, _size);
}

LightPointVisible::LightPointVisible() = default;

LightPointVisible::LightPointVisible(LODShapeWithShadow* shape, ColorVal color, ColorVal ambient, float size)
    : LightPoint(color, ambient), _shape(shape), _size(size)
{
}

void LightPointVisible::ToDraw(ClipFlags clipFlags, bool dimmed)
{
    // reflector: draw volumetrical light object
    if (GScene->MainLight()->NightEffect() < 0.01)
    {
        return;
    }
    if (dimmed)
    {
        return;
    }
    Color c = GetObjectColor();
    GScene->DrawVolumeLight(_shape, PackedColor(c), *this, _size);
}

void LightPointVisible::Load(const ParamEntry& cls)
{
    LightPoint::Load(cls);
    RString shapeName = GetShapeName(cls >> "shape");
    _shape = Shapes.New(shapeName, false, false);
    if (!_shape)
        LOG_ERROR(Graphics, "LightPointVisible: shape='{}' failed to load", static_cast<const char*>(shapeName));
    _size = cls >> "size";
}

LightPointOnVehicle::LightPointOnVehicle(LODShapeWithShadow* shape, ColorVal color, ColorVal ambient, Object* vehicle,
                                         Vector3Par position, float size)
    : LightPointVisible(shape, color, ambient, size), AttachedOnVehicle(vehicle, position, Vector3(0, 0, 1))
{
}

LightPointOnVehicle::LightPointOnVehicle(Object* vehicle, Vector3Par position)
    : AttachedOnVehicle(vehicle, position, Vector3(0, 0, 1))
{
}

void LightPointOnVehicle::Load(const ParamEntry& cls)
{
    LightPointVisible::Load(cls);
    Object* obj = AttachedOn();
    LODShape* shape = obj ? obj->GetShape() : nullptr;
    if (shape)
    {
        RString pos = cls >> "position";
        Vector3Val position = shape->MemoryPoint(pos);
        SetAttachedPos(position, Vector3(0, 0, 1));
    }
}
} // namespace Poseidon
