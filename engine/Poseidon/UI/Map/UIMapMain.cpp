#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/UI/Map/UIMapCommon.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/Version.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <SDL3/SDL_scancode.h>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <SDL3/SDL_keycode.h>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Terrain/Roads.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>
#include <Poseidon/Dev/Debug/DebugCheats.hpp>
#include <Poseidon/AI/AIRadio.hpp>
#include <Poseidon/UI/InGame/InGameUIImpl.hpp>
#include <float.h>
#include <limits.h>
#include <string.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using Poseidon::Foundation::EnumName;

using namespace Poseidon::Dev;

#define EXPOSURE_COEF 50.0F

using namespace Poseidon;
MainMapInfo GMainMapInfo;

CStaticMapMain::CStaticMapMain(ControlsContainer* parent, int idc, const ParamEntry& cls)
#if _ENABLE_CHEATS
    : CStaticMap(parent, idc, cls, 0.001, 0.5, 0.16)
#else
    : CStaticMap(parent, idc, cls, 0.05, 0.5, 0.16)
#endif
{
#if _ENABLE_CHEATS
    _showUnits = false;
    _showTargets = false;
    _showSensors = false;
    _showVariables = false;
#endif
    _activeMarker = -1;

    _infoCommand.Load(cls >> "Command");
    _colorActiveMarker = GetPackedColor(cls >> "ActiveMarker" >> "color");
    _sizeActiveMarker = cls >> "ActiveMarker" >> "size";

    _iconPosition = GEngine->TextBank()->Load(GetPictureName(Pars >> "CfgInGameUI" >> "Cursor" >> "mission"));

    GMainMapInfo.Clear();
}

namespace Poseidon
{
void SetCommandState(int id, CommandState state, AISubgroup* subgrp)
{
    AIGroup* grp = subgrp->GetGroup();
    for (int i = 0; i < GMainMapInfo.commands.Size();)
    {
        CommandInfo& cmd = GMainMapInfo.commands[i];
        if (cmd.grp == grp && cmd.id == id)
        {
            cmd.state = state;
            cmd.subgrp = subgrp;
            i++;
        }
        else if (cmd.subgrp == subgrp)
        {
            GMainMapInfo.commands.Delete(i);
        }
        else
        {
            i++;
        }
    }
}
} // namespace Poseidon

void AddUnitInfo(AIUnit* unit)
{
    if (!unit)
    {
        return;
    }
    DisplayMap* display = dynamic_cast<DisplayMainMap*>(GWorld->Map());
    if (!display)
    {
        return;
    }
    CStaticMapMain* map = display->GetMap();
    if (!map)
    {
        return;
    }
    map->AddUnitInfo(unit);
}

void AddEnemyInfo(TargetType* id, const VehicleType* type, int x, int z)
{
    if (!id)
    {
        return;
    }
    DisplayMap* display = dynamic_cast<DisplayMainMap*>(GWorld->Map());
    if (!display)
    {
        return;
    }
    CStaticMapMain* map = display->GetMap();
    if (!map)
    {
        return;
    }
    map->AddEnemyInfo(id, type, x, z);
}

void CStaticMapMain::AddUnitInfo(AIUnit* unit)
{
    PoseidonAssert(unit);
    int index = -1;
    for (int i = 0; i < GMainMapInfo.unitInfo.Size(); i++)
    {
        if (GMainMapInfo.unitInfo[i].unit == unit)
        {
            index = i;
            break;
        }
    }
    if (index < 0)
    {
        index = GMainMapInfo.unitInfo.Add();
    }
    MapUnitInfo& info = GMainMapInfo.unitInfo[index];
    info.unit = unit;
    info.time = Glob.clock.GetTimeOfDay();
    if (info.time >= 1.0)
    {
        info.time--;
    }
    info.time *= 24;
    float coef = 100.0 / (LandGrid * LandRange);
    Vector3Val pos = unit->Position();
    info.x = toIntFloor(coef * pos.X());
    info.z = toIntFloor(coef * pos.Z());
}

void CStaticMapMain::AddEnemyInfo(TargetType* id, const VehicleType* type, int x, int z)
{
    PoseidonAssert(id);
    int index = -1;
    for (int i = 0; i < GMainMapInfo.enemyInfo.Size(); i++)
    {
        if (GMainMapInfo.enemyInfo[i].id == id)
        {
            index = i;
            break;
        }
    }
    if (index < 0)
    {
        index = GMainMapInfo.enemyInfo.Add();
    }
    MapEnemyInfo& info = GMainMapInfo.enemyInfo[index];
    info.id = id;
    info.type = type;
    info.time = Glob.clock.GetTimeOfDay();
    if (info.time >= 1.0)
    {
        info.time--;
    }
    info.time *= 24;
    info.x = x;
    info.z = z;
}

template <>
const EnumName* Poseidon::Foundation::GetEnumNames(CommandState dummy)
{
    static const EnumName CommandStateNames[] = {EnumName(CSSent, "SENT"), EnumName(CSReceived, "RECEIVED"),
                                                 EnumName(CSSucceed, "SUCCEED"), EnumName(CSFailed, "FAILED"),
                                                 EnumName()};
    return CommandStateNames;
}

LSError CommandInfo::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.SerializeRef("Group", grp, 1))
    PARAM_CHECK(ar.Serialize("id", id, 1))
    PARAM_CHECK(ar.SerializeEnum("state", state, 1))
    PARAM_CHECK(ar.SerializeRef("Subgroup", subgrp, 1))
    PARAM_CHECK(ar.Serialize("position", position, 1))
    return LSOK;
}

LSError MapUnitInfo::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.SerializeRef("Unit", unit, 1))
    PARAM_CHECK(ar.Serialize("time", time, 1))
    PARAM_CHECK(ar.Serialize("x", x, 1))
    PARAM_CHECK(ar.Serialize("z", z, 1))
    return LSOK;
}

LSError MapEnemyInfo::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.SerializeRef("Target", id, 1))
    PARAM_CHECK(Poseidon::Serialize(ar, "type", type, 1))
    PARAM_CHECK(ar.Serialize("time", time, 1))
    PARAM_CHECK(ar.Serialize("x", x, 1))
    PARAM_CHECK(ar.Serialize("z", z, 1))
    return LSOK;
}

LSError MainMapInfo::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("Commands", commands, 1))
    PARAM_CHECK(ar.Serialize("UnitInfo", unitInfo, 1))
    PARAM_CHECK(ar.Serialize("EnemyInfo", enemyInfo, 1))
    return LSOK;
}

void MainMapInfo::Clear()
{
    unitInfo.Clear();
    enemyInfo.Clear();
    commands.Clear();
}

LSError SerializeMapInfo(ParamArchive& ar, RString name, int minVersion)
{
    PARAM_CHECK(ar.Serialize(name, GMainMapInfo, minVersion))
    return LSOK;
}

LSError CStaticMapMain::Serialize(ParamArchive& ar)
{
    return LSOK;
}

void CStaticMapMain::DrawCommands()
{
    for (int i = 0; i < GMainMapInfo.commands.Size();)
    {
        CommandInfo& cmd = GMainMapInfo.commands[i];
        if (cmd.state == CSFailed ||                       // command failed
            (cmd.state != CSSent && cmd.subgrp == nullptr) // subgroup destroyed
        )
        {
            GMainMapInfo.commands.Delete(i);
        }
        else
        {
            DrawCoord pt = WorldToScreen(cmd.position);
            DrawSign(_infoCommand.icon, _infoCommand.color, pt, _infoCommand.size, _infoCommand.size, 0);
            if (cmd.subgrp)
            {
                pt.x += 0.015;

                char buffer[256];
                PackedBoolArray list = cmd.subgrp->GetUnitsList();
                CreateUnitsList(list, buffer);
                float h = _size;
                float w = GEngine->GetTextWidth(_size, _font, buffer);

                // FIX clipping
                if (pt.x + w < _x || pt.y + 0.5 * h < _y || pt.x > _x + _w || pt.y - 0.5 * h > _y + _h)
                {
                    i++;
                    continue;
                }
                GEngine->DrawText(Point2DFloat(pt.x, pt.y - 0.5 * h), _size, Rect2DFloat(_x, _y, _w, _h), _font,
                                  _ftColor, buffer);
            }
            i++;
        }
    }
}

void CStaticMapMain::DrawMarkers()
{
    int n = markersMap.Size();
    for (int i = 0; i < n; i++)
    {
        ArcadeMarkerInfo& mInfo = markersMap[i];
        switch (mInfo.markerType)
        {
            case MTIcon:
            {
                if (mInfo.size == 0)
                {
                    break;
                }
                if (!mInfo.icon)
                {
                    break;
                }
                DrawSign(mInfo.icon, mInfo.color, mInfo.position, mInfo.size * mInfo.a, mInfo.size * mInfo.b,
                         mInfo.angle * (H_PI / 180.0), Localize(mInfo.text));
            }
            break;
            case MTRectangle:
                FillRectangle(mInfo.position, mInfo.a, mInfo.b, mInfo.angle, mInfo.color, mInfo.fill);
                break;
            case MTEllipse:
                FillEllipse(mInfo.position, mInfo.a, mInfo.b, mInfo.angle, mInfo.color, mInfo.fill);
                break;
        }
    }
}

void CStaticMapMain::DrawSensors()
{
    int n = sensorsMap.Size();
    for (int i = 0; i < n; i++)
    {
        Vehicle* veh = sensorsMap[i];
        Detector* det = dyn_cast<Detector>(veh);
        if (!det)
        {
            continue;
        }
        PackedColor color = _colorSensor;
        if (!det->IsActive())
        {
            color.SetA8(color.A8() / 2);
        }

        float angle = (180 / H_PI) * atan2(det->_sinAngle, det->_cosAngle);
        if (det->_rectangular)
        {
            DrawRectangle(det->Position(), det->_a, det->_b, angle, color);
        }
        else
        {
            DrawEllipse(det->Position(), det->_a, det->_b, angle, color);
        }
        DrawSign(_iconSensor, color, det->Position(), 16, 16, 0);
    }
}

DrawCoord CStaticMapMain::GetWaypointPosition(const ArcadeWaypointInfo& wInfo)
{
    // do not update waypoints position
    // waypoints are now only signs in map
    Point3 pos = wInfo.position;

    return WorldToScreen(pos);
}

void CStaticMapMain::DrawWaypoints(AICenter* center, AIGroup* myGroup)
{
#if _ENABLE_CHEATS
    if (_showUnits)
    {
        EntityAI* veh = dyn_cast<EntityAI>(GWorld->CameraOn());
        if (!veh)
            return;
        myGroup = veh->GetGroup();
        if (!myGroup)
            return;
        center = myGroup->GetCenter();
    }
#endif

    int index = INT_MAX;
    if (myGroup->GetCurrent())
    {
        index = myGroup->GetCurrent()->_fsm->Var(0);
    }

    int m = myGroup->NWaypoints();

#if _ENABLE_CHEATS
    if (!_showUnits)
#endif
    {
        int nshow = 0;
        for (int j = 1; j < m; j++)
        {
            const ArcadeWaypointInfo& wInfo = myGroup->GetWaypoint(j);
            bool wpShow = wInfo.showWP == ShowAlways || wInfo.showWP == ShowEasy && USER_CONFIG.easyMode;
            if (wpShow)
            {
                nshow++;
            }
        }
        if (nshow == 0)
        {
            return;
        }
    }

    // lines
    if (m > 1)
    {
        const ArcadeWaypointInfo& wInfo = myGroup->GetWaypoint(0);
        DrawCoord pt1 = GetWaypointPosition(wInfo);
        for (int j = 1; j < m; j++)
        {
            const ArcadeWaypointInfo& wInfo = myGroup->GetWaypoint(j);
            bool wpShow = wInfo.showWP == ShowAlways || wInfo.showWP == ShowEasy && USER_CONFIG.easyMode;
            if (
#if _ENABLE_CHEATS
                !_showUnits &&
#endif
                !wpShow)
            {
                continue;
            }
            // draw line
            DrawCoord pt2 = GetWaypointPosition(wInfo);
            GLOB_ENGINE->DrawLine(Line2DPixel(pt1.x * _wScreen, pt1.y * _hScreen, pt2.x * _wScreen, pt2.y * _hScreen),
                                  _colorActiveMission, _colorActiveMission, _clipRect);
            pt1 = pt2;
        }
    }

    // waypoints
    for (int j = 0; j < m; j++)
    {
        const ArcadeWaypointInfo& wInfo = myGroup->GetWaypoint(j);
        bool wpShow = wInfo.showWP == ShowAlways || wInfo.showWP == ShowEasy && USER_CONFIG.easyMode;
        if (j > 0 &&
#if _ENABLE_CHEATS
            !_showUnits &&
#endif
            !wpShow)
        {
            continue;
        }
        MapTypeInfo& info = j < index ? _infoWaypointCompleted : _infoWaypoint;
        DrawCoord pt = GetWaypointPosition(wInfo);
        DrawSign(info.icon, info.color, pt, info.size, info.size, 0);
        if (wInfo.HasEffect())
        {
            pt.x += 0.015;
            DrawSign(_iconCamera, _colorCamera, pt, 12, 12, 0);
        }
    }
}

void CStaticMapMain::DrawLabel(struct SignInfo& info, PackedColor color)
{
    AICenter* myCenter = GetMyCenter();

    RString text;
    Point3 pos;
    switch (info._type)
    {
        case signNone:
            break;
        case signUnit:
            if (info._unit)
            {
                text = info._unit->GetGroup()->GetName();
                pos = info._unit->Position();
            }
            break;
        case signVehicle:
            if (myCenter && info._id != nullptr)
            {
                const AITargetInfo* enemy = myCenter->FindTargetInfo(info._id);
                if (enemy)
                {
                    text = enemy->_type->GetDisplayName();
                    pos = enemy->_realPos;
                }
            }
            break;
        case signStatic:
            if (myCenter && info._id != nullptr)
            {
                const AITargetInfo* building = myCenter->FindTargetInfo(info._id);
                if (building)
                {
                    text = building->_type->GetDisplayName();
                    pos = building->_realPos;
                }
            }
            break;
        case signArcadeWaypoint:
            if (myCenter)
            {
                if (info._indexGroup < 0 || info._indexGroup >= myCenter->NGroups())
                {
                    break;
                }
                AIGroup* group = myCenter->GetGroup(info._indexGroup);
                if (group)
                {
                    PoseidonAssert(info._index < group->NWaypoints());
                    const ArcadeWaypointInfo& wInfo = group->GetWaypoint(info._index);
                    text = LocalizeString(IDS_AC_MOVE + wInfo.type - ACMOVE);
                    pos = wInfo.position;
                }
            }
            break;
        case signArcadeSensor:
        {
            Vehicle* veh = sensorsMap[info._index];
            Detector* det = dyn_cast<Detector>(veh);
            if (!det)
            {
                break;
            }
            pos = det->Position();
            if (det->GetText().GetLength() > 0)
            {
                text = det->GetText();
            }
            else
            {
                text = LocalizeString(IDS_SENSOR);
            }
            ArcadeSensorType type = (ArcadeSensorType)det->GetAction();
            if (type != ASTNone)
            {
                text = text + RString(" (") + LocalizeString(IDS_SENSORTYPE_NONE + type) + RString(")");
            }
        }
        break;
    }

    if (text.GetLength() > 0)
    {
        DrawCoord posMap = WorldToScreen(pos);

        // place label
        float w = GLOB_ENGINE->GetTextWidth(_sizeLabel, _fontLabel, text);
        float h = _sizeLabel;
        posMap.x -= 0.5 * w;
        posMap.y -= 0.01 + h;

        MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
        GLOB_ENGINE->Draw2D(mip, _colorLabelBackground,
                            Rect2DPixel(posMap.x * _wScreen, posMap.y * _hScreen, w * _wScreen, h * _hScreen),
                            _clipRect);

        GLOB_ENGINE->DrawText(posMap, _sizeLabel, Rect2DFloat(_x, _y, _w, _h), _fontLabel, color, text);
    }
}

void CStaticMapMain::DrawExposureEnemy(AICenter* center, float x, float y, float xStep, float yStep, int i, int j,
                                       int iStep, int jStep)
{
    Draw2DPars pars;
    int maxAlpha8 = 0;
    const float coef = 0.5 / (256 * EXPOSURE_COEF);

    float alpha = coef * center->GetExposureOptimistic(i, j);
    int alpha8TL = toIntFloor(alpha * 255);
    alpha = coef * center->GetExposureOptimistic(i + iStep, j);
    int alpha8TR = toIntFloor(alpha * 255);
    alpha = coef * center->GetExposureOptimistic(i, j + jStep);
    int alpha8BL = toIntFloor(alpha * 255);
    alpha = coef * center->GetExposureOptimistic(i + iStep, j + jStep);
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
        pars.SetColor(_colorExposureEnemy);
        // TL
        pars.colorTL.SetA8(alpha8TL);
        // TR
        pars.colorTR.SetA8(alpha8TR);
        // BL
        pars.colorBL.SetA8(alpha8BL);
        // BR
        pars.colorBR.SetA8(alpha8BR);

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

        // draw exposure text

        // draw text information about the field
        if (xStep > 0.05f)
        {
            float exposure = center->GetExposureOptimistic(i, j - 1);
            if (exposure > 0)
            {
                float ty = y;
                float tsize = xStep * 5 * _fontNames->Height();
                // float tsize = 0.2;
                ty += tsize;
                ty += tsize;
                ty += tsize;
                GEngine->DrawTextF(Point2DFloat(x, ty), tsize, Rect2DFloat(x, y, xStep, yStep), _fontNames, _colorNames,
                                   "enemy %.0f", exposure);
                ty += tsize;
            }
        }
    }
}

void CStaticMapMain::DrawExposureUnknown(AICenter* center, float x, float y, float xStep, float yStep, int i, int j,
                                         int iStep, int jStep)
{
    Draw2DPars pars;
    int maxAlpha8 = 0;
    const float coef = 0.5 / (256 * EXPOSURE_COEF);

    float alpha = coef * center->GetExposureUnknown(i, j);
    int alpha8TL = toIntFloor(alpha * 255);
    alpha = coef * center->GetExposureUnknown(i + iStep, j);
    int alpha8TR = toIntFloor(alpha * 255);
    alpha = coef * center->GetExposureUnknown(i, j + jStep);
    int alpha8BL = toIntFloor(alpha * 255);
    alpha = coef * center->GetExposureUnknown(i + iStep, j + jStep);
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
        pars.SetColor(_colorExposureUnknown);
        // TL
        pars.colorTL.SetA8(alpha8TL);
        // TR
        pars.colorTR.SetA8(alpha8TR);
        // BL
        pars.colorBL.SetA8(alpha8BL);
        // BR
        pars.colorBR.SetA8(alpha8BR);

        pars.vTL += 1;
        pars.uTR += 1;
        pars.vTR += 1;
        pars.uBR += 1;

        Rect2DPixel rect(x, y, xStep, yStep);

        pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
        pars.spec = NoZBuf | IsAlpha | IsAlphaFog | NoClamp;
        GLOB_ENGINE->Draw2D(pars, rect, _clipRect);
    }
}

inline PackedColor CostToColor(float cost)
{
    static const float int1 = 1.0;
    static const float invint1 = (1.0 / int1);
    static const float int2 = 4.0;
    static const float invint2 = (1.0 / int2);
    static const float int12 = int1 + int2;

    static const float alpha = 0.5;
    static const Color colorR(1, 0, 0, alpha);
    static const Color colorG(0, 1, 0, alpha);
    static const Color colorY(1, 1, 0, alpha);

    saturateMin(cost, int12);
    if (cost <= int1)
    {
        return PackedColor(colorY * (cost * invint1) + colorG * (1 - cost * invint1));
    }
    else
    {
        cost -= int1;
        return PackedColor(colorR * (cost * invint2) + colorY * (1 - cost * invint2));
    }
}

void CStaticMapMain::DrawCost(AISubgroup* subgroup, float invCost, float x, float y, float xStep, float yStep, int i,
                              int j, int iStep, int jStep)
{
    GeographyInfo geogr = GLOB_LAND->GetGeography(i, j - 1);

    Draw2DPars pars;
    pars.SetU(0, 0);
    pars.SetV(0, 0);
    pars.vTL += 1;
    pars.uTR += 1;
    pars.vTR += 1;
    pars.uBR += 1;

    float cost;
    if (iStep == 1 && jStep == -1)
    {
        // ALL
        cost = subgroup->GetFieldCost(i, j + jStep) * invCost;
        pars.SetColor(CostToColor(cost));
    }
    else
    {
        // TL
        cost = subgroup->GetFieldCost(i, j) * invCost;
        pars.colorTL = CostToColor(cost);
        // TR
        cost = subgroup->GetFieldCost(i + iStep, j) * invCost;
        pars.colorTR = CostToColor(cost);
        // BL
        cost = subgroup->GetFieldCost(i, j + jStep) * invCost;
        pars.colorBL = CostToColor(cost);
        // BR
        cost = subgroup->GetFieldCost(i + iStep, j + jStep) * invCost;
        pars.colorBR = CostToColor(cost);
    }

    pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(nullptr, 0, 0);
    pars.spec = NoZBuf | IsAlpha | IsAlphaFog | NoClamp;
    Rect2DPixel rect(x * _wScreen, y * _hScreen, xStep * _wScreen, yStep * _hScreen);
    GEngine->Draw2D(pars, rect, _clipRect);

    // draw text information about the field
    if (xStep > 0.05f)
    {
        float ty = y;
        float tsize = xStep * 5 * _fontNames->Height();
        // float tsize = 0.2;
        GEngine->DrawTextF(Point2DFloat(x, ty), tsize, Rect2DFloat(x, y, xStep, yStep), _fontNames, _colorNames,
                           "softObj %d", geogr.u.howManyObjects);
        ty += tsize;

        if (geogr.u.howManyHardObjects > 0)
        {
            GEngine->DrawTextF(Point2DFloat(x, ty), tsize, Rect2DFloat(x, y, xStep, yStep), _fontNames, _colorNames,
                               "hardObj %d", geogr.u.howManyHardObjects);
            ty += tsize;
        }

        GEngine->DrawTextF(Point2DFloat(x, ty), tsize, Rect2DFloat(x, y, xStep, yStep), _fontNames, _colorNames,
                           "grad %d", geogr.u.gradient);
        ty += tsize;

        if (geogr.u.forestOuter)
        {
            GEngine->DrawText(Point2DFloat(x, ty), tsize, Rect2DFloat(x, y, xStep, yStep), _fontNames, _colorNames,
                              "Outer forest");
            ty += tsize;
        }

        if (geogr.u.forestInner)
        {
            GEngine->DrawText(Point2DFloat(x, ty), tsize, Rect2DFloat(x, y, xStep, yStep), _fontNames, _colorNames,
                              "Inner forest");
            ty += tsize;
        }

        if (geogr.u.road)
        {
            GEngine->DrawText(Point2DFloat(x, ty), tsize, Rect2DFloat(x, y, xStep, yStep), _fontNames, _colorNames,
                              "Road");
            ty += tsize;
        }
        if (geogr.u.track)
        {
            GEngine->DrawText(Point2DFloat(x, ty), tsize, Rect2DFloat(x, y, xStep, yStep), _fontNames, _colorNames,
                              "Track");
            ty += tsize;
        }

        if (geogr.u.waterDepth > 0)
        {
            GEngine->DrawTextF(Point2DFloat(x, ty), tsize, Rect2DFloat(x, y, xStep, yStep), _fontNames, _colorNames,
                               "Water %d", geogr.u.waterDepth);
            ty += tsize;
        }
        if (geogr.u.full)
        {
            GEngine->DrawText(Point2DFloat(x, ty), tsize, Rect2DFloat(x, y, xStep, yStep), _fontNames, _colorNames,
                              "Square full");
            ty += tsize;
        }
    }

    if (iStep == 1 && jStep == -1)
    {
        RoadList& roadList = GRoadNet->GetRoadList(i, j - 1);
        int i, j, n = roadList.Size();
        if (n > 0)
        {
            bool found = false;
            for (i = 0; i < n; i++)
            {
                RoadLink* item = roadList[i];
                for (j = 0; j < item->NConnections(); j++)
                {
                    if (!item->Connections()[j])
                    {
                        found = true;
                        break;
                    }
                }
                if (found)
                {
                    break;
                }
            }
            PackedColor color = PackedColor(found ? Color(1, .2, .2, 1) : Color(1, 1, 1, 1));
            GLOB_ENGINE->DrawLine(
                Line2DPixel(x * _wScreen, y * _hScreen, (x + xStep) * _wScreen, (y + yStep) * _hScreen), color, color,
                _clipRect);
            GLOB_ENGINE->DrawLine(
                Line2DPixel(x * _wScreen, (y + yStep) * _hScreen, (x + xStep) * _wScreen, y * _hScreen), color, color,
                _clipRect);
        }
    }
}

void CStaticMapMain::DrawInfo()
{
    if (_scaleX > 0.2)
    {
        return;
    }

    float coef = 100.0;   // width of field in global coord.
    float invCoef = 0.01; // 1.0 / coef;

    int xFrom = toIntFloor(-_mapX * _scaleX * coef);
    int zFrom = toIntFloor((_invScaleY - (_h - _mapY)) * _scaleY * coef);
    int xTo = toIntFloor((_w - _mapX) * _scaleX * coef);
    int zTo = toIntFloor((_invScaleY - (-_mapY)) * _scaleY * coef);

    float xBeg = _x + _mapX + xFrom * _invScaleX * invCoef;
    float zBeg = _y + _mapY + (_invScaleY - zFrom * _invScaleY * invCoef);
    float xFld = _invScaleX * invCoef;
    float zFld = _invScaleY * invCoef;

    float zAct = zBeg;
    for (int z = zFrom; z <= zTo; z++)
    {
        float xAct = xBeg;
        for (int x = xFrom; x <= xTo; x++)
        {
            DrawInfo(xAct, zAct - zFld, xFld, zFld, x, z);
            xAct += xFld;
        }
        zAct -= zFld;
    }
}

void CStaticMapMain::DrawInfo(float x, float y, float xStep, float yStep, int i, int j)
{
    static const int maxSubgroups = 3;
    static const int maxEnemies = 3;

    float tsize = xStep * 6 * _fontNames->Height();
    float ty = y + 0.5 * (yStep - (maxSubgroups + 1) * tsize);

    int indices[maxEnemies];
    for (int l = 0; l < maxEnemies; l++)
    {
        indices[l] = -1;
    }
    for (int k = 0; k < GMainMapInfo.enemyInfo.Size(); k++)
    {
        MapEnemyInfo& info = GMainMapInfo.enemyInfo[k];
        if (info.x != i || info.z != j)
        {
            continue;
        }
        for (int l = 0; l < maxEnemies; l++)
        {
            if (indices[l] < 0)
            {
                indices[l] = k;
                break;
            }
            else
            {
                float kCost = info.type->GetCost();
                float lCost = GMainMapInfo.enemyInfo[indices[l]].type->GetCost();
                if (kCost >= lCost)
                {
                    for (int i = maxEnemies - 1; i > l; i--)
                    {
                        indices[i] = indices[i - 1];
                    }
                    indices[l] = k;
                    break;
                }
            }
        }
    }
    if (indices[0] >= 0)
    {
        float left = x;
        float maxTime = 0;
        for (int l = 0; l < maxEnemies; l++)
        {
            int k = indices[l];
            if (k < 0)
            {
                break;
            }
            MapEnemyInfo& info = GMainMapInfo.enemyInfo[k];
            saturateMax(maxTime, info.time);
        }
        int hour = toIntFloor(maxTime);
        int minute = toIntFloor(60.0 * (maxTime - hour));
        GEngine->DrawTextF(Point2DFloat(x, ty), tsize, Rect2DFloat(x, y, xStep, yStep), _fontNames, _colorEnemy,
                           "% 2d:%02d", hour, minute);
        left += GEngine->GetTextWidthF(tsize, _fontNames, "% 2d:%02d", hour, minute);
        float height = tsize;
        float width = height * (_hScreen / _wScreen);
        for (int l = 0; l < maxEnemies; l++)
        {
            int k = indices[l];
            if (k < 0)
            {
                break;
            }
            MapEnemyInfo& info = GMainMapInfo.enemyInfo[k];
            Texture* texture = info.type->GetIcon();
            MipInfo mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
            GEngine->Draw2D(mip, _colorEnemy,
                            Rect2DPixel(left * _wScreen, ty * _hScreen, width * _wScreen, height * _hScreen),
                            Rect2DPixel(x * _wScreen, y * _hScreen, xStep * _wScreen, yStep * _hScreen));
            left += width;
        }
        ty += height;
    }

    // newest infos first
    int n = 0;
    for (int k = GMainMapInfo.unitInfo.Size() - 1; k >= 0; k--)
    {
        MapUnitInfo& info = GMainMapInfo.unitInfo[k];
        AIUnit* unit = info.unit;
        if (!unit)
        {
            GMainMapInfo.unitInfo.Delete(k);
            continue;
        }
        if (info.x != i || info.z != j)
        {
            continue;
        }
        int hour = toIntFloor(info.time);
        int minute = toIntFloor(60.0 * (info.time - hour));
        GEngine->DrawTextF(Point2DFloat(x, ty), tsize, Rect2DFloat(x, y, xStep, yStep), _fontNames, _colorFriendly,
                           "% 2d:%02d %d", hour, minute, unit->ID());
        ty += tsize;
        n++;
        if (n >= maxSubgroups)
        {
            break;
        }
    }
}

AIUnit* CStaticMapMain::GetMyUnit()
{
    return GLOB_WORLD->FocusOn();
}

AICenter* CStaticMapMain::GetMyCenter()
{
    AIUnit* me = GetMyUnit();
    AIGroup* myGroup = me ? me->GetGroup() : nullptr;
    AICenter* myCenter = myGroup ? myGroup->GetCenter() : nullptr;

    if (myCenter)
    {
        return myCenter;
    }

    switch (Glob.header.playerSide)
    {
        case TEast:
            return GLOB_WORLD->GetEastCenter();
        case TWest:
            return GLOB_WORLD->GetWestCenter();
        case TGuerrila:
            return GLOB_WORLD->GetGuerrilaCenter();
        default:
            Fail("Side");
            return nullptr;
    }
}

void CStaticMapMain::DrawExposure()
{
#if _ENABLE_CHEATS
    if (!_show)
        return;
#endif

    AICenter* myCenter = GetMyCenter();
    if (!myCenter)
    {
        return;
    }

    float invLandRange = 1.0 / LandRange;
    float ptsLand = 800.0 * _invScaleX * invLandRange; // avoid dependece on video resolution
    float invPtsLand = 1.0 / ptsLand;

    int iStep = toIntCeil(ptsPerSquareExp * invPtsLand);
    float xStep = iStep * invLandRange * _invScaleX;
    int jStep = iStep;
    float yStep = jStep * invLandRange * _invScaleY;
    int iMin = iStep * toIntFloor(-_mapX / xStep);
    float xMin = _x - fmod(-_mapX, xStep);
    int jMin = LandRange - jStep * toIntFloor(-_mapY / yStep);
    float yMin = _y - fmod(-_mapY, yStep);

    float y = yMin;
    int j = jMin;
    while (j >= 0 && y <= _y + _h)
    {
        float x = xMin;
        int i = iMin;
        while (i < LandRange && x <= _x + _w)
        {
            DrawExposureEnemy(myCenter, x, y, xStep, yStep, i, j, iStep, -jStep);
            DrawExposureUnknown(myCenter, x, y, xStep, yStep, i, j, iStep, -jStep);
            i += iStep;
            x += xStep;
        }
        j -= jStep;
        y += yStep;
    }
}

void CStaticMapMain::DrawCost()
{
    AIUnit* me = GetMyUnit();
    if (!me)
    {
        return;
    }
    AISubgroup* mySubgroup = me->GetSubgroup();
    if (!mySubgroup)
    {
        return;
    }

    GeographyInfo geogr;
    geogr.packed = 0;
    float costNormal = -FLT_MAX;
    for (int i = 0; i < mySubgroup->NUnits(); i++)
    {
        AIUnit* unit = mySubgroup->GetUnit(i);
        if (!unit || !unit->IsUnit())
        {
            continue;
        }
        float cost = unit->GetVehicle()->GetCost(geogr) * unit->GetVehicle()->GetFieldCost(geogr);
        if (cost > costNormal)
        {
            costNormal = cost;
        }
    }
    float invCost = 1.0 / (costNormal * LandGrid);

    float invLandRange = 1.0 / LandRange;
    float ptsLand = 800.0 * _invScaleX * invLandRange; // avoid dependece on video resolution
    float invPtsLand = 1.0 / ptsLand;

    int iStep = toIntCeil(ptsPerSquareCost * invPtsLand);
    float xStep = iStep * invLandRange * _invScaleX;
    int jStep = iStep;
    float yStep = jStep * invLandRange * _invScaleY;
    int iMin = iStep * toIntFloor(-_mapX / xStep);
    float xMin = _x - fmod(-_mapX, xStep);
    int jMin = LandRange - jStep * toIntFloor(-_mapY / yStep);
    float yMin = _y - fmod(-_mapY, yStep);

    float y = yMin;
    int j = jMin;
    while (j >= 0 && y <= _y + _h)
    {
        float x = xMin;
        int i = iMin;
        while (i < LandRange && x <= _x + _w)
        {
            // get geography info
            DrawCost(mySubgroup, invCost, x, y, xStep, yStep, i, j, iStep, -jStep);
            i += iStep;
            x += xStep;
        }
        j -= jStep;
        y += yStep;
    }
}

void CStaticMapMain::DrawUnits(AICenter* center)
{
    if (!center)
    {
        return;
    }
    AICenter* myCenter = GetMyCenter();
    if (!myCenter)
    {
        return;
    }

    PackedColor colorAlive;
    PackedColor colorDead;
    TargetSide side = center->GetSide();
    if (myCenter->IsFriendly(side))
    {
        colorAlive = _colorFriendly;
        colorDead = PackedColor(Color(0, 1, 1));
    }
    else if (myCenter->IsEnemy(side))
    {
        colorAlive = _colorEnemy;
        colorDead = PackedColor(Color(1, 1, 0));
    }
    else if (myCenter->IsNeutral(side))
    {
        colorAlive = _colorNeutral;
        colorDead = PackedColor(Color(1, 0, 1));
    }
    else
    {
        colorAlive = _colorUnknown;
        colorDead = PackedColor(Color(1, 0, 1));
    }

    for (int i = 0; i < center->NGroups(); i++)
    {
        AIGroup* grp = center->GetGroup(i);
        if (!grp)
        {
            continue;
        }
        for (int j = 0; j < MAX_UNITS_PER_GROUP; j++)
        {
            AIUnit* unit = grp->UnitWithID(j + 1);
            if (!unit)
            {
                continue;
            }
            int size = 10;
            if (unit->IsGroupLeader())
            {
                size = 16;
            }
            PackedColor color = colorAlive;
            if (unit->GetLifeState() != AIUnit::LSAlive)
            {
                color = colorDead;
            }
            Vector3 dir = unit->GetVehicle()->Direction();
            float azimut = atan2(dir.X(), dir.Z());
            DrawSign(unit->GetVehicle()->GetIcon(), color, unit->Position(), size, size, azimut);
        }
    }
}

void CStaticMapMain::DrawExt(float alpha)
{
    CStaticMap::DrawExt(alpha);

#if _ENABLE_CHEATS
    if (!_show)
        return;

    if (_showUnits || _showTargets)
        DrawExposure();
    if (CHECK_DIAG(DECostMap))
        DrawCost();
#endif

    AIUnit* me = GetMyUnit();
    AIGroup* myGroup = me ? me->GetGroup() : nullptr;
    AICenter* myCenter = GetMyCenter();
    if (!myCenter)
    {
        return;
    }

    DrawCommands();
    DrawMarkers();
#if _ENABLE_CHEATS
    if (_showSensors)
        DrawSensors();
#endif
    if (myGroup)
    {
        DrawWaypoints(myCenter, myGroup);
    }

    // Dev-panel show-all-units cheat — independent of the legacy
    // _showUnits (which is _ENABLE_CHEATS-only).  Draws every AICenter
    // unconditionally so the player sees ALL sides regardless of
    // fog-of-war / side filtering.
    if (DebugCheats::Cmd_ShowAllUnits::IsActive())
    {
        DrawUnits(GWorld->GetWestCenter());
        DrawUnits(GWorld->GetEastCenter());
        DrawUnits(GWorld->GetGuerrilaCenter());
        DrawUnits(GWorld->GetCivilianCenter());
        DrawUnits(GWorld->GetLogicCenter());
    }
#if _ENABLE_CHEATS
    if (_showUnits)
    {
        DrawUnits(GWorld->GetWestCenter());
        DrawUnits(GWorld->GetEastCenter());
        DrawUnits(GWorld->GetGuerrilaCenter());
        DrawUnits(GWorld->GetCivilianCenter());
        DrawUnits(GWorld->GetLogicCenter());
    }
    else if (_showTargets)
    {
        // draw targets
        int n = myCenter->NTargets();
        for (int i = 0; i < n; i++)
        {
            const AITargetInfo& info = myCenter->GetTarget(i);
            if (info._vanished)
                continue;
            Object* objTgt = info._idExact;
            EntityAI* veh = dyn_cast<EntityAI>(objTgt);
            if (!veh)
                continue;
            // const VehicleType *type = veh->GetType(info.FadingTypeAccuracy());
            // TargetSide side = veh->GetTargetSide(info.FadingSideAccuracy());
            const VehicleType* type = info._type;
            TargetSide side = info._destroyed ? TCivilian : info._side;

            // if (info._disappeared) side = TCivilian;

            int size = 16;
            int alpha8 = 255;
            if (!type->IsKindOf(GLOB_WORLD->Preloaded(VTypeStrategic)))
            {
                if (!type->IsKindOf(GLOB_WORLD->Preloaded(VTypeAllVehicles)))
                    continue;
                float alpha = info.FadingPositionAccuracy();
                if (alpha < 0.1)
                    continue;
                alpha8 = toIntFloor(alpha * 255);
                if (alpha8 < 0)
                    alpha8 = 0;
                if (alpha8 > 255)
                    alpha8 = 255;
            }

            PackedColor color;
            if (side == TCivilian)
                color = _colorCivilian;
            else if (myCenter->IsFriendly(side))
                color = _colorFriendly;
            else if (myCenter->IsEnemy(side))
                color = _colorEnemy;
            else if (myCenter->IsNeutral(side))
                color = _colorNeutral;
            else
                color = _colorUnknown;
            color.SetA8(alpha8);

            float azimut = atan2(info._dir.X(), info._dir.Z());
            DrawSign(type->GetIcon(), color, info._realPos, size, size, azimut);
        }
    }
    else
#endif
        if (USER_CONFIG.IsEnabled(DTMap))
    {
        if (myGroup)
        {
            int n = myGroup->GetTargetList().Size();
            for (int i = 0; i < n; i++)
            {
                const Target* target = myGroup->GetTargetList()[i];
                if (!target->IsKnownBy(me) || target->vanished || target->destroyed)
                {
                    continue;
                }

                const VehicleType* type = target->type;
                if (!type->IsKindOf(GWorld->Preloaded(VTypeAllVehicles)) &&
                    !type->IsKindOf(GWorld->Preloaded(VTypeStrategic)))
                {
                    continue;
                }

                // visibility
                float visible = target->FadingSpotability();
                if (visible <= 0.01)
                {
                    continue;
                }
                saturateMin(visible, 1);

                // color
                TargetSide side = target->side;
                PackedColor color;
                if (side == TCivilian)
                {
                    color = _colorCivilian;
                }
                else if (myCenter->IsFriendly(side))
                {
                    color = _colorFriendly;
                }
                else if (myCenter->IsEnemy(side))
                {
                    color = _colorEnemy;
                }
                else if (myCenter->IsNeutral(side))
                {
                    color = _colorNeutral;
                }
                else
                {
                    color = _colorUnknown;
                }
                color.SetA8(toIntFloor(color.A8() * visible));

                int size = 16;
                Vector3 pos = target->AimingPosition();
                Vector3 dir = VZero; // ???
                float azimut = atan2(dir.X(), dir.Z());
                DrawSign(target->type->GetIcon(), color, target->position, size, size, azimut);
            }
        }
    }
    else
    {
        DrawInfo();
    }

    // draw selected units
#if _ENABLE_CHEATS
    if (_showUnits && myGroup && me == myGroup->Leader() || USER_CONFIG.IsEnabled(DTMap))
    {
        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            AIUnit* unit = Poseidon::GetSelectedUnit(i);
            if (!unit)
                continue;
            Point3 pos = unit->Position();
            DrawCoord posMap = WorldToScreen(pos);
            bool draw = DrawSign(_iconSelect, _colorSelect, posMap, 16, 16, 0);
            if (draw)
            {
                float x = posMap.x + 0.015;
                float y = posMap.y - 0.015;
                GLOB_ENGINE->DrawTextF(Point2DFloat(x, y), _sizeUnits, _fontUnits, _colorSelect, "%d", i + 1);
            }
        }
    }
#endif

    // draw myself
    if (
#if _ENABLE_CHEATS
        _showUnits ||
#endif
        USER_CONFIG.IsEnabled(DTMap))
    {
        Object* myVehicle = nullptr;
        if (me)
        {
            myVehicle = me->GetVehicle();
        }
        else if (GLOB_WORLD->CameraOn())
        {
            myVehicle = GLOB_WORLD->CameraOn();
        }

        if (myVehicle)
        {
            Point3 pos = myVehicle->Position();
            bool draw = DrawSign(_iconPlayer, _colorMe, pos, 16, 16, 0);
            if (draw)
            {
                // draw orientation
                Vector3 dir = myVehicle->Direction();
                dir[1] = 0;
                dir.Normalize();
                DrawCoord ptB = WorldToScreen(pos);
                DrawCoord ptE;
                ptE.x = ptB.x + 0.02 * dir.X();
                ptE.y = ptB.y - 0.02 * dir.Z();
                GLOB_ENGINE->DrawLine(
                    Line2DPixel(ptB.x * _wScreen, ptB.y * _hScreen, ptE.x * _wScreen, ptE.y * _hScreen), _colorMe,
                    _colorMe, _clipRect);
            }
        }
    }
    else if (me)
    {
        const VehicleType* type = me->GetVehicle()->GetType();
        if (type->IsKindOf(GWorld->Preloaded(VTypeAir)) || type->IsKindOf(GWorld->Preloaded(VTypeTank)))
        {
            PackedColor color = PackedColor(Color(0, 0, 0, 1));
            const float h1 = 4.0 * _hScreen * (1.0 / 480);
            const float w1 = 4.0 * _wScreen * (1.0 / 640);
            const float h2 = 8.0 * _hScreen * (1.0 / 480);
            const float w2 = 8.0 * _wScreen * (1.0 / 640);
            DrawCoord pt = WorldToScreen(me->Position());
            pt.x *= _wScreen;
            pt.y *= _hScreen;
            GEngine->DrawLine(Line2DPixel(_clipRect.x, pt.y, pt.x - w1, pt.y), color, color, _clipRect);
            GEngine->DrawLine(Line2DPixel(pt.x + w1, pt.y, _clipRect.x + _clipRect.w, pt.y), color, color, _clipRect);
            GEngine->DrawLine(Line2DPixel(pt.x, _clipRect.y, pt.x, pt.y - h1), color, color, _clipRect);
            GEngine->DrawLine(Line2DPixel(pt.x, pt.y + h1, pt.x, _clipRect.y + _clipRect.h), color, color, _clipRect);
            MipInfo mip = GEngine->TextBank()->UseMipmap(_iconPosition, 0, 0);
            GEngine->Draw2D(mip, color, Rect2DPixel(toInt(pt.x - w2) + 0.5, toInt(pt.y - h2) + 0.5, 2 * w2, 2 * h2));
        }
    }

    //	if (_showTargets || _showUnits || USER_CONFIG.easyMode)
    {
        DrawLabel(_infoMove, _colorInfoMove);
    }
#if _ENABLE_CHEATS
    if (_showUnits || _showTargets)
    {
        // draw labels
        // draw my path
        AIUnit* myPilot = me ? me->GetVehicle()->PilotUnit() : nullptr;
        if (myPilot)
        {
            int n = myPilot->GetPlanner().GetPlanSize();
            int m = myPilot->GetPlanner().FindBestIndex(me->Position());
            PackedColor color = PackedColor(Color(0, 1, 1));
            for (int i = 0; i < m; i++)
            {
                Point3 pos;
                myPilot->GetPlanner().GetPlanPosition(i, pos);
                DrawSign(nullptr, color, pos, 2, 2, 0);
            }
            for (int i = m; i < n; i++)
            {
                Point3 pos;
                myPilot->GetPlanner().GetPlanPosition(i, pos);
                DrawSign(nullptr, _colorPath, pos, 2, 2, 0);
            }
        }

        int n = sensorsMap.Size();
        for (int i = 0; i < n; i++)
        {
            Vehicle* veh = sensorsMap[i];
            if (!veh)
                continue;
            Detector* sensor = dyn_cast<Detector>(veh);
            PoseidonAssert(sensor);
            if (!sensor)
                continue;
            if (!sensor->IsCountdown())
                continue;
            GLOB_ENGINE->DrawTextF(Point2DFloat(_x, _y), 1.5 * _font->Height(), _font, _ftColor, "COUNTDOWN: %.0f",
                                   sensor->GetCountdown());
        }

        // draw camera positions of multiplayer players
        const AutoArray<PlayerIdentity>* identities = GetNetworkManager().GetIdentities();
        if (identities)
            for (int i = 0; i < identities->Size(); i++)
            {
                int dpnid = identities->Get(i).dpnid;
                Vector3 pos = GetNetworkManager().GetCameraPosition(dpnid);
                if (pos.SquareSize() > 0.01)
                {
                    DrawSign(nullptr, PackedColor(Color(1, 0.8, 0, 1)), pos, 2, 2, 0);
                }
            }
    }
#endif

    if (_activeMarker >= 0)
    {
        const float rectH2 = 0.5 * _sizeActiveMarker * _hScreen * (1.0 / 480);
        const float rectW2 = 0.5 * _sizeActiveMarker * _wScreen * (1.0 / 640);
        ArcadeMarkerInfo& mInfo = markersMap[_activeMarker];
        DrawCoord pt = WorldToScreen(mInfo.position);
        pt.x *= _wScreen;
        pt.y *= _hScreen;
        GEngine->DrawLine(Line2DPixel(pt.x - rectW2, pt.y - rectH2, pt.x + rectW2, pt.y - rectH2), _colorActiveMarker,
                          _colorActiveMarker, _clipRect);
        GEngine->DrawLine(Line2DPixel(pt.x + rectW2, pt.y - rectH2, pt.x + rectW2, pt.y + rectH2), _colorActiveMarker,
                          _colorActiveMarker, _clipRect);
        GEngine->DrawLine(Line2DPixel(pt.x + rectW2, pt.y + rectH2, pt.x - rectW2, pt.y + rectH2), _colorActiveMarker,
                          _colorActiveMarker, _clipRect);
        GEngine->DrawLine(Line2DPixel(pt.x - rectW2, pt.y + rectH2, pt.x - rectW2, pt.y - rectH2), _colorActiveMarker,
                          _colorActiveMarker, _clipRect);
        GEngine->DrawLine(Line2DPixel(_clipRect.x, pt.y, pt.x - rectW2, pt.y), _colorActiveMarker, _colorActiveMarker,
                          _clipRect);
        GEngine->DrawLine(Line2DPixel(pt.x + rectW2, pt.y, _clipRect.x + _clipRect.w, pt.y), _colorActiveMarker,
                          _colorActiveMarker, _clipRect);
        GEngine->DrawLine(Line2DPixel(pt.x, _clipRect.y, pt.x, pt.y - rectH2), _colorActiveMarker, _colorActiveMarker,
                          _clipRect);
        GEngine->DrawLine(Line2DPixel(pt.x, pt.y + rectH2, pt.x, _clipRect.y + _clipRect.h), _colorActiveMarker,
                          _colorActiveMarker, _clipRect);
    }

#if _ENABLE_CHEATS
    if (_showVariables)
    {
        float top = _y + 0.2;
        float left = _x + 0.02;
        const VarBankType& vars = GWorld->GetGameState()->GetVariables();
        if (vars.NItems() > 0)
        {
            // !!! avoid GetTable when NItems == 0
            for (int i = 0; i < vars.NTables(); i++)
            {
                for (int j = 0; j < vars.GetTable(i).Size(); j++)
                {
                    GameVariable& var = vars.GetTable(i)[j];
                    if (GWorld->GetGameState()->IsVisible(var))
                        continue;
                    GLOB_ENGINE->DrawTextF(Point2DFloat(left, top), _sizeLabel, _fontLabel,
                                           PackedColor(Color(0, 0, 0, 1)), "%s = %s", (const char*)var.GetName(),
                                           (const char*)var.GetValueText());
                    top += _sizeLabel;
                }
            }
        }
    }
#endif

    DrawLegend();
}

// simulation
void CStaticMapMain::Center()
{
    Point3 pt = _defaultCenter;
    if (GLOB_WORLD->FocusOn())
    {
        pt = GLOB_WORLD->FocusOn()->Position();
    }
    else if (GLOB_WORLD->CameraOn())
    {
        pt = GLOB_WORLD->CameraOn()->Position();
    }
    CStaticMap::Center(pt);
}

void CStaticMapMain::FindUnit(AICenter* center, Vector3Par pt, SignInfo& info, float& minDist)
{
    if (!center)
    {
        return;
    }

    for (int i = 0; i < center->NGroups(); i++)
    {
        AIGroup* grp = center->GetGroup(i);
        if (!grp)
        {
            continue;
        }
        for (int j = 0; j < MAX_UNITS_PER_GROUP; j++)
        {
            AIUnit* unit = grp->UnitWithID(j + 1);
            if (!unit)
            {
                continue;
            }
            float dist = (pt - unit->Position()).SquareSizeXZ();
            if (dist < minDist)
            {
                minDist = dist;
                info._type = signUnit;
                info._unit = unit;
            }
        }
    }
}

void CStaticMapMain::FindWaypoint(AICenter* myCenter, AIGroup* myGroup, Vector3Par pt, SignInfo& info, float& minDist)
{
#if _ENABLE_CHEATS
    if (_showUnits)
    {
        EntityAI* veh = dyn_cast<EntityAI>(GWorld->CameraOn());
        if (!veh)
            return;
        myGroup = veh->GetGroup();
        if (!myGroup)
            return;
        myCenter = myGroup->GetCenter();
    }
#endif

    int i = myGroup->ID() - 1;
    int m = myGroup->NWaypoints();

#if _ENABLE_CHEATS
    if (!_showUnits)
#endif
    {
        int nshow = 0;
        for (int j = 1; j < m; j++)
        {
            const ArcadeWaypointInfo& wInfo = myGroup->GetWaypoint(j);
            bool wpShow = wInfo.showWP == ShowAlways || wInfo.showWP == ShowEasy && USER_CONFIG.easyMode;
            if (wpShow)
            {
                nshow++;
            }
        }
        if (nshow == 0)
        {
            return;
        }
    }

    // waypoints
    for (int j = 0; j < m; j++)
    {
        const ArcadeWaypointInfo& wInfo = myGroup->GetWaypoint(j);
        bool wpShow = wInfo.showWP == ShowAlways || wInfo.showWP == ShowEasy && USER_CONFIG.easyMode;
        if (j > 0 &&
#if _ENABLE_CHEATS
            !_showUnits &&
#endif
            !wpShow)
        {
            continue;
        }
        float dist = (pt - wInfo.position).SquareSizeXZ();
        if (dist < minDist)
        {
            minDist = dist;
            info._type = signArcadeWaypoint;
            info._indexGroup = i;
            info._index = j;
        }
    }
}

SignInfo CStaticMapMain::FindSign(float x, float y)
{
    AIUnit* me = GetMyUnit();
    AIGroup* myGroup = me ? me->GetGroup() : nullptr;
    AICenter* myCenter = GetMyCenter();

    struct SignInfo info;
    info._type = signNone;
    info._unit = nullptr;
    info._id = nullptr;
    if (!myCenter)
    {
        return info;
    }

    Point3 pt = ScreenToWorld(DrawCoord(x, y));
    float sizeLand = LandGrid * LandRange;
    float dist, minDist = 0.02 * sizeLand * _scaleX;
    minDist *= minDist;

#if _ENABLE_CHEATS
    if (_showSensors)
    {
        for (int i = 0; i < sensorsMap.Size(); i++)
        {
            Vehicle* veh = sensorsMap[i];
            Detector* det = dyn_cast<Detector>(veh);
            if (!det)
                continue;

            dist = (pt - det->Position()).SquareSizeXZ();
            if (dist < minDist)
            {
                minDist = dist;
                info._type = signArcadeSensor;
                info._index = i;
            }
        }
    }

    if (_showUnits)
    {
        FindUnit(GWorld->GetWestCenter(), pt, info, minDist);
        FindUnit(GWorld->GetEastCenter(), pt, info, minDist);
        FindUnit(GWorld->GetGuerrilaCenter(), pt, info, minDist);
        FindUnit(GWorld->GetCivilianCenter(), pt, info, minDist);
        FindUnit(GWorld->GetLogicCenter(), pt, info, minDist);
    }
    else if (_showTargets)
    {
        // draw targets
        int n = myCenter->NTargets();
        for (int i = 0; i < n; i++)
        {
            const AITargetInfo& target = myCenter->GetTarget(i);
            if (target._vanished)
                continue;
            Object* objTgt = target._idExact;
            EntityAI* veh = dyn_cast<EntityAI>(objTgt);
            if (!veh)
                continue;
            const VehicleType* type = veh->GetType(target.FadingTypeAccuracy());
            //			TargetSide side = veh->GetTargetSide(target.FadingSideAccuracy());

            if (!type->IsKindOf(GLOB_WORLD->Preloaded(VTypeStrategic)))
            {
                if (!type->IsKindOf(GLOB_WORLD->Preloaded(VTypeAllVehicles)))
                    continue;
                float alpha = target.FadingPositionAccuracy();
                if (alpha < 0.1)
                    continue;
            }

            dist = (pt - target._realPos).SquareSizeXZ();
            if (dist < minDist)
            {
                if (target._type->IsKindOf(GLOB_WORLD->Preloaded(VTypeStatic)))
                {
                    minDist = dist;
                    info._type = signStatic;
                    info._id = target._idExact;
                }
                else
                {
                    minDist = dist;
                    info._type = signVehicle;
                    info._id = target._idExact;
                }
            }
        }
    }
    else
#endif
        if (USER_CONFIG.IsEnabled(DTMap))
    {
        if (myGroup)
        {
            int n = myGroup->GetTargetList().Size();
            for (int i = 0; i < n; i++)
            {
                const Target* target = myGroup->GetTargetList()[i];
                if (!target->IsKnownBy(me) || target->vanished || target->destroyed)
                {
                    continue;
                }

                // visibility
                float visible = target->FadingSpotability();
                if (visible <= 0.01)
                {
                    continue;
                }

                dist = (pt - target->AimingPosition()).SquareSizeXZ();
                if (dist < minDist)
                {
                    if (target->type->IsKindOf(GLOB_WORLD->Preloaded(VTypeStatic)))
                    {
                        minDist = dist;
                        info._type = signStatic;
                        info._id = target->idExact;
                    }
                    else
                    {
                        minDist = dist;
                        info._type = signVehicle;
                        info._id = target->idExact;
                    }
                }
            }
        }
    }

    // check waypoints
    if (myGroup)
    {
        FindWaypoint(myCenter, myGroup, pt, info, minDist);
    }

    // check markers
    int n = markersMap.Size();
    for (int i = 0; i < n; i++)
    {
        ArcadeMarkerInfo& mInfo = markersMap[i];
        dist = (pt - mInfo.position).SquareSizeXZ();
        if (dist < minDist)
        {
            minDist = dist;
            info._type = signArcadeMarker;
            info._index = i;
        }
    }

    return info;
}

void CStaticMapMain::IssueMove(float x, float y)
{
    AIUnit* me = GetMyUnit();
    AIGroup* myGroup = me ? me->GetGroup() : nullptr;
    if (!myGroup)
    {
        return;
    }

    PackedBoolArray list = Poseidon::ListSelectedUnits();
    if (me == myGroup->Leader() && !list.IsEmpty())
    {
        // command for units
        Command cmd;
        cmd._message = Command::Move;
        cmd._context = Command::CtxUI;
        cmd._destination = ScreenToWorld(DrawCoord(x, y));
        cmd._destination[1] = GLOB_LAND->RoadSurfaceY(cmd._destination[0], cmd._destination[2]);
        myGroup->SendCommand(cmd, list);

        int index = GMainMapInfo.commands.Add();
        GMainMapInfo.commands[index].grp = myGroup;
        GMainMapInfo.commands[index].id = cmd._id;
        GMainMapInfo.commands[index].state = CSSent;
        GMainMapInfo.commands[index].position = cmd._destination;
        Poseidon::ClearSelectedUnits();
    }
    else
    {
        Transport* veh = me->GetVehicleIn();
        if (veh && veh->CommanderUnit() == me && veh->PilotUnit() != me)
        {
            // command for vehicle
            Vector3 pos = ScreenToWorld(DrawCoord(x, y));
            pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos[0], pos[2]);
            veh->SendMove(pos);
        }
    }
}

void CStaticMapMain::IssueWatch(float x, float y)
{
    AIUnit* me = GetMyUnit();
    AIGroup* myGroup = me ? me->GetGroup() : nullptr;
    if (!myGroup)
    {
        return;
    }

    PackedBoolArray list = Poseidon::ListSelectedUnits();
    if (me == myGroup->Leader() && !list.IsEmpty())
    {
        // command for units
        Vector3 pos;
        pos = ScreenToWorld(DrawCoord(x, y));
        pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos[0], pos[2]);
        myGroup->SendState(new RadioMessageWatchPos(myGroup, list, pos));
        Poseidon::ClearSelectedUnits();
    }
}

void CStaticMapMain::IssueAttack(TargetType* target)
{
    AIUnit* me = GetMyUnit();
    AIGroup* myGroup = me ? me->GetGroup() : nullptr;
    if (myGroup && me == myGroup->Leader())
    {
        PackedBoolArray list = Poseidon::ListSelectedUnits();
        Command cmd;
        cmd._message = Command::AttackAndFire;
        cmd._context = Command::CtxUI;
        cmd._target = target;
        myGroup->SendCommand(cmd, list);
        Poseidon::ClearSelectedUnits();
    }
}

void CStaticMapMain::IssueGetIn(TargetType* target)
{
    AIUnit* me = GetMyUnit();
    AIGroup* myGroup = me ? me->GetGroup() : nullptr;
    if (myGroup && me == myGroup->Leader())
    {
        PackedBoolArray list = Poseidon::ListSelectedUnits();
        Command cmd;
        cmd._message = Command::GetIn;
        cmd._context = Command::CtxUI;
        cmd._target = target;
        myGroup->SendCommand(cmd, list);
        Poseidon::ClearSelectedUnits();
    }
}

bool CStaticMapMain::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (nChar == 0)
    {
        return false;
    }

    if (CStaticMap::OnKeyDown(nChar, nRepCnt, nFlags))
    {
        return true;
    }

    switch (nChar)
    {
        case SDLK_DELETE:
        {
            if (_infoMove._type == signArcadeMarker)
            {
                const char* userDefined = "_user_defined";
                RString name = markersMap[_infoMove._index].name;
                if (strnicmp(name, userDefined, strlen(userDefined)) == 0)
                {
                    GetNetworkManager().MarkerDelete(name);
                    markersMap.Delete(_infoMove._index);
                    _infoClick._type = signNone;
                    _infoClickCandidate._type = signNone;
                    _infoMove._type = signNone;
                    _dragging = false;
                    _selecting = false;
                }
            }
            return true;
        }
    }
    return false;
}

void CStaticMapMain::ProcessCheats()
{
#if _ENABLE_CHEATS
    auto& input = InputSubsystem::Instance();
    if (input.GetCheat1ToDo(SDL_SCANCODE_A))
    {
        _showUnits = !_showUnits;
    }
    if (input.GetCheat2ToDo(SDL_SCANCODE_A))
    {
        _showSensors = !_showSensors;
    }
    if (input.GetCheat1ToDo(SDL_SCANCODE_D))
    {
        _showTargets = !_showTargets;
    }
    if (input.GetCheat2ToDo(SDL_SCANCODE_V))
    {
        _showVariables = !_showVariables;
    }
#endif
    CStaticMap::ProcessCheats();
}

void CStaticMapMain::OnLButtonDown(float x, float y)
{
    _infoClick = FindSign(x, y);
    switch (_infoClick._type)
    {
        case signArcadeWaypoint:
        {
            AICenter* center = GetMyCenter();
            if (!center)
            {
                break;
            }
            if (_infoClick._indexGroup < 0)
            {
                break;
            }
            PoseidonAssert(_infoClick._indexGroup < center->NGroups());
            AIGroup* group = center->GetGroup(_infoClick._indexGroup);
            if (!group)
            {
                break;
            }
            const ArcadeWaypointInfo& wInfo = group->GetWaypoint(_infoClick._index);
            bool wpShow = wInfo.showWP == ShowAlways || wInfo.showWP == ShowEasy && USER_CONFIG.easyMode;
            if (
#if _ENABLE_CHEATS
                !_showUnits &&
#endif
                !wpShow)
            {
                break;
            }

            _dragging = true;
            _lastPos = ScreenToWorld(DrawCoord(x, y));
        }
            return; // avoid IssueMove
    }

    //	IssueMove(x, y);
}

extern RString GMapOnSingleClick;

void CStaticMapMain::OnLButtonClick(float x, float y)
{
    if (!_dragging)
    {
        // Click-to-teleport cheat — if active, snap the player's
        // vehicle (the soldier when on foot, the transport when
        // crewed) to the clicked world point and swallow the click so
        // the normal move/watch handler doesn't also fire.  Same
        // entity-resolution rule the setPos SQF operator uses for
        // soldiers in vehicles.
        if (DebugCheats::Cmd_MapTeleport::IsActive() && GWorld)
        {
            Person* player = GWorld->GetRealPlayer();
            if (player && player->Brain())
            {
                EntityAI* veh = player->Brain()->GetVehicle();
                if (veh)
                {
                    Vector3 pos = ScreenToWorld(DrawCoord(x, y));
                    if (GLandscape)
                        pos[1] = GLandscape->SurfaceYAboveWater(pos[0], pos[2]) + 0.5f;
                    Matrix4 trans = veh->Transform();
                    trans.SetPosition(pos);
                    veh->Move(trans);
                    return;
                }
            }
        }

        bool alt = InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_LALT);
        bool shift = InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_LSHIFT);
        bool ctrl = InputSubsystem::Instance().IsKeyDown(SDL_SCANCODE_LCTRL);
        bool issueMove = true;
        if (GMapOnSingleClick.GetLength() > 0)
        {
            Vector3 pos = ScreenToWorld(DrawCoord(x, y));
            GameState* state = GWorld->GetGameState();
            GameValue value = state->CreateGameValue(GameArray);
            GameArrayType& array = value;
            AIUnit* me = GetMyUnit();
            AIGroup* myGroup = me ? me->GetGroup() : nullptr;
            if (myGroup && me == myGroup->Leader())
            {
                for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                {
                    AIUnit* unit = Poseidon::GetSelectedUnit(i);
                    if (unit)
                    {
                        array.Add(GameValueExt(unit->GetPerson()));
                    }
                }
            }

            GameValue posValue = state->CreateGameValue(GameArray);
            GameArrayType& posArray = posValue;
            posArray.Realloc(3);
            posArray.Add(pos[0]);
            posArray.Add(pos[2]);
            posArray.Add(pos[1] - GLandscape->SurfaceYAboveWater(pos[0], pos[2]));

            GameVarSpace vars;
            state->BeginContext(&vars);
            state->VarSetLocal("_units", value, true);
            state->VarSetLocal("_pos", posArray, true);
            state->VarSetLocal("_alt", alt, true);
            state->VarSetLocal("_shift", shift, true);
			state->VarSetLocal("_ctrl",ctrl,true);
            issueMove = !state->EvaluateMultipleBool(GMapOnSingleClick);
            state->EndContext();
        }

        if (issueMove)
        {
            if (alt)
            {
                IssueWatch(x, y);
            }
            else
            {
                IssueMove(x, y);
            }
        }
    }
}

void CStaticMapMain::OnLButtonDblClick(float x, float y)
{
    _parent->CreateChild(new DisplayInsertMarker(_parent, x, y, _x, _y, _w, _h, _parent->SimulationEnabled()));
}

void CStaticMapMain::OnLButtonUp(float x, float y)
{
    if (_dragging)
    {
        _dragging = false;

        PoseidonAssert(_infoClick._type == signArcadeWaypoint);
        AICenter* center = GetMyCenter();
        if (!center)
        {
            return;
        }
        if (_infoClick._indexGroup < 0)
        {
            return;
        }
        PoseidonAssert(_infoClick._indexGroup < center->NGroups());
        AIGroup* group = center->GetGroup(_infoClick._indexGroup);
        if (!group)
        {
            return;
        }
        if (!group->GetCurrent())
        {
            return;
        }

        int& index = group->GetCurrent()->_fsm->Var(0);
        if (index == _infoClick._index)
        {
            AIGroupContext ctx(group);
            ctx._fsm = group->GetCurrent()->_fsm;
            ctx._task = const_cast<Mission*>(group->GetMission());
            ctx._fsm->SetState(1, &ctx);
        }
    }
}

void CStaticMapMain::OnMouseHold(float x, float y, bool active)
{
    if (_dragging)
    {
        PoseidonAssert(_infoClick._type == signArcadeWaypoint);
        AICenter* center = GetMyCenter();
        if (!center)
        {
            return;
        }
        if (_infoClick._indexGroup < 0)
        {
            return;
        }
        PoseidonAssert(_infoClick._indexGroup < center->NGroups());
        AIGroup* group = center->GetGroup(_infoClick._indexGroup);
        if (!group)
        {
            return;
        }
        ArcadeWaypointInfo& wInfo = group->GetWaypoint(_infoClick._index);

        Vector3 curPos = ScreenToWorld(DrawCoord(x, y));
        Vector3 offset = curPos - _lastPos;

        Vector3 pos = wInfo.position + offset;
        pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos[0], pos[2]);
        wInfo.position = pos;

        _lastPos = curPos;
    }

    CStaticMap::OnMouseHold(x, y);
}
