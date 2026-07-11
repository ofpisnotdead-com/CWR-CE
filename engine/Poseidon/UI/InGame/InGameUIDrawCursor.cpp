
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/UI/InGame/InGameUIImpl.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Terrain/Visibility.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>

#include <Poseidon/Core/resincl.hpp>

#include <Poseidon/World/Entities/Infantry/MoveActions.hpp>

#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/UI/InGame/InGameUIDrawShared.hpp>
#include <stdio.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

namespace Poseidon
{

AIUnit* GetSelectedUnit(int i);

bool InGameUI::DrawMouseCursor(const Camera& camera, AIUnit* unit, bool td)
{
    EntityAI* vehicle = unit->GetVehicle();

    Poseidon::PreloadedTexture cursor = CursorStrategy;
    Poseidon::PreloadedTexture cursor2 = CursorStrategyMove;
    Texture* cursorTex = nullptr;
    Texture* cursorTex2 = nullptr;
    bool strategic = true;
    switch (_modeAuto)
    {
        case UIFire:
        case UIFirePosLock:
            // check weapon cursor
            cursorTex = vehicle->GetCursorTexture(unit->GetPerson());
            cursorTex2 = cursorTex;
            cursor = cursor2 = CursorAim;
            strategic = false;
            break;
        case UIStrategy:
            cursor = CursorStrategyMove;
            break;
        case UIStrategyMove:
            cursor = CursorStrategy;
            break;
        case UIStrategySelect:
            cursor = CursorStrategySelect;
            break;
        case UIStrategyAttack:
            cursor = CursorStrategyAttack;
            break;
        case UIStrategyFire:
            cursor = CursorStrategyAttack;
            break;
        case UIStrategyGetIn:
            cursor = CursorStrategyGetIn;
            break;
        case UIStrategyWatch:
            cursor = CursorStrategyWatch;
            break;
    }

    // dim cursor with time when non-active
    float age = GetCursorAge();
    const float startDim = 5, endDim = 10;
    float cursorA = 1;
    if (age > startDim)
    {
        // interpolate dim
        if (age < endDim)
        {
            float factor = (age - startDim) * (1 / (endDim - startDim));
            cursorA = (1 - factor) + cursorDim * factor;
        }
        else
        {
            cursorA = cursorDim;
        }
        // update alpha
    }

    int weapon = vehicle->SelectedWeapon();
    if (_showCursors && _lockTarget.IdExact() != nullptr && weapon >= 0 &&
        (vehicle->CommanderUnit() == unit || vehicle->GunnerUnit() == unit))
    {
        // find locked target info
        Vector3 relPos = _lockTarget->LandAimingPosition() - camera.Position();
        float aimed = vehicle->GetAimed(weapon, _lockTarget);
        // check actual visibility
        DrawTargetInfo(camera, unit, relPos, GPreloadedTextures.New(CursorTarget),
                       GPreloadedTextures.New(aimed > 0.25 ? CursorLocked : CursorTarget), cursorColor, 1, aimed,
                       _lockTarget, -1, _lockTarget.IdExact() != _target.IdExact(), false, td);
    }
    // keep cursor on screen
    // assign cursor direction
    // LOG_DEBUG(UI, "World {:.1f},{:.1f},{:.1f}",_worldCursor[0],_worldCursor[1],_worldCursor[2]);
    // Matrix4Val camInvTransform=camera.GetInvTransform();

    Vector3 curDir = GetCursorDirection();
    vehicle->LimitCursor(GWorld->GetCameraType(), curDir);

    Vector3 newDir = curDir;
    vehicle->OverrideCursor(GWorld->GetCameraType(), newDir);

    if (_showCursors)
    {
        float textA = cursorA;
        if (
#if _ENABLE_CHEATS
            !_showAll &&
#endif
            !vehicle->ShowCursor(weapon, GWorld->GetCameraType()) && !strategic)
        {
            cursorA = 0;
        }
        PackedColor color = PackedColorRGB(cursorColor, toIntFloor(cursorColor.A8() * cursorA));

        bool drawInfo = strategic;
        if (!strategic && _target && !_target->type->IsKindOf(GWorld->Preloaded(VTypeStatic)))
        {
            if (_target->side == TCivilian)
            {
                drawInfo = false;
            }
            else if (unit->GetGroup()->GetCenter()->IsFriendly(_target->side))
            {
                drawInfo = USER_CONFIG.IsEnabled(DTFriendlyTag);
            }
            else
            {
                drawInfo = USER_CONFIG.IsEnabled(DTEnemyTag);
            }
        }

        if (!cursorTex)
        {
            cursorTex = GPreloadedTextures.New(cursor);
        }
        if (!cursorTex2)
        {
            cursorTex2 = GPreloadedTextures.New(cursor2);
        }
        DrawTargetInfo(camera, unit, newDir, cursorTex, cursorTex2, color, textA, textA, _target, _housePos, drawInfo,
                       ModeIsStrategy(_mode), td);
    }

    return true;
}

float HowMuchInteresting(AIUnit* unit, const Target* tgt);

bool InGameUI::DrawTargetInfo(const Camera& camera, AIUnit* unit, Vector3Par dir, Texture* cursor, Texture* cursor2,
                              PackedColor color, float cursorA, float cursor2A, const Target* target, int housePos,
                              bool info, bool extended, bool td)
{
    EntityAI* vehicle = unit->GetVehicle();

    Matrix4Val camInvTransform = camera.GetInvTransform();

    Vector3Val mDir = camInvTransform.Rotate(dir);

    // width/height assume fovLeft/fovTop = 1/0.75
    AspectSettings asp;
    GEngine->GetAspectSettings(asp);

    const float mScrH = 32.0f / (800 * asp.topFOV);
    const float mScrW = 32.0f / (800 * asp.leftFOV);

    float cx, cy;
    {
        Point3 pos = camInvTransform.Rotate(dir);
        if (pos.Z() <= 0)
        {
            return false;
        }
        float invZ = 1.0 / pos.Z();
        cx = pos.X() * invZ * camera.InvLeft();
        cy = -pos.Y() * invZ * camera.InvTop();
    }

    float mScrX = cx * 0.5 + 0.5 - mScrW * 0.5;
    float mScrY = cy * 0.5 + 0.5 - mScrH * 0.5;
    const int w2d = GLOB_ENGINE->Width2D();
    const int h2d = GLOB_ENGINE->Height2D();

    const int w3d = GLOB_ENGINE->Width();
    const int h3d = GLOB_ENGINE->Height();

    int mx = toInt(mScrX * w3d);
    int my = toInt(mScrY * h3d);
    int mw = toInt(mScrW * w3d);
    int mh = toInt(mScrH * h3d);

    float xAngle = atan2(mDir.X(), mDir.Z());
    float yAngle = atan2(mDir.Y(), mDir.SizeXZ());
    bool tactical = td && xAngle >= MIN_X && xAngle <= MAX_X && yAngle >= MIN_Y && yAngle <= MAX_Y;

    float xScreen = 0, yScreen = 0;
    if (tactical)
    {
        xScreen = toInt((tdX + 0.5 * tdW + xAngle * tdW * INV_W - 0.5 * tdCurW) * w2d);
        yScreen = toInt((tdY + 0.5 * tdH - yAngle * tdH * INV_H - 0.5 * tdCurH) * h2d);
    }

    Texture* textureDef = GLOB_SCENE->Preloaded(TextureWhite);
    Texture* texture;
    MipInfo mip;
    float size = 0.02;
    AICenter* center = unit->GetGroup()->GetCenter();
    if (vehicle->CommanderUnit() && vehicle->CommanderUnit()->GetGroup())
    {
        center = vehicle->CommanderUnit()->GetGroup()->GetCenter();
    }
    PoseidonAssert(center);

    if (cursor2 != cursor)
    { // all strategy cursors use the same background
        PackedColor colorBlack = PackedColorRGB(PackedBlack, toIntFloor(color.A8() * cursor2A));
        texture = cursor2;
        mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);

        GLOB_ENGINE->Draw2D(mip, colorBlack, Rect2DAbs(mx + 1, my + 1, mw, mh));
        // GLOB_ENGINE->TextBank()->ReleaseMipmap();
    }

    // if (cursor!=CursorStrategyMove)
    { // mode dependent cursor
        texture = cursor;
        mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);

        // draw black to see cursor on light background
        PackedColor colorBlack = PackedColorRGB(PackedBlack, color.A8());
        GLOB_ENGINE->Draw2D(mip, colorBlack, Rect2DAbs(mx + 1, my + 1, mw, mh));

        GLOB_ENGINE->Draw2D(mip, color, Rect2DAbs(mx, my, mw, mh));
        if (tactical)
        {
            GLOB_ENGINE->Draw2D(mip, tdCursorColor, Rect2DPixel(xScreen, yScreen, tdCurW * w2d, tdCurH * h2d),
                                Rect2DPixel(tdX * w2d, tdY * h2d, tdW * w2d, tdH * h2d));
        }
        // GLOB_ENGINE->TextBank()->ReleaseMipmap();
    }

    if (cursor2 != cursor)
    { // all strategy cursors use the same background
        PackedColor color2A = PackedColorRGB(color, toIntFloor(color.A8() * cursor2A));
        texture = cursor2;
        mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);

        GLOB_ENGINE->Draw2D(mip, color2A, Rect2DAbs(mx, my, mw, mh));
        if (tactical)
        {
            GLOB_ENGINE->Draw2D(mip, tdCursorColor, Rect2DPixel(xScreen, yScreen, tdCurW * w2d, tdCurH * h2d),
                                Rect2DPixel(tdX * w2d, tdY * h2d, tdW * w2d, tdH * h2d));
        }
    }

    if (!info)
    {
        return true;
    }

    bool reallyExtended = false;
    Object* vObj = target ? target->idExact : nullptr;
    EntityAI* v = dyn_cast<EntityAI>(vObj);

    if (extended && v && v->CommanderUnit() && vehicle->CommanderUnit())
    {
        AIGroup* vGroup = v->CommanderUnit()->GetGroup();
        AIGroup* vehGroup = vehicle->CommanderUnit()->GetGroup();
        // BUG: in VC++5.0
        // reallyExtended=( v->Brain()->GetGroup()==vehicle->Brain()->GetGroup() );
        reallyExtended = (vGroup == vehGroup);
    }
    // draw information texts

    Point2DAbs pos = Point2DAbs((mScrX + mScrW) * w3d, mScrY * h3d);
    GEngine->PixelAlignXY(pos);

    float width, height;
    if (reallyExtended)
    { // extened info expected
        width = 0.1;
        height = size;
    }
    else
    { // simple info only
        width = 0;
        height = 0;
    }

    // dimmed colors
    PackedColor bgColorA = PackedColorRGB(cursorBgColor, toIntFloor(cursorBgColor.A8() * cursorA));
    PackedColor ftColorA = PackedColorRGB(ftColor, toIntFloor(ftColor.A8() * cursorA));

    RString vName;
    if (v)
    {
        PoseidonAssert(target->type);
        vName = target->type->GetDisplayName();
        if (housePos >= 0)
        {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), LocalizeString(IDS_HOUSE_POSITION), housePos + 1);
            vName = vName + RString(" ") + RString(buffer);
        }
        AIUnit* u = v->CommanderUnit();
        if (u)
        {
            Person* person = u->GetPerson();
            if (person->IsNetworkPlayer())
            {
                vName = vName + RString(" (") + person->GetInfo()._name + RString(")");
            }
#if _ENABLE_CHEATS
            else if (CHECK_DIAG(DECombat) && v->GetVarName().GetLength() > 0)
                vName = vName + RString(" (") + v->GetVarName() + RString(")");
#endif
        }
        float vLen = GLOB_ENGINE->GetTextWidthF(size, _font24, vName);
        saturateMax(width, vLen);
        height += size;
    }
    bool distanceValid = _groundPointValid;
    float distance = _groundPointDistance;
    if (v)
    {
        distanceValid = true;
#if _ENABLE_CHEATS
        if (_showAll && target->idExact)
        {
            distance = target->idExact->AimingPosition().Distance(vehicle->Position());
        }
        else
#endif
        {
            distance = target->LandAimingPosition().Distance(vehicle->Position());
        }
    }

    if (distanceValid)
    {
        // make space for distance info
        float dLen = GLOB_ENGINE->GetTextWidthF(size, _font24, STR_POS_DIST, distance);
        height += size;
        saturateMax(width, dLen);
    }

    mip = GLOB_ENGINE->TextBank()->UseMipmap(textureDef, 0, 0);
    GLOB_ENGINE->Draw2D(mip, bgColorA, Rect2DAbs(pos.x, pos.y, width * w2d, height * h2d));
    // GLOB_ENGINE->TextBank()->ReleaseMipmap();

    if (distanceValid)
    {
        GLOB_ENGINE->DrawTextF(pos, size, _font24, ftColorA, STR_POS_DIST, distance);
        pos.y += GEngine->PixelAlignedY(size * h2d);
    }
    if (v)
    {
        // get AI info
        TargetSide side = target->type->_typicalSide;
        if (!target->idExact->EngineIsOn())
        {
            AIGroup* grp = v->GetGroup();
            if (grp != unit->GetGroup())
            {
                side = TCivilian; // empty marked as civilian
            }
        }

        if (USER_CONFIG.IsEnabled(DTEnemyTag)
#if _ENABLE_CHEATS
            || _showAll
#endif
        )
        {
            side = target->side;
            if (target->destroyed)
            {
                side = TCivilian;
            }
        }
        if (!center)
        {
            color = civilianColor;
        }
        else if (side == TCivilian)
        {
            color = civilianColor;
        }
        else if (center->IsFriendly(side))
        {
            color = friendlyColor;
        }
        else if (center->IsEnemy(side))
        {
            color = enemyColor;
        }
        else if (center->IsNeutral(side))
        {
            color = neutralColor;
        }
        else
        {
            color = unknownColor;
        }
        PackedColor colorA = PackedColorRGB(color, toIntFloor(color.A8() * cursorA));

#if _ENABLE_CHEATS
        // do not use stringtable - debug texts only
        if (CHECK_DIAG(DECombat))
        {
            RString extVName = (vName + RString(" ") + target->type->GetDisplayName() + RString(" ") +
                                FindEnumName(target->side) + RString(target->sideChecked ? "" : "?"));
            GLOB_ENGINE->DrawText(pos, size, _font24, colorA, extVName);
            pos.y += GEngine->PixelAlignedY(size * h2d);
        }
        else
#endif
        {
            GLOB_ENGINE->DrawText(pos, size, _font24, colorA, vName);
            pos.y += GEngine->PixelAlignedY(size * h2d);
        }
        if (reallyExtended)
        {
            AIUnit* u = v->CommanderUnit();
            // extended target info
            GLOB_ENGINE->DrawTextF(
                pos, size, _font24, ftColorA, "%d: %s", u->ID(),
                (const char*)LocalizeString(IDS_PRIVATE + ClampRankIndex(u->GetPerson()->GetInfo()._rank)));
            pos.y += GEngine->PixelAlignedY(size * h2d);
        }
#if _ENABLE_CHEATS
        // do not use stringtable - debug texts only
        if (CHECK_DIAG(DECombat))
        {
            GLOB_ENGINE->DrawText(pos, size, _font24, ftColorA, v->DiagText());
            pos.y += GEngine->PixelAlignedY(size * h2d);

            if (target->vanished)
            {
                GLOB_ENGINE->DrawText(pos, size, _font24, ftColorA, "Vanished");
                pos.y += GEngine->PixelAlignedY(size * h2d);
            }
            if (target->destroyed)
            {
                GLOB_ENGINE->DrawText(pos, size, _font24, ftColorA, "Destroyed");
                pos.y += GEngine->PixelAlignedY(size * h2d);
            }
            if (GWorld->GetMode() == GModeNetware)
            {
                // draw network ID
                GLOB_ENGINE->DrawTextF(pos, size, _font24, ftColorA, "%s: %x", v->IsLocal() ? "Local" : "Remote",
                                       v->GetNetworkId());
                pos.y += GEngine->PixelAlignedY(size * h2d);
            }

            AIUnit* u = v->CommanderUnit();
            if (u)
            {
                GLOB_ENGINE->DrawTextF(pos, size, _font24, ftColorA, "TTL %.2f", u->GetTimeToLive());
                pos.y += GEngine->PixelAlignedY(size * h2d);
                char state[256] = "";
                switch (u->GetState())
                {
                    case AIUnit::Wait:
                        snprintf(state, sizeof(state), "%s", (const char*)"Unit - WAIT");
                        break;
                    case AIUnit::Init:
                        snprintf(state, sizeof(state), "%s", (const char*)"Unit - INIT");
                        break;
                    case AIUnit::Busy:
                        snprintf(state, sizeof(state), "%s", (const char*)"Unit - BUSY");
                        break;
                    case AIUnit::Completed:
                        snprintf(state, sizeof(state), "%s", (const char*)"Unit - OK");
                        break;
                    case AIUnit::Delay:
                        snprintf(state, sizeof(state), "Unit - DELAY %.1f", u->GetDelay() - Glob.time);
                        break;
                    case AIUnit::InCargo:
                        snprintf(state, sizeof(state), "%s", (const char*)"Unit - IN CARGO");
                        break;
                    case AIUnit::Stopping:
                        snprintf(state, sizeof(state), "%s", (const char*)"Unit - STOPPING");
                        break;
                    case AIUnit::Stopped:
                    {
                        Transport* trans = dyn_cast<Transport>(v);
                        float t = trans ? trans->GetGetInTimeout() - Glob.time : 0;
                        snprintf(state, sizeof(state), "Unit - STOPPED %.1f", t);
                    }
                    break;
                    case AIUnit::Replan:
                        snprintf(state, sizeof(state), "%s", (const char*)"Unit - REPLAN");
                        break;
                    case AIUnit::Sending:
                        snprintf(state, sizeof(state), "%s", (const char*)"Unit - SENDING");
                        break;
                    default:
                        snprintf(state, sizeof(state), "%s", (const char*)"Unit - UNDEF");
                        break;
                }
                if (*state)
                {
                    GLOB_ENGINE->DrawText(pos, size, _font24, ftColorA, state);
                    pos.y += GEngine->PixelAlignedY(size * h2d);
                }
                *state = 0;
                AISubgroup* subgrp = u->GetSubgroup();
                switch (subgrp->GetMode())
                {
                    case AISubgroup::Wait:
                        snprintf(state, sizeof(state), "%s", (const char*)"Subgrp - WAIT");
                        break;
                    case AISubgroup::PlanAndGo:
                        snprintf(state, sizeof(state), "%s", (const char*)"Subgrp - PLAN&GO");
                        break;
                    case AISubgroup::DirectGo:
                        snprintf(state, sizeof(state), "Subgrp - DIRECT GO");
                        break;
                    default:
                        snprintf(state, sizeof(state), "%s", (const char*)"Subgrp - UNDEF");
                        break;
                }
                if (subgrp->GetCurrent())
                {
                    strncat(state, " ", sizeof(state) - strlen(state) - 1);
                    strncat(state, LocalizeString(IDS_NOCOMMAND + subgrp->GetCommand()->_message),
                            sizeof(state) - strlen(state) - 1);
                    strncat(state, " ", sizeof(state) - strlen(state) - 1);
                    strncat(state, subgrp->GetCurrent()->_fsm->GetStateName(), sizeof(state) - strlen(state) - 1);
                }
                if (*state)
                {
                    GLOB_ENGINE->DrawText(pos, size, _font24, ftColorA, state);
                    pos.y += GEngine->PixelAlignedY(size * h2d);
                }
                // semaphore
                *state = 0;
                switch (u->GetSemaphore())
                {
                    case AI::SemaphoreBlue:
                        snprintf(state, sizeof(state), "%s", (const char*)"Never Fire, Keep Formation");
                        break;
                    case AI::SemaphoreGreen:
                        snprintf(state, sizeof(state), "%s", (const char*)"Hold Fire, Keep Formation");
                        break;
                    case AI::SemaphoreWhite:
                        snprintf(state, sizeof(state), "%s", (const char*)"Hold Fire, Loose Formation");
                        break;
                    case AI::SemaphoreYellow:
                        snprintf(state, sizeof(state), "%s", (const char*)"Open Fire, Keep Formation");
                        break;
                    case AI::SemaphoreRed:
                        snprintf(state, sizeof(state), "%s", (const char*)"Open Fire, Loose Formation");
                        break;
                }
                if (*state)
                {
                    GLOB_ENGINE->DrawText(pos, size, _font24, ftColorA, state);
                    pos.y += GEngine->PixelAlignedY(size * h2d);
                }

                // behaviour
                *state = 0;
                switch (u->GetCombatMode())
                {
                    case CMCareless:
                        snprintf(state, sizeof(state), "%s", (const char*)"Careless");
                        break;
                    case CMSafe:
                        snprintf(state, sizeof(state), "%s", (const char*)"Safe");
                        break;
                    case CMAware:
                        snprintf(state, sizeof(state), "%s", (const char*)"Aware");
                        break;
                    case CMCombat:
                        snprintf(state, sizeof(state), "%s", (const char*)"Combat");
                        break;
                    case CMStealth:
                        snprintf(state, sizeof(state), "%s", (const char*)"Stealth");
                        break;
                }
                if (*state)
                {
                    GLOB_ENGINE->DrawText(pos, size, _font24, ftColorA, state);
                    pos.y += GEngine->PixelAlignedY(size * h2d);
                }

                AIGroup* grp = subgrp->GetGroup();
                if (grp && grp->GetCurrent())
                {
                    snprintf(state, sizeof(state), "%s", (const char*)"Grp - ");
                    strncat(state, grp->GetCurrent()->_fsm->GetStateName(), sizeof(state) - strlen(state) - 1);
                    if (*state)
                    {
                        GLOB_ENGINE->DrawText(pos, size, _font24, ftColorA, state);
                        pos.y += GEngine->PixelAlignedY(size * h2d);
                    }
                }
            }
        }
#endif

#if _ENABLE_CHEATS
        if (CHECK_DIAG(DECombat))
        {
            // do not use stringtable - debug texts only

            // draw sensor information
            GEngine->DrawTextF(pos, size, _font24, ftColorA, "uncertainity %.2f %.0f", target->FadingAccuracy(),
                               Glob.time.Diff(target->accuracyTime));
            pos.y += GEngine->PixelAlignedY(size * h2d);
            GLOB_ENGINE->DrawTextF(pos, size, _font24, ftColorA, "side Uncertainity %.2f %.0f",
                                   target->FadingSideAccuracy(), Glob.time.Diff(target->sideAccuracyTime));
            pos.y += GEngine->PixelAlignedY(size * h2d);
            GLOB_ENGINE->DrawTextF(pos, size, _font24, ftColorA, "spotability %.2f %.0f", target->FadingSpotability(),
                                   Glob.time.Diff(target->spotabilityTime));
            pos.y += GEngine->PixelAlignedY(size * h2d);
            GLOB_ENGINE->DrawTextF(pos, size, _font24, ftColorA, "seen %.1f k=%c (d=%.1f,sd=%.1f)",
                                   Glob.time.Diff(target->lastSeen), target->isKnown ? 'Y' : 'N',
                                   target->delay.Diff(Glob.time), target->delaySensor.Diff(Glob.time));
            pos.y += GEngine->PixelAlignedY(size * h2d);
            GEngine->DrawTextF(pos, size, _font24, ftColorA, "Error %.0f", target->posError.Size());
            pos.y += GEngine->PixelAlignedY(size * h2d);
            GEngine->DrawTextF(pos, size, _font24, ftColorA,
                               "Pattern %.2f, move %.2f, v. fire %.2f, aud %.2f, a. fire %.2f", v->GetHidden(),
                               v->VisibleMovement(), v->VisibleFire(), v->Audible(), v->AudibleFire());
            pos.y += GEngine->PixelAlignedY(size * h2d);

            GEngine->DrawTextF(pos, size, _font24, ftColorA, "Interesting %.2f, interest %.2f",
                               HowMuchInteresting(unit, target),
                               HowMuchInteresting(unit, target) / unit->Position().Distance(target->position));
            pos.y += GEngine->PixelAlignedY(size * h2d);
            //
            AIUnit* u = v->CommanderUnit();
            Person* vb = (u ? u->GetPerson() : nullptr);
            Person* vbMe = (vehicle->CommanderUnit() ? vehicle->CommanderUnit()->GetPerson() : nullptr);
            // SensorRowID rIdMe=vbMe->GetSensorRowID();
            SensorRowID rId = vb ? vb->GetSensorRowID() : SensorRowID(-1);
            SensorColID cId = v->GetSensorColID();
            SensorList* list = GWorld->GetSensorList();

            {
                RString diag = "Vis ignored";
                if (vehicle->CommanderUnit() || vehicle->GetType()->IsKindOf(GWorld->Preloaded(VTypeStrategic)) ||
                    vehicle->GetType()->IsKindOf(GWorld->Preloaded(VTypeAllVehicles)))
                {
                    if (vbMe && v)
                    {
                        // do not check acutal visibility of non-strategic targets
                        diag = list->DiagText(vbMe, v);
                    }
                }
                GLOB_ENGINE->DrawText(pos, size, _font24, ftColorA, diag);
                pos.y += GEngine->PixelAlignedY(size * h2d);
            }
            GLOB_ENGINE->DrawTextF(pos, size, _font24, ftColorA, "land vis %.2f, fire vis %.2f",
                                   GLandscape->Visible(vehicle, v),
                                   GLandscape->Visible(vehicle, v, 1, ObjIntersectFire));
            pos.y += GEngine->PixelAlignedY(size * h2d);

            if (u)
            {
                GLOB_ENGINE->DrawTextF(pos, size, _font24, ftColorA, "Form: %.0f, %.0f", u->GetFormationRelative().X(),
                                       u->GetFormationRelative().Z());
                pos.y += GEngine->PixelAlignedY(size * h2d);
            }

            GLOB_ENGINE->DrawTextF(pos, size, _font24, ftColorA, "Row %d, Col %d", rId, cId);
        }
#endif
    }
    return true;
}

void InGameUI::DrawCursor(const Camera& camera, EntityAI* vehicle, Vector3Val dir, float size, Texture* texture,
                          float width, float height, PackedColor color, bool drawInTD, CursorTexts texts)
{
    // width/height assume fovLeft/fovTop = 1/0.75
    AspectSettings asp;
    GEngine->GetAspectSettings(asp);
    width *= 1.0f / asp.leftFOV;
    height *= 0.75f / asp.topFOV;

    const int w3d = GLOB_ENGINE->Width();
    const int h3d = GLOB_ENGINE->Height();

    const int w2d = GLOB_ENGINE->Width2D();
    const int h2d = GLOB_ENGINE->Height2D();

    Matrix4Val camInvTransform = camera.GetInvTransform();
    Vector3Val dirCam = camInvTransform.Rotate(dir);
    if (drawInTD)
    {
        float xAngle = atan2(dirCam.X(), dirCam.Z());
        float yAngle = atan2(dirCam.Y(), dirCam.SizeXZ());
        if (xAngle >= MIN_X && xAngle <= MAX_X && yAngle >= MIN_Y && yAngle <= MAX_Y)
        {
            float xScreen = toInt((tdX + 0.5 * tdW + xAngle * tdW * INV_W - 0.5 * tdCurW) * w2d);
            float yScreen = toInt((tdY + 0.5 * tdH - yAngle * tdH * INV_H - 0.5 * tdCurH) * h2d);
            MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
            GLOB_ENGINE->Draw2D(mip, color, Rect2DPixel(xScreen, yScreen, tdCurW * w2d, tdCurH * h2d),
                                Rect2DPixel(tdX * w2d, tdY * w2d, tdW * w2d, tdH * h2d));
            // GLOB_ENGINE->TextBank()->ReleaseMipmap();
        }
    }

    float invSize = dirCam.InvSize();
    float coef;
    if (size > 0)
    {
        coef = size * invSize * camera.InvLeft();
        saturate(coef, actMin, actMax);
    }
    else if (size > -0.1)
    {
        coef = actMin;
    }
    else
    {
        coef = 1;
    }

    float xMax = 1 - width * coef;
    float yMax = 1 - height * coef;
    float xScreen = 0, yScreen = 0;
    bool visibleX = true;
    bool visibleY = true;
    bool rightEdge = false;
    if (dirCam.Z() > 0)
    {
        float invZ = 1 / dirCam.Z();
        xScreen = 0.5 * (1.0 + dirCam.X() * invZ * camera.InvLeft() - width * coef);
        yScreen = 0.5 * (1.0 - dirCam.Y() * invZ * camera.InvTop() - height * coef);
        if (xScreen < 0)
        {
            xScreen = 0, visibleX = false;
        }
        if (xScreen > xMax)
        {
            xScreen = xMax, visibleX = false, rightEdge = true;
        }
        if (yScreen < 0)
        {
            yScreen = 0, visibleY = false;
        }
        if (yScreen > yMax)
        {
            yScreen = yMax, visibleY = false;
        }
    }
    else
    {
        visibleX = visibleY = false;
    }

    if (!visibleX)
    {
        Vector3 dir = dirCam;
        // saturate x to fit in screen
        // calculation:
        // xs = dirCam.X()/dirCam.Z() * camera.InvLeft();
        // assume dirCam.Z()==1
        // xs should be in (-0.5,+0.5)
        // xs = dirCam.X()*camera.InvLeft();
        // dirCam.X() = xs/camera.InvLeft() = xs*camera.Left()
        float xAngle = atan2(dir[0], dir[2]);
        // wanted angle
        float wxAngle = atan2(fSign(dir[0]) * camera.Left(), 1);
        Matrix3 rotY(MRotationY, xAngle - wxAngle);
        dir = rotY * dir;
        rightEdge = (dir[0] > 0);

        coef = actMin;
        xMax = 1 - width * coef;
        yMax = 1 - height * coef;

        float invZ = 1 / dir.Z();
        xScreen = 0.5 * (1.0 + dir.X() * invZ * camera.InvLeft() - width * coef);
        yScreen = 0.5 * (1.0 - dir.Y() * invZ * camera.InvTop() - height * coef);

        saturate(xScreen, 0, xMax);
        saturate(yScreen, 0, yMax);

        texture = GLOB_SCENE->Preloaded(CursorOutArrow);
        visibleX = true;
        // visibleY = true;
    }

    if (visibleX)
    {
        Draw2DPars pars;
        pars.mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
        pars.spec = NoZBuf | IsAlpha | ClampU | ClampV | IsAlphaFog;
        if (rightEdge)
        {
            pars.SetU(1, 0);
        }
        else
        {
            pars.SetU(0, 1);
        }
        pars.SetV(0, 1);

        Rect2DAbs rect;
        rect.x = xScreen * w3d;
        rect.y = yScreen * h3d;
        rect.w = width * coef * w3d;
        rect.h = height * coef * h3d;

#if 1 // shadowed weapon cursor
        rect.x += 1;
        rect.y += 1;

        PackedColor colorBlack = PackedColorRGB(PackedBlack, color.A8());
        pars.SetColor(colorBlack);
        GLOB_ENGINE->Draw2D(pars, rect);

        rect.x -= 1;
        rect.y -= 1;
#endif
        pars.SetColor(color);

        GLOB_ENGINE->Draw2D(pars, rect);

        // GLOB_ENGINE->TextBank()->ReleaseMipmap();

        for (int i = 0; i < texts.Size(); i++)
        {
            RString text = texts[i].text;
            if (!text || text.GetLength() == 0)
            {
                continue;
            }
            float upDown = texts[i].upDown;

            float yScreenText = yScreen + height * coef * upDown;
            float xScreenText = xScreen;
            float textSize = 0.02;
            float textW = GEngine->GetTextWidth(textSize, _font24, text) * w2d / w3d;
            if (rightEdge)
            {
                xScreenText -= textW + 0.003;
            }
            else
            {
                xScreenText += width * coef;
            }

            MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(GLOB_SCENE->Preloaded(TextureWhite), 0, 0);
            GLOB_ENGINE->Draw2D(mip, cursorBgColor,
                                Rect2DAbs(xScreenText * w3d, yScreenText * h3d, textW * w3d, textSize * h2d));
            // GLOB_ENGINE->TextBank()->ReleaseMipmap();

            Point2DAbs pos(xScreenText * w3d, yScreenText * h3d);
            GEngine->PixelAlignXY(pos);
            GEngine->DrawText(pos, textSize, _font24, color, text);
        }
    }
}

void InGameUI::DrawCommand(const Command& cmd, AIUnit* unit, const Camera& camera, bool td, float alpha)
{
    Vector3 pos;
    float size = 0;

    switch (cmd._message)
    {
        case Command::Move:
        case Command::Heal:
        case Command::Repair:
        case Command::Refuel:
        case Command::Rearm:
        case Command::Support:
            pos = cmd._destination;
            break;
        case Command::Attack:
#if ENABLE_HOLDFIRE_FIX
        case Command::AttackAndFire:
#endif
        case Command::Fire:
        case Command::GetIn:
        {
            Target* target = cmd._targetE;
            if (!target)
            {
                Object* tgt = cmd._target;
                const TargetList& visibleList = *VisibleList();
                int n = visibleList.Size();
                for (int i = 0; i < n; i++)
                {
                    Target* tar = visibleList[i];
                    if (tgt == tar->idExact)
                    {
                        target = tar;
                        break;
                    }
                }
            }
            if (!target)
            {
                return;
            }
            pos = target->LandAimingPosition();
            size = target->VisibleSize();
        }
        break;
        case Command::GetOut:
        {
            pos = unit->AimingPosition();
            size = unit->VisibleSize();
        }
        break;
        case Command::Join:
        {
            AISubgroup* s = cmd._joinToSubgroup;
            if (!s)
            {
                s = unit->GetGroup()->MainSubgroup();
            }
            PoseidonAssert(s);
            PoseidonAssert(s != unit->GetSubgroup());
            AIUnit* u = s->Leader();
            if (!u)
            {
                return;
            }
            pos = u->AimingPosition();
            size = u->VisibleSize();
        }
        break;
        default:
            return;
    }

    Vector3 dir = pos - camera.Position();
    Vector3 dirU = pos - unit->AimingPosition();

    char buffer[256];
    snprintf(buffer, sizeof(buffer), STR_OBJ_DIST, (const char*)LocalizeString(IDS_NOCOMMAND + cmd._message),
             dirU.Size());
    CursorTexts texts;
    texts.Add(buffer, 0);
    PackedColor color = PackedColorRGB(missionColor, toIntFloor(missionColor.A8() * alpha));
    DrawCursor(camera, unit->GetVehicle(), dir, size, _iconMission, actW, actH, color, td, texts);
}

void InGameUI::DrawHUDNonAI(const Camera& camera, Entity* vehicle, CameraType cam)
{
    if (!GWorld->HasOptions())
    {
        // dim cursor with time when non-active
        float age = GetCursorAge();
        const float startDim = 5, endDim = 10;
        float cursorA = 1;
        if (age > startDim)
        {
            // interpolate dim
            if (age < endDim)
            {
                float factor = (age - startDim) * (1 / (endDim - startDim));
                cursorA = (1 - factor) + cursorDim * factor;
            }
            else
            {
                cursorA = cursorDim;
            }
            // update alpha
        }

        Vector3 curDir = GetCursorDirection();
        vehicle->LimitCursor(GWorld->GetCameraType(), curDir);

        Vector3 newDir = curDir;
        vehicle->OverrideCursor(GWorld->GetCameraType(), newDir);

        if (_showCursors)
        {
            PackedColor color = PackedColorRGB(cursorColor, toIntFloor(cursorColor.A8() * cursorA));

            Texture* cursor1 = GPreloadedTextures.New(CursorAim);

            // adapted (simplified) from DrawTargetInfo

            Matrix4Val camInvTransform = camera.GetInvTransform();

            const float mScrH = 32.0 / 600;
            const float mScrW = 32.0 / 800;

            float cx, cy;
            Vector3 pos = camInvTransform.Rotate(newDir);
            if (pos.Z() > 0)
            {
                float invZ = 1.0 / pos.Z();
                cx = pos.X() * invZ * camera.InvLeft();
                cy = -pos.Y() * invZ * camera.InvTop();

                float mScrX = cx * 0.5 + 0.5 - mScrW * 0.5;
                float mScrY = cy * 0.5 + 0.5 - mScrH * 0.5;
                const int w = GLOB_ENGINE->Width();
                const int h = GLOB_ENGINE->Height();

                int mx = toInt(mScrX * w);
                int my = toInt(mScrY * h);
                int mw = toInt(mScrW * w);
                int mh = toInt(mScrH * h);

                MipInfo mip;

                mip = GLOB_ENGINE->TextBank()->UseMipmap(cursor1, 0, 0);

                // draw black to see cursor on light background
                PackedColor colorBlack = PackedColorRGB(PackedBlack, color.A8());
                GLOB_ENGINE->Draw2D(mip, colorBlack, Rect2DAbs(mx + 1, my + 1, mw, mh));

                GLOB_ENGINE->Draw2D(mip, color, Rect2DAbs(mx, my, mw, mh));
            }
        }
    }
}

void InGameUI::DrawHUD(const Camera& camera, EntityAI* vehicle, CameraType cam)
{
    // draw overlay - called before FinishDraw
    // draw global information
    Texture* texture;
    PackedColor color;

    // draw unit related information
    AIUnit* unit = GWorld->FocusOn();
    if (!unit)
    {
        return;
    }
    AISubgroup* subgroup = unit->GetSubgroup();
    if (!subgroup)
    {
        return;
    }
    // No subgroup->Leader() guard here (the original engine has none): after the
    // player dies in a vehicle the HUD must keep drawing so the action menu shows
    // "No action available". Leader() stays non-null anyway via its dead-leader
    // fallback (AIGroup.hpp), and nothing below dereferences it.
    AIGroup* group = subgroup->GetGroup();
    if (!group)
    {
        return;
    }
    vehicle = unit->GetVehicle();
    if (!vehicle)
    {
        return;
    }

    if (!ShouldShowGameplayHUD())
    {
        _actions.Clear();
        return;
    }

    bool isMap = GWorld->HasMap();
    AIUnit* commanderUnit = vehicle->CommanderUnit();
    if (!commanderUnit)
    {
        commanderUnit = unit;
    }

    if (_showGroupInfo && unit->IsGroupLeader() && group->NUnits() > 1)
    {
        DrawGroupInfo(vehicle);
    }

    if (_showMenu)
    {
        DrawMenu();
    }

    _hintTop = piY;
    if (_showUnitInfo && !isMap)
    {
        DrawUnitInfo(vehicle);
    }

    DrawHint();

    if (GetNetworkManager().IsControlsPaused())
    {
        float age = GetNetworkManager().GetLastMsgAgeReliable();
        BString<256> text;
        sprintf(text, LocalizeString(IDS_MP_NO_MESSAGE), age);

        float w = GEngine->GetTextWidth(_clSize, _clFont, text);
        float h = _clSize;
        GEngine->DrawText(Point2DFloat(_clX + 0.5f * (_clW - w), _clY + 0.5f * (_clH - h)), _clSize, _clFont, _clColor,
                          text);
    }

    if (isMap)
    {
        return;
    }

    if (_showCursors && !GWorld->GetPlayerSuspended())
    {
        _actions.OnDraw();
    }

    // TD cheat
#if _ENABLE_CHEATS
    if (InputSubsystem::Instance().GetCheat1ToDo(SDL_SCANCODE_D))
    {
        tdCheat = !tdCheat;
        GlobalShowMessage(500, "ShowDBase %s", tdCheat ? "On" : "Off");
    }
#endif

    Transport* transport = dyn_cast<Transport>(vehicle);
    bool canSeeTD = false;
    bool canSeeCompass = false;
    if (transport)
    {
        int canSee = 0;
        if (unit == transport->ObserverUnit())
        {
            canSee = transport->Type()->_commanderCanSee;
        }
        else if (unit == transport->PilotUnit())
        {
            canSee = transport->Type()->_driverCanSee;
        }
        else if (unit == transport->GunnerUnit())
        {
            canSee = transport->Type()->_gunnerCanSee;
        }
        canSeeTD = (canSee & CanSeeRadar) != 0;
        canSeeCompass = (canSee & CanSeeCompass) != 0;
    }
    bool td =
#if _ENABLE_CHEATS
        tdCheat || _showAll ||
#endif
        _showTacticalDisplay && canSeeTD;
    if (td)
    {
        const TargetList& visibleList = *VisibleList();
        DrawTacticalDisplay(camera, unit, visibleList);
        // note: tactical display does not necessary imply seeing compass
    }
    if (canSeeCompass)
    {
        DrawCompass(vehicle);
    }

    if (USER_CONFIG.IsEnabled(DTClockIndicator))
    {
        DrawGroupDir(camera, group);
    }

    if (!canSeeTD && !USER_CONFIG.IsEnabled(DTHUD))
    {
        if (_showCursors)
        {
            float age = Glob.uiTime - _lastFormTime;
            if (age < formDimEndTime)
            {
                float alpha = 1.0;
                if (age > formDimStartTime)
                {
                    alpha = (formDimEndTime - age) / (formDimEndTime - formDimStartTime);
                }
                // - formation position
                if (commanderUnit != subgroup->Commander() && subgroup->Commander() != nullptr)
                {
                    if (!unit->IsInCargo())
                    {
                        PackedColor colorA = PackedColorRGB(missionColor, toIntFloor(missionColor.A8() * alpha));
                        Vector3 dir = commanderUnit->GetFormationAbsolute() - camera.Position();
                        DrawCursor(camera, vehicle, dir,
                                   0, // size - default
                                   _iconMission, actW, actH, colorA,
                                   false // do not draw in tactical display
                        );
                    }
                }
                // - leader
                if (!commanderUnit->IsGroupLeader() && group->Leader() != nullptr)
                {
                    PackedColor colorA = PackedColorRGB(leaderColor, toIntFloor(missionColor.A8() * alpha));
                    Vector3 dir = group->Leader()->AimingPosition() - camera.Position();
                    Vector3 dirU = group->Leader()->AimingPosition() - commanderUnit->Position();
                    char buffer[256];
                    snprintf(buffer, sizeof(buffer), STR_OBJ_DIST, (const char*)LocalizeString(IDS_LEADER),
                             dirU.Size());
                    CursorTexts texts;
                    texts.Add(buffer, 0.7);
                    DrawCursor(camera, vehicle, dir, group->Leader()->VisibleSize(), _iconLeader, actW, actH, colorA,
                               td, texts);
                }
            }

            // - target
            Target* target = unit->GetTargetAssigned();
            if (target)
            {
                float age = Glob.uiTime - _lastTargetTime;
                if (age < targetDimEndTime)
                {
                    float alpha = 1.0;
                    if (age > targetDimStartTime)
                    {
                        alpha = (targetDimEndTime - age) / (targetDimEndTime - targetDimStartTime);
                    }
                    PackedColor colorA = PackedColorRGB(missionColor, toIntFloor(missionColor.A8() * alpha));

                    Vector3 dir = target->LandAimingPosition() - camera.Position();
                    Vector3 dirU = target->LandAimingPosition() - unit->AimingPosition();
                    char buffer[256];

                    snprintf(buffer, sizeof(buffer), STR_OBJ_DIST, (const char*)LocalizeString(IDS_MENU_TARGET),
                             dirU.Size());
                    CursorTexts texts;
                    texts.Add(buffer, 0);
                    DrawCursor(camera, vehicle, dir,
                               0, // size
                               _iconMission, actW, actH, colorA, td, texts);
                }
            }

            // - command
            const Command* cmd = subgroup->GetCommand();
            if (cmd)
            {
                if (cmd->_id != _lastCmdId)
                {
                    _lastCmdId = cmd->_id;
                    _lastCmdTime = Glob.uiTime;
                }
                float age = Glob.uiTime - _lastCmdTime;
                if (cmd && age < cmdDimEndTime)
                {
                    float alpha = 1.0;
                    if (age > cmdDimStartTime)
                    {
                        alpha = (cmdDimEndTime - age) / (cmdDimEndTime - cmdDimStartTime);
                    }

                    if (commanderUnit != subgroup->Commander() && subgroup->Commander() != nullptr)
                    {
                        if (cmd->_message == Command::Attack || cmd->_message == Command::AttackAndFire ||
                            cmd->_message == Command::Fire)
                        {
                            DrawCommand(*cmd, commanderUnit, camera, td, alpha);
                        }
                    }
                    else
                    {
                        if (cmd->_context != Command::CtxMission)
                        {
                            DrawCommand(*cmd, commanderUnit, camera, td, alpha);
                        }
                    }
                }
            }

            // - selected units
            for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
            {
                AIUnit* u = GetSelectedUnit(i);
                if (!u)
                {
                    continue;
                }
                if (u == unit && GWorld->GetCameraType() < CamExternal)
                {
                    continue;
                }
                float age = Glob.uiTime - _lastSelTime[i];
                if (age >= meDimEndTime)
                {
                    continue;
                }

                float alpha = 1.0;
                if (age > meDimStartTime)
                {
                    alpha = (meDimEndTime - age) / (meDimEndTime - meDimStartTime);
                }
                PackedColor colorA = PackedColorRGB(selectColor, toIntFloor(selectColor.A8() * alpha));

                Vector3 dir = u->AimingPosition() - camera.Position();
                char buffer[256];
                CursorTexts texts;
                float dist = u->AimingPosition().Distance(unit->AimingPosition());
                if (dist >= 50.0f)
                {
                    snprintf(buffer, sizeof(buffer), STR_POS_DIST, dist);
                    texts.Add(buffer, 0);
                }
                snprintf(buffer, sizeof(buffer), "%d", i + 1);
                texts.Add(buffer, 0.7);
                DrawCursor(camera, vehicle, dir, u->VisibleSize(), _iconSelect, actW, actH, colorA, td, texts);
            }
        }
        // goto MouseCursor;
    }

    if (_showTankDirection)
    {
        DrawTankDirection(camera);
    }

    if (_showCursors && (canSeeCompass || USER_CONFIG.IsEnabled(DTHUD)))
    {
        // draw active targets
        // - ourself
        if (GWorld->GetCameraType() >= CamExternal)
        {
            float age = Glob.uiTime - _lastMeTime;
            float alpha = 1;
            if (age > meDimStartTime)
            {
                // interpolate dim
                if (age < meDimEndTime)
                {
                    float factor = (age - meDimStartTime) * (1 / (meDimEndTime - meDimStartTime));
                    alpha = (1 - factor) + meDim * factor;
                }
                else
                {
                    alpha = meDim;
                }
                // update alpha
            }
            PackedColor colorA = PackedColorRGB(meColor, toIntFloor(meColor.A8() * alpha));
            Vector3 dir = commanderUnit->AimingPosition() - camera.Position();
            DrawCursor(camera, vehicle, dir, commanderUnit->VisibleSize(), _iconMe, actW, actH, colorA, td);
        }

        // - selected units
        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            AIUnit* u = GetSelectedUnit(i);
            if (!u)
            {
                continue;
            }
            if (u == unit && GWorld->GetCameraType() < CamExternal)
            {
                continue;
            }

            Vector3 dir = u->AimingPosition() - camera.Position();
            char buffer[256];
            CursorTexts texts;
            float dist = u->AimingPosition().Distance(unit->AimingPosition());
            if (dist >= 50.0f)
            {
                snprintf(buffer, sizeof(buffer), STR_POS_DIST, dist);
                texts.Add(buffer, 0);
            }
            snprintf(buffer, sizeof(buffer), "%d", i + 1);
            texts.Add(buffer, 0.7);
            DrawCursor(camera, vehicle, dir, u->VisibleSize(), _iconSelect, actW, actH, selectColor, td, texts);
        }

        // - leader
        if (!commanderUnit->IsGroupLeader() && group->Leader() != nullptr)
        {
            Vector3 dir = group->Leader()->AimingPosition() - camera.Position();
            Vector3 dirU = group->Leader()->AimingPosition() - commanderUnit->Position();
            char buffer[256];
            snprintf(buffer, sizeof(buffer), STR_OBJ_DIST, (const char*)LocalizeString(IDS_LEADER), dirU.Size());
            CursorTexts texts;
            texts.Add(buffer, 0.7);
            DrawCursor(camera, vehicle, dir, group->Leader()->VisibleSize(), _iconLeader, actW, actH, leaderColor, td,
                       texts);
        }

        // - target
        Target* target = unit->GetTargetAssigned();
        if (target)
        {
            Vector3 dir = target->LandAimingPosition() - camera.Position();
            Vector3 dirU = target->LandAimingPosition() - unit->AimingPosition();
            char buffer[256];

            snprintf(buffer, sizeof(buffer), STR_OBJ_DIST, (const char*)LocalizeString(IDS_MENU_TARGET), dirU.Size());
            CursorTexts texts;
            texts.Add(buffer, 0);
            DrawCursor(camera, vehicle, dir,
                       0, // size
                       _iconMission, actW, actH, missionColor, td, texts);
        }

        const Mission* mis = group->GetMission();
        if (commanderUnit != subgroup->Commander() && subgroup->Commander() != nullptr)
        {
            // no subgroup leader
            Vector3 dir = subgroup->Leader()->AimingPosition() - camera.Position();
            Vector3 dirU = subgroup->Leader()->AimingPosition() - commanderUnit->AimingPosition();
            char buffer[256];

            // draw FOLLOW ME
            snprintf(buffer, sizeof(buffer), STR_OBJ_DIST, (const char*)LocalizeString(IDS_JOIN), dirU.Size());
            if (subgroup->Leader() != group->Leader())
            {
                CursorTexts texts;
                texts.Add(buffer, 0);
                DrawCursor(camera, vehicle, dir, subgroup->Leader()->VisibleSize(), _iconMission, actW, actH,
                           missionColor, td, texts);
            }

            // draw FORMATION position
            if (!unit->IsInCargo())
            {
                dir = commanderUnit->GetFormationAbsolute() - camera.Position();
                dirU = commanderUnit->GetFormationAbsolute() - commanderUnit->AimingPosition();
                if (dirU.SquareSizeXZ() > Square(5))
                {
                    DrawCursor(camera, vehicle, dir,
                               0, // size - default
                               _iconMission, actW, actH, missionColor,
                               false // do not draw in tactical display
                    );
                }
            }

            // draw COMMAND
            const Command* cmd = subgroup->GetCommand();
            if (cmd)
            {
                if (cmd->_message == Command::Attack || cmd->_message == Command::AttackAndFire ||
                    cmd->_message == Command::Fire)
                {
                    DrawCommand(*cmd, commanderUnit, camera, td);
                }
            }
        }
        else
        {
            // subgroup leader
            if (group->Leader())
            {
                const Command* cmd = subgroup->GetCommand();
                if (cmd && cmd->_message != Command::Wait &&
                    (!commanderUnit->IsGroupLeader() || cmd->_context != Command::CtxMission))
                {
                    DrawCommand(*cmd, commanderUnit, camera, td);
                }
            }

            if (commanderUnit->IsGroupLeader() && mis)
            {
                // group leader
                PoseidonAssert(mis->_action == Mission::Arcade);
                int& index = group->GetCurrent()->_fsm->Var(0);
                int& waiting = group->GetCurrent()->_fsm->Var(4);
                if (index < group->NWaypoints())
                {
                    const ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);
                    int type = wInfo.type;
                    Point3 pos = mis->_destination;
                    if (
                        // type == ACDESTROY ||
                        type == ACGETIN || type == ACJOIN || type == ACLEADER)
                    {
                        // if (mis->_targetE != nullptr) pos = mis->_target->AimingPosition();
                        // else
                        if (mis->_target != nullptr)
                        {
                            pos = mis->_target->Position();
                        }
                    }

                    RString description;
                    if (waiting)
                    {
                        description = LocalizeString(IDS_SYNC_WAITING);
                    }
                    else
                    {
                        if (wInfo.description.GetLength() > 0)
                        {
                            description = Localize(wInfo.description);
                        }
                        else
                        {
                            description = LocalizeString(IDS_AC_MOVE + type - ACMOVE);
                        }
                    }

                    Vector3 dir = pos - camera.Position();
                    Vector3 dirU = pos - commanderUnit->AimingPosition();
                    char buffer[256];
                    snprintf(buffer, sizeof(buffer), STR_OBJ_DIST, (const char*)description, dirU.Size());
                    CursorTexts texts;
                    texts.Add(buffer, 0);
                    DrawCursor(camera, vehicle, dir, 0, _iconMission, 2 * actW, 2 * actH, missionColor, td, texts);
                }
            }
        }
    }

    // MouseCursor:

    CameraType type = GWorld->GetCameraType();
    if (_showCursors)
    {
        // draw weapon cursor
        const float mouseScrH = 32.0 / 600;
        const float mouseScrW = 32.0 / 800;

        int weapon = vehicle->SelectedWeapon();
        if (weapon >= 0 && weapon < vehicle->NMagazineSlots() && vehicle->GetMagazineSlot(weapon)._magazine &&
            vehicle->ShowAim(weapon, type) && (vehicle->CommanderUnit() == unit || vehicle->GunnerUnit() == unit))
        {
            if (!ModeIsStrategy(_mode) || _lockTarget != nullptr || _lockAimValidUntil >= Glob.uiTime)
            {
                if (canSeeTD || USER_CONFIG.IsEnabled(DTWeaponCursor))
                {
                    texture = vehicle->GetCursorAimTexture(unit->GetPerson());
                    if (!texture)
                    {
                        texture = GPreloadedTextures.New(CursorWeapon);
                    }
                    DrawCursor(camera, vehicle, vehicle->GetWeaponDirection(weapon), -1, texture, mouseScrW, mouseScrH,
                               cursorColor, td);
                }
            }
        }

#if _ENABLE_CHEATS
        if (!unit->IsAnyPlayer() && CHECK_DIAG(DECombat))
        {
            // draw watch direction (green)
            texture = GLOB_SCENE->Preloaded(CursorTarget);
            static const PackedColor watchColorTgt(Color(0, 1, 0, 0.5));
            static const PackedColor watchColorPos(Color(1, 1, 0, 0.5));
            static const PackedColor watchColorDir(Color(0, 1, 1, 0.5));
            static const PackedColor watchColorFrm(Color(0, 1, 1, 0.25));

            static const PackedColor targetColor(Color(1, 0, 0));

            PackedColor color;
            switch (unit->GetWatchMode())
            {
                case AIUnit::WMTgt:
                    color = watchColorTgt;
                    break;
                case AIUnit::WMPos:
                    color = watchColorPos;
                    break;
                case AIUnit::WMNo:
                    color = watchColorFrm;
                    break;
                // case AIUnit::WMDir:
                default:
                    color = watchColorDir;
                    break;
            }
            DrawCursor(camera, vehicle, unit->GetWatchDirection(), -1, texture, mouseScrW, mouseScrH, color, td);
            texture = GLOB_SCENE->Preloaded(CursorLocked);
            DrawCursor(camera, vehicle, unit->GetWatchHeadDirection(), -1, texture, mouseScrW, mouseScrH, color, td);

            // draw target (red)
        }
#endif
    }

    if (!GWorld->HasOptions())
    {
        if (ModeIsStrategy(_mode) || canSeeTD || USER_CONFIG.IsEnabled(DTWeaponCursor))
        {
            DrawMouseCursor(camera, unit, td);
        }
    }

    if (_dragging)
    {
        Matrix4Val camInvTransform = camera.GetInvTransform();
        Point3 posStart = camInvTransform.Rotate(_startSelection);
        Point3 posEnd = camInvTransform.Rotate(_endSelection);
        if (posStart.Z() > 0 && posEnd.Z() > 0)
        {
            float invZ = 1.0 / posStart.Z();
            float x1 = 0.5 * (1.0 + posStart.X() * invZ * camera.InvLeft());
            float y1 = 0.5 * (1.0 - posStart.Y() * invZ * camera.InvTop());
            invZ = 1.0 / posEnd.Z();
            float x2 = 0.5 * (1.0 + posEnd.X() * invZ * camera.InvLeft());
            float y2 = 0.5 * (1.0 - posEnd.Y() * invZ * camera.InvTop());
            const int w = GLOB_ENGINE->Width();
            const int h = GLOB_ENGINE->Height();
            saturate(x1, 0, 1);
            x1 *= w;
            saturate(y1, 0, 1);
            y1 *= h;
            saturate(x2, 0, 1);
            x2 *= w;
            saturate(y2, 0, 1);
            y2 *= h;
            GEngine->DrawLine(Line2DPixel(x1, y1, x2, y1), dragColor, dragColor);
            GEngine->DrawLine(Line2DPixel(x2, y1, x2, y2), dragColor, dragColor);
            GEngine->DrawLine(Line2DPixel(x2, y2, x1, y2), dragColor, dragColor);
            GEngine->DrawLine(Line2DPixel(x1, y2, x1, y1), dragColor, dragColor);
        }
    }
}

} // namespace Poseidon
