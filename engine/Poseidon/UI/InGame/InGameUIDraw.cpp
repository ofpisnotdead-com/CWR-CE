
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Input/TouchInput.hpp>
#include <SDL3/SDL_scancode.h>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/AI/AI.hpp>
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
#include <stdio.h>
#include <string.h>
#include <cmath>
#include <utility>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

#define INV_H_PI (1 / H_PI)

#include <Poseidon/UI/InGame/InGameUIDrawShared.hpp>
#include <Poseidon/UI/InGame/InGameUIGroupUnitLabel.hpp>

using namespace Poseidon;
void DrawFrame(Texture* corner, PackedColor color, const Rect2DPixel& frame)
{
    const int screenW = GLOB_ENGINE->Width2D();
    const int screenH = GLOB_ENGINE->Height2D();

    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(corner, 0, 0);
    float invCornerH = 1, invCornerW = 1;
    if (corner)
    {
        invCornerH = 1200.0 * (1.0 / (screenH * corner->AHeight()));
        invCornerW = 1600.0 * (1.0 / (screenW * corner->AWidth()));
    }
    Draw2DPars pars;
    Rect2DPixel rect;
    rect.w = 0.5 * frame.w;
    rect.h = 0.5 * frame.h;
    pars.mip = mip;
    pars.SetColor(color);
    pars.spec = NoZBuf | IsAlpha | ClampU | ClampV | IsAlphaFog;
    float coefX = 0.5 * frame.w * invCornerW;
    float coefY = 0.5 * frame.h * invCornerH;

    rect.x = frame.x;
    rect.y = frame.y;
    pars.SetU(0, coefX);
    pars.SetV(0, coefY);
    GLOB_ENGINE->Draw2D(pars, rect);

    rect.x = frame.x + rect.w;
    rect.y = frame.y;
    pars.SetU(coefX, 0);
    pars.SetV(0, coefY);
    GLOB_ENGINE->Draw2D(pars, rect);

    rect.x = frame.x;
    rect.y = frame.y + rect.h;
    pars.SetU(0, coefX);
    pars.SetV(coefY, 0);
    GLOB_ENGINE->Draw2D(pars, rect);

    rect.x = frame.x + rect.w;
    rect.y = frame.y + rect.h;
    pars.SetU(coefX, 0);
    pars.SetV(coefY, 0);
    GLOB_ENGINE->Draw2D(pars, rect);

    // GLOB_ENGINE->TextBank()->ReleaseMipmap();
}

struct DrawActionInfo
{
    RString text;
    //	PackedColor color;
    bool selected;
};

static void FormatWeapon(char* buffer, size_t bufferSize, const char* format, EntityAI* veh, int weapon)
{
    if (veh)
    {
        if (weapon >= 0 && weapon < veh->NMagazineSlots())
        {
            const MagazineSlot& slot = veh->GetMagazineSlot(weapon);
            RStringB displayName = slot._muzzle->GetDisplayName();
            const WeaponModeType* mode = veh->GetWeaponMode(weapon);
            if (mode)
            {
                displayName = mode->GetDisplayName();
            }
            snprintf(buffer, bufferSize, format, (const char*)displayName);
        }
        else
        {
            snprintf(buffer, bufferSize, "%s", (const char*)"Weapon Error");
            RptF("Weapon action - bad weapon %d", weapon);
        }
    }
}

#define CX(x) (toInt((x) * w) + 0.5)
#define CY(y) (toInt((y) * h) + 0.5)

RString UIAction::GetDisplayName(AIUnit* unit) const
{
    char buffer[1024];
    buffer[0] = 0;
    switch (type)
    {
        case ATGetInCommander:
            if (!target)
            {
                RptF("Get in action - missing target");
            }
            else
            {
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_GETIN_COMMANDER),
                         (const char*)target->GetDisplayName());
            }
            break;
        case ATGetInDriver:
            if (!target)
            {
                RptF("Get in action - missing target");
            }
            else
            {
                if (target->GetType()->IsKindOf(GWorld->Preloaded(VTypeAir)))
                {
                    snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_GETIN_PILOT),
                             (const char*)target->GetDisplayName());
                }
                else
                {
                    snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_GETIN_DRIVER),
                             (const char*)target->GetDisplayName());
                }
            }
            break;
        case ATGetInGunner:
            if (!target)
            {
                RptF("Get in action - missing target");
            }
            else
            {
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_GETIN_GUNNER),
                         (const char*)target->GetDisplayName());
            }
            break;
        case ATGetInCargo:
            if (!target)
            {
                RptF("Get in action - missing target");
            }
            else
            {
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_GETIN_CARGO),
                         (const char*)target->GetDisplayName());
            }
            break;
        case ATHeal:
            if (!target)
            {
                RptF("Heal action - missing target");
            }
            else
            {
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_HEAL),
                         (const char*)target->GetDisplayName());
            }
            break;
        case ATRepair:
            if (!target)
            {
                RptF("Repair action - missing target");
            }
            else
            {
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_REPAIR),
                         (const char*)target->GetDisplayName());
            }
            break;
        case ATRefuel:
            if (!target)
            {
                RptF("Refuel action - missing target");
            }
            else
            {
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_REFUEL),
                         (const char*)target->GetDisplayName());
            }
            break;
        case ATRearm:
            if (!target)
            {
                RptF("Rearm action - missing target");
            }
            else
            {
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_REARM),
                         (const char*)target->GetDisplayName());
            }
            break;
        case ATTakeWeapon:
        {
            EntityAI* veh = target;
            if (veh)
            {
                Ref<WeaponType> weapon = WeaponTypes.New(param3);
                AUTO_STATIC_ARRAY(Ref<const WeaponType>, conflict, 16);
                if (veh->FindWeapon(weapon) && (!unit || unit->GetPerson()->CheckWeapon(weapon, conflict)))
                {
                    char drop[1024];
                    drop[0] = 0;
                    int n = 0;
                    for (int i = 0; i < conflict.Size(); i++)
                    {
                        if (n > 0)
                        {
                            strncat(drop, ", ", sizeof(drop) - strlen(drop) - 1);
                        }
                        strncat(drop, (const char*)conflict[i]->GetDisplayName(), sizeof(drop) - strlen(drop) - 1);
                        n++;
                    }
                    if (n > 0)
                    {
                        snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_DROPTAKEWEAPON),
                                 (const char*)weapon->GetDisplayName(), drop);
                    }
                    else
                    {
                        snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_TAKEWEAPON),
                                 (const char*)weapon->GetDisplayName());
                    }
                }
                else
                {
                    RptF("Take weapon action - bad weapon %x %s", (void*)weapon,
                         weapon ? (const char*)weapon->GetName() : "");
                }
            }
            else
            {
                // no target
                Ref<WeaponType> weapon = WeaponTypes.New(param3);
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_TAKEWEAPON),
                         (const char*)weapon->GetDisplayName());
            }
        }
        break;
        case ATTakeMagazine:
        {
            EntityAI* veh = target;
            if (veh)
            {
                Ref<const Magazine> magazine = veh->FindMagazine(param3);
                AUTO_STATIC_ARRAY(Ref<const Magazine>, conflict, 16);
                if (magazine && (!unit || unit->GetPerson()->CheckMagazine(magazine, conflict)))
                {
                    char drop[1024];
                    drop[0] = 0;
                    int n = 0;
                    for (int i = 0; i < conflict.Size(); i++)
                    {
                        if (n > 0)
                        {
                            strncat(drop, ", ", sizeof(drop) - strlen(drop) - 1);
                        }
                        if (conflict[i]->_ammo > 0 && conflict[i]->_type != magazine->_type)
                        {
                            strncat(drop, (const char*)conflict[i]->_type->GetDisplayName(),
                                    sizeof(drop) - strlen(drop) - 1);
                            n++;
                        }
                    }
                    if (n > 0)
                    {
                        snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_DROPTAKEMAGAZINE),
                                 (const char*)magazine->_type->GetDisplayName(), drop);
                    }
                    else
                    {
                        snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_TAKEMAGAZINE),
                                 (const char*)magazine->_type->GetDisplayName());
                    }
                }
                else
                {
                    RptF("Take magazine action - bad magazine %x %s", (const void*)magazine,
                         magazine ? (const char*)magazine->_type->GetName() : "");
                }
            }
        }
        break;
        case ATDropWeapon:
        {
            Ref<WeaponType> weapon = WeaponTypes.New(param3);
            PoseidonAssert(weapon);
            if (target)
            {
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_PUT_WEAPON),
                         (const char*)weapon->GetDisplayName(), (const char*)target->GetDisplayName());
            }
            else
            {
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_DROP_WEAPON),
                         (const char*)weapon->GetDisplayName());
            }
            break;
        }
        case ATDropMagazine:
        {
            Ref<MagazineType> type = MagazineTypes.New(param3);
            PoseidonAssert(type);
            if (target)
            {
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_PUT_MAGAZINE),
                         (const char*)type->GetDisplayName(), (const char*)target->GetDisplayName());
            }
            else
            {
                snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_DROP_MAGAZINE),
                         (const char*)type->GetDisplayName());
            }
            break;
        }
        case ATGetOut:
            snprintf(buffer, sizeof(buffer), "%s", (const char*)LocalizeString(IDS_ACTION_GETOUT));
            break;
        case ATLightOn:
            snprintf(buffer, sizeof(buffer), "%s", (const char*)LocalizeString(IDS_ACTION_LIGHTON));
            break;
        case ATLightOff:
            snprintf(buffer, sizeof(buffer), "%s", (const char*)LocalizeString(IDS_ACTION_LIGHTOFF));
            break;
        case ATEngineOn:
            snprintf(buffer, sizeof(buffer), "%s", (const char*)LocalizeString(IDS_ACTION_ENGINEON));
            break;
        case ATEngineOff:
            snprintf(buffer, sizeof(buffer), "%s", (const char*)LocalizeString(IDS_ACTION_ENGINEOFF));
            break;
        case ATSwitchWeapon:
            FormatWeapon(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_WEAPON), target, param);
            break;
        case ATUseWeapon:
        {
            EntityAI* veh = target;
            if (veh)
            {
                const WeaponModeType* mode = veh->GetWeaponMode(param);
                if (mode)
                {
                    int left = 0;
                    const MagazineSlot& slot = veh->GetMagazineSlot(param);
                    const Magazine* magazine = slot._magazine;
                    // count magazines
                    for (int i = 0; i < veh->NMagazines(); i++)
                    {
                        const Magazine* m = veh->GetMagazine(i);
                        if (!m || m->_ammo == 0)
                        {
                            continue;
                        }
                        if (m->_type == magazine->_type)
                        {
                            left++;
                        }
                    }
                    snprintf(buffer, sizeof(buffer), mode->_useActionTitle, (const char*)mode->GetDisplayName(), left);
                }
                else
                {
                    snprintf(buffer, sizeof(buffer), "%s", (const char*)"Weapon Error");
                    RptF("Weapon action - bad weapon %d", param);
                }
            }
        }
        break;
        case ATUseMagazine:
        {
            EntityAI* veh = target;
            if (veh)
            {
                const Magazine* magazine = veh->FindMagazine(param, param2);
                if (magazine)
                {
                    // count magazines
                    int left = 0;
                    for (int i = 0; i < veh->NMagazines(); i++)
                    {
                        const Magazine* m = veh->GetMagazine(i);
                        if (!m || m->_ammo == 0)
                        {
                            continue;
                        }
                        if (m->_type == magazine->_type)
                        {
                            left++;
                        }
                    }
                    snprintf(buffer, sizeof(buffer), magazine->_type->_useActionTitle,
                             (const char*)magazine->_type->GetDisplayName(), left);
                }
                else
                {
                    RptF("Use magazine action - bad magazine %d:%d", param, param2);
                }
            }
        }
        break;
        case ATEject:
            snprintf(buffer, sizeof(buffer), "%s", (const char*)LocalizeString(IDS_ACTION_EJECT));
            break;
        case ATLoadMagazine:
        {
            EntityAI* veh = target;
            if (veh)
            {
                const Magazine* magazine = veh->FindMagazine(param, param2);
                if (magazine)
                {
                    snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_MAGAZINE),
                             (const char*)magazine->_type->GetDisplayName());
                }
                else
                {
                    RptF("Load magazine action - bad magazine %d:%d", param, param2);
                }
            }
        }
        break;
        case ATTakeFlag:
            snprintf(buffer, sizeof(buffer), "%s", (const char*)LocalizeString(IDS_ACTION_TAKEFLAG));
            break;
        case ATReturnFlag:
            snprintf(buffer, sizeof(buffer), "%s", (const char*)LocalizeString(IDS_ACTION_RETURNFLAG));
            break;
        case ATTurnIn:
            snprintf(buffer, sizeof(buffer), "%s", (const char*)LocalizeString(IDS_ACTION_TURNIN));
            break;
        case ATTurnOut:
            snprintf(buffer, sizeof(buffer), "%s", (const char*)LocalizeString(IDS_ACTION_TURNOUT));
            break;
        case ATSitDown:
            return LocalizeString(IDS_ACTION_SITDOWN);
        case ATSalute:
            return LocalizeString(IDS_ACTION_SALUTE);
        case ATHideBody:
            return LocalizeString(IDS_ACTION_HIDE_BODY);
        case ATNVGoggles:
            if (unit->GetPerson()->IsNVWanted())
            {
                return LocalizeString(IDS_ACTION_TAKEOFF_GOGGLES);
            }
            else
            {
                return LocalizeString(IDS_ACTION_TAKEON_GOGGLES);
            }
            break;
        default:
            if (target)
            {
                return target->GetActionName(*this);
            }
            break;
    }
    return buffer;
}

void UIActions::OnDraw()
{
    AIUnit* unit = GWorld->FocusOn();
    if (!unit)
    {
        return;
    }

    int n = Size();
    saturate(n, 1, _rows);

    float alpha = GetAlpha();
    if (alpha <= 0.01)
    {
        return;
    }

    PackedColor bgColorA = PackedColorRGB(_bgColor, toIntFloor(_bgColor.A8() * alpha));
    PackedColor selColorA = PackedColorRGB(_selColor, toIntFloor(_selColor.A8() * alpha));
    PackedColor textColorA = PackedColorRGB(_textColor, toIntFloor(_textColor.A8() * alpha));

    bool canUp = false;
    bool canDown = false;

    AUTO_STATIC_ARRAY(DrawActionInfo, array, 16);
    array.Resize(n);
    if (Size() == 0)
    {
        array[0].text = LocalizeString(IDS_NO_ACTION);
        array[0].selected = false;
    }
    else
    {
        int selected = FindSelected();
        PoseidonAssert(selected >= 0);
        int offset = 0;
        if (selected >= n)
        {
            offset = selected - n + 1;
        }
        canUp = offset > 0;
        canDown = Size() > offset + _rows;
        for (int i = 0; i < n; i++)
        {
            const UIAction& action = Get(offset + i);

            array[i].text = action.GetDisplayName(unit);
            array[i].selected = offset + i == selected;
        }
    }

    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();
    const float border = 0.005;

    float height = n * _h + 2 * border;
    float top = _bottom - height;
    float width = 0;
    for (int i = 0; i < n; i++)
    {
        saturateMax(width, GEngine->GetTextWidth(_size, _font, array[i].text));
    }
    width += 2 * border;
    float left = _right - width;

    DrawFrame(GLOB_SCENE->Preloaded(Corner), bgColorA, Rect2DPixel(left * w, top * h, width * w, height * h));
    top += border;

    const float hA = 0.015;
    const float wA = 0.7 * hA;
    const float wA2 = 0.5 * wA;
    if (canUp)
    {
        GEngine->DrawLine(Line2DPixel(CX(_right + border), CY(top + hA), CX(_right + border + wA), CY(top + hA)),
                          textColorA, textColorA);
        GEngine->DrawLine(Line2DPixel(CX(_right + border + wA), CY(top + hA), CX(_right + border + wA2), CY(top)),
                          textColorA, textColorA);
        GEngine->DrawLine(Line2DPixel(CX(_right + border + wA2), CY(top), CX(_right + border), CY(top + hA)),
                          textColorA, textColorA);
    }
    if (canDown)
    {
        float bottom = _bottom - border;
        GEngine->DrawLine(Line2DPixel(CX(_right + border), CY(bottom - hA), CX(_right + border + wA), CY(bottom - hA)),
                          textColorA, textColorA);
        GEngine->DrawLine(Line2DPixel(CX(_right + border + wA), CY(bottom - hA), CX(_right + border + wA2), CY(bottom)),
                          textColorA, textColorA);
        GEngine->DrawLine(Line2DPixel(CX(_right + border + wA2), CY(bottom), CX(_right + border), CY(bottom - hA)),
                          textColorA, textColorA);
    }

    for (int i = 0; i < n; i++)
    {
        PackedColor color = array[i].selected ? selColorA : textColorA;
        GEngine->DrawText(Point2DFloat(left + border, top), _size, _font, color, array[i].text);
        top += _h;
        if (array[i].selected)
        {
            float wT = GEngine->GetTextWidth(_size, _font, array[i].text);
            GEngine->DrawLine(Line2DPixel(CX(left + border), CY(top), CX(left + border + wT), CY(top)), color, color);
        }
    }
}

PackedColor InGameUI::ColorFromHit(float hit)
{
    const float halfValue = 0.4;
    const float minValue = 0.1;
    const float fullValue = 0.9;

    if (hit >= fullValue)
    {
        return tankColorFullDammage;
    }
    if (hit <= minValue)
    {
        return tankColor;
    }

    if (hit < halfValue)
    {
        float f = (hit - minValue) / (halfValue - minValue);
        Color c = Color((ColorVal)tankColor) * (1 - f) + Color((ColorVal)tankColorHalfDammage) * f;
        return PackedColor(c);
    }
    else
    {
        float f = (hit - halfValue) / (fullValue - halfValue);
        Color c = Color((ColorVal)tankColorHalfDammage) * (1 - f) + Color((ColorVal)tankColorFullDammage) * f;
        return PackedColor(c);
    }
}

void InGameUI::DrawTankDirection(const Camera& camera)
{
    if (_tankPos >= 1)
    {
        return;
    }
    AIUnit* unit = GWorld->FocusOn();
    PoseidonAssert(unit);
    EntityAI* veh = unit->GetVehicle();

    float tankLeft = tankX;

    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();

    Matrix4Val camInvTransform = camera.GetInvTransform();
    Vector3 dirHull(VRotate, camInvTransform, veh->Direction());
    Vector3 dirTurret(VRotate, camInvTransform, veh->GetWeaponDirection(0));
    Vector3 dirObsTurret(VRotate, camInvTransform, veh->GetEyeDirection());

    Matrix4 mrot(MZero);
    Matrix4 mtrans1 = Matrix4(MTranslation, Vector3(-0.5, 0, -0.5));
    Matrix4 mtrans2 = Matrix4(MTranslation, Vector3(0.5, 0, 0.5));

    Draw2DPars pars;
    pars.spec = NoZBuf | IsAlpha | IsAlphaFog | ClampU | ClampV;

    // Rect2D rect(tankLeft * w, tankY * h, tankW * w, tankH * h);

    const float size = 0.67;

    Rect2DPixel rect(w * (tankLeft + tankW * 0.5 - tankW * size * 0.5), h * (tankY + tankH * 0.5 - tankH * size * 0.5),
                     tankW * w * size, tankH * h * size);

#define v000 VZero
#define v001 VForward
#define v100 VAside
#define v101 Vector3(1, 0, 1)

    mrot.SetUpAndDirection(VUp, dirHull);
    Matrix4 m = mtrans2 * mrot * mtrans1;
    Vector3 uv = m.FastTransform(v000);
    pars.uTR = uv[2];
    pars.vTR = uv[0];
    uv = m.FastTransform(v001);
    pars.uTL = uv[2];
    pars.vTL = uv[0];
    uv = m.FastTransform(v100);
    pars.uBR = uv[2];
    pars.vBR = uv[0];
    uv = m.FastTransform(v101);
    pars.uBL = uv[2];
    pars.vBL = uv[0];

    // draw all hull components
    pars.SetColor(ColorFromHit(veh->GetHitForDisplay(0)));
    pars.mip = GEngine->TextBank()->UseMipmap(_imageHull, 0, 0);
    GEngine->Draw2D(pars, rect, rect);

    pars.SetColor(ColorFromHit(veh->GetHitForDisplay(1)));
    pars.mip = GEngine->TextBank()->UseMipmap(_imageEngine, 0, 0);
    GEngine->Draw2D(pars, rect, rect);

    pars.SetColor(ColorFromHit(veh->GetHitForDisplay(3)));
    pars.mip = GEngine->TextBank()->UseMipmap(_imageLTrack, 0, 0);
    GEngine->Draw2D(pars, rect, rect);

    pars.SetColor(ColorFromHit(veh->GetHitForDisplay(2)));
    pars.mip = GEngine->TextBank()->UseMipmap(_imageRTrack, 0, 0);
    GEngine->Draw2D(pars, rect, rect);

    mrot.SetUpAndDirection(VUp, dirTurret);
    m = mtrans2 * mrot * mtrans1;
    uv = m.FastTransform(v000);
    pars.uTR = uv[2];
    pars.vTR = uv[0];
    uv = m.FastTransform(v001);
    pars.uTL = uv[2];
    pars.vTL = uv[0];
    uv = m.FastTransform(v100);
    pars.uBR = uv[2];
    pars.vBR = uv[0];
    uv = m.FastTransform(v101);
    pars.uBL = uv[2];
    pars.vBL = uv[0];

    // draw all turret components
    pars.SetColor(ColorFromHit(veh->GetHitForDisplay(4)));
    pars.mip = GEngine->TextBank()->UseMipmap(_imageTurret, 0, 0);
    GEngine->Draw2D(pars, rect, rect);

    pars.SetColor(ColorFromHit(veh->GetHitForDisplay(5)));
    pars.mip = GEngine->TextBank()->UseMipmap(_imageGun, 0, 0);
    GEngine->Draw2D(pars, rect, rect);

    Vector3 offset(0.05 * tankW, 0, 0);
    offset = mrot.FastTransform(offset);

    const float size2 = 0.25;
    // const float offX2 = 0.1;
    // const float offY2 = 0.0;

    Rect2DPixel rect2(w * (tankLeft + tankW * 0.5 - tankW * size2 * 0.5 + offset.X()),
                      h * (tankY + tankH * 0.5 - tankH * size2 * 0.5) - w * offset.Z(), tankW * w * size2,
                      tankH * h * size2);

    // observer turret
    pars.SetColor(ColorFromHit(veh->GetHitForDisplay(4)));
    mrot.SetUpAndDirection(VUp, dirObsTurret);
    m = mtrans2 * mrot * mtrans1;
    uv = m.FastTransform(v000);
    pars.uTR = uv[2];
    pars.vTR = uv[0];
    uv = m.FastTransform(v001);
    pars.uTL = uv[2];
    pars.vTL = uv[0];
    uv = m.FastTransform(v100);
    pars.uBR = uv[2];
    pars.vBR = uv[0];
    uv = m.FastTransform(v101);
    pars.uBL = uv[2];
    pars.vBL = uv[0];
    pars.mip = GEngine->TextBank()->UseMipmap(_imageObsTurret, 0, 0);
    GEngine->Draw2D(pars, rect2, rect2);
}

void InGameUI::DrawMenu()
{
    _commandMenuTapZones.Clear();

    if (_tmPos >= 1.0)
    {
        return;
    }

    Texture* textureDef = GLOB_SCENE->Preloaded(TextureWhite);
    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();
    const float border = 0.005;
    const bool touch = TouchInput_IsEnabled();
    // Bigger text and padded rows for touch - easier to read and tap on a
    // touchscreen. Keyboard/mouse users get the original compact size,
    // unchanged, since they don't need the extra room.
    const float size = touch ? 0.036f : 0.02f;
    const float rowPadY = touch ? 0.006f : 0.0f;
    const float rowHeight = size + 2 * rowPadY;

    float tmLeft = tmX + (1 - tmX) * _tmPos;

    // The config's tmW/tmH is sized for the default (small) text. Growing
    // `size` for touch without also growing the frame would overflow rows
    // past the background box, so touch instead sizes the frame to fit its
    // own (larger) content; keyboard keeps the exact original fixed frame.
    float frameW = tmW;
    float frameH = tmH;
    if (touch)
    {
        int visibleCount = 0;
        int separatorCount = 0;
        float maxRowWidth = 0;
        for (int i = 0; i < _menuCurrent->_items.Size(); i++)
        {
            MenuItem* item = _menuCurrent->_items[i];
            if (!item->_visible)
            {
                continue;
            }
            if (item->_cmd == CMD_SEPARATOR)
            {
                separatorCount++;
                continue;
            }
            visibleCount++;
            float ow = GEngine->GetTextWidth(size, _font24, item->_char);
            float tw = GEngine->GetTextWidth(size, _font24, item->_text);
            saturateMax(maxRowWidth, floatMax(0.02f, ow + 0.01f) + tw);
        }
        frameH = floatMax(tmH, visibleCount * rowHeight + separatorCount * 2 * border + 2 * border);
        frameW = floatMax(tmW, maxRowWidth + 2 * border);
    }

    Texture* corner = GLOB_SCENE->Preloaded(Corner);
    DrawFrame(corner, bgColor, Rect2DPixel(tmLeft * w, tmY * h, frameW * w, frameH * h));

    MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(textureDef, 0, 0);

    float width;
    width = GLOB_ENGINE->GetTextWidth(size, _font24, _menuCurrent->_text) + 2 * border;

    float height = size + 2 * border;
    float left = tmLeft + frameW - border - width;
    float top = tmY + frameH;
    GLOB_ENGINE->Draw2D(mip, capBgColor, Rect2DPixel(left * w, top * h, width * w, height * h));

    GLOB_ENGINE->DrawText(Point2DFloat(left + border, top + border), size, _font24, capFtColor, _menuCurrent->_text);

    top = tmY + border;
    height = rowHeight;
    PackedColor color;
    for (int i = 0; i < _menuCurrent->_items.Size(); i++)
    {
        MenuItem* item = _menuCurrent->_items[i];
        if (!item->_visible)
        {
            continue;
        }

        if (item->_cmd == CMD_SEPARATOR)
        {
            top += border;
            GEngine->DrawLine(Line2DPixel((tmLeft + border) * w, top * h, (tmLeft + frameW - border) * w, top * h),
                              menuDisabledColor, menuDisabledColor);
            top += border;
            continue;
        }

        if (item->_check)
        {
            color = menuCheckedColor;
        }
        else if (item->_enable)
        {
            color = menuEnabledColor;
        }
        else
        {
            color = menuDisabledColor;
        }

        bool bottom = item->_key == SDL_SCANCODE_BACKSPACE;
        float t = bottom ? tmY + frameH - border - height : top;

        if (item->_enable)
        {
            // Full row width, not just the text's bounding box - a bigger,
            // more forgiving touch target than a tight text-only hit-box.
            CommandMenuTapZone& zone = _commandMenuTapZones.Append();
            zone.x = tmLeft + border;
            zone.y = t;
            zone.w = frameW - 2 * border;
            zone.h = rowHeight;
            zone.key = item->_key;
        }

        const float textTop = t + rowPadY;
        GLOB_ENGINE->DrawText(Point2DFloat(tmLeft + border, textTop), size, _font24, color, item->_char);
        float ow = GEngine->GetTextWidth(size, _font24, item->_char);
        left = tmLeft + floatMax(0.02, ow + 0.01);
        GLOB_ENGINE->DrawText(Point2DFloat(left, textTop), size, _font24, color, item->_text);
        if (!bottom)
        {
            top += height;
        }
    }
}

int InGameUI::CommandMenuKeyAtTouch(float normX, float normY) const
{
    // _commandMenuTapZones is only populated while DrawMenu actually draws
    // rows (gated on _showMenu via ShouldShowGameplayHUD()/DrawHUD, and on
    // _tmPos - the menu's slide-in animation - internally) - if the menu
    // isn't open this frame, it's empty and every tap here is a miss.
    for (int i = 0; i < _commandMenuTapZones.Size(); i++)
    {
        const CommandMenuTapZone& zone = _commandMenuTapZones[i];
        if (normX >= zone.x && normX <= zone.x + zone.w && normY >= zone.y && normY <= zone.y + zone.h)
        {
            return zone.key;
        }
    }
    return 0;
}

void InGameUI::DrawTacticalDisplay(const Camera& camera, AIUnit* unit, const TargetList& list)
{
    Texture* textureDef = GLOB_SCENE->Preloaded(TextureWhite);
    MipInfo mip;
    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();
    AICenter* center = unit->GetGroup()->GetCenter();
    PoseidonAssert(center);

    Texture* corner = GLOB_SCENE->Preloaded(Corner);
    DrawFrame(corner, bgColor, Rect2DPixel(tdX * w, tdY * h, tdW * w, tdH * h));

    Vector3 dir = Vector3(0, 0, 1);
    float xAngle = atan2(dir.X(), dir.Z());
    float yAngle = atan2(dir.Y(), dir.SizeXZ());
    float leftAngle = atan(camera.Left());
    float topAngle = atan(camera.Top());
    float xMin = xAngle - leftAngle;
    float xMax = xAngle + leftAngle;
    float yMin = yAngle - topAngle;
    float yMax = yAngle + topAngle;
    if (xMax >= MIN_X && xMin <= MAX_X && yMax >= MIN_Y && yMin <= MAX_Y)
    {
        if (yMin < MIN_Y)
        {
            yMin = MIN_Y;
        }
        if (yMax > MAX_Y)
        {
            yMax = MAX_Y;
        }
        mip = GLOB_ENGINE->TextBank()->UseMipmap(textureDef, 0, 0);
        if (xMin < MIN_X)
        {
            xMin += TOT_W;
            GLOB_ENGINE->Draw2D(mip, cameraColor,
                                Rect2DPixel((tdX + 0.5 * tdW + xMin * tdW * INV_W) * w,
                                            (tdY + 0.5 * tdH - yMax * tdH * INV_H) * h,
                                            ((MAX_X - xMin) * tdW * INV_W) * w, ((yMax - yMin) * tdH * INV_H) * h));
            GLOB_ENGINE->Draw2D(mip, cameraColor,
                                Rect2DPixel((tdX + 0.5 * tdW + MIN_X * tdW * INV_W) * w,
                                            (tdY + 0.5 * tdH - yMax * tdH * INV_H) * h,
                                            ((xMax - MIN_X) * tdW * INV_W) * w, ((yMax - yMin) * tdH * INV_H) * h));
        }
        else if (xMax > MAX_X)
        {
            xMax -= TOT_W;
            GLOB_ENGINE->Draw2D(mip, cameraColor,
                                Rect2DPixel((tdX + 0.5 * tdW + xMin * tdW * INV_W) * w,
                                            (tdY + 0.5 * tdH - yMax * tdH * INV_H) * h,
                                            ((MAX_X - xMin) * tdW * INV_W) * w, ((yMax - yMin) * tdH * INV_H) * h));
            GLOB_ENGINE->Draw2D(mip, cameraColor,
                                Rect2DPixel((tdX + 0.5 * tdW + MIN_X * tdW * INV_W) * w,
                                            (tdY + 0.5 * tdH - yMax * tdH * INV_H) * h,
                                            ((xMax - MIN_X) * tdW * INV_W) * w, ((yMax - yMin) * tdH * INV_H) * h));
        }
        else
        {
            GLOB_ENGINE->Draw2D(mip, cameraColor,
                                Rect2DPixel((tdX + 0.5 * tdW + xMin * tdW * INV_W) * w,
                                            (tdY + 0.5 * tdH - yMax * tdH * INV_H) * h,
                                            ((xMax - xMin) * tdW * INV_W) * w, ((yMax - yMin) * tdH * INV_H) * h));
        }
        // GLOB_ENGINE->TextBank()->ReleaseMipmap();
    }

    Matrix4Val camInvTransform = camera.GetInvTransform();

    const VehicleType* nonstrategic = GWorld->Preloaded(VTypeNonStrategic);

    int n = list.Size();
    PackedColor color;
    bool modeRadar = true;
#if _ENABLE_CHEATS
    if (_showAll)
        modeRadar = false;
    if (tdCheat)
        modeRadar = false;
#endif

    EntityAI* myVeh = unit->GetVehicle();
    const VehicleType* manType = GWorld->Preloaded(VTypeMan);

    for (int i = 0; i < n; i++)
    {
        Target& tar = *list[i];
        EntityAI* vehExact = tar.idExact;

        if (unit->GetVehicle() == vehExact)
        {
            continue;
        }
        if (tar.vanished)
        {
            continue;
        }

        // tactical display has two modes, radar and show targets
        // each mode displays different information
        // info is distinguished by different "visible" calculation
        float visible = 0;
        float tDim = 0.004;
        Vector3 pos;
        TargetSide side = TSideUnknown;

        if (modeRadar)
        {
            // check landscape visibility and range
            if (!vehExact)
            {
                continue;
            }
            pos = vehExact->AimingPosition();

            // check visibility to target
            // (only for non-static)
            float dist2 = myVeh->Position().Distance2(pos);
            visible = myVeh->CalcVisibility(vehExact, dist2);

            // check if it is (or might be) man
            // if (vehExact->GetType()->IsKindOf(manType) )
            if (vehExact)
            {
                // should be detected when:
                // is IR target and we have IR scanner
                // or is laser target and we have laser scanner
                // IR scanner can be assumed as always present -
                // irTarget || laserTarget && laserScanner
                // !(!irTarget && (!laserTarget || !laserScanner))

                if (!vehExact->GetType()->_irTarget &&
                    // only laser scanner can see laser targets
                    (!vehExact->GetType()->GetLaserTarget() || !myVeh->GetType()->GetLaserScanner()))
                {
                    tDim = 0.002;
#if _ENABLE_CHEATS
                    if (!tdCheat)
#endif
                        continue;
                }
            }

            side = RadarTargetSide(unit, tar);
        }
        else
        {
            if (!tar.IsKnownBy(unit)
#if _ENABLE_CHEATS
                && !_showAll
#endif
            )
                continue;

            pos = (
#if _ENABLE_CHEATS
                _showAll && tar.idExact ? tar.idExact->AimingPosition() :
#endif
                                        tar.LandAimingPosition());
            visible = tar.FadingSpotability();
            // float visible=tar.FadingVisibility();
#if _ENABLE_CHEATS
            if (_showAll)
            {
                visible = 1;
                if (vehExact && !vehExact->GetType()->_irTarget)
                {
                    tDim = 0.002;
                }
            }
            else
#endif
            {
                if (tar.type->IsKindOf(manType) || tar.FadingAccuracy() < manType->GetAccuracy() ||
                    vehExact && !vehExact->GetType()->_irTarget)
                {
                    tDim = 0.002;
#if _ENABLE_CHEATS
                    if (!tdCheat)
#endif
                        continue;
                }
            }

            side = tar.type->_typicalSide;
        }

        if (visible <= 0.01)
        {
            continue;
        }

        if (vehExact && vehExact->GetType()->IsKindOf(nonstrategic))
        {
            continue;
        }

        dir = camInvTransform.Rotate(pos - camera.Position());

        xAngle = atan2(dir.X(), dir.Z());
        yAngle = atan2(dir.Y(), dir.SizeXZ());
        if (xAngle >= MIN_X && xAngle <= MAX_X && yAngle >= MIN_Y && yAngle <= MAX_Y)
        {
            float tDim2 = tDim * 2;
            float xScreen = (tdX + 0.5 * tdW + xAngle * tdW * INV_W - tDim) * w;
            float yScreen = (tdY + 0.5 * tdH - yAngle * tdH * INV_H - tDim) * h;

            if (tar.idExact && !tar.idExact->EngineIsOn())
            {
                AIGroup* grp = tar.idExact->GetGroup();
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
                side = tar.side;
                if (tar.destroyed)
                {
                    side = TCivilian;
                }
            }

            if (side == TCivilian)
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

            // float visible=tar.visibility;
            saturateMin(visible, 1);
            color.SetA8(toIntFloor(color.A8() * visible));
            mip = GLOB_ENGINE->TextBank()->UseMipmap(textureDef, 0, 0);
            GLOB_ENGINE->Draw2D(mip, color, Rect2DPixel(xScreen, yScreen, tDim2 * w, tDim2 * h),
                                Rect2DPixel(tdX * w, tdY * h, tdW * w, tdH * h));
            // GLOB_ENGINE->TextBank()->ReleaseMipmap();
        }
    }
}

void InGameUI::DrawCompass(EntityAI* vehicle)
{
    // Texture *textureDef = GLOB_SCENE->Preloaded(TextureWhite);
    Texture* texture;
    MipInfo mip;
    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();

    Texture* corner = GLOB_SCENE->Preloaded(Corner);
    DrawFrame(corner, bgColor, Rect2DPixel(coX * w, coY * h, coW * w, coH * h));

    //	Vector3Val dir = vehicle->Direction();
    Camera& cam = *GLOB_SCENE->GetCamera();
    Vector3Val dir = cam.Direction();

    // avoid singularity when heading up
    float xAngle;
    if (fabs(dir.X()) + fabs(dir.Z()) > 1e-3)
    {
        xAngle = atan2(dir.X(), dir.Z());
    }
    else
    {
        xAngle = 0;
    }
    xAngle *= 2 * INV_H_PI;
    for (int i = -2; i <= 2; i++)
    {
        float xLeft = xAngle + i;
        int xBase = toIntFloor(xLeft);
        if (xLeft - xBase >= 1.0)
        {
            xBase++;
        }
        int xBaseMod4 = xBase & (4 - 1);
        xLeft = xLeft + (xBaseMod4 - xBase);
        xBase = xBaseMod4;
        Poseidon::PreloadedTexture seg[4] = {Compass180, Compass270, Compass000, Compass090};
        texture = GLOB_SCENE->Preloaded(seg[xBase]);
        float xOffset = xLeft - xBase;
        mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
        float xScreen = (coX + 0.5 * coW + 0.25 * coW * (i - xOffset)) * w;
        GLOB_ENGINE->Draw2D(mip, compassColor, Rect2DPixel(xScreen, coY * h, 0.25 * coW * w, coH * h),
                            Rect2DPixel(coX * w, coY * h, coW * w, coH * h));
        // GLOB_ENGINE->TextBank()->ReleaseMipmap();
    }

    Rect2DPixel compassClip(coX * w, 0, coW * w, h);
    float ySize = coH * h * 0.5;
    float xSize = ySize * w / h * (3.0 / 4);
    float xPos = (coX + coW * 0.5) * w;
    PackedColor col = compassDirColor;
    GLOB_ENGINE->DrawLine(Line2DPixel(xPos, coY * h, xPos + xSize, coY * h - ySize), col, col, compassClip);
    GLOB_ENGINE->DrawLine(Line2DPixel(xPos, coY * h, xPos - xSize, coY * h - ySize), col, col, compassClip);

    int weapon = vehicle->SelectedWeapon();
    if (weapon >= 0)
    {
        // draw turret direction

        Vector3Val dir = vehicle->DirectionWorldToModel(vehicle->GetWeaponDirection(weapon));

        if (dir.SquareSizeXZ() > 0.1)
        {
            xAngle = atan2(dir.X(), dir.Z());
            float xPos = (coX + 0.5 * coW + xAngle * coW * INV_W) * w;
            PackedColor col = compassTurretDirColor;
            GLOB_ENGINE->DrawLine(Line2DPixel(xPos, coY * h, xPos + xSize, coY * h - ySize), col, col, compassClip);
            GLOB_ENGINE->DrawLine(Line2DPixel(xPos, coY * h, xPos - xSize, coY * h - ySize), col, col, compassClip);
        }
    }
}

void InGameUI::DrawUnitInfo(EntityAI* vehicle)
{
    if (!_unitInfo)
    {
        return;
    }

    AIUnit* unit = GWorld->FocusOn();
    PoseidonAssert(unit);
    AISubgroup* subgroup = unit->GetSubgroup();
    PoseidonAssert(subgroup);
    AIGroup* group = subgroup->GetGroup();
    PoseidonAssert(group);
    vehicle = unit->IsInCargo() ? unit->GetPerson() : unit->GetVehicle();
    const VehicleType* type = vehicle->GetType();
    Transport* transport = dyn_cast<Transport>(vehicle);

    PackedColor color, barBlinkColor;
    if (_blinkState)
    {
        barBlinkColor = barBlinkOnColor;
    }
    else
    {
        barBlinkColor = barBlinkOffColor;
    }

    UnitInfoType unitInfoType = type->GetUnitInfoType();
    if (unitInfoType == UnitInfoCar)
    {
        return; // car has no info
    }
    if (unitInfoType != _lastUnitInfoType)
    {
        PoseidonAssert(unitInfoType >= 0);
        RString name = (Res >> "RscInGameUI" >> "unitInfoTypes")[unitInfoType];
        _unitInfo->Reload(Res >> "RscInGameUI" >> name);
        _lastUnitInfoType = unitInfoType;
    }

    char buffer[256];
    Rank rank = unit->GetPerson()->GetRank();
    bool dirty = false;

    if (_unitInfo->time)
    {
        _unitInfo->time->SetTime(Glob.clock);
        dirty = true; // ???
    }
    if (_unitInfo->date)
    {
        Glob.clock.FormatDate(LocalizeString(IDS_DATE_FORMAT), buffer);
        if (_unitInfo->date->SetText(buffer))
        {
            dirty = true;
        }
    }
    if (_unitInfo->name)
    {
        if (_unitInfo->name->SetText(unit->GetPerson()->GetInfo()._name))
        {
            dirty = true;
        }
    }
    if (_unitInfo->unit)
    {
        snprintf(buffer, sizeof(buffer), "%d %s: %s", unit->ID(), group->GetName(),
                 (const char*)LocalizeString(IDS_PRIVATE + ClampRankIndex(rank)));
        if (_unitInfo->unit->SetText(buffer))
        {
            dirty = true;
        }
    }
    if (_unitInfo->vehicle)
    {
        if (_unitInfo->vehicle->SetText(vehicle->GetType()->GetDisplayName()))
        {
            dirty = true;
        }
    }
    if (_unitInfo->speed)
    {
        snprintf(buffer, sizeof(buffer), LocalizeString(IDS_UI_SPEED), 3.6 * vehicle->ModelSpeed().Z());
        if (_unitInfo->speed->SetText(buffer))
        {
            dirty = true;
        }
    }
    if (_unitInfo->alt)
    {
        Vector3Val pos = vehicle->Position();
        float y = pos.Y() + vehicle->GetShape()->Min().Y();
        float y0 = GLOB_LAND->RoadSurfaceYAboveWater(pos.X(), pos.Z());
        snprintf(buffer, sizeof(buffer), LocalizeString(IDS_UI_ALT), y - y0);
        if (_unitInfo->alt->SetText(buffer))
        {
            dirty = true;
        }
    }
    if (_unitInfo->valueExp)
    {
        float expCur = unit->GetPerson()->GetExperience() - AI::ExpForRank(rank);
        float expMax =
            rank >= RankColonel ? AI::ExpForRank(RankColonel) : AI::ExpForRank((Rank)(rank + 1)) - AI::ExpForRank(rank);
        PoseidonAssert(expMax > 0);
        float expCoef = 0.9 * (expCur / expMax);
        saturate(expCoef, 0, 1);
        _unitInfo->valueExp->SetPos(expCoef);
    }
    if (_unitInfo->formation)
    {
        if (_unitInfo->formation->SetText(LocalizeString(IDS_COLUMN + subgroup->GetFormation())))
        {
            dirty = true;
        }
    }
    if (_unitInfo->combatMode)
    {
        if (_unitInfo->combatMode->SetText(LocalizeString(IDS_IGNORE + unit->GetSemaphore())))
        {
            dirty = true;
        }
    }
    if (_unitInfo->valueHealth)
    {
        float health = 0;
        Vehicle* person = unit->GetPerson();
        if (person)
        {
            health = 1 - person->GetTotalDammage();
            saturate(health, 0, 1);
        }
        if (health > 0.5)
        {
            color = barGreenColor;
        }
        else if (health > 0.3)
        {
            color = barYellowColor;
        }
        else if (health > 0.15)
        {
            color = barRedColor;
        }
        else
        {
            color = barBlinkColor;
        }
        _unitInfo->valueHealth->SetPos(health);
        _unitInfo->valueHealth->SetBarColor(color);
    }
    if (_unitInfo->valueArmor)
    {
        float armor = 1 - vehicle->GetTotalDammage();
        saturate(armor, 0, 1);
        if (armor > 0.5)
        {
            color = barGreenColor;
        }
        else if (armor > 0.3)
        {
            color = barYellowColor;
        }
        else if (armor > 0.15)
        {
            color = barRedColor;
        }
        else
        {
            color = barBlinkColor;
        }
        _unitInfo->valueArmor->SetPos(armor);
        _unitInfo->valueArmor->SetBarColor(color);
    }
    if (_unitInfo->valueFuel)
    {
        float fuel = 0;
        float fuelCapacity = vehicle->GetType()->GetFuelCapacity();
        if (fuelCapacity > 0)
        {
            fuel = vehicle->GetFuel() / fuelCapacity;
            saturate(fuel, 0, 1);
        }
        if (fuel > 0.5)
        {
            color = barGreenColor;
        }
        else if (fuel > 0.3)
        {
            color = barYellowColor;
        }
        else if (fuel > 0.15)
        {
            color = barRedColor;
        }
        else
        {
            color = barBlinkColor;
        }
        _unitInfo->valueFuel->SetPos(fuel);
        _unitInfo->valueFuel->SetBarColor(color);
    }

    if (_unitInfo->cargoMan)
    {
        if (transport && transport->GetMaxManCargo() > 0)
        {
            _unitInfo->cargoMan->ShowCtrl(true);
            snprintf(buffer, sizeof(buffer), LocalizeString(IDS_UI_CARGO_INF), transport->GetManCargoSize());
            if (_unitInfo->cargoMan->SetText(buffer))
            {
                dirty = true;
            }
        }
        else
        {
            _unitInfo->cargoMan->ShowCtrl(false);
        }
    }
    if (_unitInfo->cargoFuel)
    {
        if (transport && transport->GetMaxFuelCargo() > 0)
        {
            _unitInfo->cargoFuel->ShowCtrl(true);
            snprintf(buffer, sizeof(buffer), LocalizeString(IDS_UI_CARGO_FUEL), transport->GetFuelCargo());
            if (_unitInfo->cargoFuel->SetText(buffer))
            {
                dirty = true;
            }
        }
        else
        {
            _unitInfo->cargoFuel->ShowCtrl(false);
        }
    }
    if (_unitInfo->cargoRepair)
    {
        if (transport && transport->GetMaxRepairCargo() > 0)
        {
            _unitInfo->cargoRepair->ShowCtrl(true);
            snprintf(buffer, sizeof(buffer), LocalizeString(IDS_UI_CARGO_REPAIR), transport->GetRepairCargo());
            if (_unitInfo->cargoRepair->SetText(buffer))
            {
                dirty = true;
            }
        }
        else
        {
            _unitInfo->cargoRepair->ShowCtrl(false);
        }
    }
    if (_unitInfo->cargoAmmo)
    {
        if (transport && transport->GetMaxAmmoCargo() > 0)
        {
            _unitInfo->cargoAmmo->ShowCtrl(true);
            snprintf(buffer, sizeof(buffer), LocalizeString(IDS_UI_CARGO_AMMO), transport->GetAmmoCargo());
            if (_unitInfo->cargoAmmo->SetText(buffer))
            {
                dirty = true;
            }
        }
        else
        {
            _unitInfo->cargoAmmo->ShowCtrl(false);
        }
    }

    int weapon = vehicle->SelectedWeapon();
    int ValidateWeapon(EntityAI * vehicle, int weapon);
    weapon = ValidateWeapon(vehicle, weapon);
    if (weapon >= 0 && weapon < vehicle->NMagazineSlots())
    {
        // non-default weapon is always shown
        if (weapon != 0)
        {
            dirty = true;
        }

        int ammoCur = 0;
        int maxAmmoCur = 0;
        bool noAmmo = true;
        //		int ammoSum = 0;
        //		int maxAmmoSum = 0;
        int magazines = 0;
        float reload = 0;
        RString displayName;

        const MagazineSlot& slot = vehicle->GetMagazineSlot(weapon);
        displayName = slot._muzzle->GetDisplayName();
        const Magazine* magazine = slot._magazine;
        const WeaponModeType* mode = nullptr;
        if (magazine)
        {
            const MagazineType* type = magazine->_type;
            if (type && slot._mode >= 0 && slot._mode < type->_modes.Size())
            {
                mode = type->_modes[slot._mode];
            }
        }
        if (mode)
        {
            displayName = mode->GetDisplayName();
            if (mode->_ammo)
            {
                ammoCur = magazine->_ammo;
                maxAmmoCur = magazine->_type->_maxAmmo;
                if (mode->_reloadTime == 0)
                {
                    reload = 0;
                }
                else
                {
                    reload = (magazine->_reloadMagazine + magazine->_reload) / mode->_reloadTime;
                }

                // reserve magazines
                for (int i = 0; i < vehicle->NMagazines(); i++)
                {
                    const Magazine* reserve = vehicle->GetMagazine(i);
                    if (reserve == magazine)
                    {
                        continue;
                    }
                    if (reserve->_type == magazine->_type)
                    {
                        if (reserve->_ammo > 0)
                        {
                            magazines++;
                        }
                    }
                }
            }
            noAmmo = ammoCur == 0;
            if (magazine->_type->_maxAmmo == 1)
            {
                ammoCur = magazines;
                if (magazine->_ammo > 0)
                {
                    ammoCur++;
                }
                magazines = 0;
            }
        }
        else if (slot._muzzle->_magazines.Size() > 0)
        {
            maxAmmoCur = slot._muzzle->_magazines[0]->_maxAmmo;
        }

        if (maxAmmoCur > 0)
        {
            if (/*ammoCur == 0*/ noAmmo || vehicle->IsActionInProgress(MFReload))
            {
                color = barRedColor;
            }
            else if (reload <= 0)
            {
                color = barGreenColor;
            }
            else if (reload <= 0.2)
            {
                color = barYellowColor;
            }
            else
            {
                color = barRedColor;
            }

            if (_unitInfo->weapon)
            {
                _unitInfo->weapon->ShowCtrl(true);
                _unitInfo->weapon->SetFtColor(color);
                if (_unitInfo->weapon->SetText(displayName))
                {
                    dirty = true;
                }
            }
            if (_unitInfo->ammo)
            {
                _unitInfo->ammo->ShowCtrl(true);
                _unitInfo->ammo->SetFtColor(color);
                //				snprintf(buffer, sizeof(buffer), "%d", ammoSum);
                if (magazines > 0)
                {
                    snprintf(buffer, sizeof(buffer), "%d | %d", ammoCur, magazines);
                }
                else
                {
                    snprintf(buffer, sizeof(buffer), "%d", ammoCur);
                }
                if (_unitInfo->ammo->SetText(buffer))
                {
                    dirty = true;
                }
            }
        }
        else
        {
            // no ammo or no muzzle
            if (_unitInfo->weapon)
            {
                _unitInfo->weapon->ShowCtrl(true);
                _unitInfo->weapon->SetFtColor(barGreenColor);
                if (_unitInfo->weapon->SetText(displayName))
                {
                    dirty = true;
                }
            }
            if (_unitInfo->ammo)
            {
                if (_unitInfo->ammo->SetText(""))
                {
                    dirty = true;
                }
            }
        }
    }
    else
    {
        _unitInfo->weapon->ShowCtrl(false);
        _unitInfo->ammo->ShowCtrl(false);
    }

    if (dirty)
    {
        _lastUnitInfoTime = Glob.uiTime;
    }

    float age = 0;
    if (vehicle->GetType()->_hideUnitInfo)
    {
        age = Glob.uiTime - _lastUnitInfoTime;
    }
    if (age < piDimEndTime)
    {
        float alpha = 1.0;
        if (age > piDimStartTime)
        {
            alpha = (piDimEndTime - age) / (piDimEndTime - piDimStartTime);
        }
        _unitInfo->DrawHUD(vehicle, alpha);

        _hintTop = _unitInfo->background->Y() + _unitInfo->background->H() + 0.02;
    }
}

void InGameUI::DrawGroupDir(const Camera& camera, AIGroup* grp)
{
    if (!grp)
    {
        return;
    }

    float age = Glob.uiTime - _lastGroupDirTime;
    if (age >= groupDirDimEndTime)
    {
        return;
    }

    float alpha = 1.0;
    if (age > groupDirDimStartTime)
    {
        alpha = (groupDirDimEndTime - age) / (groupDirDimEndTime - groupDirDimStartTime);
    }

    PackedColor bgColorA = PackedColorRGB(bgColor, toIntFloor(bgColor.A8() * alpha));
    PackedColor ftColorA = PackedColorRGB(ftColor, toIntFloor(ftColor.A8() * alpha));

    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();

    Texture* corner = GLOB_SCENE->Preloaded(Corner);
    DrawFrame(corner, bgColorA, Rect2DPixel(gdX * w, gdY * h, gdW * w, gdH * h));

    Matrix4Val camInvTransform = camera.GetInvTransform();
    Vector3 dir(VRotate, camInvTransform, grp->MainSubgroup()->GetFormationDirection());
    dir[0] = -dir[0];

    Matrix4 mrot(MZero);
    Matrix4 mtrans1 = Matrix4(MTranslation, Vector3(-0.5, 0, -0.5));
    Matrix4 mtrans2 = Matrix4(MTranslation, Vector3(0.5, 0, 0.5));

    Draw2DPars pars;
    pars.spec = NoZBuf | IsAlpha | IsAlphaFog | ClampU | ClampV;

    const float size = 1.1;

    Rect2DPixel rect(w * (gdX + gdW * 0.5 - gdW * size * 0.5), h * (gdY + gdH * 0.5 - gdH * size * 0.5), gdW * w * size,
                     gdH * h * size);

#define v000 VZero
#define v001 VForward
#define v100 VAside
#define v101 Vector3(1, 0, 1)

    mrot.SetUpAndDirection(-VUp, -dir);
    Matrix4 m = mtrans2 * mrot * mtrans1;
    Vector3 uv = m.FastTransform(v000);
    pars.uTR = uv[2];
    pars.vTR = uv[0];
    uv = m.FastTransform(v001);
    pars.uTL = uv[2];
    pars.vTL = uv[0];
    uv = m.FastTransform(v100);
    pars.uBR = uv[2];
    pars.vBR = uv[0];
    uv = m.FastTransform(v101);
    pars.uBL = uv[2];
    pars.vBL = uv[0];

    pars.SetColor(ftColorA);
    pars.mip = GEngine->TextBank()->UseMipmap(_imageGroupDir, 0, 0);
    GEngine->Draw2D(pars, rect, rect);
}

void InGameUI::DrawHint()
{
    if (_hint->GetHint().GetLength() == 0)
    {
        return;
    }
    float age = Glob.uiTime - _hintTime;
    if (age < hintDimEndTime)
    {
        float alpha = 1.0;
        if (age > hintDimStartTime)
        {
            alpha = (hintDimEndTime - age) / (hintDimEndTime - hintDimStartTime);
        }
        _hint->SetPosition(_hintTop);
        _hint->DrawHUD(nullptr, alpha);
    }
}

void InGameUI::DrawGroupUnit(AIUnit* u, float xScreen, float yScreen, float alpha, int align)
{
    PoseidonAssert(u);
    int id = u->ID();
    UnitDescription& info = _groupInfo[id - 1];

    char unitID[8];
    FormatGroupUnitLabel(unitID, sizeof(unitID), id, info.leader, ShouldShowGroupUnitLeaderDebugSuffix());

    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();
    float size = 0.02;
    const float border = 0.005;

    float height = size;
    float width = GEngine->GetTextWidth(size, _font24, unitID);
    switch (align)
    {
        case ST_LEFT:
            break;
        case ST_CENTER:
            xScreen -= 0.5 * width;
            break;
        case ST_RIGHT:
            xScreen -= width;
            break;
    }

    PackedColor color;
    if (info.player || info.selected)
    {
        if (info.player)
        {
            color = uiColorPlayer;
        }
        else
        {
            color = uiColorSelected;
        }
        color.SetA8(toIntFloor(color.A8() * alpha));
        float x1 = (xScreen - border) * w;
        float x2 = (xScreen + width + border) * w;
        float y1 = yScreen * h;
        float y2 = (yScreen + height) * h;
        GEngine->DrawLine(Line2DPixel(x1, y1, x2, y1), color, color);
        GEngine->DrawLine(Line2DPixel(x2, y1, x2, y2), color, color);
        GEngine->DrawLine(Line2DPixel(x2, y2, x1, y2), color, color);
        GEngine->DrawLine(Line2DPixel(x1, y2, x1, y1), color, color);
    }

    color = teamColors[info.team];
    color.SetA8(toIntFloor(color.A8() * alpha));

    GEngine->DrawText(Point2DFloat(xScreen, yScreen), size, _font24, color, unitID);
}

void InGameUI::DrawGroupInfo(EntityAI* vehicle)
{
    _groupBarTapZoneCount = 0;

    AIUnit* unit = GWorld->FocusOn();
    PoseidonAssert(unit);
    AISubgroup* subgroup = unit->GetSubgroup();
    PoseidonAssert(subgroup);
    AIGroup* group = subgroup->GetGroup();
    PoseidonAssert(group);

    bool changed = false;
    bool isValid = false;
    int i;
    for (i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* u = group->UnitWithID(i + 1);
        UnitDescription& info = _groupInfo[i];
        bool valid = (u && u->GetSubgroup() && !group->GetReportedDown(u) && unit->IsGroupLeader());
        if (valid != info.valid)
        {
            info.valid = valid;
            changed = true;
        }
        if (!valid)
        {
            continue;
        }

        isValid = true;
        EntityAI* veh = u->GetVehicle();
        Transport* trans = u->GetVehicleIn();
        // const VehicleType *type = veh->GetType();

        if (trans != info.vehicle)
        {
            info.vehicle = trans;
            changed = true;
        }

        if (u->GetPerson() != info.person)
        {
            info.person = u->GetPerson();
            changed = true;
        }

        AIUnit::ResourceState problemStatus = group->GetWorstStateReported(u);
        bool problems = problemStatus >= AIUnit::RSCritical;
        if (problems != info.problems)
        {
            info.problems = problems;
            changed = true;
        }

        if (u->GetSemaphore() != info.semaphore)
        {
            info.semaphore = u->GetSemaphore();
            changed = true;
        }

        UnitDescription::Status status;
        Command::Message cmd = Command::NoCommand;
        int commander = -1;
        if (u->IsCommander())
        {
            status = UnitDescription::commander;
        }
        else if (u->IsGunner())
        {
            status = UnitDescription::gunner;
        }
        else if (u->IsInCargo())
        {
            status = UnitDescription::cargo;
            if (veh->CommanderUnit() && veh->CommanderUnit()->GetGroup() == group)
            {
                commander = veh->CommanderUnit()->ID();
            }
        }
        else if (u->GetSubgroup() == group->MainSubgroup())
        {
            if (u->IsAway())
            {
                status = UnitDescription::away;
            }
            else
            {
                status = UnitDescription::none;
            }
        }
        else if (!u->GetSubgroup()->HasCommand())
        {
            status = UnitDescription::wait;
        }
        else
        {
            cmd = u->GetSubgroup()->GetCommand()->_message;
            status = UnitDescription::command;
        }

        if (status != info.status)
        {
            info.status = status;
            changed = true;
        }
        if (cmd != info.cmd)
        {
            info.cmd = cmd;
            changed = true;
        }
        if (commander != info.vehCommander)
        {
            info.vehCommander = commander;
            changed = true;
        }

        bool player = u == unit;
        if (player != info.player)
        {
            info.player = player;
            changed = true;
        }

        bool playerVehicle;
        if (status == UnitDescription::cargo)
        {
            playerVehicle = player;
        }
        else
        {
            playerVehicle = veh == unit->GetVehicle();
        }
        if (playerVehicle != info.playerVehicle)
        {
            info.playerVehicle = playerVehicle;
            changed = true;
        }

        bool selected = Poseidon::GetSelectedUnit(i) != nullptr;
        if (selected != info.selected)
        {
            info.selected = selected;
            changed = true;
        }

        Team team = Poseidon::GetTeam(i);
        if (team != info.team)
        {
            info.team = team;
            changed = true;
        }

        int leader;
        if (u->GetSubgroup() == u->GetGroup()->MainSubgroup())
        {
            leader = -1;
        }
        else
        {
            leader = u->GetSubgroup()->Leader()->ID();
        }
        if (leader != info.leader)
        {
            info.leader = leader;
            changed = true;
        }
    }

    if (!isValid)
    {
        return;
    }

    if (changed)
    {
        _lastGroupInfoTime = Glob.uiTime;
    }
    float age = Glob.uiTime - _lastGroupInfoTime;
    const float startDim = 90, endDim = 95;
    float Alpha = 1;
    if (age > startDim)
    {
        // interpolate dim
        if (age < endDim)
        {
            float factor = (age - startDim) * (1 / (endDim - startDim));
            Alpha = (1 - factor) + groupInfoDim * factor;
        }
        else
        {
            Alpha = groupInfoDim;
        }
        // update alpha
    }
    PackedColor bgColorA = PackedColorRGB(bgColor, toIntFloor(bgColor.A8() * Alpha));
    PackedColor capLnColorA = PackedColorRGB(ftColor, toIntFloor(capLnColor.A8() * Alpha));
    PackedColor ftColorA = PackedColorRGB(ftColor, toIntFloor(ftColor.A8() * Alpha));

    // Texture *textureDef = GLOB_SCENE->Preloaded(TextureWhite);
    Texture* texture;
    MipInfo mip;
    const int w = GLOB_ENGINE->Width2D();
    const int h = GLOB_ENGINE->Height2D();
    const float border = 0.005;
    float size = 0.02;

    Texture* corner = GLOB_SCENE->Preloaded(Corner);
    DrawFrame(corner, bgColorA, Rect2DPixel(uiX * w, uiY * h, uiW * w, uiH * h));

    // ADD protection

    float left = uiX + border;
    float width = (uiW - (MAX_UNITS_PER_GROUP + 1) * border) * (1.0 / MAX_UNITS_PER_GROUP);
    float height = width * 2.0 / 3.0;
    PackedColor color;
    for (i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        UnitDescription& info = _groupInfo[i];
        if (!info.valid)
        {
            continue;
        }
        if (info.status == UnitDescription::commander)
        {
            if (info.vehicle->DriverBrain())
            {
                continue;
            }
        }
        if (info.status == UnitDescription::gunner)
        {
            if (info.vehicle->DriverBrain() || info.vehicle->CommanderBrain())
            {
                continue;
            }
        }

        if (_groupBarTapZoneCount < MAX_UNITS_PER_GROUP)
        {
            GroupBarTapZone& zone = _groupBarTapZones[_groupBarTapZoneCount++];
            zone.x = left;
            zone.y = uiY + border;
            zone.w = width;
            zone.h = height;
            zone.unitIds.Clear();
            zone.unitIds.Add(i + 1);
            if (info.vehicle)
            {
                // Fold in any other group unit sharing this vehicle whose own
                // slot got skipped above (a commander/gunner deferring to a
                // driver, or a gunner deferring to a commander) - they're
                // represented by this same icon, so a tap here should select
                // all of them, not just unit i+1.
                for (int j = 0; j < MAX_UNITS_PER_GROUP; j++)
                {
                    if (j == i || !_groupInfo[j].valid || _groupInfo[j].vehicle != info.vehicle)
                    {
                        continue;
                    }
                    const UnitDescription& other = _groupInfo[j];
                    const bool folded = (other.status == UnitDescription::commander && info.vehicle->DriverBrain()) ||
                                        (other.status == UnitDescription::gunner &&
                                         (info.vehicle->DriverBrain() || info.vehicle->CommanderBrain()));
                    if (folded)
                    {
                        zone.unitIds.Add(j + 1);
                    }
                }
            }
        }

        // draw rectangle

        // draw picture
        Texture* picture1 = nullptr;
        Texture* picture2 = nullptr;

        PoseidonAssert(info.person);
        if (info.vehicle == nullptr || info.status == UnitDescription::cargo)
        {
            int index = info.person->FindWeaponType(MaskSlotSecondary);
            if (index < 0)
            {
                index = info.person->FindWeaponType(MaskSlotPrimary);
            }
            if (index < 0)
            {
                index = info.person->FindWeaponType(MaskSlotHandGun);
            }

            if (index >= 0)
            {
                const WeaponType* weapon = info.person->GetWeaponSystem(index);
                picture1 = weapon->GetPicture();
                if (!picture1)
                {
                    picture1 = _imageDefaultWeapons;
                }
            }
            else
            {
                picture1 = _imageNoWeapons;
            }

            picture2 = info.person->GetPicture();
        }
        else
        {
            picture1 = info.vehicle->GetPicture();
        }

        if (picture1)
        {
            if (info.problems)
            {
                color = pictureProblemsColor;
            }
            else
            {
                color = pictureColor;
            }
            color.SetA8(toIntFloor(color.A8() * Alpha));
            mip = GLOB_ENGINE->TextBank()->UseMipmap(picture1, 0, 0);
            GLOB_ENGINE->Draw2D(mip, color, Rect2DPixel(left * w, (uiY + border) * h, width * w, height * h));
        }
        if (picture2)
        {
            if (info.problems)
            {
                color = pictureProblemsColor;
            }
            else
            {
                color = pictureColor;
            }
            color.SetA8(toIntFloor(color.A8() * Alpha));
            mip = GLOB_ENGINE->TextBank()->UseMipmap(picture2, 0, 0);

            float hh = 0.5 * height;
            float ww = 0.75 * hh;
            float xx = left + 0.5 * ww;
            float yy = (uiY + border) + 0.4 * height;

            GLOB_ENGINE->Draw2D(mip, color, Rect2DPixel(xx * w, yy * h, ww * w, hh * h));
        }

        // draw semaphore
        switch (info.semaphore)
        {
            case AI::SemaphoreBlue:
            case AI::SemaphoreGreen:
            case AI::SemaphoreWhite:
                // hold fire indication
                color = holdFireColor;
                color.SetA8(toIntFloor(holdFireColor.A8() * Alpha));
                mip = GLOB_ENGINE->TextBank()->UseMipmap(_imageSemaphore, 0, 0);
                GLOB_ENGINE->Draw2D(mip, color, Rect2DPixel(left * w, (uiY + border) * h, semW * w, semH * h));
                break;
        }

        // draw status
        const char* text = nullptr;
        switch (info.status)
        {
            case UnitDescription::none:
                break;
            case UnitDescription::wait:
                text = LocalizeString(IDS_STATE_WAIT);
                goto DrawCommand;
            case UnitDescription::away:
                text = LocalizeString(IDS_AWAY);
                goto DrawCommand;
            case UnitDescription::command:
            {
                int icmd = info.cmd;
                if (icmd == Command::AttackAndFire)
                {
                    icmd = Command::Attack;
                }
                text = LocalizeString(IDS_STATE_NOCOMMAND + icmd);
            }
            DrawCommand:
                if (text)
                {
                    //				float widthText = GLOB_ENGINE->GetTextWidth(size, _font24, text);
                    GLOB_ENGINE->DrawText(Point2DFloat(left + semW + border, uiY + 0.5 * border), size, _font24,
                                          ftColorA, text);
                }
                break;
            case UnitDescription::cargo:
            {
                if (info.vehicle)
                {
                    texture = info.vehicle->GetPicture();
                }
                else
                {
                    // in multiplayer, flag InCargo may be set but vehicle is nullptr yet
                    // Fail("No vehicle");
                    texture = nullptr;
                }
                float picW = 0.35 * width;
                float picH = 0.35 * height;
                if (texture)
                {
                    mip = GLOB_ENGINE->TextBank()->UseMipmap(texture, 0, 0);
                    GLOB_ENGINE->Draw2D(mip, ftColorA,
                                        Rect2DPixel((left + width - picW) * w, (uiY + border) * h, picW * w, picH * h));
                    // GLOB_ENGINE->TextBank()->ReleaseMipmap();
                    texture = nullptr;
                }
                if (info.vehCommander >= 0)
                {
                    float widthText = GLOB_ENGINE->GetTextWidthF(size, _font24, "%d", info.vehCommander);
                    GLOB_ENGINE->DrawTextF(Point2DFloat(left + width - 0.5 * (picW + widthText), uiY + border + picH),
                                           size, _font24, ftColorA, "%d", info.vehCommander);
                }
            }
            break;
        }

        // draw units No
        float yScreen = uiY + uiH - size - border;

        // commander
        const TransportType* type = info.vehicle ? info.vehicle->Type() : nullptr;
        if (info.status != UnitDescription::cargo && type && type->HasCommander())
        {
            AIUnit* u = info.vehicle->CommanderBrain();
            if (u && u->GetGroup() == group)
            {
                float xScreen = left + 2 * border;
                DrawGroupUnit(u, xScreen, yScreen, Alpha, ST_LEFT);
            }
        }

        // driver
        float driverPos = left + width - 2 * border;
        float gunnerPos = left + 0.5 * width;
        int driverAlign = ST_RIGHT;
        int gunnerAlign = ST_CENTER;

        if (!type || type->DriverIsCommander() || !type->HasGunner())
        {
            swap(driverPos, gunnerPos);
            swap(driverAlign, gunnerAlign);
        }

        {
            AIUnit* u = !info.vehicle || info.status == UnitDescription::cargo ? info.person->Brain()
                                                                               : info.vehicle->DriverBrain();
            if (u && u->GetGroup() == group)
            {
                // float xScreen = left + 0.5 * width;
                DrawGroupUnit(u, driverPos, yScreen, Alpha, driverAlign);
            }
        }

        // gunner
        if (info.status != UnitDescription::cargo && type && type->HasGunner())
        {
            AIUnit* u = info.vehicle->GunnerBrain();
            if (u && u->GetGroup() == group)
            {
                // float xScreen = left + width - 2 * border;
                DrawGroupUnit(u, gunnerPos, yScreen, Alpha, gunnerAlign);
            }
        }

        left += width + border;
    }
}

AutoArray<int> InGameUI::GroupBarUnitsAtTouch(float normX, float normY) const
{
    // DrawGroupInfo (and the _groupBarTapZones it records) only runs when
    // InGameUIDrawCursor.cpp's own "_showGroupInfo && leader && NUnits()>1"
    // check passes - if it doesn't run this frame, _groupBarTapZones holds
    // whatever it last recorded while the bar *was* shown. Re-check the same
    // condition here so a stale zone from an earlier frame can't be tapped
    // after the bar stops being relevant (e.g. the player loses group lead).
    if (!_showGroupInfo)
    {
        return AutoArray<int>();
    }
    AIUnit* unit = GWorld->FocusOn();
    if (!unit)
    {
        return AutoArray<int>();
    }
    AISubgroup* subgroup = unit->GetSubgroup();
    if (!subgroup)
    {
        return AutoArray<int>();
    }
    AIGroup* group = subgroup->GetGroup();
    if (!group || !unit->IsGroupLeader() || group->NUnits() <= 1)
    {
        return AutoArray<int>();
    }

    // Looks up the icon layout DrawGroupInfo itself recorded while rendering
    // (_groupBarTapZones), rather than re-deriving positions independently -
    // that guessing approach broke as soon as a vehicle crew (driver +
    // gunner + commander) got dedup'd onto one icon, since the "next" icon's
    // real position depends on how many earlier slots got folded away.
    for (int i = 0; i < _groupBarTapZoneCount; i++)
    {
        const GroupBarTapZone& zone = _groupBarTapZones[i];
        // Extend the hit zone upward (away from the screen's bottom edge,
        // never downward) beyond the drawn icon - the icons are small and
        // this is the bottom-most HUD element, both a fat-finger target and
        // right where iOS's own edge-swipe gesture already competes for the
        // touch.
        const float top = zone.y - zone.h;
        const float bottom = zone.y + zone.h;
        if (normX >= zone.x && normX <= zone.x + zone.w && normY >= top && normY <= bottom)
        {
            return zone.unitIds;
        }
    }
    return AutoArray<int>();
}
