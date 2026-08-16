#include <Poseidon/UI/Settings/ContextControlsConfig.hpp>

#include <Poseidon/Input/InputBinding.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/UserActionDesc.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/UI/Settings/SettingsFile.hpp>

#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{
namespace
{
constexpr int kContextControlsVersion = 3;
constexpr int kGamepadButtonA = 0;
constexpr int kGamepadButtonB = 1;
constexpr int kGamepadButtonX = 2;
constexpr int kGamepadButtonY = 3;
constexpr int kGamepadButtonLB = 4;
constexpr int kGamepadButtonRB = 5;
constexpr int kGamepadButtonLT = 6;
constexpr int kGamepadButtonRT = 7;
constexpr int kGamepadButtonBack = 8;
constexpr int kGamepadButtonStart = 9;
constexpr int kGamepadButtonRightStick = 11;

constexpr int kGamepadPovUp = 0;
constexpr int kGamepadPovRight = 2;
constexpr int kGamepadPovDown = 4;
constexpr int kGamepadPovLeft = 6;

constexpr int kGamepadAxisLeftX = 0;
constexpr int kGamepadAxisLeftY = 1;
constexpr int kGamepadAxisRightX = 3;
constexpr int kGamepadAxisRightY = 4;
constexpr int kGamepadAxisLeftTrigger = 5;
constexpr int kGamepadAxisRightTrigger = 2;

const char* ContextPrefix(InputContext ctx)
{
    switch (ctx)
    {
        case InputContext::Menu:
            return "ctxMenu";
        case InputContext::Infantry:
            return "ctxInfantry";
        case InputContext::CarDriver:
            return "ctxCarDriver";
        case InputContext::TankDriver:
            return "ctxTankDriver";
        case InputContext::TankGunner:
            return "ctxTankGunner";
        case InputContext::HeliPilot:
            return "ctxHeliPilot";
        case InputContext::PlanePilot:
            return "ctxPlanePilot";
        case InputContext::ShipDriver:
            return "ctxShipDriver";
        case InputContext::Gunner:
            return "ctxGunner";
        case InputContext::Spectator:
            return "ctxSpectator";
        case InputContext::Map:
            return "ctxMap";
        case InputContext::Chat:
            return "ctxChat";
        case InputContext::Editor:
            return "ctxEditor";
        default:
            return "ctxUnknown";
    }
}

RString BindingName(InputContext ctx, const UserActionDesc& desc)
{
    return RString(ContextPrefix(ctx)) + RString(desc.name);
}

RString ModifierName(InputContext ctx, const UserActionDesc& desc)
{
    return BindingName(ctx, desc) + RString("_mod");
}

RString ScaleName(InputContext ctx, const UserActionDesc& desc)
{
    return BindingName(ctx, desc) + RString("_scale");
}

void BindAxis(InputProfile& profile, UserAction action, int axis, float scale)
{
    profile.Bind(action, InputBinding(InputCode::GamepadAx(axis), InputCode{}, ActivationMode::OnHold, scale));
}

void BindAxis(InputProfile& profile, UserAction action, int axis, InputCode modifier, float scale)
{
    profile.Bind(action, InputBinding(InputCode::GamepadAx(axis), modifier, ActivationMode::OnHold, scale));
}

void BindButton(InputProfile& profile, UserAction action, int button)
{
    profile.Bind(action, InputCode::GamepadBtn(button));
}

void BindButton(InputProfile& profile, UserAction action, int button, InputCode modifier)
{
    profile.Bind(action, InputBinding(InputCode::GamepadBtn(button), modifier));
}

void BindPov(InputProfile& profile, UserAction action, int pov)
{
    profile.Bind(action, InputCode::GamepadPov(pov));
}

void BindPov(InputProfile& profile, UserAction action, int pov, InputCode modifier)
{
    profile.Bind(action, InputBinding(InputCode::GamepadPov(pov), modifier));
}

void ApplyCommonGamepadDefaults(InputProfile& profile)
{
    BindPov(profile, UAPrevAction, kGamepadPovUp);
    BindPov(profile, UANextAction, kGamepadPovDown);
    BindButton(profile, UAAction, kGamepadButtonA);

    BindButton(profile, UAMap, kGamepadButtonBack);
    BindPov(profile, UACompass, kGamepadPovLeft);
    BindPov(profile, UAWatch, kGamepadPovRight);
    BindButton(profile, UAHelp, kGamepadButtonStart);
    BindButton(profile, UAPersonView, kGamepadButtonB);
}

void ApplyCombatGamepadDefaults(InputProfile& profile)
{
    BindButton(profile, UAFire, kGamepadButtonRT);
    BindButton(profile, UAReloadMagazine, kGamepadButtonY);
    BindButton(profile, UAToggleWeapons, kGamepadButtonX);
    BindButton(profile, UAOptics, kGamepadButtonLT);
    BindButton(profile, UALookCenter, kGamepadButtonRightStick);
}

void ApplyInfantryGamepadDefaults(InputProfile& profile)
{
    ApplyCombatGamepadDefaults(profile);

    BindAxis(profile, UAMoveForward, kGamepadAxisLeftY, -1.0f);
    BindAxis(profile, UAMoveBack, kGamepadAxisLeftY, 1.0f);
    BindAxis(profile, UAMoveLeft, kGamepadAxisLeftX, -1.0f);
    BindAxis(profile, UAMoveRight, kGamepadAxisLeftX, 1.0f);

    BindAxis(profile, UAAimUp, kGamepadAxisRightY, -1.0f);
    BindAxis(profile, UAAimDown, kGamepadAxisRightY, 1.0f);
    BindAxis(profile, UAAimLeft, kGamepadAxisRightX, -1.0f);
    BindAxis(profile, UAAimRight, kGamepadAxisRightX, 1.0f);
}

void ApplyFreelookDefaults(InputProfile& profile)
{
    InputCode lb = InputCode::GamepadBtn(kGamepadButtonLB);
    profile.Bind(UALookAround, lb);
    BindAxis(profile, UALookUp, kGamepadAxisRightY, lb, -1.0f);
    BindAxis(profile, UALookDown, kGamepadAxisRightY, lb, 1.0f);
    BindAxis(profile, UALookLeft, kGamepadAxisRightX, lb, -1.0f);
    BindAxis(profile, UALookRight, kGamepadAxisRightX, lb, 1.0f);
}

void ApplyDirectFreelookDefaults(InputProfile& profile)
{
    BindAxis(profile, UALookUp, kGamepadAxisRightY, -1.0f);
    BindAxis(profile, UALookDown, kGamepadAxisRightY, 1.0f);
    BindAxis(profile, UALookLeft, kGamepadAxisRightX, -1.0f);
    BindAxis(profile, UALookRight, kGamepadAxisRightX, 1.0f);
    BindButton(profile, UALookCenter, kGamepadButtonRightStick);
}

void ApplyDriverGamepadDefaults(InputProfile& profile)
{
    BindAxis(profile, UAMoveForward, kGamepadAxisRightTrigger, 1.0f);
    BindAxis(profile, UAMoveBack, kGamepadAxisLeftTrigger, 1.0f);
    BindAxis(profile, UATurnLeft, kGamepadAxisLeftX, -1.0f);
    BindAxis(profile, UATurnRight, kGamepadAxisLeftX, 1.0f);
    BindButton(profile, UATurbo, kGamepadButtonRB);
    BindButton(profile, UAFire, kGamepadButtonLB);
}

void ApplyContextDefaults(InputContext ctx, InputProfile& profile)
{
    ApplyCommonGamepadDefaults(profile);

    switch (ctx)
    {
        case InputContext::Infantry:
            ApplyInfantryGamepadDefaults(profile);
            ApplyFreelookDefaults(profile);
            break;
        case InputContext::CarDriver:
            ApplyDriverGamepadDefaults(profile);
            ApplyDirectFreelookDefaults(profile);
            break;
        case InputContext::Map:
        case InputContext::Menu:
            BindButton(profile, UAMapZoomIn, kGamepadButtonRB);
            BindButton(profile, UAMapZoomOut, kGamepadButtonLB);
            break;
        default:
            break;
    }
}
} // namespace

void ContextControlsConfig::LoadDefaults()
{
    for (int i = 0; i < ContextCount; ++i)
    {
        profiles[i].LoadDefaults();
        ApplyContextDefaults(static_cast<InputContext>(i), profiles[i]);
    }
}

bool ContextControlsConfig::Load(const std::string& path)
{
    ParamFile cfg;
    if (!ReadSettingsFile(path, cfg))
        return false;

    int version = 0;
    if (auto* e = cfg.FindEntry("contextControlsVersion"))
        version = (int)*e;

    // A file written before newer actions existed has no entries for them. Seed
    // each profile with defaults first so those actions come up bound, then let
    // the file override the actions it does list.
    const bool seedDefaults = version < kContextControlsVersion;
    migratedOnLoad = seedDefaults;

    UserActionDesc* descs = InputSubsystem::GetUserActionDesc();
    for (int c = 0; c < ContextCount; ++c)
    {
        InputContext ctx = static_cast<InputContext>(c);
        InputProfile& profile = profiles[c];
        profile.ClearAll();
        if (seedDefaults)
        {
            profile.LoadDefaults();
            ApplyContextDefaults(ctx, profile);
        }
        for (int a = 0; a < UAN; ++a)
        {
            const ParamEntry* entry = cfg.FindEntry(BindingName(ctx, descs[a]));
            if (!entry)
                continue;
            profile.ClearBindings(static_cast<UserAction>(a));

            const ParamEntry* modEntry = cfg.FindEntry(ModifierName(ctx, descs[a]));
            const ParamEntry* scaleEntry = cfg.FindEntry(ScaleName(ctx, descs[a]));
            const int n = entry->GetSize();
            for (int i = 0; i < n; ++i)
            {
                InputCode code = InputCode::FromLegacy((int)(*entry)[i]);
                if (!code.valid())
                {
                    // Preserve an empty positional slot (a cleared primary that
                    // keeps its alt): gameplay skips it, the controls page shows a dash.
                    profile.Bind(static_cast<UserAction>(a), InputBinding{});
                    continue;
                }

                int modRaw = -1;
                if (modEntry && i < modEntry->GetSize())
                    modRaw = (int)(*modEntry)[i];
                InputCode modifier = modRaw >= 0 ? InputCode::FromLegacy(modRaw) : InputCode{};

                float scale = 1.0f;
                if (scaleEntry && i < scaleEntry->GetSize())
                    scale = (float)(*scaleEntry)[i];

                profile.Bind(static_cast<UserAction>(a), InputBinding(code, modifier, ActivationMode::OnHold, scale));
            }
        }
    }

    return true;
}

bool ContextControlsConfig::Save(const std::string& path) const
{
    ParamFile cfg;
    cfg.Add("contextControlsVersion", kContextControlsVersion);

    UserActionDesc* descs = InputSubsystem::GetUserActionDesc();
    for (int c = 0; c < ContextCount; ++c)
    {
        InputContext ctx = static_cast<InputContext>(c);
        const InputProfile& profile = profiles[c];
        for (int a = 0; a < UAN; ++a)
        {
            const auto& entries = profile.GetBindingEntries(static_cast<UserAction>(a));
            if (entries.empty())
                continue;

            ParamEntry* codes = cfg.AddArray(BindingName(ctx, descs[a]));
            codes->Clear();
            ParamEntry* mods = cfg.AddArray(ModifierName(ctx, descs[a]));
            mods->Clear();
            ParamEntry* scales = cfg.AddArray(ScaleName(ctx, descs[a]));
            scales->Clear();

            for (const InputBinding& binding : entries)
            {
                codes->AddValue(binding.code.toLegacy());
                mods->AddValue(binding.modifier.valid() ? binding.modifier.toLegacy() : -1);
                scales->AddValue(binding.scale);
            }
        }
    }

    return WriteSettingsFile(path, cfg);
}
} // namespace Poseidon
