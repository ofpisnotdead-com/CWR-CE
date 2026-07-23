#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/AI/Path/AIDefs.hpp>

#include <Poseidon/AI/AIRadio.hpp>
#include <Poseidon/AI/UnitNumberList.hpp>

#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Network/NetworkCustomAssets.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>

#include <Poseidon/UI/Locale/Languages.hpp>
#include <Poseidon/UI/Locale/Sentences.hpp>
#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/UI/OptionsUI.hpp>
#include <Poseidon/UI/InGame/InGameUIImpl.hpp>

#include <ctype.h>

#include <Poseidon/Game/Chat.hpp>

#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <stdio.h>
#include <string.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

void AddUnitInfo(AIUnit* subgroup);

namespace Poseidon
{
using Foundation::EnumName;

// Defined at global scope in UI/OptionsUI.cpp. Declared here at file scope so the
// reference inside RadioChannel::Say (a Poseidon-class member) binds to it rather
// than a phantom Poseidon::GetUserDirectory.
} // namespace Poseidon
#include <Poseidon/Network/Network.hpp>

namespace Poseidon
{
RString GetUserDirectory();
void PositionToAA11(Vector3Val pos, char* buffer);
} // namespace Poseidon

#define PAUSE_AFTER_WORD 0.00F
#define PAUSE_IN_NUMBER 0.00F

#define PAUSE_AFTER_NUMBER PAUSE_AFTER_WORD
#define PAUSE_IN_UNIT_LIST 0.00F
#define PAUSE_AFTER_UNIT_LIST 0.05F

// helper functions

namespace Poseidon
{
PackedBoolArray GetUnitsList(AIGroup* grp, OLinkArray<AIUnit>& units)
{
    PackedBoolArray result;
    int i;
    for (i = 0; i < units.Size(); i++)
    {
        AIUnit* unit = units[i];
        if (!unit)
        {
            continue;
        }
        if (grp && unit->GetGroup() != grp)
        {
            continue;
        }
        result.Set(unit->ID() - 1, true);
    }
    return result;
}

static void Spell(RadioSentence& sentence, const char* buffer, float pauseAfter)
{
    int i, n = strlen(buffer);
    for (i = 0; i < n; i++)
    {
        float pause = (i == n - 1) ? pauseAfter : PAUSE_IN_NUMBER;
        char c = tolower(buffer[i]);
        switch (c)
        {
            case '0':
                sentence.Add("zero", pause);
                break;
            case '1':
                sentence.Add("one", pause);
                break;
            case '2':
                sentence.Add("two", pause);
                break;
            case '3':
                sentence.Add("three", pause);
                break;
            case '4':
                sentence.Add("four", pause);
                break;
            case '5':
                sentence.Add("five", pause);
                break;
            case '6':
                sentence.Add("six", pause);
                break;
            case '7':
                sentence.Add("seven", pause);
                break;
            case '8':
                sentence.Add("eight", pause);
                break;
            case '9':
                sentence.Add("nine", pause);
                break;
            case 'a':
                sentence.Add("alpha", pause);
                break;
            case 'b':
                sentence.Add("bravo", pause);
                break;
            case 'c':
                sentence.Add("charlie", pause);
                break;
            case 'd':
                sentence.Add("delta", pause);
                break;
            case 'e':
                sentence.Add("echo", pause);
                break;
            case 'f':
                sentence.Add("foxtrot", pause);
                break;
            case 'g':
                sentence.Add("golf", pause);
                break;
            case 'h':
                sentence.Add("hotel", pause);
                break;
            case 'i':
                sentence.Add("india", pause);
                break;
            case 'j':
                sentence.Add("juliet", pause);
                break;
            case '.':
            case ',':
            case ':':
            case ';':
            case '?':
            case '!':
            case '-':
            case '(':
            case ')':
                break;
            default:
                Fail("Character");
                break;
        }
    }
}

static void SayUnitNumber(RadioSentence& sentence, int number, float pause)
{
    switch (number)
    {
        case 1:
            sentence.Add("one", pause);
            break;
        case 2:
            sentence.Add("two", pause);
            break;
        case 3:
            sentence.Add("three", pause);
            break;
        case 4:
            sentence.Add("four", pause);
            break;
        case 5:
            sentence.Add("five", pause);
            break;
        case 6:
            sentence.Add("six", pause);
            break;
        case 7:
            sentence.Add("seven", pause);
            break;
        case 8:
            sentence.Add("eight", pause);
            break;
        case 9:
            sentence.Add("nine", pause);
            break;
        case 10:
            sentence.Add("ten", pause);
            break;
        case 11:
            sentence.Add("eleven", pause);
            break;
        case 12:
            sentence.Add("twelve", pause);
            break;
        default:
            Fail("Unit ID");
            break;
    }
}

static void SayNumber(RadioSentence& sentence, int number, float pauseAfter, const char* format = "%d", int mode = 1)
{
    switch (mode)
    {
        default:
            char buffer[32];
            snprintf(buffer, sizeof(buffer), format, number);
            Spell(sentence, buffer, pauseAfter);
            break;
        case 2:
            SayUnitNumber(sentence, number, pauseAfter);
            break;
    }
}

static void SayUnit(RadioSentence& sentence, AIUnit* unit, int mode, float pause)
{
    AI_ERROR(unit);
    AI_ERROR(unit->ID() > 0 && unit->ID() <= MAX_UNITS_PER_GROUP);
    if (mode == 1)
    {
        SayUnitNumber(sentence, unit->ID(), pause);
    }
    else if (mode == 2)
    {
        switch (unit->ID())
        {
            case 1:
                sentence.Add("1ToLeader", pause);
                break;
            case 2:
                sentence.Add("2ToLeader", pause);
                break;
            case 3:
                sentence.Add("3ToLeader", pause);
                break;
            case 4:
                sentence.Add("4ToLeader", pause);
                break;
            case 5:
                sentence.Add("5ToLeader", pause);
                break;
            case 6:
                sentence.Add("6ToLeader", pause);
                break;
            case 7:
                sentence.Add("7ToLeader", pause);
                break;
            case 8:
                sentence.Add("8ToLeader", pause);
                break;
            case 9:
                sentence.Add("9ToLeader", pause);
                break;
            case 10:
                sentence.Add("10ToLeader", pause);
                break;
            case 11:
                sentence.Add("11ToLeader", pause);
                break;
            case 12:
                sentence.Add("12ToLeader", pause);
                break;
            default:
                Fail("Unit ID");
                break;
        }
    }
    else
        Fail("Mode");
}

PackedBoolArray PrepareList(AIGroup* grp, PackedBoolArray list, bool wholeCrew)
{
    if (!wholeCrew)
    {
        return list;
    }
    if (!grp)
    {
        return list;
    }

    PackedBoolArray result = list;
    int i;
    for (i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (!list.Get(i))
        {
            continue;
        }
        AIUnit* unit = grp->UnitWithID(i + 1);
        if (!unit)
        {
            continue;
        }
        Transport* veh = unit->GetVehicleIn();
        if (!veh)
        {
            continue;
        }
        // one of the vehicle is always CommanderUnit
        AIUnit* commander = veh->CommanderUnit();
        if (commander && unit != commander)
        {
            // pass all commands to vehicle commander
            result.Set(i, false);
            if (commander->GetGroup() == grp && commander != grp->Leader())
            {
                result.Set(commander->ID() - 1, true);
            }
        }
    }
    return result;
}

static bool AllUnits(AIGroup* grp, PackedBoolArray list, bool wholeCrew)
{
    if (!grp)
    {
        return false;
    }
    if (list.GetCount() <= 1)
    {
        return false;
    }

    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = grp->UnitWithID(i + 1);
        if (!unit)
        {
            continue;
        }
        if (wholeCrew)
        {
            Transport* veh = unit->GetVehicleIn();
            if (veh)
            {
                AIUnit* commander = veh->CommanderUnit();
                if (commander)
                {
                    unit = commander;
                }
            }
        }
        if (unit == grp->Leader())
        {
            continue;
        }
        if (!list.Get(unit->ID() - 1))
        {
            return false;
        }
    }

    return true;
}

template <>
const EnumName* Foundation::GetEnumNames(Team dummy)
{
    static const EnumName TeamNames[] = {EnumName(TeamMain, "MAIN"),     EnumName(TeamRed, "RED"),
                                         EnumName(TeamGreen, "GREEN"),   EnumName(TeamBlue, "BLUE"),
                                         EnumName(TeamYellow, "YELLOW"), EnumName()};
    return TeamNames;
}

void SetTeam(int i, Team team);

static int WholeTeam(AIGroup* grp, PackedBoolArray list, bool wholeCrew)
{
    if (!grp)
    {
        return -1;
    }
    if (list.GetCount() <= 1)
    {
        return -1;
    }

    int all = 0;
    int main = 0;
    int red = 0;
    int green = 0;
    int blue = 0;
    int yellow = 0;
    int mainOut = 0;
    int redOut = 0;
    int greenOut = 0;
    int blueOut = 0;
    int yellowOut = 0;

    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = grp->UnitWithID(i + 1);
        if (!unit)
        {
            continue;
        }
        if (wholeCrew)
        {
            Transport* veh = unit->GetVehicleIn();
            if (veh && veh->GetType()->HasCommander())
            {
                AIUnit* commander = veh->CommanderBrain();
                AIUnit* driver = veh->DriverBrain();
                AIUnit* gunner = veh->GunnerBrain();
                if (commander)
                {
                    if (driver == unit || gunner == unit)
                    {
                        unit = commander;
                    }
                }
                else if (driver)
                {
                    if (gunner == unit)
                    {
                        unit = driver;
                    }
                }
            }
        }
        if (unit == grp->Leader())
        {
            continue;
        }

        Team team = GetTeam(unit->ID() - 1);
        if (list.Get(unit->ID() - 1))
        {
            all++;
            switch (team)
            {
                case TeamMain:
                    main++;
                    break;
                case TeamRed:
                    red++;
                    break;
                case TeamGreen:
                    green++;
                    break;
                case TeamBlue:
                    blue++;
                    break;
                case TeamYellow:
                    yellow++;
                    break;
            }
        }
        else
        {
            switch (team)
            {
                case TeamMain:
                    mainOut++;
                    break;
                case TeamRed:
                    redOut++;
                    break;
                case TeamGreen:
                    greenOut++;
                    break;
                case TeamBlue:
                    blueOut++;
                    break;
                case TeamYellow:
                    yellowOut++;
                    break;
            }
        }
    }

    if (all > 0)
    {
        if (main == all && mainOut == 0)
        {
            return TeamMain;
        }
        if (red == all && redOut == 0)
        {
            return TeamRed;
        }
        if (green == all && greenOut == 0)
        {
            return TeamGreen;
        }
        if (blue == all && blueOut == 0)
        {
            return TeamBlue;
        }
        if (yellow == all && yellowOut == 0)
        {
            return TeamYellow;
        }
    }
    return -1;
}

static void SayUnits(RadioSentence& sentence, int n, AIGroup* grp, PackedBoolArray list, int mode, float pauseAfter)
{
    if (!grp)
    {
        return;
    }

    int i;
    for (i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (!list.Get(i))
        {
            continue;
        }
        float pause;
        if (--n == 0)
        {
            pause = pauseAfter;
        }
        else
        {
            pause = PAUSE_IN_UNIT_LIST;
        }
        if (mode == 1)
        {
            switch (i + 1)
            {
                case 1:
                    sentence.Add("one", pause);
                    break;
                case 2:
                    sentence.Add("two", pause);
                    break;
                case 3:
                    sentence.Add("three", pause);
                    break;
                case 4:
                    sentence.Add("four", pause);
                    break;
                case 5:
                    sentence.Add("five", pause);
                    break;
                case 6:
                    sentence.Add("six", pause);
                    break;
                case 7:
                    sentence.Add("seven", pause);
                    break;
                case 8:
                    sentence.Add("eight", pause);
                    break;
                case 9:
                    sentence.Add("nine", pause);
                    break;
                case 10:
                    sentence.Add("ten", pause);
                    break;
                case 11:
                    sentence.Add("eleven", pause);
                    break;
                case 12:
                    sentence.Add("twelve", pause);
                    break;
                default:
                    Fail("Unit ID");
                    break;
            }
        }
    }
}

void CreateUnitsList(PackedBoolArray list, char* buffer)
{
    strcpy(buffer, JoinUnitNumbers(list, MAX_UNITS_PER_GROUP).Data());
}

int SentenceParamSimpleList::Add(RString text, TargetSide side)
{
    for (int i = 0; i < Size(); i++)
    {
        SentenceParamSimple& param = Set(i);
        if (param.type == SPTText && param.side == side && strcmp(param.text, text) == 0 && param.number < 9)
        {
            param.number++;
            return i;
        }
    }
    int index = base::Add();
    SentenceParamSimple& param = Set(index);
    param.type = SPTText;
    param.number = 1;
    param.side = side;
    param.text = text;
    return index;
}

int SentenceParams::Add(AISubgroup* subgrp, bool wholeCrew)
{
    int index = base::Add();
    Set(index).type = SPTUnitList;
    Set(index).grp = subgrp->GetGroup();
    Set(index).list = subgrp->GetUnitsList();
    Set(index).wholeCrew = wholeCrew;
    return index;
}

int SentenceParams::Add(OLinkArray<AIUnit>& units, bool wholeCrew)
{
    int index = base::Add();
    Set(index).type = SPTUnitList;
    Set(index).grp = units[0]->GetGroup();
    Set(index).list = GetUnitsList(nullptr, units);
    Set(index).wholeCrew = wholeCrew;
    return index;
}

int SentenceParams::Add(Ref<SentenceParamSimpleList> targets)
{
    int index = base::Add();
    Set(index).type = SPTTargetList;
    Set(index).targets = targets;
    return index;
}

void SentenceParams::AddAzimutRelDir(Vector3Par dir)
{
    static const RStringB azimSay[13] = {
        "at12", "at1", "at2", "at3", "at4", "at5", "at6", "at7", "at8", "at9", "at10", "at11", "at12",
    };
    static const RStringB azimWrite[13] = {
        LocalizeString(IDS_WORD_AT12), LocalizeString(IDS_WORD_AT1),  LocalizeString(IDS_WORD_AT2),
        LocalizeString(IDS_WORD_AT3),  LocalizeString(IDS_WORD_AT4),  LocalizeString(IDS_WORD_AT5),
        LocalizeString(IDS_WORD_AT6),  LocalizeString(IDS_WORD_AT7),  LocalizeString(IDS_WORD_AT8),
        LocalizeString(IDS_WORD_AT9),  LocalizeString(IDS_WORD_AT10), LocalizeString(IDS_WORD_AT11),
        LocalizeString(IDS_WORD_AT12),
    };

    int azimut = toInt(atan2(dir.X(), dir.Z()) * (6 / H_PI));
    if (azimut < 0)
    {
        azimut += 12;
    }

    if (azimut >= 0 && azimut <= 12)
    {
        AddWord(azimSay[azimut], azimWrite[azimut]);
    }
}

void SentenceParams::AddDistance(float dist)
{
    static const RStringB distSay[] = {
        "dist50",  // 0..50
        "dist100", // 50..150
        "dist200",  "dist200",  "dist500",
        "dist500", // 500
        "dist500",  "dist1000", "dist1000", "dist1000",
        "dist1000", // 1000
        "dist1000", "dist1000", "dist1000", "dist1000", "dist1000", "dist2000", "dist2000", "dist2000", "dist2000",
        "dist2000", // 2000
        "dist2000", "dist2000", "dist2000", "dist2000", "dist2000",
        "far", // 2500 (and more)
    };
    static const RStringB distWrite[] = {
        LocalizeString(IDS_WORD_DIST50),
        LocalizeString(IDS_WORD_DIST100), // 50..150
        LocalizeString(IDS_WORD_DIST200),  LocalizeString(IDS_WORD_DIST200),  LocalizeString(IDS_WORD_DIST500),
        LocalizeString(IDS_WORD_DIST500), // 500
        LocalizeString(IDS_WORD_DIST500),  LocalizeString(IDS_WORD_DIST1000), LocalizeString(IDS_WORD_DIST1000),
        LocalizeString(IDS_WORD_DIST1000),
        LocalizeString(IDS_WORD_DIST1000), // 1000
        LocalizeString(IDS_WORD_DIST1000), LocalizeString(IDS_WORD_DIST1000), LocalizeString(IDS_WORD_DIST1000),
        LocalizeString(IDS_WORD_DIST1000), LocalizeString(IDS_WORD_DIST1000), LocalizeString(IDS_WORD_DIST2000),
        LocalizeString(IDS_WORD_DIST2000), LocalizeString(IDS_WORD_DIST2000), LocalizeString(IDS_WORD_DIST2000),
        LocalizeString(IDS_WORD_DIST2000), // 2000
        LocalizeString(IDS_WORD_DIST2000), LocalizeString(IDS_WORD_DIST2000), LocalizeString(IDS_WORD_DIST2000),
        LocalizeString(IDS_WORD_DIST2000), LocalizeString(IDS_WORD_DIST2000),
        LocalizeString(IDS_WORD_DISTFAR), // 2500 (and more)
    };

    int dist100 = toInt(dist * 0.01);
    const int maxDist = sizeof(distWrite) / sizeof(*distWrite) - 1;
    saturate(dist100, 0, maxDist);

    AddWord(distSay[dist100], distWrite[dist100]);
}

void SentenceParams::AddRelativePosition(Vector3Par dir)
{
    // convert position to relative position
    int azimut = toInt(0.1 * atan2(dir.X(), dir.Z()) * (180 / H_PI));
    if (azimut < 0)
    {
        azimut += 36;
    }
    Add("%02d", azimut);
}

void SentenceParams::AddAzimut(Vector3Par pos)
{
    AIUnit* unit = GWorld->FocusOn();
    // FIXED for dedicated server
    if (!unit)
    {
        AddAzimutRelDir(VForward);
        return;
    }

    AIGroup* grp = unit ? unit->GetGroup() : nullptr;
    AIUnit* leader = grp ? grp->Leader() : nullptr;
    if (leader)
    {
        Matrix4 transf;
        transf.SetOriented(grp->MainSubgroup()->GetFormationDirection(), VUp);
        transf.SetPosition(leader->Position());
        AddAzimutRelDir(transf.InverseRotation().FastTransform(pos));
    }
    else
    {
        AddAzimutRelDir(GWorld->CameraOn()->GetInvTransform().FastTransform(pos));
    }
}

void SentenceParams::AddAzimutDir(Vector3Par dir)
{
    AIUnit* unit = GWorld->FocusOn();
    // FIXED for dedicated server
    if (!unit)
    {
        AddAzimutRelDir(VForward);
        return;
    }

    AIGroup* grp = unit ? unit->GetGroup() : nullptr;
    AIUnit* leader = grp ? grp->Leader() : nullptr;
    if (leader)
    {
        Matrix3 orient(MDirection, grp->MainSubgroup()->GetFormationDirection(), VUp);
        AddAzimutRelDir(orient.InverseRotation() * dir);
    }
    else
    {
        AddAzimutRelDir(GWorld->CameraOn()->GetInvTransform().Rotate(dir));
    }
}

void ShowGroupDir(AIGroup* to)
{
    AIUnit* unit = GWorld->FocusOn();
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* u = to->UnitWithID(i + 1);
        if (!u)
        {
            continue;
        }
        if (u == unit)
        {
            GWorld->UI()->ShowGroupDir();
        }
        else if (u->GetPerson()->IsRemotePlayer())
        {
            GetNetworkManager().ShowGroupDir(u->GetPerson(), u->GetGroup()->MainSubgroup()->GetFormationDirection());
        }
    }
}

void ShowGroupDir(OLinkArray<AIUnit>& to)
{
    AIUnit* unit = GWorld->FocusOn();
    for (int i = 0; i < to.Size(); i++)
    {
        AIUnit* u = to[i];
        if (!u)
        {
            continue;
        }
        if (u == unit)
        {
            GWorld->UI()->ShowGroupDir();
        }
        else if (u->GetPerson()->IsRemotePlayer())
        {
            GetNetworkManager().ShowGroupDir(u->GetPerson(), u->GetGroup()->MainSubgroup()->GetFormationDirection());
        }
    }
}

static RString SelectVariant(const ParamEntry& cls, float rnd)
{
    int n = (cls.GetSize() + 1) / 2;

    int i, ii = n - 1;
    for (i = 0; i < ii; i++)
    {
        float r = cls[2 * i + 1];
        rnd -= r;
        if (rnd <= 0)
        {
            ii = i;
            break;
        }
    }

    return cls[2 * ii];
}

RString SPEECH(int x)
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "Speech%d", x);
    return buffer;
}

static void SaySentence(RadioSentence& sentence, int language, const ParamEntry& clsVariant, SentenceParams& params,
                        float pauseAfterMessage)
{
    const ParamEntry& cls = clsVariant >> SPEECH(language);

    int n = cls.GetSize();
    int nParams = params.Size();
    for (int i = 0; i < n; i++)
    {
        RString word = cls[i];
        if (!word || word.GetLength() == 0)
        {
            continue;
        }
        if (word[0] == '\'')
        {
            continue;
        }
        if (word[0] == '%')
        {
            if (word[1] == 'q')
            {
                // do not say MicOut
                // do not wait
                return;
            }
            else
            {
                const char* format = word;
                format++;
                int index = 0;
                int mode = 1;
                while (isdigit(*format))
                {
                    index *= 10;
                    index += *format - '0';
                    format++;
                }
                index--;
                if (*format == '.')
                {
                    format++;
                    mode = 0;
                    while (isdigit(*format))
                    {
                        mode *= 10;
                        mode += *format - '0';
                        format++;
                    }
                }
                if (index < 0 || index >= nParams)
                {
                    continue;
                }
                SentenceParam param = params[index];
                switch (param.type)
                {
                    case SPTWordText:
                        sentence.Add(param.text, PAUSE_AFTER_WORD);
                        break;
                    case SPTText:
                        Spell(sentence, param.text, PAUSE_AFTER_WORD);
                        break;
                    case SPTNumber:
                        SayNumber(sentence, param.number, PAUSE_AFTER_WORD, "%d", mode);
                        break;
                    case SPTFormat:
                        SayNumber(sentence, param.number, PAUSE_AFTER_WORD, param.text, mode);
                        break;
                    case SPTUnit:
                        SayUnit(sentence, param.unit, mode, PAUSE_AFTER_UNIT_LIST);
                        break;
                    case SPTUnitList:
                    {
                        PackedBoolArray list = PrepareList(param.grp, param.list, param.wholeCrew);
                        if (AllUnits(param.grp, list, param.wholeCrew))
                        {
                            sentence.Add("all", PAUSE_AFTER_UNIT_LIST);
                        }
                        else
                        {
                            int team = WholeTeam(param.grp, list, param.wholeCrew);
                            if (team >= 0)
                            {
                                switch (team)
                                {
                                    case TeamMain:
                                        sentence.Add("whiteTeam", PAUSE_AFTER_UNIT_LIST);
                                        break;
                                    case TeamRed:
                                        sentence.Add("redTeam", PAUSE_AFTER_UNIT_LIST);
                                        break;
                                    case TeamGreen:
                                        sentence.Add("greenTeam", PAUSE_AFTER_UNIT_LIST);
                                        break;
                                    case TeamBlue:
                                        sentence.Add("blueTeam", PAUSE_AFTER_UNIT_LIST);
                                        break;
                                    case TeamYellow:
                                        sentence.Add("yellowTeam", PAUSE_AFTER_UNIT_LIST);
                                        break;
                                }
                            }
                            else
                            {
                                int i, n = 0;
                                for (i = 0; i < MAX_UNITS_PER_GROUP; i++)
                                {
                                    if (list.Get(i))
                                    {
                                        n++;
                                    }
                                }
                                SayUnits(sentence, n, param.grp, list, mode, PAUSE_AFTER_UNIT_LIST);
                            }
                        }
                    }
                    break;
                    case SPTTargetList:
                        for (int j = 0; j < param.targets->Size(); j++)
                        {
                            SentenceParamSimple& target = (*param.targets)[j];
                            if (j > 0 && j == param.targets->Size() - 1)
                            {
                                sentence.Add("and", PAUSE_AFTER_WORD);
                            }
                            if (target.number > 1)
                            {
                                SayNumber(sentence, target.number, PAUSE_AFTER_WORD);
                            }
                            AI_ERROR(target.type == SPTText);
                            Spell(sentence, target.text, PAUSE_AFTER_WORD);
                        }
                        break;
                }
            }
        }
        else
        {
            sentence.Add(word, PAUSE_AFTER_WORD);
        }
    }

    sentence.Add("xmit", 0);
}

static RString WriteSentence(RString format, SentenceParams& params)
{
    int nParams = params.Size();
    char result[2048], c;
    const char* src = format;
    char* dst = result;

    while ((c = *src) != 0)
    {
        if (c == '%')
        {
            src++;
            int index = 0;
            int mode = 1;
            while (isdigit(*src))
            {
                index *= 10;
                index += *src - '0';
                src++;
            }
            index--;
            if (*src == '.')
            {
                src++;
                mode = 0;
                while (isdigit(*src))
                {
                    mode *= 10;
                    mode += *src - '0';
                    src++;
                }
            }
            if (index < 0 || index >= nParams)
            {
                continue;
            }
            SentenceParam param = params[index];
            switch (param.type)
            {
                case SPTWordText:
                {
                    strcpy(dst, param.text2);
                    dst += param.text2.GetLength();
                }
                break;
                case SPTText:
                    strcpy(dst, param.text);
                    dst += param.text.GetLength();
                    break;
                case SPTNumber:
                {
                    char buffer[32];
                    snprintf(buffer, sizeof(buffer), "%d", param.number);
                    strcpy(dst, buffer);
                    dst += strlen(buffer);
                }
                break;
                case SPTFormat:
                {
                    char buffer[32];
                    snprintf(buffer, sizeof(buffer), param.text, param.number);
                    strcpy(dst, buffer);
                    dst += strlen(buffer);
                }
                break;
                case SPTUnit:
                    if (param.unit)
                    {
                        if (mode == 1)
                        {
                            char buffer[32];
                            snprintf(buffer, sizeof(buffer), "%d", param.unit->ID());
                            strcpy(dst, buffer);
                            dst += strlen(buffer);
                        }
                        else if (mode == 2)
                        {
                            if (param.unit->ID() > 0 && param.unit->ID() <= MAX_UNITS_PER_GROUP)
                            {
                                RString word = LocalizeString(IDS_1_TO_LEADER + param.unit->ID() - 1);
                                strcpy(dst, word);
                                dst += word.GetLength();
                            }
                        }
                        else
                            Fail("Mode");
                    }
                    break;
                case SPTUnitList:
                {
                    PackedBoolArray list = PrepareList(param.grp, param.list, param.wholeCrew);
                    if (AllUnits(param.grp, list, param.wholeCrew))
                    {
                        strcpy(dst, LocalizeString(IDS_WORD_ALL));
                        dst += strlen(dst);
                    }
                    else
                    {
                        int team = WholeTeam(param.grp, list, param.wholeCrew);
                        if (team >= 0)
                        {
                            switch (team)
                            {
                                case TeamMain:
                                    strcpy(dst, LocalizeString(IDS_WORD_TEAM_MAIN));
                                    break;
                                case TeamRed:
                                    strcpy(dst, LocalizeString(IDS_WORD_TEAM_RED));
                                    break;
                                case TeamGreen:
                                    strcpy(dst, LocalizeString(IDS_WORD_TEAM_GREEN));
                                    break;
                                case TeamBlue:
                                    strcpy(dst, LocalizeString(IDS_WORD_TEAM_BLUE));
                                    break;
                                case TeamYellow:
                                    strcpy(dst, LocalizeString(IDS_WORD_TEAM_YELLOW));
                                    break;
                            }
                            dst += strlen(dst);
                        }
                        else if (mode == 1)
                        {
                            char buffer[256];
                            CreateUnitsList(list, buffer);
                            strcpy(dst, buffer);
                            dst += strlen(buffer);
                        }
                    }
                }
                break;
            }
        }
        else
        {
            *dst = c;
            src++;
            dst++;
        }
    }
    *dst = 0;

    return result;
}

const float coefAa11 = 100.0 / (256 * 50);
const float invCoefAa11 = 1.0 / coefAa11;

template <>
const EnumName* Foundation::GetEnumNames(RadioMessageType dummy)
{
    static const EnumName RadioMessageTypeNames[] = {EnumName(RMTCommand, "COMMAND"),
                                                     EnumName(RMTFormation, "FORM"),
                                                     EnumName(RMTSemaphore, "SEM"),
                                                     EnumName(RMTBehaviour, "BEHAVIOUR"),
                                                     EnumName(RMTOpenFire, "OPEN FIRE"),
                                                     EnumName(RMTCeaseFire, "CEASE FIRE"),
                                                     EnumName(RMTLooseFormation, "LOOSE FORM"),
                                                     EnumName(RMTReportStatus, "STATUS"),
                                                     EnumName(RMTTarget, "TARGET"),
                                                     EnumName(RMTGroupAnswer, "GRP ANS"),
                                                     EnumName(RMTSubgroupAnswer, "SUBGRP ANS"),
                                                     EnumName(RMTReturnToFormation, "RETURN TO FORM"),
                                                     EnumName(RMTUnitAnswer, "UNIT ANS"),
                                                     EnumName(RMTFireStatus, "FIRE STATUS"),
                                                     EnumName(RMTUnitKilled, "UNIT KILLED"),
                                                     EnumName(RMTText, "TEXT"),
                                                     EnumName(RMTReportTarget, "REPORT TARGET"),
                                                     EnumName(RMTObjectDestroyed, "OBJ DESTROYED"),
                                                     EnumName(RMTContact, "CONTACT"),
                                                     EnumName(RMTUnderFire, "UNDER FIRE"),
                                                     EnumName(RMTClear, "CLEAR"),
                                                     EnumName(RMTRepeatCommand, "REPEAT COMMAND"),
                                                     EnumName(RMTWhereAreYou, "WHERE ARE YOU"),
                                                     EnumName(RMTNotifyCommand, "NOTIFY"),
                                                     EnumName(RMTCommandConfirm, "CONFIRM"),
                                                     EnumName(RMTPosition, "POSITION"),
                                                     EnumName(RMTFormationPos, "FORM POS"),
                                                     EnumName(RMTTeam, "TEAM"),
                                                     EnumName(RMTAskSupply, "ASK SUPPLY"),
                                                     EnumName(RMTSupportAsk, "SUPPORT ASK"),
                                                     EnumName(RMTSupportConfirm, "SUPPORT CONFIRM"),
                                                     EnumName(RMTSupportReady, "SUPPORT READY"),
                                                     EnumName(RMTSupportDone, "SUPPORT DONE"),
                                                     EnumName(RMTJoin, "JOIN"),
                                                     EnumName(RMTJoinDone, "JOIN DONE"),
                                                     EnumName(RMTWatchAround, "WATCH AROUND"),
                                                     EnumName(RMTWatchDir, "WATCH DIR"),
                                                     EnumName(RMTWatchPos, "WATCH POS"),
                                                     EnumName(RMTWatchTgt, "WATCH TGT"),
                                                     EnumName(RMTWatchAuto, "WATCH AUTO"),
                                                     EnumName(RMTVehicleMove, "V MOVE"),
                                                     EnumName(RMTVehicleFire, "V FIRE"),
                                                     EnumName(RMTVehicleFormation, "V FORM"),
                                                     EnumName(RMTVehicleCeaseFire, "V CEASE FIRE"),
                                                     EnumName(RMTVehicleSimpleCommand, "V SIMPLE COMMAND"),
                                                     EnumName(RMTVehicleTarget, "V TARGET"),
                                                     EnumName(RMTVehicleLoad, "V LOAD"),
                                                     EnumName(RMTVehicleAzimut, "V AZIMUT"),
                                                     EnumName(RMTVehicleStopTurning, "V STOP TURNING"),
                                                     EnumName(RMTVehicleFireFailed, "V FIRE FAILED"),
                                                     EnumName()};
    return RadioMessageTypeNames;
}

template <>
const EnumName* Foundation::GetEnumNames(AI::Answer dummy)
{
    static const EnumName AnswerNames[] = {EnumName(AI::NoAnswer, "NO ANSWER"),
                                           EnumName(AI::StepCompleted, "STEP OK"),
                                           EnumName(AI::StepTimeOut, "STEP TIMEOUT"),
                                           EnumName(AI::UnitDestroyed, "DESTROYED"),
                                           EnumName(AI::HealthCritical, "HEALTH CRIT"),
                                           EnumName(AI::DammageCritical, "DAMM CRIT"),
                                           EnumName(AI::FuelCritical, "FUEL CRIT"),
                                           EnumName(AI::ReportPosition, "REPORT POS"),
                                           EnumName(AI::ReportSemaphore, "REPORT SEM"),
                                           EnumName(AI::AmmoCritical, "AMMO CRIT"),
                                           EnumName(AI::FuelLow, "FUEL LOW"),
                                           EnumName(AI::AmmoLow, "AMMO LOW"),
                                           EnumName(AI::IsLeader, "LEADER"),
                                           EnumName(AI::CommandCompleted, "COMM OK"),
                                           EnumName(AI::CommandFailed, "COMM FAIL"),
                                           EnumName(AI::SubgroupDestinationUnreacheable, "SUBGRP UNREACH"),
                                           EnumName(AI::MissionCompleted, "MISS OK"),
                                           EnumName(AI::MissionFailed, "MISS FAIL"),
                                           EnumName(AI::DestinationUnreacheable, "UNREACH"),
                                           EnumName(AI::GroupDestroyed, "GRP DESTROYED"),
                                           EnumName()};
    return AnswerNames;
}

template <>
const EnumName* Foundation::GetEnumNames(ReportSubject dummy)
{
    static const EnumName ReportSubjectNames[] = {EnumName(ReportNew, "NEW"), EnumName(ReportDestroy, "DESTROY"),
                                                  EnumName()};
    return ReportSubjectNames;
}

LSError ReportTargetInfo::Serialize(ParamArchive& ar)
{
    if (ar.IsLoading())
    {
        TargetId oldTarget;
        PARAM_CHECK(ar.SerializeRef("Target", oldTarget, 1))
        if (oldTarget)
        {
        }
    }
    return LSOK;
}

RadioMessage::RadioMessage()
{
    _transmitted = false;
    _playerMsg = false;
}

float RadioMessage::GetDuration() const
{
    return 1.0;
}

Speaker* RadioMessage::GetSpeaker() const
{
    AIUnit* sender = GetSender();
    return sender ? sender->GetSpeaker() : nullptr;
}

static RadioMessage* CreateRadioMessage(RadioMessageType type)
{
    if (type >= (RadioMessageType)RMTFirstVehicle)
    {
        return CreateVehicleMessage(type);
    }
    else
    {
        switch (type)
        {
            case RMTCommand:
                return new RadioMessageCommand();
            case RMTFormation:
                return new RadioMessageFormation();
            case RMTSemaphore:
                return new RadioMessageSemaphore();
            case RMTBehaviour:
                return new RadioMessageBehaviour();
            case RMTOpenFire:
                return new RadioMessageOpenFire();
            case RMTCeaseFire:
                return new RadioMessageCeaseFire();
            case RMTLooseFormation:
                return new RadioMessageLooseFormation();
            case RMTReportStatus:
                return new RadioMessageReportStatus();
            case RMTTarget:
                return new RadioMessageTarget();
            case RMTGroupAnswer:
                return new RadioMessageGroupAnswer();
            case RMTSubgroupAnswer:
                return new RadioMessageSubgroupAnswer();
            case RMTReturnToFormation:
                return new RadioMessageReturnToFormation();
            case RMTUnitAnswer:
                return new RadioMessageUnitAnswer();
            case RMTFireStatus:
                return new RadioMessageFireStatus();
            case RMTUnitKilled:
                return new RadioMessageUnitKilled();
            case RMTText:
                return new RadioMessageText();
            case RMTReportTarget:
                return new RadioMessageReportTarget();
            case RMTObjectDestroyed:
                return new RadioMessageObjectDestroyed();
            case RMTContact:
                return new RadioMessageContact();
            case RMTUnderFire:
                return new RadioMessageUnderFire();
            case RMTClear:
                return new RadioMessageClear();
            case RMTRepeatCommand:
                return new RadioMessageRepeatCommand();
            case RMTWhereAreYou:
                return new RadioMessageWhereAreYou();
            case RMTNotifyCommand:
                return new RadioMessageNotifyCommand();
            case RMTCommandConfirm:
                return new RadioMessageCommandConfirm();
            case RMTWatchAround:
                return new RadioMessageWatchAround();
            case RMTWatchDir:
                return new RadioMessageWatchDir();
            case RMTWatchPos:
                return new RadioMessageWatchPos();
            case RMTWatchTgt:
                return new RadioMessageWatchTgt();
            case RMTWatchAuto:
                return new RadioMessageWatchAuto();
            case RMTPosition:
                return new RadioMessageUnitPos();
            case RMTFormationPos:
                return new RadioMessageFormationPos();
            case RMTTeam:
                return new RadioMessageTeam();
            case RMTAskSupply:
                return new RadioMessageAskSupply();
            case RMTSupportAsk:
                return new RadioMessageSupportAsk();
            case RMTSupportConfirm:
                return new RadioMessageSupportConfirm();
            case RMTSupportReady:
                return new RadioMessageSupportReady();
            case RMTSupportDone:
                return new RadioMessageSupportDone();
            case RMTJoin:
                return new RadioMessageJoin();
            case RMTJoinDone:
                return new RadioMessageJoinDone();
        }
    }
    Fail("Message Type");
    return nullptr;
}

RadioMessage* RadioMessage::CreateObject(ParamArchive& ar)
{
    RadioMessageType type;
    if (ar.SerializeEnum("type", type, 1) != LSOK)
    {
        return nullptr;
    }
    return CreateRadioMessage(type);
}

LSError RadioMessage::Serialize(ParamArchive& ar)
{
    if (ar.IsSaving())
    {
        RadioMessageType type = (RadioMessageType)GetType();
        PARAM_CHECK(ar.SerializeEnum("type", type, 1))
    }
    PARAM_CHECK(ar.Serialize("language", _language, 1, English))
    PARAM_CHECK(ar.Serialize("priority", _priority, 1, 0))
    PARAM_CHECK(ar.Serialize("timeout", _timeOut, 1))
    PARAM_CHECK(ar.Serialize("playerMsg", _playerMsg, 1, false))
    return LSOK;
}

RadioMessage* RadioChannel::CreateMessage(int type)
{
    return CreateRadioMessage((RadioMessageType)type);
}

void RadioSentence::Say(RadioChannel* channel, Speaker* speaker)
{
    bool transmit = false;
    for (int i = 0; i < Size(); i++)
    {
        RString id = Get(i).id;
        if (id.GetLength() <= 0)
        {
            // log all words
            char buf[1024];
            *buf = 0;
            for (int s = 0; s < Size(); s++)
            {
                strncat(buf, "'", sizeof(buf) - strlen(buf) - 1);
                strncat(buf, Get(s).id, sizeof(buf) - strlen(buf) - 1);
                strncat(buf, "' ", sizeof(buf) - strlen(buf) - 1);
            }
            RptF("Empty word in sentence %s", buf);
            continue;
        }
        else if (stricmp(id, "xmit") == 0)
        {
            transmit = true;
        }
        else
        {
            channel->Say(speaker, Get(i).id, Get(i).pauseAfter, transmit);
            transmit = false;
        }
    }
}

void RadioChannel::Say(RString waveName, AIUnit* sender, RString senderName, RString player, float duration)
{
    if (!sender && senderName.GetLength() == 0)
    {
        return;
    }

    SoundPars pars;
    if (waveName[0] == '#')
    {
        GChatList.Add(_chatChannel, sender, Poseidon::BuildNetworkCustomRadioMenuText(RString(waveName + 1)), false,
                      false);

        pars.name = Poseidon::BuildNetworkCustomRadioSoundPath(player, RString(waveName + 1),
                                                               GetUserDirectory() + RString("sound\\"));
        pars.freq = 1;
        pars.freqRnd = 1;
        pars.vol = 1;
        pars.volRnd = 1;
    }
    else
    {
        const ParamEntry* cls = FindRadio(waveName, pars);

        if (cls)
        {
            if (sender)
            {
                GChatList.Add(_chatChannel, sender, *cls >> "title", false, false);
            }
            else
            {
                GChatList.Add(_chatChannel, Pars >> "CfgHQIdentities" >> senderName >> "name", *cls >> "title", false,
                              false);
            }
        }
    }

    if (pars.name.GetLength() > 0 && GWorld->GetAcceleratedTime() <= 1)
    {
        IWave* wave = GSoundScene->OpenAndPlayOnce2D(pars.name, 1, pars.freq, false);
        if (wave)
        {
            wave->SetKind(WaveSpeech);
        }
        if (!_saying)
        {
            // start queue
            _saying = wave;
            if (_saying)
            {
                _saying->Play(); // start playback (once)
            }
        }
        else
        {
            if (wave)
            {
                wave->Queue(_saying);
            }
        }
        _pauseAfterMessage = 0;
    }
    else
    {
        _pauseAfterMessage = duration;
    }

    if (sender)
    {
        _speaker = *sender->GetSpeaker();
    }
    else
    {
        const ParamEntry& cls = Pars >> "CfgHQIdentities" >> senderName;
        RString name = cls >> "speaker";
        float pitch = cls >> "pitch";
        Ref<BasicSpeaker> basicSpeaker = new BasicSpeaker(Pars >> "CfgVoice" >> name);
        _speaker = Speaker(basicSpeaker, pitch);
    }
    // start playing noise
    if (_saying)
    {
        if (_speaker.IsSpeakerValid() && _noiseType == RNRadio)
        {
            _noise = _speaker.SayNoPitch("loop", true);
        }
    }
}

bool IsSpeakingDirect(AIUnit* sender);
} // namespace Poseidon

bool IsSpeakingRadio(AIUnit* sender)
{
    using namespace Poseidon;
    if (!sender)
    {
        return false;
    }

    // gloabal channel
    const RadioMessage* msg = GWorld->GetRadio().GetActualMessage();
    if (msg && msg->GetSender() == sender)
    {
        return true;
    }

    // vehicle channel
    Transport* veh = sender->GetVehicleIn();
    if (veh)
    {
        msg = veh->GetRadio().GetActualMessage();
        if (msg && msg->GetSender() == sender)
        {
            return true;
        }
    }

    // group channel
    AIGroup* grp = sender->GetGroup();
    if (grp)
    {
        msg = grp->GetRadio().GetActualMessage();
        if (msg && msg->GetSender() == sender)
        {
            return true;
        }

        // side channel
        AICenter* center = grp->GetCenter();
        if (center)
        {
            msg = center->GetRadio().GetActualMessage();
            if (msg && msg->GetSender() == sender)
            {
                return true;
            }
        }
    }

    return false;
}
namespace Poseidon
{

void RadioChannel::SilentProcess()
{
    if (_actualMsg)
    {
        if (!_actualMsg->IsTransmitted())
        {
            _actualMsg->Transmitted(); // confirm xmit done
        }
        // SetTransmitted is needless
        _actualMsg = nullptr;
    }
    // FIX
    while (_messageQueue.Size() > 0)
    {
        Ref<RadioMessage> msg = _messageQueue[0];
        _messageQueue.Delete(0);
        if (msg->GetType() != RMTText)
        {
            msg->Transmitted();
        }
    }
}

void RadioChannel::NextMessage()
{
    if (_actualMsg)
    {
        if (!_actualMsg->IsTransmitted())
        {
            _actualMsg->Transmitted(); // confirm xmit done
        }
        // SetTransmitted is needless
        _actualMsg = nullptr;
    }

    while (_messageQueue.Size() > 0 && ((!_messageQueue[0]) || Glob.time > _messageQueue[0]->GetTimeOut()))
    {
        _messageQueue[0]->Canceled();
        _messageQueue.Delete(0);
    }

    if (_messageQueue.Size() <= 0)
    {
        return;
    }

    AIUnit* sender = _messageQueue[0]->GetSender();
    if (IsSpeakingRadio(sender) || IsSpeakingDirect(sender))
    {
        _pauseAfterMessage = 0; // check in next simulation step
        return;
    }

    _actualMsg = _messageQueue[0];
    _messageQueue.Delete(0);

    float pauseAfterMessage = 0;
    if ((!sender || sender != GWorld->FocusOn()) && _messageQueue.Size() <= 5)
    {
        pauseAfterMessage = GRandGen.Gauss(0.1, 1.0, 5.0);
    }

    if (_audible)
    {
        SentenceParams params;
        const char* sentence = _actualMsg->PrepareSentence(params);
        if (sentence)
        {
            const ParamEntry& clsSent = Res >> "RadioProtocol" >> sentence;
            float rnd = GRandGen.RandomValue();
            RString variant = SelectVariant(clsSent >> "versions", rnd);
            const ParamEntry& clsVariant = clsSent >> variant;

            RadioSentence sent;
            SaySentence(sent, _actualMsg->GetLanguage(), clsVariant, params, pauseAfterMessage);
            if (GWorld->GetAcceleratedTime() <= 1)
            {
                sent.Say(this, _actualMsg->GetSpeaker());
                _pauseAfterMessage = 0;
            }
            else
            {
                _pauseAfterMessage = _actualMsg->GetDuration();
            }

            RString text = WriteSentence(clsVariant >> "text", params);
            GChatList.Add(_chatChannel, _actualMsg->GetSender(), text, _actualMsg->IsPlayerMsg(), false);
            SendRadioChat(_chatChannel, _actualMsg->GetSender(), _object, text, sent);
        }
        else
        {
            RString wave = _actualMsg->GetWave();
            if (wave.GetLength() > 0)
            {
                Say(wave, _actualMsg->GetSender(), _actualMsg->GetSenderName(), "", _actualMsg->GetDuration());
            }
            else
            {
                _pauseAfterMessage = _actualMsg->GetDuration();
            }
            // Do not send over network - used as title effects etc.
            // - activated on each client
        }
    }
    else
    {
        _pauseAfterMessage = _actualMsg->GetDuration();
        if (GWorld->GetMode() == GModeNetware)
        {
            SentenceParams params;
            const char* sentence = _actualMsg->PrepareSentence(params);
            if (sentence)
            {
                const ParamEntry& clsSent = Res >> "RadioProtocol" >> sentence;
                float rnd = GRandGen.RandomValue();
                RString variant = SelectVariant(clsSent >> "versions", rnd);
                const ParamEntry& clsVariant = clsSent >> variant;

                RadioSentence sent;
                SaySentence(sent, _actualMsg->GetLanguage(), clsVariant, params, pauseAfterMessage);
                RString text = WriteSentence(clsVariant >> "text", params);

                SendRadioChat(_chatChannel, _actualMsg->GetSender(), _object, text, sent);
            }
            else
            {
                // Do not send over network - used as title effects etc.
                // - activated on each client
            }
        }
    }
}

bool UnitAlive(AIUnit* unit)
{
    // check if unit is alive
    // if it is, confirm it to leader
    if (!unit)
    {
        return false;
    }
    AIGroup* grp = unit->GetGroup();
    if (!grp)
    {
        return false;
    }
    if (unit->GetLifeState() != AIUnit::LSAlive)
    {
        return false;
    }
    grp->SetReportedDown(unit, false);
    grp->SetReportBeforeTime(unit, TIME_MAX);
    return true;
}

LSError RadioMessageUnitKilled::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("from", _from, 1))
    PARAM_CHECK(ar.SerializeRef("who", _who, 1))
    return LSOK;
}

const char* RadioMessageUnitKilled::GetPriorityClass()
{
    return "PriorityReport";
}

const char* RadioMessageUnitKilled::PrepareSentence(SentenceParams& params)
{
    if (!_from)
    {
        return nullptr;
    }
    if (!_who)
    {
        return nullptr;
    }
    if (_from->GetLifeState() != AIUnit::LSAlive)
    {
        return nullptr;
    }

    SetPlayerMsg(_from == GWorld->FocusOn());

    const char* sentence;

    sentence = "SentUnitKilled";
    params.Add(_who);
    params.Add(_from);

    return sentence;
}

void RadioMessageUnitKilled::Transmitted()
{
    // nothing to do
    if (!UnitAlive(_from))
    {
        return;
    }
    if (!_who)
    {
        return;
    }
    // remove unit from the subgroup and group
    if (_who->GetLifeState() == AIUnit::LSDead)
    {
        AISubgroup* subgrp = _who->GetSubgroup();
        subgrp->ReceiveAnswer(_who, AI::UnitDestroyed);
    }
    else
    {
        // force unit to report status
        // temporarily hide in group list (MIA status)
        AIGroup* grp = _who->GetGroup();
        if (grp && !_who->IsAnyPlayer() || _who->GetLifeState() != AIUnit::LSAlive)
        {
            grp->SetReportedDown(_who, true);
        }
        if (_who->GetLifeState() == AIUnit::LSAlive)
        {
            _who->ReportStatus();
        }
    }
}

LSError RadioMessageAskSupply::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("from", _from, 1))
    PARAM_CHECK(ar.SerializeEnum("message", _message, 1, Command::Heal))
    return LSOK;
}

const char* RadioMessageAskSupply::GetPriorityClass()
{
    return "NormalCommand";
}

void RadioMessageAskSupply::Transmitted() {}

const char* RadioMessageAskSupply::PrepareSentence(SentenceParams& params)
{
    return nullptr;
}

LSError RadioMessageSupportAsk::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("from", _from, 1))
    PARAM_CHECK(ar.SerializeEnum("type", _type, 1, (UIActionType)ATNone))
    return LSOK;
}

const char* RadioMessageSupportAsk::GetPriorityClass()
{
    return "NormalCommand";
}

void RadioMessageSupportAsk::Transmitted()
{
    AIUnit* sender = GetSender();
    if (!_from || !sender)
    {
        return;
    }

    AICenter* center = _from->GetCenter();
    AI_ERROR(center);
    center->AskSupport(_from, _type);
}

const char* RadioMessageSupportAsk::PrepareSentence(SentenceParams& params)
{
    AIUnit* sender = GetSender();
    if (!_from || !sender)
    {
        return nullptr;
    }

    Vector3Val pos = sender->Position();
    char buffer[6];
    PositionToAA11(pos, buffer);
    params.Add(RString(buffer));

    switch (_type)
    {
        case ATHeal:
            return "SentSupportAskHeal";
        case ATRepair:
            return "SentSupportAskRepair";
        case ATRearm:
            return "SentSupportAskRearm";
        case ATRefuel:
            return "SentSupportAskRefuel";
        default:
            Fail("Action");
            return nullptr;
    }
}

LSError RadioMessageSupportConfirm::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("from", _from, 1))
    return LSOK;
}

const char* RadioMessageSupportConfirm::GetPriorityClass()
{
    return "NormalCommand";
}

void RadioMessageSupportConfirm::Transmitted() {}

const char* RadioMessageSupportConfirm::PrepareSentence(SentenceParams& params)
{
    AIUnit* sender = GetSender();
    if (!_from || !sender)
    {
        return nullptr;
    }

    const AIGroup* group = _from->GetSupportedGroup();
    if (!group)
    {
        return nullptr;
    }
    params.AddWord("", group->GetName());

    Vector3Val pos = _from->GetSupportPos();
    char buffer[6];
    PositionToAA11(pos, buffer);
    params.Add(RString(buffer));

    return "SentSupportConfirm";
}

LSError RadioMessageSupportReady::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("from", _from, 1))
    return LSOK;
}

const char* RadioMessageSupportReady::GetPriorityClass()
{
    return "NormalCommand";
}

void RadioMessageSupportReady::Transmitted() {}

const char* RadioMessageSupportReady::PrepareSentence(SentenceParams& params)
{
    AIUnit* sender = GetSender();
    if (!_from || !sender)
    {
        return nullptr;
    }

    const AIGroup* group = _from->GetSupportedGroup();
    if (!group)
    {
        return nullptr;
    }
    params.AddWord("", group->GetName());

    Vector3Val pos = _from->GetSupportPos();
    char buffer[6];
    PositionToAA11(pos, buffer);
    params.Add(RString(buffer));

    return "SentSupportReady";
}

LSError RadioMessageSupportDone::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("from", _from, 1))
    return LSOK;
}

const char* RadioMessageSupportDone::GetPriorityClass()
{
    return "NormalCommand";
}

void RadioMessageSupportDone::Transmitted()
{
    AIUnit* sender = GetSender();
    if (!_from || !sender)
    {
        return;
    }

    AICenter* center = _from->GetCenter();
    AI_ERROR(center);
    center->SupportDone(_from);
}

const char* RadioMessageSupportDone::PrepareSentence(SentenceParams& params)
{
    AIUnit* sender = GetSender();
    if (!_from || !sender)
    {
        return nullptr;
    }

    return "SentSupportDone";
}

RadioMessageJoin::RadioMessageJoin(AIGroup* from, PackedBoolArray list)
{
    _from = from;
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (list.Get(i))
        {
            AIUnit* unit = from->UnitWithID(i + 1);
            if (unit)
            {
                _to.Add(unit);
            }
        }
    }
}

LSError RadioMessageJoin::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("From", _from, 1))
    PARAM_CHECK(ar.SerializeRefs("To", _to, 1))
    return LSOK;
}

const char* RadioMessageJoin::GetPriorityClass()
{
    return "NormalCommand";
}

bool RadioMessageJoin::IsTo(AIUnit* unit) const
{
    for (int i = 0; i < _to.Size(); i++)
    {
        AIUnit* u = _to[i];
        if (u == unit)
        {
            return true;
        }
    }
    return false;
}

const char* RadioMessageJoin::PrepareSentence(SentenceParams& params)
{
    AIUnit* sender = GetSender();
    if (!_from || !sender)
    {
        return nullptr;
    }

    _to.Compact();
    if (_to.Size() <= 0)
    {
        return nullptr;
    }

    SetPlayerMsg(IsTo(GWorld->FocusOn()));

    params.Add(_to, true);
    params.Add(_from->Leader());

    return "SentCmdFollow";
}

LSError RadioMessageJoinDone::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("From", _from, 1))
    PARAM_CHECK(ar.SerializeRef("To", _to, 1))
    return LSOK;
}

const char* RadioMessageJoinDone::GetPriorityClass()
{
    return "JoinCompleted";
}

const char* RadioMessageJoinDone::PrepareSentence(SentenceParams& params)
{
    if (!_from)
    {
        return nullptr;
    }

    SetPlayerMsg(_from == GWorld->FocusOn());

    params.Add(_from);
    return "SentJoinCompleted";
}

LSError RadioMessageUnitAnswer::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("From", _from, 1))
    PARAM_CHECK(ar.SerializeRef("To", _to, 1))
    PARAM_CHECK(ar.SerializeEnum("answer", _answer, 1))
    return LSOK;
}

const char* RadioMessageUnitAnswer::GetPriorityClass()
{
    switch (_answer)
    {
        case AI::HealthCritical:
        case AI::DammageCritical:
            return "CriticalReport";
        case AI::FuelCritical:
        case AI::AmmoCritical:
        case AI::IsLeader:
            return "PriorityReport";
        case AI::FuelLow:
        case AI::AmmoLow:
        case AI::ReportPosition:
        case AI::ReportSemaphore:
            return "Report";
        case AI::SubgroupDestinationUnreacheable:
            return "Failure";
        default:
            Fail("Answer");
            return "Default";
    }
}

const char* RadioMessageUnitAnswer::PrepareSentence(SentenceParams& params)
{
    if (!_from)
    {
        return nullptr;
    }
    if (!_to)
    {
        return nullptr;
    }

    if (_from->GetLifeState() != AIUnit::LSAlive)
    {
        return nullptr;
    }

    SetPlayerMsg(_from == GWorld->FocusOn());

    const char* sentence;

    params.Add(_from);
    switch (_answer)
    {
        case AI::HealthCritical:
            sentence = "SentHealthCritical";
            break;
        case AI::DammageCritical:
            sentence = "SentDammageCritical";
            break;
        case AI::FuelCritical:
            sentence = "SentFuelCritical";
            break;
        case AI::FuelLow:
            sentence = "SentFuelLow";
            break;
        case AI::AmmoCritical:
            sentence = "SentAmmoCritical";
            break;
        case AI::AmmoLow:
            sentence = "SentAmmoLow";
            break;
        case AI::ReportPosition:
        {
            Vector3Val pos = _from->Position();
            char buffer[6];
            PositionToAA11(pos, buffer);
            sentence = "SentReportPosition";
            params.Add(RString(buffer));
        }
        break;
        case AI::ReportSemaphore:
            return nullptr;
        case AI::IsLeader:
            sentence = "SentIsLeader";
            break;
        case AI::SubgroupDestinationUnreacheable:
            sentence = "SentDestinationUnreacheable";
            break;
        default:
            Fail("Answer");
            return nullptr;
    }

    return sentence;
}

void RadioMessageUnitAnswer::Transmitted()
{
    if (_to && _from)
    {
        if (UnitAlive(_from))
        {
            AIGroup* grp = _to->GetGroup();
            if (grp)
            {
                grp->SetReportedDown(_from, false);
                grp->SetReportBeforeTime(_from, TIME_MAX);
            }
            if (!_to->IsLocal())
            {
                AI_ERROR(_from->IsAnyPlayer());
                if (_from->IsAnyPlayer())
                {
                    GetNetworkManager().AskForReceiveUnitAnswer(_from, _to, _answer);
                }
                return;
            }
            _to->ReceiveAnswer(_from, _answer);
            if (_answer == AI::ReportPosition)
            {
                if (grp && grp->IsPlayerGroup())
                {
                    AddUnitInfo(_from);
                }
            }

            if (_answer == AI::ReportPosition || _answer == AI::IsLeader)
            {
                // show leader when report position or taking command takes place
                AIUnit* player = GWorld->FocusOn();
                AIGroup* pgrp = player ? player->GetGroup() : nullptr;
                if (pgrp && pgrp->Leader() == _from && GWorld->UI())
                {
                    GWorld->UI()->ShowFormPosition();
                }
                // unit reported - clear flag we are waiting for report
            }
        }
    }
}

LSError RadioMessageFireStatus::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("From", _from, 1))
    PARAM_CHECK(ar.Serialize("answer", _answer, 1, false))
    return LSOK;
}

const char* RadioMessageFireStatus::GetPriorityClass()
{
    return "Report";
}

const char* RadioMessageFireStatus::PrepareSentence(SentenceParams& params)
{
    if (!_from)
    {
        return nullptr;
    }

    SetPlayerMsg(_from == GWorld->FocusOn());

    if (_answer)
    {
        return "SentFireReady";
    }
    else
    {
        return "SentFireNegative";
    }
}

void RadioMessageFireStatus::Transmitted() {}

LSError RadioMessageSubgroupAnswer::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("From", _from, 1))
    PARAM_CHECK(ar.SerializeRef("Leader", _leader, 1))
    PARAM_CHECK(ar.SerializeRef("To", _to, 1))
    PARAM_CHECK(ar.SerializeEnum("answer", _answer, 1))
    PARAM_CHECK(ar.Serialize("cmd", _cmd, 2))
    PARAM_CHECK(ar.Serialize("display", _display, 1, false))
    PARAM_CHECK(ar.Serialize("forceLeader", _forceLeader, 1, false))
    return LSOK;
}

RadioMessageSubgroupAnswer::RadioMessageSubgroupAnswer() = default;

RadioMessageSubgroupAnswer::RadioMessageSubgroupAnswer(AISubgroup* from, AIGroup* to, AI::Answer answer, Command* cmd,
                                                       bool display, AIUnit* leader)
{
    _from = from;
    _leader = leader ? leader : from->Leader();
    _forceLeader = leader != nullptr;
    _to = to;
    _answer = answer;
    if (cmd)
    {
        _cmd = *cmd;
    }
    _display = display;
}

const char* RadioMessageSubgroupAnswer::GetPriorityClass()
{
    switch (_answer)
    {
        case AI::CommandCompleted:
            return "Completition";
        case AI::CommandFailed:
        case AI::SubgroupDestinationUnreacheable:
            return "Failure";
        default:
            Fail("Answer");
            return "Default";
    }
}

const char* RadioMessageSubgroupAnswer::PrepareSentence(SentenceParams& params)
{
    if (!_display)
    {
        return nullptr;
    }

    if (!_forceLeader || !_leader)
    {
        if (_from && _from->Leader())
        {
            _leader = _from->Leader();
        }
        if (!_leader)
        {
            return nullptr;
        }
    }

    if (_leader->GetLifeState() != AIUnit::LSAlive)
    {
        return nullptr;
    }

    const char* sentence;
    switch (_answer)
    {
        case AI::CommandCompleted:
            if (_cmd._message == Command::Attack || _cmd._message == Command::AttackAndFire)
            {
                return nullptr; // no message
            }
            else
            {
                sentence = "SentCommandCompleted";
            }
            break;
        case AI::CommandFailed:
            sentence = "SentCommandFailed";
            break;
        case AI::SubgroupDestinationUnreacheable:
            sentence = "SentDestinationUnreacheable";
            break;
        default:
            Fail("Answer");
            return nullptr;
    }

    params.Add(_leader);
    SetPlayerMsg(_leader == GWorld->FocusOn());

    return sentence;
}

AIUnit* RadioMessageSubgroupAnswer::GetSender() const
{
    if (_forceLeader && _leader)
    {
        return _leader;
    }
    return _from && _from->Leader() ? _from->Leader() : (AIUnit*)_leader;
}

void RadioMessageSubgroupAnswer::Transmitted()
{
    AIGroup* grp = _to;
    if (!grp)
    {
        return;
    }
    AIUnit* fromLeader = _leader;
    if (_from && _from->Leader())
    {
        fromLeader = _from->Leader();
    }
    if (!fromLeader)
    {
        return;
    }

    if (!UnitAlive(fromLeader))
    {
        return;
    }
    // if command was some kind of supply, reset supply status
    const Command& cmd = GetCommand();
    Command::Message msg = cmd._message;
    if (msg == Command::Action)
    {
        // get action ID
        switch (cmd._action)
        {
            case ATHeal:
                msg = Command::Heal;
                break;
            case ATRepair:
                msg = Command::Repair;
                break;
            case ATRefuel:
                msg = Command::Refuel;
                break;
            case ATRearm:
                msg = Command::Rearm;
                break;
            case ATTakeWeapon:
                msg = Command::Rearm;
                break;
            case ATTakeMagazine:
                msg = Command::Rearm;
                break;
        }
    }
    switch (msg)
    {
        case Command::Heal:
            grp->SetHealthStateReported(fromLeader, AIUnit::RSNormal);
            break;
        case Command::Rearm:
            grp->SetAmmoStateReported(fromLeader, AIUnit::RSNormal);
            break;
        case Command::Repair:
            grp->SetDammageStateReported(fromLeader, AIUnit::RSNormal);
            break;
        case Command::Refuel:
            grp->SetFuelStateReported(fromLeader, AIUnit::RSNormal);
            break;
        case Command::Action:
            // if it was action, it might have changed unit state
            break;
    }
    if (_from)
    {
        grp->ReceiveAnswer(_from, _answer);
    }
}

#define REPEAT_SENDER_AFTER 20.0

LSError RadioMessageGroupAnswer::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("From", _from, 1))
    PARAM_CHECK(ar.SerializeRef("To", _to, 1))
    PARAM_CHECK(ar.SerializeEnum("answer", _answer, 1))
    return LSOK;
}

const char* RadioMessageGroupAnswer::GetPriorityClass()
{
    return "Default";
}

const char* RadioMessageGroupAnswer::PrepareSentence(SentenceParams& params)
{
    return nullptr;
}

void RadioMessageGroupAnswer::Transmitted()
{
    if (_to)
    {
        _to->ReceiveAnswer(_from, _answer);
    }
}

const char* RadioMessageReturnToFormation::GetPriorityClass()
{
    return "NormalCommand";
}

LSError RadioMessageReturnToFormation::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("From", _from, 1))
    PARAM_CHECK(ar.SerializeRef("Leader", _leader, 1))
    PARAM_CHECK(ar.SerializeRef("To", _to, 1))
    return LSOK;
}

RadioMessageGroupAnswer::RadioMessageGroupAnswer(AIGroup* from, AICenter* to, AI::Answer answer)
{
    _from = from;
    _to = to;
    _answer = answer;
}
} // namespace Poseidon
