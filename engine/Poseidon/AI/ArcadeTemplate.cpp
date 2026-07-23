
#include <Poseidon/AI/ArcadeTemplate.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Random/randomGen.hpp>

#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/World/World.hpp>

#include <Poseidon/Network/Network.hpp>

#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <stdio.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/RStringArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/platform.hpp>

#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/UI/Locale/Stringtable/CodepageTranscode.hpp>
#include <Poseidon/Game/Chat.hpp>

using namespace Poseidon;
namespace Poseidon
{
using Foundation::EnumName;

static RString DecodeMissionUserText(RString text)
{
    return DecodeLegacyTextToRString(text, GLanguage);
}

template <>
const EnumName* Foundation::GetEnumNames(TitleType dummy)
{
    static const EnumName TitleTypeNames[] = {EnumName(TitleNone, "NONE"), EnumName(TitleObject, "OBJECT"),
                                              EnumName(TitleResource, "RES"), EnumName(TitleText, "TEXT"), EnumName()};
    return TitleTypeNames;
}

template <>
const EnumName* Foundation::GetEnumNames(ArcadeWaypointType dummy)
{
    static const EnumName ArcadeWaypointTypeNames[] = {EnumName(ACUNDEFINED, "UNDEF"),
                                                       EnumName(ACMOVE, "MOVE"),
                                                       EnumName(ACDESTROY, "DESTROY"),
                                                       EnumName(ACGETIN, "GETIN"),
                                                       EnumName(ACSEEKANDDESTROY, "SAD"),
                                                       EnumName(ACJOIN, "JOIN"),
                                                       EnumName(ACLEADER, "LEADER"),
                                                       EnumName(ACGETOUT, "GETOUT"),
                                                       EnumName(ACCYCLE, "CYCLE"),
                                                       EnumName(ACLOAD, "LOAD"),
                                                       EnumName(ACUNLOAD, "UNLOAD"),
                                                       EnumName(ACTRANSPORTUNLOAD, "TR UNLOAD"),
                                                       EnumName(ACHOLD, "HOLD"),
                                                       EnumName(ACSENTRY, "SENTRY"),
                                                       EnumName(ACGUARD, "GUARD"),
                                                       EnumName(ACTALK, "TALK"),
                                                       EnumName(ACSCRIPTED, "SCRIPTED"),
                                                       EnumName(ACSUPPORT, "SUPPORT"),
                                                       EnumName(ACAND, "AND"),
                                                       EnumName(ACOR, "OR"),
                                                       EnumName()};
    return ArcadeWaypointTypeNames;
}

template <>
const EnumName* Foundation::GetEnumNames(SpeedMode dummy)
{
    static const EnumName SpeedModeNames[] = {EnumName(SpeedUnchanged, "UNCHANGED"), EnumName(SpeedLimited, "LIMITED"),
                                              EnumName(SpeedNormal, "NORMAL"), EnumName(SpeedFull, "FULL"), EnumName()};
    return SpeedModeNames;
}

template <>
const EnumName* Foundation::GetEnumNames(CombatMode dummy)
{
    static const EnumName CombatModeNames[] = {EnumName(CMUnchanged, "UNCHANGED"),
                                               EnumName(CMCareless, "CARELESS"),
                                               EnumName(CMSafe, "SAFE"),
                                               EnumName(CMAware, "AWARE"),
                                               EnumName(CMCombat, "COMBAT"),
                                               EnumName(CMStealth, "STEALTH"),
                                               EnumName()};
    return CombatModeNames;
}

template <>
const EnumName* Foundation::GetEnumNames(AWPShow dummy)
{
    static const EnumName AWPShowNames[] = {EnumName(ShowNever, "NEVER"), EnumName(ShowEasy, "EASY"),
                                            EnumName(ShowAlways, "ALWAYS"), EnumName()};
    return AWPShowNames;
}

template <>
const EnumName* Foundation::GetEnumNames(ArcadeUnitSpecial dummy)
{
    static const EnumName ArcadeUnitSpecialNames[] = {EnumName(ASpNone, "NONE"), EnumName(ASpCargo, "CARGO"),
                                                      EnumName(ASpFlying, "FLY"), EnumName(ASpForm, "FORM"),
                                                      EnumName()};
    return ArcadeUnitSpecialNames;
}

template <>
const EnumName* Foundation::GetEnumNames(ArcadeUnitAge dummy)
{
    static const EnumName ArcadeUnitAgeNames[] = {
        EnumName(AAActual, "ACTUAL"),  EnumName(AA5Min, "5 MIN"),      EnumName(AA10Min, "10 MIN"),
        EnumName(AA15Min, "15 MIN"),   EnumName(AA30Min, "30 MIN"),    EnumName(AA60Min, "60 MIN"),
        EnumName(AA120Min, "120 MIN"), EnumName(AAUnknown, "UNKNOWN"), EnumName()};
    return ArcadeUnitAgeNames;
}

template <>
const EnumName* Foundation::GetEnumNames(ArcadeUnitPlayer dummy)
{
    static const EnumName ArcadeUnitPlayerNames[] = {
        EnumName(APNonplayable, "NONPLAY"), EnumName(APPlayerCommander, "PLAYER COMMANDER"),
        EnumName(APPlayerDriver, "PLAYER DRIVER"), EnumName(APPlayerGunner, "PLAYER GUNNER"),
        EnumName(APPlayableC, "PLAY C"), EnumName(APPlayableD, "PLAY D"), EnumName(APPlayableG, "PLAY G"),
        EnumName(APPlayableCD, "PLAY CD"), EnumName(APPlayableCG, "PLAY CG"), EnumName(APPlayableDG, "PLAY DG"),
        EnumName(APPlayableCDG, "PLAY CDG"),
        // old versions
        EnumName(APPlayableCDG, "PLAY"), EnumName(APPlayerCommander, "P1 COMMANDER"),
        EnumName(APPlayerDriver, "P1 DRIVER"), EnumName(APPlayerGunner, "P1 GUNNER"),
        EnumName(APPlayerCommander, "P2 COMMANDER"), EnumName(APPlayerDriver, "P2 DRIVER"),
        EnumName(APPlayerGunner, "P2 GUNNER"), EnumName(APNonplayable, "-1"), EnumName(APPlayableCDG, "0"),
        EnumName(APPlayerCommander, "1"), EnumName(APPlayerCommander, "2"), EnumName(APNonplayable, "-1.000000"),
        EnumName(APPlayableCDG, "0.000000"), EnumName(APPlayerCommander, "1.000000"),
        EnumName(APPlayerCommander, "2.000000"), EnumName()};
    return ArcadeUnitPlayerNames;
}

template <>
const EnumName* Foundation::GetEnumNames(LockState dummy)
{
    static const EnumName LockStateNames[] = {EnumName(LSUnlocked, "UNLOCKED"), EnumName(LSDefault, "DEFAULT"),
                                              EnumName(LSLocked, "LOCKED"), EnumName()};
    return LockStateNames;
}

template <>
const EnumName* Foundation::GetEnumNames(ArcadeSensorActivation dummy)
{
    static const EnumName ArcadeSensorActivationNames[] = {EnumName(ASANone, "NONE"),
                                                           EnumName(ASAEast, "EAST"),
                                                           EnumName(ASAWest, "WEST"),
                                                           EnumName(ASAGuerrila, "GUER"),
                                                           EnumName(ASACivilian, "CIV"),
                                                           EnumName(ASALogic, "LOGIC"),
                                                           EnumName(ASAAnybody, "ANY"),
                                                           EnumName(ASAAlpha, "ALPHA"),
                                                           EnumName(ASABravo, "BRAVO"),
                                                           EnumName(ASACharlie, "CHARLIE"),
                                                           EnumName(ASADelta, "DELTA"),
                                                           EnumName(ASAEcho, "ECHO"),
                                                           EnumName(ASAFoxtrot, "FOXTROT"),
                                                           EnumName(ASAGolf, "GOLF"),
                                                           EnumName(ASAHotel, "HOTEL"),
                                                           EnumName(ASAIndia, "INDIA"),
                                                           EnumName(ASAJuliet, "JULIET"),
                                                           EnumName(ASAStatic, "STATIC"),
                                                           EnumName(ASAVehicle, "VEHICLE"),
                                                           EnumName(ASAGroup, "GROUP"),
                                                           EnumName(ASALeader, "LEADER"),
                                                           EnumName(ASAMember, "MEMBER"),
                                                           EnumName()};
    return ArcadeSensorActivationNames;
}

template <>
const EnumName* Foundation::GetEnumNames(ArcadeSensorActivationType dummy)
{
    static const EnumName ArcadeSensorActivationTypeNames[] = {EnumName(ASATPresent, "PRESENT"),
                                                               EnumName(ASATNotPresent, "NOT PRESENT"),
                                                               EnumName(ASATWestDetected, "WEST D"),
                                                               EnumName(ASATEastDetected, "EAST D"),
                                                               EnumName(ASATGuerrilaDetected, "GUER D"),
                                                               EnumName(ASATCiviliansDetected, "CIV D"),
                                                               EnumName()};
    return ArcadeSensorActivationTypeNames;
}

template <>
const EnumName* Foundation::GetEnumNames(ArcadeSensorType dummy)
{
    static const EnumName ArcadeSensorTypeNames[] = {
        EnumName(ASTNone, "NONE"),          EnumName(ASTEastGuarded, "EAST G"),
        EnumName(ASTWestGuarded, "WEST G"), EnumName(ASTGuerrilaGuarded, "GUER G"),
        EnumName(ASTSwitch, "SWITCH"),      EnumName(ASTEnd1, "END1"),
        EnumName(ASTEnd2, "END2"),          EnumName(ASTEnd3, "END3"),
        EnumName(ASTEnd4, "END4"),          EnumName(ASTEnd5, "END5"),
        EnumName(ASTEnd6, "END6"),          EnumName(ASTLoose, "LOOSE"),
        EnumName(ASTEnd1, "WIN"),           EnumName()};
    return ArcadeSensorTypeNames;
}

template <>
const EnumName* Foundation::GetEnumNames(MarkerType dummy)
{
    static const EnumName MarkerTypeNames[] = {EnumName(MTIcon, "ICON"), EnumName(MTRectangle, "RECTANGLE"),
                                               EnumName(MTEllipse, "ELLIPSE"), EnumName()};
    return MarkerTypeNames;
}

const float MinAbility = 0.2; // private
const float MinRank = RankPrivate;
const float MaxAbility = 1;
const float MaxRank = RankColonel;

float RankToSkill(int rank)
{
    float factor = (MaxAbility - MinAbility) / (MaxRank - MinRank);
    return (rank - MinRank) * factor + MinAbility;
}

ArcadeUnitInfo::ArcadeUnitInfo()
{
    Init();
}

ArcadeUnitInfo::ArcadeUnitInfo(const ArcadeUnitInfo& src)
{
    presence = src.presence;
    presenceCondition = src.presenceCondition;
    position = src.position;
    placement = src.placement;
    azimut = src.azimut;
    special = src.special;
    age = src.age;
    id = src.id;
    side = src.side;
    vehicle = src.vehicle;
    icon = src.icon;
    size = src.size;
    player = src.player;
    leader = src.leader;
    lock = src.lock;
    rank = src.rank;
    skill = src.skill;
    health = src.health;
    fuel = src.fuel;
    ammo = src.ammo;
    name = src.name;
    markers = src.markers;
    selected = src.selected;
    init = src.init;
}

void ArcadeUnitInfo::Init()
{
    presence = 1.0;
    presenceCondition = "true";
    position = VZero;
    placement = 0;
    azimut = 0;
    special = ASpForm;
    age = AAUnknown;
    id = 0;
    side = TWest;
    vehicle = "";
    type = nullptr;
    icon = nullptr;
    size = 0;
    player = APNonplayable;
    leader = false;
    lock = LSDefault;
    rank = RankPrivate;
    skill = (MaxAbility + MinAbility) * 0.5f;
    health = 1.0;
    fuel = 1.0;
    ammo = 1.0;
    name = "";
    markers.Clear();
    selected = false;
    init = "";
}

void ArcadeUnitInfo::AddOffset(Vector3Par offset)
{
    position += offset;
    position[1] = GLOB_LAND->RoadSurfaceYAboveWater(position[0], position[2]);
}

void ArcadeUnitInfo::Rotate(Vector3Par center, float angle, bool sel)
{
    if (sel && !selected)
    {
        return;
    }

    // rotation
    azimut += (180.0 / H_PI) * angle;

    Vector3 dir = position - center;
    Matrix3 rot(MRotationY, -angle);
    dir = rot * dir;

    position = center + dir;
    position[1] = GLandscape->RoadSurfaceYAboveWater(position[0], position[2]);
}

void ArcadeUnitInfo::CalculateCenter(Vector3& sum, int& count, bool sel)
{
    if (sel && !selected)
    {
        return;
    }

    sum += position;
    count++;
}

void ArcadeUnitInfo::RequiredAddons(FindArrayRStringCI& addOns)
{
    const ParamEntry* patches = Pars.FindEntry("CfgPatches");
    if (!patches)
    {
        return;
    }
    for (int i = 0; i < patches->GetEntryCount(); i++)
    {
        const ParamEntry& patch = patches->GetEntry(i);
        for (int j = 0; j < (patch >> "units").GetSize(); j++)
        {
            RStringB patchVehicle = (patch >> "units")[j];
            if (stricmp(patchVehicle, vehicle) == 0)
            {
                addOns.AddUnique(patch.GetName());
                goto Break;
            }
        }
    }
Break:
    // more robust check - check owner of given unit type
    const ParamEntry* entry = (Pars >> "CfgVehicles").FindEntry(vehicle);
    if (entry)
    {
        const RStringB& owner = entry->GetOwner();
        if (owner.GetLength() > 0)
        {
            addOns.AddUnique(owner);
        }
    }
}

LSError ArcadeUnitInfo::Serialize(ParamArchive& ar)
{
    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        Init();
    }

    PARAM_CHECK(ar.Serialize("presence", presence, 1, 1.0))
    PARAM_CHECK(ar.Serialize("presenceCondition", presenceCondition, 1, "true"))
    PARAM_CHECK(ar.Serialize("position", position, 1))
    PARAM_CHECK(ar.Serialize("placement", placement, 1, 0))
    PARAM_CHECK(ar.Serialize("azimut", azimut, 1, 0))

    PARAM_CHECK(ar.SerializeEnum("special", special, 1, ASpForm))
    PARAM_CHECK(ar.SerializeEnum("age", age, 1, AAUnknown))
    PARAM_CHECK(ar.Serialize("id", id, 1))
    PARAM_CHECK(ar.SerializeEnum("side", side, 1))

    PARAM_CHECK(ar.Serialize("vehicle", vehicle, 1))
    PARAM_CHECK(ar.SerializeEnum("player", player, 1, APNonplayable))
    PARAM_CHECK(ar.Serialize("leader", leader, 1, 0))
    if (ar.IsSaving() || ar.GetArVersion() >= 11)
        PARAM_CHECK(ar.SerializeEnum("lock", lock, 1, (LockState)LSDefault))
    else
    {
        bool locked;
        PARAM_CHECK(ar.Serialize("locked", locked, 7, false))
        lock = locked ? LSLocked : LSDefault;
    }
    PARAM_CHECK(ar.SerializeEnum("rank", rank, 1, RankPrivate))
    PARAM_CHECK(ar.Serialize("skill", skill, 1, -1.0))
    PARAM_CHECK(ar.Serialize("health", health, 1, 1.0))
    PARAM_CHECK(ar.Serialize("fuel", fuel, 1, 1.0))
    PARAM_CHECK(ar.Serialize("ammo", ammo, 1, 1.0))
    PARAM_CHECK(ar.Serialize("text", name, 7, ""))
    PARAM_CHECK(ar.SerializeArray("markers", markers, 1))
    PARAM_CHECK(ar.Serialize("init", init, 7, ""))

    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        RString iconName = Pars >> "CfgVehicles" >> vehicle >> "icon";
        icon = GlobLoadTexture(GetPictureName(iconName));
        size = Pars >> "CfgVehicles" >> vehicle >> "mapSize";

        ATSParams* params = (ATSParams*)ar.GetParams();
        AI_ERROR(params);
        AI_ERROR(id >= 0);
        saturateMax(params->nextVehId, id + 1);

        if (skill < 0)
        {
            skill = RankToSkill(rank);
        }
    }

    return LSOK;
}

ArcadeSensorInfo::ArcadeSensorInfo()
{
    Init();
}

ArcadeSensorInfo::ArcadeSensorInfo(const ArcadeSensorInfo& src)
{
    position = src.position;
    a = src.a;
    b = src.b;
    angle = src.angle;
    rectangular = src.rectangular;
    activationBy = src.activationBy;
    activationType = src.activationType;
    repeating = src.repeating;
    timeoutMin = src.timeoutMin;
    timeoutMid = src.timeoutMid;
    timeoutMax = src.timeoutMax;
    interruptable = src.interruptable;
    type = src.type;
    object = src.object;
    age = src.age;
    idStatic = src.idStatic;
    idVehicle = src.idVehicle;
    text = src.text;
    name = src.name;
    expCond = src.expCond;
    expActiv = src.expActiv;
    expDesactiv = src.expDesactiv;
    effects = src.effects;
    synchronizations = src.synchronizations;
    selected = src.selected;
}

void ArcadeSensorInfo::Init()
{
    position = VZero;
    a = 50.0;
    b = 50.0;
    angle = 0;
    rectangular = false;
    activationBy = ASANone;
    activationType = ASATPresent;
    repeating = false;
    timeoutMin = 0;
    timeoutMid = 0;
    timeoutMax = 0;
    interruptable = false;
    type = ASTNone;
    object = "EmptyDetector";
    age = AAUnknown;
    idStatic = -1;
    idVehicle = -1;
    text = "";
    name = "";
    expCond = "this";
    expActiv = "";
    expDesactiv = "";
    effects.Init();
    synchronizations.Clear();
    selected = false;
}

void ArcadeSensorInfo::AddOffset(Vector3Par offset)
{
    position += offset;
    position[1] = GLOB_LAND->RoadSurfaceYAboveWater(position[0], position[2]);
}

void ArcadeSensorInfo::Rotate(Vector3Par center, float alpha, bool sel)
{
    if (sel && !selected)
    {
        return;
    }

    // rotation
    angle += (180.0 / H_PI) * alpha;

    Vector3 dir = position - center;
    Matrix3 rot(MRotationY, -alpha);
    dir = rot * dir;

    position = center + dir;
    position[1] = GLandscape->RoadSurfaceYAboveWater(position[0], position[2]);
}

void ArcadeSensorInfo::CalculateCenter(Vector3& sum, int& count, bool sel)
{
    if (sel && !selected)
    {
        return;
    }

    sum += position;
    count++;
}

LSError ArcadeSensorInfo::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("position", position, 1))
    PARAM_CHECK(ar.Serialize("a", a, 1, 50.0))
    PARAM_CHECK(ar.Serialize("b", b, 1, 50.0))
    PARAM_CHECK(ar.Serialize("angle", angle, 1, 0))
    PARAM_CHECK(ar.Serialize("rectangular", rectangular, 7, false))

    PARAM_CHECK(ar.SerializeEnum("activationBy", activationBy, 1, ASANone))
    PARAM_CHECK(ar.SerializeEnum("activationType", activationType, 1, ASATPresent))
    PARAM_CHECK(ar.Serialize("repeating", repeating, 1, 0))
    PARAM_CHECK(ar.Serialize("timeoutMin", timeoutMin, 1, 0))
    PARAM_CHECK(ar.Serialize("timeoutMid", timeoutMid, 1, 0))
    PARAM_CHECK(ar.Serialize("timeoutMax", timeoutMax, 1, 0))
    PARAM_CHECK(ar.Serialize("interruptable", interruptable, 1, 0))
    PARAM_CHECK(ar.SerializeEnum("type", type, 1, ASTNone))
    PARAM_CHECK(ar.Serialize("object", object, 1, "EmptyDetector"))
    PARAM_CHECK(ar.SerializeEnum("age", age, 1))

    PARAM_CHECK(ar.Serialize("idStatic", idStatic, 1, -1))
    PARAM_CHECK(ar.Serialize("idVehicle", idVehicle, 1, -1))

    PARAM_CHECK(ar.Serialize("text", text, 3, ""))
    PARAM_CHECK(ar.Serialize("name", name, 7, ""))

    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        text = DecodeMissionUserText(text);
    }

    // if value not present, use default - possible in all versions
    PARAM_CHECK(ar.Serialize("expCond", expCond, 1, "this"))
    PARAM_CHECK(ar.Serialize("expActiv", expActiv, 1, ""))
    PARAM_CHECK(ar.Serialize("expDesactiv", expDesactiv, 1, ""))

    PARAM_CHECK(ar.Serialize("Effects", effects, 1))

    PARAM_CHECK(ar.SerializeArray("synchronizations", synchronizations, 1))

    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        ATSParams* params = (ATSParams*)ar.GetParams();
        AI_ERROR(params);
        for (int i = 0; i < synchronizations.Size(); i++)
        {
            int& sync = synchronizations[i];
            AI_ERROR(sync >= 0);
            saturateMax(params->nextSyncId, sync + 1);
        }
    }

    return LSOK;
}

ArcadeMarkerInfo::ArcadeMarkerInfo()
{
    Init();
}

ArcadeMarkerInfo::ArcadeMarkerInfo(const ArcadeMarkerInfo& src)
{
    position = src.position;
    name = src.name;
    text = src.text;
    markerType = src.markerType;
    type = src.type;
    colorName = src.colorName;
    color = src.color;
    fillName = src.fillName;
    fill = src.fill;
    icon = src.icon;
    size = src.size;
    a = src.a;
    b = src.b;
    angle = src.angle;
    selected = src.selected;
}

void ArcadeMarkerInfo::Init()
{
    position = VZero;
    name = "";
    text = "";
    markerType = MTIcon;
    type = "";
    colorName = "Default";
    color = PackedBlack;
    fillName = "Solid";
    fill = nullptr;
    icon = nullptr;
    size = 24;
    a = 1;
    b = 1;
    angle = 0;
    selected = false;
}

void ArcadeMarkerInfo::AddOffset(Vector3Par offset)
{
    position += offset;
    position[1] = GLOB_LAND->RoadSurfaceYAboveWater(position[0], position[2]);
}

void ArcadeMarkerInfo::Rotate(Vector3Par center, float alpha, bool sel)
{
    if (sel && !selected)
    {
        return;
    }

    // rotation
    angle += (180.0 / H_PI) * alpha;

    Vector3 dir = position - center;
    Matrix3 rot(MRotationY, -alpha);
    dir = rot * dir;

    position = center + dir;
    position[1] = GLandscape->RoadSurfaceYAboveWater(position[0], position[2]);
}

void ArcadeMarkerInfo::CalculateCenter(Vector3& sum, int& count, bool sel)
{
    if (sel && !selected)
    {
        return;
    }

    sum += position;
    count++;
}

LSError ArcadeMarkerInfo::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("position", position, 1))
    PARAM_CHECK(ar.Serialize("name", name, 1))
    PARAM_CHECK(ar.Serialize("text", text, 1, ""))
    PARAM_CHECK(ar.SerializeEnum("markerType", markerType, 1, MTIcon))
    PARAM_CHECK(ar.Serialize("type", type, 1))
    PARAM_CHECK(ar.Serialize("colorName", colorName, 1, "Default"))
    PARAM_CHECK(ar.Serialize("fillName", fillName, 1, "Solid"))
    PARAM_CHECK(ar.Serialize("a", a, 1, 1.0f))
    PARAM_CHECK(ar.Serialize("b", b, 1, 1.0f))
    PARAM_CHECK(ar.Serialize("angle", angle, 1, 0.0f))
    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        text = DecodeMissionUserText(text);
        OnTypeChanged();
        OnColorChanged();
        OnFillChanged();
    }

    return LSOK;
}

class IndicesMarker
{
  public:
    int position;
    int name;
    int text;
    int markerType;
    int type;
    int colorName;
    int fillName;
    int a;
    int b;
    int angle;

    IndicesMarker();
    void Scan(NetworkMessageFormatBase* format);
};

IndicesMarker::IndicesMarker()
{
    position = -1;
    name = -1;
    text = -1;
    markerType = -1;
    type = -1;
    colorName = -1;
    fillName = -1;
    a = -1;
    b = -1;
    angle = -1;
}

void IndicesMarker::Scan(NetworkMessageFormatBase* format)
{
    SCAN(position)
    SCAN(name) SCAN(text) SCAN(markerType) SCAN(type) SCAN(colorName) SCAN(fillName) SCAN(a) SCAN(b) SCAN(angle)
}

} // namespace Poseidon
IndicesMarker* GetIndicesMarker()
{
    using namespace Poseidon;
    return new IndicesMarker();
}
void DeleteIndicesMarker(IndicesMarker* marker)
{
    using namespace Poseidon;
    delete marker;
}
namespace Poseidon
{

} // namespace Poseidon
void ScanIndicesMarker(IndicesMarker* marker, NetworkMessageFormatBase* format)
{
    using namespace Poseidon;
    marker->Scan(format);
}
namespace Poseidon
{

void ArcadeMarkerInfo::CreateFormat(NetworkMessageFormat& format)
{
    format.Add("position", NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Marker position"));
    format.Add("name", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Marker (unique) name"));
    format.Add("text", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Marker title"));
    format.Add("markerType", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, MTIcon),
               DOC_MSG("Marker type (icon, rectangle, ellipse)"));
    format.Add("type", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Marker icon"));
    format.Add("colorName", NDTString, NCTNone, DEFVALUE(RString, "Default"), DOC_MSG("Marker color name"));
    format.Add("fillName", NDTString, NCTNone, DEFVALUE(RString, "Solid"), DOC_MSG("Marker fill name"));
    format.Add("a", NDTFloat, NCTNone, DEFVALUE(float, 1.0f), DOC_MSG("Width"));
    format.Add("b", NDTFloat, NCTNone, DEFVALUE(float, 1.0f), DOC_MSG("Height"));
    format.Add("angle", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Rotation"));
}

TMError ArcadeMarkerInfo::TransferMsg(NetworkMessageContext& ctx, IndicesMarker* indices)
{
    TMCHECK(ctx.IdxTransfer(indices->position, position))
    TMCHECK(ctx.IdxTransfer(indices->name, name))
    TMCHECK(ctx.IdxTransfer(indices->text, text))
    TMCHECK(ctx.IdxTransfer(indices->markerType, (int&)markerType))
    TMCHECK(ctx.IdxTransfer(indices->type, type))
    TMCHECK(ctx.IdxTransfer(indices->colorName, colorName))
    TMCHECK(ctx.IdxTransfer(indices->fillName, fillName))
    TMCHECK(ctx.IdxTransfer(indices->a, a))
    TMCHECK(ctx.IdxTransfer(indices->b, b))
    TMCHECK(ctx.IdxTransfer(indices->angle, angle))
    if (!ctx.IsSending())
    {
        OnTypeChanged();
        OnColorChanged();
        OnFillChanged();
    }
    return TMOK;
}

void ArcadeMarkerInfo::OnColorChanged()
{
    if (markerType == MTIcon && stricmp(colorName, "Default") == 0)
    {
        const ParamEntry& cls = Pars >> "CfgMarkers" >> type;
        color = GetPackedColor(cls >> "color");
    }
    else
    {
        const ParamEntry& cls = Pars >> "CfgMarkerColors" >> colorName;
        color = GetPackedColor(cls >> "color");
    }
}

void ArcadeMarkerInfo::OnFillChanged()
{
    const ParamEntry& cls = Pars >> "CfgMarkerBrushes" >> fillName;
    RString brush = cls >> "texture";
    if (brush.GetLength() == 0)
    {
        fill = nullptr;
    }
    else
    {
        fill = GlobLoadTexture(GetPictureName(brush));
    }
}

void ArcadeMarkerInfo::OnTypeChanged()
{
    if (markerType == MTIcon)
    {
        const ParamEntry& cls = Pars >> "CfgMarkers" >> type;
        icon = GlobLoadTexture(GetPictureName(cls >> "icon"));
        size = cls >> "size";
    }
}

ArcadeEffects::ArcadeEffects()
{
    Init();
}

ArcadeEffects::ArcadeEffects(const ArcadeEffects& src)
{
    condition = src.condition;
    cameraEffect = src.cameraEffect;
    cameraPosition = src.cameraPosition;
    sound = src.sound;
    voice = src.voice;
    soundEnv = src.soundEnv;
    soundDet = src.soundDet;
    track = src.track;
    titleType = src.titleType;
    titleEffect = src.titleEffect;
    title = src.title;
}

void ArcadeEffects::Init()
{
    condition = "true";
    cameraEffect = "";
    cameraPosition = CamEffectBack;
    sound = "$NONE$";
    voice = "";
    soundEnv = "";
    soundDet = "";
    track = "$NONE$";
    titleType = TitleNone;
    titleEffect = TitPlain;
    title = "";
}

LSError ArcadeEffects::Serialize(ParamArchive& ar)
{
    if (ar.IsSaving() || ar.GetArVersion() >= 9)
        PARAM_CHECK(ar.Serialize("condition", condition, 9, "true"))
    else
    {
        bool playerOnly;
        PARAM_CHECK(ar.Serialize("playerOnly", playerOnly, 1, false))
        if (playerOnly)
        {
            condition = "thisList";
        }
        else
        {
            condition = "true";
        }
    }

    PARAM_CHECK(ar.Serialize("cameraEffect", cameraEffect, 1, ""))
    PARAM_CHECK(ar.SerializeEnum("cameraPosition", cameraPosition, 1, (CamEffectPosition)CamEffectBack))

    PARAM_CHECK(ar.Serialize("sound", sound, 1, "$NONE$"))
    PARAM_CHECK(ar.Serialize("voice", voice, 5, ""))
    PARAM_CHECK(ar.Serialize("soundEnv", soundEnv, 5, ""))
    PARAM_CHECK(ar.Serialize("soundDet", soundDet, 5, ""))
    PARAM_CHECK(ar.Serialize("track", track, 1, "$NONE$"))

    PARAM_CHECK(ar.SerializeEnum("titleType", titleType, 1, TitleNone))
    PARAM_CHECK(ar.SerializeEnum("titleEffect", titleEffect, 1, (TitEffectName)TitPlain))
    PARAM_CHECK(ar.Serialize("title", title, 1, ""))

    return LSOK;
}

LSError ArcadeEffects::WorldSerialize(ParamArchive& ar)
{
    if (ar.IsSaving() || ar.GetArVersion() >= 5)
        PARAM_CHECK(ar.Serialize("condition", condition, 5, "true"))
    else
    {
        bool playerOnly;
        PARAM_CHECK(ar.Serialize("playerOnly", playerOnly, 1, false))
        if (playerOnly)
        {
            condition = "thisList";
        }
        else
        {
            condition = "true";
        }
    }

    PARAM_CHECK(ar.Serialize("cameraEffect", cameraEffect, 1, ""))
    PARAM_CHECK(ar.SerializeEnum("cameraPosition", cameraPosition, 1, (CamEffectPosition)CamEffectBack))

    PARAM_CHECK(ar.Serialize("sound", sound, 1, "$NONE$"))
    PARAM_CHECK(ar.Serialize("voice", voice, 1, ""))
    PARAM_CHECK(ar.Serialize("soundEnv", soundEnv, 1, ""))
    PARAM_CHECK(ar.Serialize("soundDet", soundDet, 1, ""))
    PARAM_CHECK(ar.Serialize("track", track, 1, "$NONE$"))

    PARAM_CHECK(ar.SerializeEnum("titleType", titleType, 1, TitleNone))
    PARAM_CHECK(ar.SerializeEnum("titleEffect", titleEffect, 1, (TitEffectName)TitPlain))
    PARAM_CHECK(ar.Serialize("title", title, 1, ""))

    return LSOK;
}

class IndicesEffects
{
  public:
    int condition;
    int cameraEffect;
    int cameraPosition;
    int sound;
    int voice;
    int soundEnv;
    int soundDet;
    int track;
    int titleType;
    int titleEffect;
    int title;

    IndicesEffects();
    void Scan(NetworkMessageFormatBase* format);
};

IndicesEffects::IndicesEffects()
{
    condition = -1;
    cameraEffect = -1;
    cameraPosition = -1;
    sound = -1;
    voice = -1;
    soundEnv = -1;
    soundDet = -1;
    track = -1;
    titleType = -1;
    titleEffect = -1;
    title = -1;
}

void IndicesEffects::Scan(NetworkMessageFormatBase* format)
{
    SCAN(condition)
    SCAN(cameraEffect)
    SCAN(cameraPosition)
    SCAN(sound) SCAN(voice) SCAN(soundEnv) SCAN(soundDet) SCAN(track) SCAN(titleType) SCAN(titleEffect) SCAN(title)
}

} // namespace Poseidon
IndicesEffects* GetIndicesEffects()
{
    using namespace Poseidon;
    return new IndicesEffects();
}
void DeleteIndicesEffects(IndicesEffects* effects)
{
    using namespace Poseidon;
    delete effects;
}
namespace Poseidon
{

} // namespace Poseidon
void ScanIndicesEffects(IndicesEffects* effects, NetworkMessageFormatBase* format)
{
    using namespace Poseidon;
    effects->Scan(format);
}
namespace Poseidon
{

void ArcadeEffects::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("condition", NDTString, NCTNone, DEFVALUE(RString, "true"),
               DOC_MSG("Condition when effect is performed"));
    format.Add("cameraEffect", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Camera effect name"));
    format.Add("cameraPosition", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, CamEffectBack),
               DOC_MSG("Camera effect position"));
    format.Add("sound", NDTString, NCTNone, DEFVALUE(RString, "$NONE$"), DOC_MSG("Sound effect (2D)"));
    format.Add("voice", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Sound effect (3D)"));
    format.Add("soundEnv", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Enviromental sound effect"));
    format.Add("soundDet", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Detector sound effect"));
    format.Add("track", NDTString, NCTNone, DEFVALUE(RString, "$NONE$"), DOC_MSG("Musical track"));
    format.Add("titleType", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, TitleNone),
               DOC_MSG("Type of title effect (text, object, resource)"));
    format.Add("titleEffect", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, TitPlain),
               DOC_MSG("Type (placement) of text title effect"));
    format.Add("title", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Content of title effect"));
}

TMError ArcadeEffects::TransferMsg(NetworkMessageContext& ctx, IndicesEffects* indices)
{
    TMCHECK(ctx.IdxTransfer(indices->condition, condition));
    TMCHECK(ctx.IdxTransfer(indices->cameraEffect, cameraEffect));
    TMCHECK(ctx.IdxTransfer(indices->cameraPosition, (int&)cameraPosition));
    TMCHECK(ctx.IdxTransfer(indices->sound, sound));
    TMCHECK(ctx.IdxTransfer(indices->voice, voice));
    TMCHECK(ctx.IdxTransfer(indices->soundEnv, soundEnv));
    TMCHECK(ctx.IdxTransfer(indices->soundDet, soundDet));
    TMCHECK(ctx.IdxTransfer(indices->track, track));
    TMCHECK(ctx.IdxTransfer(indices->titleType, (int&)titleType));
    TMCHECK(ctx.IdxTransfer(indices->titleEffect, (int&)titleEffect));
    TMCHECK(ctx.IdxTransfer(indices->title, title));
    return TMOK;
}

ArcadeWaypointInfo::ArcadeWaypointInfo()
{
    Init();
}

ArcadeWaypointInfo::ArcadeWaypointInfo(const ArcadeWaypointInfo& src)
{
    position = src.position;
    placement = src.placement;
    id = src.id;
    idStatic = src.idStatic;
    housePos = src.housePos;
    speed = src.speed;
    combat = src.combat;
    type = src.type;
    timeoutMin = src.timeoutMin;
    timeoutMid = src.timeoutMid;
    timeoutMax = src.timeoutMax;
    combatMode = src.combatMode;
    formation = src.formation;
    description = src.description;
    expCond = src.expCond;
    expActiv = src.expActiv;
    script = src.script;
    showWP = src.showWP;
    synchronizations = src.synchronizations;
    effects = src.effects;
    selected = src.selected;
}

void ArcadeWaypointInfo::Init()
{
    position = VZero;
    placement = 0;
    id = -1;
    idStatic = -1;
    housePos = -1;
    speed = SpeedUnchanged;
    combat = CMUnchanged;
    type = ACUNDEFINED;
    timeoutMin = 0;
    timeoutMid = 0;
    timeoutMax = 0;
    combatMode = (AI::Semaphore)-1;
    formation = (AI::Formation)-1;
    description = "";
    expCond = "true";
    expActiv = "";
    script = "";
    showWP = ShowNever;
    synchronizations.Clear();
    effects.Init();
    selected = false;
}

void ArcadeWaypointInfo::AddOffset(Vector3Par offset)
{
    position += offset;
    position[1] = GLOB_LAND->RoadSurfaceYAboveWater(position[0], position[2]);
}

void ArcadeWaypointInfo::Rotate(Vector3Par center, float angle, bool sel)
{
    if (sel && !selected)
    {
        return;
    }

    // rotation

    Vector3 dir = position - center;
    Matrix3 rot(MRotationY, -angle);
    dir = rot * dir;

    position = center + dir;
    position[1] = GLandscape->RoadSurfaceYAboveWater(position[0], position[2]);
}

void ArcadeWaypointInfo::CalculateCenter(Vector3& sum, int& count, bool sel)
{
    if (sel && !selected)
    {
        return;
    }

    sum += position;
    count++;
}

LSError ArcadeWaypointInfo::Serialize(ParamArchive& ar)
{
    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        Init();
    }

    PARAM_CHECK(ar.Serialize("position", position, 1))
    PARAM_CHECK(ar.Serialize("placement", placement, 1, 0))
    PARAM_CHECK(ar.Serialize("id", id, 1, -1))
    PARAM_CHECK(ar.Serialize("idStatic", idStatic, 1, -1))
    PARAM_CHECK(ar.Serialize("housePos", housePos, 2, -1))
    PARAM_CHECK(ar.SerializeEnum("type", type, 1, ACMOVE))
    PARAM_CHECK(ar.SerializeEnum("combatMode", combatMode, 1, (AI::Semaphore)-1))
    PARAM_CHECK(ar.SerializeEnum("formation", formation, 1, (AI::Formation)-1))
    PARAM_CHECK(ar.SerializeEnum("speed", speed, 1, SpeedUnchanged))
    PARAM_CHECK(ar.SerializeEnum("combat", combat, 4, CMUnchanged))
    PARAM_CHECK(ar.Serialize("description", description, 1, ""))
    PARAM_CHECK(ar.Serialize("expCond", expCond, 7, "true"))
    PARAM_CHECK(ar.Serialize("expActiv", expActiv, 7, ""))
    PARAM_CHECK(ar.Serialize("script", script, 7, ""))
    PARAM_CHECK(ar.SerializeArray("synchronizations", synchronizations, 1))
    PARAM_CHECK(ar.Serialize("Effects", effects, 1))
    PARAM_CHECK(ar.Serialize("timeoutMin", timeoutMin, 1, 0))
    PARAM_CHECK(ar.Serialize("timeoutMid", timeoutMid, 1, 0))
    PARAM_CHECK(ar.Serialize("timeoutMax", timeoutMax, 1, 0))
    if (ar.GetArVersion() >= 10)
    {
        PARAM_CHECK(ar.SerializeEnum("showWP", showWP, 1, ShowEasy))
    }
    else
    {
        bool show = showWP != ShowNever;
        PARAM_CHECK(ar.Serialize("show", show, 1, false))
        if (show)
        {
            showWP = ShowEasy;
        }
        else
        {
            showWP = ShowNever;
        }
    }

    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        description = DecodeMissionUserText(description);
        ATSParams* params = (ATSParams*)ar.GetParams();
        AI_ERROR(params);
        for (int i = 0; i < synchronizations.Size(); i++)
        {
            int& sync = synchronizations[i];
            AI_ERROR(sync >= 0);
            saturateMax(params->nextSyncId, sync + 1);
        }
    }
    return LSOK;
}

LSError WaypointInfo::Serialize(ParamArchive& ar)
{
    // world serialization
    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        Init();
    }

    PARAM_CHECK(ar.Serialize("position", position, 1))
    PARAM_CHECK(ar.Serialize("placement", placement, 1, 0))
    PARAM_CHECK(ar.Serialize("id", id, 1, -1))
    PARAM_CHECK(ar.Serialize("idStatic", idStatic, 1, -1))
    PARAM_CHECK(ar.Serialize("housePos", housePos, 1, -1))
    PARAM_CHECK(ar.SerializeEnum("type", type, 1, ACMOVE))
    PARAM_CHECK(ar.SerializeEnum("combatMode", combatMode, 1, (AI::Semaphore)-1))
    PARAM_CHECK(ar.SerializeEnum("formation", formation, 1, (AI::Formation)-1))
    PARAM_CHECK(ar.SerializeEnum("speed", speed, 1, SpeedUnchanged))
    PARAM_CHECK(ar.SerializeEnum("combat", combat, 1, CMUnchanged))
    PARAM_CHECK(ar.Serialize("description", description, 1, ""))
    PARAM_CHECK(ar.Serialize("expCond", expCond, 1, "true"))
    PARAM_CHECK(ar.Serialize("expActiv", expActiv, 1, ""))
    PARAM_CHECK(ar.Serialize("script", script, 1, ""))
    PARAM_CHECK(ar.SerializeArray("synchronizations", synchronizations, 1))
    ParamArchive arSubcls;
    if (!ar.OpenSubclass("Effects", arSubcls))
    {
        return LSStructure;
    }
    effects.WorldSerialize(arSubcls);
    PARAM_CHECK(ar.Serialize("timeoutMin", timeoutMin, 1, 0))
    PARAM_CHECK(ar.Serialize("timeoutMid", timeoutMid, 1, 0))
    PARAM_CHECK(ar.Serialize("timeoutMax", timeoutMax, 1, 0))
    if (ar.GetArVersion() >= 9)
    {
        PARAM_CHECK(ar.SerializeEnum("showWP", showWP, 1, ShowEasy))
    }
    else
    {
        bool show = showWP != ShowNever;
        PARAM_CHECK(ar.Serialize("show", show, 1, false))
        if (show)
        {
            showWP = ShowEasy;
        }
        else
        {
            showWP = ShowNever;
        }
    }
    return LSOK;
}

class IndicesWaypoint : public NetworkMessageIndices
{
  public:
    int position;
    int placement;
    int id;
    int idStatic;
    int housePos;
    int type;
    int combatMode;
    int formation;
    int speed;
    int combat;
    int timeoutMin;
    int timeoutMid;
    int timeoutMax;
    int description;
    int expCond;
    int expActiv;
    int script;
    int showWP;
    int synchronizations;
    IndicesEffects* effects;

    IndicesWaypoint();
    ~IndicesWaypoint() override;
    NetworkMessageIndices* Clone() const override { return new IndicesWaypoint; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesWaypoint::IndicesWaypoint()
{
    position = -1;
    placement = -1;
    id = -1;
    idStatic = -1;
    housePos = -1;
    type = -1;
    combatMode = -1;
    formation = -1;
    speed = -1;
    combat = -1;
    timeoutMin = -1;
    timeoutMid = -1;
    timeoutMax = -1;
    description = -1;
    expCond = -1;
    expActiv = -1;
    script = -1;
    showWP = -1;
    synchronizations = -1;

    effects = GetIndicesEffects();
}

IndicesWaypoint::~IndicesWaypoint()
{
    DeleteIndicesEffects(effects);
}

void IndicesWaypoint::Scan(NetworkMessageFormatBase* format)
{
    SCAN(position)
    SCAN(placement)
    SCAN(id)
    SCAN(idStatic)
    SCAN(housePos)
    SCAN(type)
    SCAN(combatMode)
    SCAN(formation)
    SCAN(speed)
    SCAN(combat)
    SCAN(timeoutMin)
    SCAN(timeoutMid)
    SCAN(timeoutMax)
    SCAN(description)
    SCAN(expCond)
    SCAN(expActiv)
    SCAN(script)
    SCAN(showWP)
    SCAN(synchronizations)

    ScanIndicesEffects(effects, format);
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesWaypoint()
{
    using namespace Poseidon;
    return new IndicesWaypoint();
}
namespace Poseidon
{

NetworkMessageFormat& ArcadeWaypointInfo::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("position", NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Waypoint position"));
    format.Add("placement", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Radius for random placement of waypoint"));
    format.Add("id", NDTInteger, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("ID of attached vehicle"));
    format.Add("idStatic", NDTInteger, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("ID of attached static object"));
    format.Add("housePos", NDTInteger, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("Waypoint position in house"));
    format.Add("type", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, ACMOVE), DOC_MSG("Waypoint type"));
    format.Add("combatMode", NDTInteger, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("Group combat mode"));
    format.Add("formation", NDTInteger, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("Group formation"));
    format.Add("speed", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, SpeedUnchanged), DOC_MSG("Group speed"));
    format.Add("combat", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, CMUnchanged), DOC_MSG("Group behaviour"));
    format.Add("timeoutMin", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Waypoint timeout"));
    format.Add("timeoutMid", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Waypoint timeout"));
    format.Add("timeoutMax", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Waypoint timeout"));
    format.Add("description", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Waypoint description"));
    format.Add("expCond", NDTString, NCTNone, DEFVALUE(RString, "true"),
               DOC_MSG("Condition for activation of waypoint"));
    format.Add("expActiv", NDTString, NCTNone, DEFVALUE(RString, ""),
               DOC_MSG("Statement processed when waypoint is activated"));
    format.Add("script", NDTString, NCTNone, DEFVALUE(RString, ""),
               DOC_MSG("Controling script for scripted waypoints"));
    format.Add("showWP", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, ShowEasy),
               DOC_MSG("When waypoint is shown in map"));
    format.Add("synchronizations", NDTIntArray, NCTSmallUnsigned, DEFVALUEINTARRAY,
               DOC_MSG("List of synchronizations"));
    ArcadeEffects::CreateFormat(cls, format);
    return format;
}

TMError ArcadeWaypointInfo::TransferMsg(NetworkMessageContext& ctx)
{
    AI_ERROR(dynamic_cast<const IndicesWaypoint*>(ctx.GetIndices()))
    const IndicesWaypoint* indices = static_cast<const IndicesWaypoint*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->position, position))
    TMCHECK(ctx.IdxTransfer(indices->placement, placement))
    TMCHECK(ctx.IdxTransfer(indices->id, id))
    TMCHECK(ctx.IdxTransfer(indices->idStatic, idStatic))
    TMCHECK(ctx.IdxTransfer(indices->housePos, housePos))
    TMCHECK(ctx.IdxTransfer(indices->type, (int&)type))
    TMCHECK(ctx.IdxTransfer(indices->combatMode, (int&)combatMode))
    TMCHECK(ctx.IdxTransfer(indices->formation, (int&)formation))
    TMCHECK(ctx.IdxTransfer(indices->speed, (int&)speed))
    TMCHECK(ctx.IdxTransfer(indices->combat, (int&)combat))
    TMCHECK(ctx.IdxTransfer(indices->timeoutMin, timeoutMin))
    TMCHECK(ctx.IdxTransfer(indices->timeoutMid, timeoutMid))
    TMCHECK(ctx.IdxTransfer(indices->timeoutMax, timeoutMax))
    TMCHECK(ctx.IdxTransfer(indices->description, description))
    TMCHECK(ctx.IdxTransfer(indices->expCond, expCond))
    TMCHECK(ctx.IdxTransfer(indices->expActiv, expActiv))
    TMCHECK(ctx.IdxTransfer(indices->script, script))
    TMCHECK(ctx.IdxTransfer(indices->showWP, (int&)showWP))
    TMCHECK(ctx.IdxTransfer(indices->synchronizations, synchronizations))
    TMCHECK(effects.TransferMsg(ctx, indices->effects))
    return TMOK;
}

bool ArcadeWaypointInfo::HasEffect() const
{
    if (effects.cameraEffect.GetLength() > 0)
    {
        return true;
    }
    if (stricmp(effects.sound, "$NONE$") != 0)
    {
        return true;
    }
    if (effects.voice.GetLength() > 0)
    {
        return true;
    }
    if (effects.soundEnv.GetLength() > 0)
    {
        return true;
    }
    if (effects.soundDet.GetLength() > 0)
    {
        return true;
    }
    if (stricmp(effects.track, "$NONE$") != 0)
    {
        return true;
    }
    if (effects.titleType != TitleNone)
    {
        return true;
    }
    return false;
}

void ArcadeGroupInfo::AddOffset(Vector3Par offset)
{
    for (int i = 0; i < units.Size(); i++)
    {
        units[i].AddOffset(offset);
    }
    for (int i = 0; i < sensors.Size(); i++)
    {
        sensors[i].AddOffset(offset);
    }
    for (int i = 0; i < waypoints.Size(); i++)
    {
        waypoints[i].AddOffset(offset);
    }
}

void ArcadeGroupInfo::Rotate(Vector3Par center, float angle, bool sel)
{
    for (int i = 0; i < units.Size(); i++)
    {
        units[i].Rotate(center, angle, sel);
    }
    for (int i = 0; i < sensors.Size(); i++)
    {
        sensors[i].Rotate(center, angle, sel);
    }
    for (int i = 0; i < waypoints.Size(); i++)
    {
        waypoints[i].Rotate(center, angle, sel);
    }
}

void ArcadeGroupInfo::CalculateCenter(Vector3& sum, int& count, bool sel)
{
    for (int i = 0; i < units.Size(); i++)
    {
        units[i].CalculateCenter(sum, count, sel);
    }
    for (int i = 0; i < sensors.Size(); i++)
    {
        sensors[i].CalculateCenter(sum, count, sel);
    }
    for (int i = 0; i < waypoints.Size(); i++)
    {
        waypoints[i].CalculateCenter(sum, count, sel);
    }
}

void ArcadeGroupInfo::Select(bool select)
{
    for (int i = 0; i < units.Size(); i++)
    {
        units[i].selected = select;
    }
    for (int i = 0; i < sensors.Size(); i++)
    {
        sensors[i].selected = select;
    }
    for (int i = 0; i < waypoints.Size(); i++)
    {
        waypoints[i].selected = select;
    }
}

void ArcadeGroupInfo::RequiredAddons(FindArrayRStringCI& addOns)
{
    for (int i = 0; i < units.Size(); i++)
    {
        units[i].RequiredAddons(addOns);
    }
}

LSError ArcadeGroupInfo::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.SerializeEnum("side", side, 1))
    PARAM_CHECK(ar.Serialize("Vehicles", units, 1))
    PARAM_CHECK(ar.Serialize("Waypoints", waypoints, 1))
    PARAM_CHECK(ar.Serialize("Sensors", sensors, 1))
    return LSOK;
}

ArcadeIntel::ArcadeIntel()
{
    Init();
}

void ArcadeIntel::Init()
{
    friends[TEast][TEast] = 1.0;
    friends[TEast][TWest] = 0.0;
    friends[TEast][TGuerrila] = 0.0;
    friends[TWest][TEast] = 0.0;
    friends[TWest][TWest] = 1.0;
    friends[TWest][TGuerrila] = 1.0;
    friends[TGuerrila][TEast] = 0.0;
    friends[TGuerrila][TWest] = 1.0;
    friends[TGuerrila][TGuerrila] = 1.0;
    weather = 0.5;
    fog = 0;
    weatherForecast = 0.5;
    fogForecast = 0;
    year = 1985;
    month = 5;
    day = 10;
    hour = 7;
    minute = 30;

    briefingName = "";
    briefingDescription = "";
}

void SendIntel(ArcadeIntel* intel);

LSError ArcadeIntel::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("briefingName", briefingName, 2, ""))
    PARAM_CHECK(ar.Serialize("briefingDescription", briefingDescription, 2, ""))

    if (ar.GetArVersion() >= 10)
    {
        PARAM_CHECK(ar.Serialize("resistanceWest", friends[TWest][TGuerrila], 1, 1.0))
        PARAM_CHECK(ar.Serialize("resistanceEast", friends[TEast][TGuerrila], 1, 0.0))
    }
    else
    {
        PARAM_CHECK(ar.Serialize("resistance", friends[TWest][TGuerrila], 1, 1.0))
        friends[TEast][TGuerrila] = 1.0f - friends[TWest][TGuerrila];
    }
    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        friends[TEast][TEast] = 1.0f;
        friends[TEast][TWest] = 0.0f;
        friends[TWest][TEast] = 0.0f;
        friends[TWest][TWest] = 1.0f;
        friends[TGuerrila][TGuerrila] = 1.0f;

        friends[TGuerrila][TEast] = friends[TEast][TGuerrila];
        friends[TGuerrila][TWest] = friends[TWest][TGuerrila];
    }

    PARAM_CHECK(ar.Serialize("startWeather", weather, 1, 0.5))
    PARAM_CHECK(ar.Serialize("startFog", fog, 1, 0))
    PARAM_CHECK(ar.Serialize("forecastWeather", weatherForecast, 1, 0.5))
    PARAM_CHECK(ar.Serialize("forecastFog", fogForecast, 1, 0))
    PARAM_CHECK(ar.Serialize("year", year, 1, 1985))
    PARAM_CHECK(ar.Serialize("month", month, 1, 5))
    PARAM_CHECK(ar.Serialize("day", day, 1, 10))
    PARAM_CHECK(ar.Serialize("hour", hour, 1, 7))
    PARAM_CHECK(ar.Serialize("minute", minute, 1, 30))

    return LSOK;
}

} // namespace Poseidon
void SelectLeader(ArcadeGroupInfo& gInfo)
{
    using namespace Poseidon;
    int maxRank = RankPrivate - 1;
    int i, iBest = -1;
    for (i = 0; i < gInfo.units.Size(); i++)
    {
        ArcadeUnitInfo& uInfo = gInfo.units[i];
        uInfo.leader = false;
        if (uInfo.rank > maxRank)
        {
            maxRank = uInfo.rank;
            iBest = i;
        }
    }
    AI_ERROR(iBest >= 0);
    AI_ERROR(maxRank >= RankPrivate);
    gInfo.units[iBest].leader = true;
    gInfo.side = gInfo.units[iBest].side;
}
namespace Poseidon
{

void SendBuildingUpdate(int id, int condition);

ArcadeTemplate::ArcadeTemplate()
{
    showHUD = true;
    showMap = true;
    showWatch = true;
    showCompass = true;
    showNotepad = true;
    showGPS = false;

    nextSyncId = 0;
    nextVehId = 0;

    randomSeed = toLargeInt(GRandGen.RandomValue() * 0x1000000) + 3;
}

void ArcadeTemplate::CheckSynchro()
{
    AutoArray<int> syncCountsW;
    AutoArray<int> syncCountsS;

    nextSyncId = 0;
    int n = groups.Size();
    int m = sensors.Size();

    for (int i = 0; i < n; i++)
    {
        ArcadeGroupInfo& gInfo = groups[i];
        int m = gInfo.waypoints.Size();
        for (int j = 0; j < m; j++)
        {
            ArcadeWaypointInfo& wInfo = gInfo.waypoints[j];
            int p = wInfo.synchronizations.Size();
            for (int l = 0; l < p; l++)
            {
                saturateMax(nextSyncId, wInfo.synchronizations[l] + 1);
            }
        }
        m = gInfo.sensors.Size();
        for (int j = 0; j < m; j++)
        {
            ArcadeSensorInfo& sInfo = gInfo.sensors[j];
            int p = sInfo.synchronizations.Size();
            for (int l = 0; l < p; l++)
            {
                saturateMax(nextSyncId, sInfo.synchronizations[l] + 1);
            }
        }
    }
    for (int j = 0; j < m; j++)
    {
        ArcadeSensorInfo& sInfo = sensors[j];
        int p = sInfo.synchronizations.Size();
        for (int l = 0; l < p; l++)
        {
            saturateMax(nextSyncId, sInfo.synchronizations[l] + 1);
        }
    }

    syncCountsW.Resize(nextSyncId);
    syncCountsS.Resize(nextSyncId);
    for (int i = 0; i < nextSyncId; i++)
    {
        syncCountsW[i] = 0;
        syncCountsS[i] = 0;
    }

    for (int i = 0; i < n; i++)
    {
        ArcadeGroupInfo& gInfo = groups[i];
        int m = gInfo.waypoints.Size();
        for (int j = 0; j < m; j++)
        {
            ArcadeWaypointInfo& wInfo = gInfo.waypoints[j];
            int p = wInfo.synchronizations.Size();
            for (int l = 0; l < p; l++)
            {
                int sync = wInfo.synchronizations[l];
                AI_ERROR(sync >= 0);
                syncCountsW[sync]++;
            }
        }
        m = gInfo.sensors.Size();
        for (int j = 0; j < m; j++)
        {
            ArcadeSensorInfo& sInfo = gInfo.sensors[j];
            int p = sInfo.synchronizations.Size();
            for (int l = 0; l < p; l++)
            {
                int sync = sInfo.synchronizations[l];
                AI_ERROR(sync >= 0);
                syncCountsS[sync]++;
            }
        }
    }
    for (int j = 0; j < m; j++)
    {
        ArcadeSensorInfo& sInfo = sensors[j];
        int p = sInfo.synchronizations.Size();
        for (int l = 0; l < p; l++)
        {
            int sync = sInfo.synchronizations[l];
            AI_ERROR(sync >= 0);
            syncCountsS[sync]++;
        }
    }

    for (int i = 0; i < n; i++)
    {
        ArcadeGroupInfo& gInfo = groups[i];
        int m = gInfo.waypoints.Size();
        for (int j = 0; j < m; j++)
        {
            ArcadeWaypointInfo& wInfo = gInfo.waypoints[j];
            int p = wInfo.synchronizations.Size();
            for (int l = 0; l < p;)
            {
                int sync = wInfo.synchronizations[l];
                AI_ERROR(sync >= 0);
                AI_ERROR(syncCountsW[sync] >= 1);
                if (syncCountsW[sync] + syncCountsS[sync] < 2)
                {
                    wInfo.synchronizations.Delete(l);
                    p--;
                }
                else
                {
                    l++;
                }
            }
        }
        m = gInfo.sensors.Size();
        for (int j = 0; j < m; j++)
        {
            ArcadeSensorInfo& sInfo = gInfo.sensors[j];
            int p = sInfo.synchronizations.Size();
            for (int l = 0; l < p;)
            {
                int sync = sInfo.synchronizations[l];
                AI_ERROR(sync >= 0);
                AI_ERROR(syncCountsS[sync] >= 1);
                if (syncCountsW[sync] < 1)
                {
                    sInfo.synchronizations.Delete(l);
                    p--;
                }
                else
                {
                    l++;
                }
            }
        }
    }
    for (int j = 0; j < m; j++)
    {
        ArcadeSensorInfo& sInfo = sensors[j];
        int p = sInfo.synchronizations.Size();
        for (int l = 0; l < p;)
        {
            int sync = sInfo.synchronizations[l];
            AI_ERROR(sync >= 0);
            AI_ERROR(syncCountsS[sync] >= 1);
            if (syncCountsW[sync] < 1)
            {
                sInfo.synchronizations.Delete(l);
                p--;
            }
            else
            {
                l++;
            }
        }
    }

    int maxSync = -1;
    for (int i = nextSyncId - 1; i >= 0; i--)
    {
        if (syncCountsW[i] >= 1 || syncCountsS[i] >= 1)
        {
            AI_ERROR(syncCountsW[i] >= 1);
            AI_ERROR(syncCountsW[i] + syncCountsS[i] >= 2);
            maxSync = i;
            break;
        }
    }
    nextSyncId = maxSync + 1;
}

bool ArcadeTemplate::IsConsistent(Display* disp, bool multiplayer)
{
    int nPlayers1 = 0;
    int side1;
    int nWestGroups = 0;
    int nEastGroups = 0;
    int nGuerrilaGroups = 0;
    int nCivilianGroups = 0;
    int nLogicGroups = 0;

    int i, n = groups.Size();
    for (i = 0; i < n; i++)
    {
        ArcadeGroupInfo& info = groups[i];
        switch (info.side)
        {
            case TWest:
                nWestGroups++;
                break;
            case TEast:
                nEastGroups++;
                break;
            case TGuerrila:
                nGuerrilaGroups++;
                break;
            case TCivilian:
                nCivilianGroups++;
                break;
            case TLogic:
                nLogicGroups++;
                break;
            default:
                Fail("Side !!!");
                RptF("Side %d", info.side);
                break;
        }
        int nUnits = 0;
        for (int j = 0; j < info.units.Size(); j++)
        {
            ArcadeUnitInfo& uInfo = info.units[j];
            switch (uInfo.player)
            {
                case APPlayerCommander:
                case APPlayerDriver:
                case APPlayerGunner:
                    nPlayers1++;
                    side1 = uInfo.side;
                    break;
            }
            const VehicleType* type = dynamic_cast<const VehicleType*>(VehicleTypes.New(uInfo.vehicle));
            if (!type)
            {
                Fail("Type is not VehicleType");
                continue;
            }
            if (type->HasDriver())
            {
                nUnits++;
            }
            if (type->HasCommander())
            {
                nUnits++;
            }
            if (type->HasGunner())
            {
                nUnits++;
            }
        }
        AI_ERROR(nUnits > 0);
        if (nUnits > MAX_UNITS_PER_GROUP)
        {
            if (disp)
            {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_MSG_LOT_UNITS), MAX_UNITS_PER_GROUP);
                disp->CreateMsgBox(MB_BUTTON_OK, buffer);
            }
            return false;
        }
    }

    AI_ERROR(multiplayer || nPlayers1 <= 1);
#if !_ENABLE_CHEATS
    if (!multiplayer && nPlayers1 == 0)
    {
        if (disp)
        {
            disp->CreateMsgBox(MB_BUTTON_OK, LocalizeString(IDS_MSG_NO_PLAYER));
        }
        return false;
    }
#endif

    if (nWestGroups > MaxGroups || nEastGroups > MaxGroups || nGuerrilaGroups > MaxGroups ||
        nCivilianGroups > MaxGroups || nLogicGroups > MaxGroups)
    {
        if (disp)
        {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), LocalizeString(IDS_MSG_LOT_GROUPS), MaxGroups);
            disp->CreateMsgBox(MB_BUTTON_OK, buffer);
        }
        return false;
    }

    return true;
}

void ArcadeTemplate::RequiredAddons(FindArrayRStringCI& addOns)
{
    for (int i = 0; i < groups.Size(); i++)
    {
        groups[i].RequiredAddons(addOns);
    }
    for (int i = 0; i < emptyVehicles.Size(); i++)
    {
        emptyVehicles[i].RequiredAddons(addOns);
    }
}

} // namespace Poseidon
void CheckPatch(FindArrayRStringCI& addOns, FindArrayRStringCI& missing)
{
    using namespace Poseidon;
    const ParamEntry* patches = Pars.FindEntry("CfgPatches");
    if (!patches)
    {
        missing = addOns;
        return;
    }
    // activate all addons requested by given mission
    GWorld->ActivateAddons(addOns);

    int m = patches->GetEntryCount();

    for (int i = 0; i < addOns.Size(); i++)
    {
        RString addOn = addOns[i];
        bool found = false;
        for (int j = 0; j < m; j++)
        {
            if (stricmp(addOn, patches->GetEntry(j).GetName()) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            missing.Add(addOn);
        }
    }
}
namespace Poseidon
{

void ArcadeTemplate::ScanRequiredAddons()
{
    FindArrayRStringCI oldAddOnsAuto = addOnsAuto;
    addOnsAuto.Resize(0);
    RequiredAddons(addOnsAuto);
    // check which items in oldAddOnsAuto are no longer listed in addOnsAuto
    // such items can be removed from addon list
    for (int i = 0; i < oldAddOnsAuto.Size(); i++)
    {
        RString old = oldAddOnsAuto[i];
        if (addOnsAuto.Find(old) < 0)
        {
            // was listed in old list, but is not in new
            // we can remove it from the required list as
            // it might be listed several times?
            addOns.Delete(old);
        }
    }
    for (int i = 0; i < addOnsAuto.Size(); i++)
    {
        addOns.AddUnique(addOnsAuto[i]);
    }
}

LSError ArcadeTemplate::Serialize(ParamArchive& ar)
{
    ATSParams* params = (ATSParams*)ar.GetParams();
    if (ar.IsSaving())
    {
        ScanRequiredAddons();
        PARAM_CHECK(ar.SerializeArray("addOns", addOns, 1))
        PARAM_CHECK(ar.SerializeArray("addOnsAuto", addOnsAuto, 1))

        CheckSynchro(); // remove invalid synchronizations
        Compact();
    }
    else if (ar.GetPass() == ParamArchive::PassFirst)
    {
        addOns.Resize(0);
        missingAddOns.Resize(0);
        PARAM_CHECK(ar.SerializeArray("addOns", addOns, 1))
        CheckPatch(addOns, missingAddOns);
        if (missingAddOns.Size() > 0)
        {
            return LSNoAddOn;
        }

        if (!params)
        {
            Fail("Params needed");
            return LSStructure;
        }
        FindArrayRStringCI addOnsBackup = addOns;
        Clear();
        addOns = addOnsBackup;
        params->nextSyncId = 0;
        params->nextVehId = 0;
    }

    PARAM_CHECK(ar.Serialize("showHUD", showHUD, 8, true))
    PARAM_CHECK(ar.Serialize("showMap", showMap, 8, true))
    PARAM_CHECK(ar.Serialize("showWatch", showWatch, 8, true))
    PARAM_CHECK(ar.Serialize("showCompass", showCompass, 8, true))
    PARAM_CHECK(ar.Serialize("showNotepad", showNotepad, 8, true))
    PARAM_CHECK(ar.Serialize("showGPS", showGPS, 8, false))
    PARAM_CHECK(ar.Serialize("randomSeed", randomSeed, 1, 1))

    PARAM_CHECK(ar.Serialize("Intel", intel, 7))
    PARAM_CHECK(ar.Serialize("Groups", groups, 1))
    PARAM_CHECK(ar.Serialize("Vehicles", emptyVehicles, 1))
    PARAM_CHECK(ar.Serialize("Markers", markers, 1))
    PARAM_CHECK(ar.Serialize("Sensors", sensors, 1))

    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        AI_ERROR(params);
        nextSyncId = params->nextSyncId;
        nextVehId = params->nextVehId;
        CheckSynchro(); // remove invalid synchronizations
        Compact();
    }
    return LSOK;
}

} // namespace Poseidon
