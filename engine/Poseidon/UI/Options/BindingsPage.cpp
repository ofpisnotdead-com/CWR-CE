#include <Poseidon/UI/Options/BindingsPage.hpp>

#include <Poseidon/UI/Options/ConfirmPage.hpp>
#include <Poseidon/UI/Options/OptionsShell.hpp>

#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/InputBinding.hpp>
#include <Poseidon/Input/InputContext.hpp>
#include <Poseidon/Input/InputProfile.hpp>
#include <Poseidon/Input/UserActionDesc.hpp>
#include <Poseidon/Input/KeyInput.hpp>

#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <utility>
#include <vector>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

extern RString GetKeyName(int dikCode);

namespace
{
// Stringtable IDs for the 5 user-facing category names.  Looked up
// lazily so the labels honour the active language.
const char* const kCategoryStrIds[ControlsCategoryCount] = {
    "STR_DISP_OPT_CTL_CAT_ONFOOT", "STR_DISP_OPT_CTL_CAT_VEHICLES", "STR_DISP_OPT_CTL_CAT_PILOT",
    "STR_DISP_OPT_CTL_CAT_GUNNER", "STR_DISP_OPT_CTL_CAT_COMMON",
};

const char* ActionLabel(UserAction a)
{
    if (a < 0 || a >= UAN)
        return "";
    UserActionDesc* descs = InputSubsystem::GetUserActionDesc();
    return LocalizeString(descs[a].desc);
}

struct ContextList
{
    const InputContext* data = nullptr;
    int count = 0;
};

ContextList ContextsForCategory(ControlsCategory cat)
{
    static constexpr InputContext onFoot[] = {InputContext::Infantry};
    static constexpr InputContext vehicles[] = {InputContext::CarDriver, InputContext::TankDriver,
                                                InputContext::ShipDriver};
    static constexpr InputContext pilot[] = {InputContext::HeliPilot, InputContext::PlanePilot};
    static constexpr InputContext gunner[] = {InputContext::TankGunner, InputContext::Gunner};
    static constexpr InputContext common[] = {
        InputContext::Menu,       InputContext::Infantry,  InputContext::CarDriver,  InputContext::TankDriver,
        InputContext::TankGunner, InputContext::HeliPilot, InputContext::PlanePilot, InputContext::ShipDriver,
        InputContext::Gunner,     InputContext::Spectator, InputContext::Map,        InputContext::Chat,
        InputContext::Editor,
    };

    switch (cat)
    {
        case ControlsCategoryOnFoot:
            return {onFoot, static_cast<int>(std::size(onFoot))};
        case ControlsCategoryVehicles:
            return {vehicles, static_cast<int>(std::size(vehicles))};
        case ControlsCategoryPilot:
            return {pilot, static_cast<int>(std::size(pilot))};
        case ControlsCategoryGunner:
            return {gunner, static_cast<int>(std::size(gunner))};
        case ControlsCategoryCommon:
            return {common, static_cast<int>(std::size(common))};
        default:
            return {onFoot, static_cast<int>(std::size(onFoot))};
    }
}

InputContext PrimaryContextForCategory(ControlsCategory cat)
{
    ContextList contexts = ContextsForCategory(cat);
    return contexts.count > 0 ? contexts.data[0] : InputContext::Infantry;
}

InputBinding MakeBinding(int packedCode, int modifier)
{
    InputCode modCode = modifier >= 0 ? InputCode::FromLegacy(modifier) : InputCode{};
    return InputBinding(InputCode::FromLegacy(packedCode), modCode);
}

bool BindingMatches(const InputBinding& binding, int packedCode, int modifier)
{
    const int bindingMod = binding.modifier.valid() ? binding.modifier.toLegacy() : -1;
    return binding.code.toLegacy() == packedCode && bindingMod == modifier;
}

void RebindProfile(InputProfile& profile, UserAction action, const std::vector<InputBinding>& bindings)
{
    profile.ClearBindings(action);
    for (const InputBinding& binding : bindings)
        profile.Bind(action, binding);
}

void RemoveBindingFromProfile(InputProfile& profile, UserAction action, int packedCode, int modifier)
{
    std::vector<InputBinding> bindings = profile.GetBindingEntries(action);
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [packedCode, modifier](const InputBinding& binding)
                                  { return BindingMatches(binding, packedCode, modifier); }),
                   bindings.end());
    RebindProfile(profile, action, bindings);
}

void ApplyBindingToProfile(InputProfile& profile, UserAction action, int visibleSlot, InputBinding binding,
                           const std::function<bool(int)>& deviceFilter)
{
    std::vector<InputBinding> bindings = profile.GetBindingEntries(action);
    int visibleSeen = 0;
    int targetIdx = -1;
    for (int i = 0; i < static_cast<int>(bindings.size()); ++i)
    {
        if (!deviceFilter(bindings[i].code.toLegacy()))
            continue;
        if (visibleSeen == visibleSlot)
        {
            targetIdx = i;
            break;
        }
        visibleSeen++;
    }

    if (targetIdx >= 0)
        bindings[targetIdx] = binding;
    else
        bindings.push_back(binding);
    RebindProfile(profile, action, bindings);
}

} // namespace

void BindingsPage::Mount(OptionsShell& shell)
{
    ScrollListPage::Mount(shell);
}

void BindingsPage::Unmount(OptionsShell& shell)
{
    InputSubsystem::Instance().SaveKeys();
    ScrollListPage::Unmount(shell);
}

void BindingsPage::ResetCurrentCategoryToDefaults()
{
    auto& sub = InputSubsystem::Instance();
    UserActionDesc* descs = InputSubsystem::GetUserActionDesc();
    const UserAction* actions = GetControlsCategoryActions(m_category);
    ContextList contexts = ContextsForCategory(m_category);

    for (int a = 0; actions[a] != UAN; ++a)
    {
        UserAction action = actions[a];
        std::vector<InputBinding> defaults;
        const KeyList& keys = descs[action].keys;
        for (int i = 0; i < keys.Size(); ++i)
        {
            if (DeviceFilter(keys[i]) && keys[i] < INPUT_DEVICE_STICK)
                defaults.push_back(MakeBinding(keys[i], -1));
        }

        for (int c = 0; c < contexts.count; ++c)
            RebindProfile(sub.GetProfile(contexts.data[c]), action, defaults);
    }
}

void BindingsPage::ApplyCapture(int actionIdx, int slot, int packedCode, int modifier, bool replaceConflict)
{
    if (actionIdx < 0 || actionIdx >= UAN)
        return;
    if (ApplyCaptureOverride(m_category, static_cast<UserAction>(actionIdx), slot, packedCode, modifier))
        return;

    auto& sub = InputSubsystem::Instance();
    ContextList contexts = ContextsForCategory(m_category);

    // Conflict resolution: drop colliding (code, modifier) pairs from
    // other actions before assigning here.  Two bindings collide only
    // when both code AND modifier match — so "Ctrl+W" and bare "W"
    // coexist.
    if (replaceConflict)
    {
        for (int c = 0; c < contexts.count; ++c)
        {
            InputProfile& profile = sub.GetProfile(contexts.data[c]);
            for (int i = 0; i < UAN; i++)
            {
                if (i == actionIdx)
                    continue;
                RemoveBindingFromProfile(profile, static_cast<UserAction>(i), packedCode, modifier);
            }
        }
    }

    const InputBinding binding = MakeBinding(packedCode, modifier);
    auto filter = [this](int code) { return DeviceFilter(code); };
    for (int c = 0; c < contexts.count; ++c)
        ApplyBindingToProfile(sub.GetProfile(contexts.data[c]), static_cast<UserAction>(actionIdx), slot, binding,
                              filter);
}

void BindingsPage::RefreshAfterCapture()
{
    if (auto* l = List())
        l->RenderPage();
}

bool BindingsPage::IsActionVisible(UserAction, ControlsCategory) const
{
    return true;
}

const char* BindingsPage::ActionLabelOverride(UserAction, ControlsCategory) const
{
    return nullptr;
}

const char* BindingsPage::BindingDisplayOverride(UserAction, ControlsCategory, int) const
{
    return nullptr;
}

bool BindingsPage::ApplyCaptureOverride(ControlsCategory, UserAction, int, int, int)
{
    return false;
}

bool BindingsPage::ResetCategoryOverride(ControlsCategory)
{
    return false;
}

// Provider

void BindingsPage::Provider::RefreshCategoryNames() const
{
    for (int i = 0; i < ControlsCategoryCount; i++)
    {
        m_categoryText[i] = LocalizeString(kCategoryStrIds[i]);
        m_categoryPtrs[i] = m_categoryText[i].c_str();
    }
}

const char* BindingsPage::Provider::RowLabel(int row) const
{
    if (row == 0)
        return LocalizeString("STR_DISP_OPT_CTL_CATEGORY");
    if (IsActionRow(row))
    {
        UserAction action = (UserAction)ActionIndex(row);
        if (const char* label = m_owner->ActionLabelOverride(action, m_owner->m_category))
            return label;
        return ActionLabel((UserAction)ActionIndex(row));
    }
    if (IsResetRow(row))
        return LocalizeString("STR_DISP_OPT_CTL_RESET_CAT");
    return "";
}

const char* BindingsPage::Provider::RowDescription(int row) const
{
    if (row == 0)
        return LocalizeString("STR_DISP_OPT_CTL_CATEGORY_DESC");
    if (IsActionRow(row))
        return LocalizeString("STR_DISP_OPT_CTL_BINDING_DESC");
    if (IsResetRow(row))
    {
        char buf[128];
        snprintf(buf, sizeof(buf), (const char*)LocalizeString("STR_DISP_OPT_CTL_RESET_CAT_DESC"),
                 m_owner->DeviceNoun());
        m_resetDescBuf = buf;
        return m_resetDescBuf.c_str();
    }
    return "";
}

OptionsScrollList::RowDef BindingsPage::Provider::RowFor(int row) const
{
    if (row == 0)
    {
        RefreshCategoryNames();
        return {502, m_categoryPtrs.data(), ControlsCategoryCount};
    }
    return {-1, nullptr, 0};
}

void BindingsPage::Provider::SetRowValue(int row, int value)
{
    if (row == 0)
    {
        int v = value % ControlsCategoryCount;
        if (v < 0)
            v += ControlsCategoryCount;
        m_owner->m_category = (ControlsCategory)v;
    }
}

OptionsScrollList::Kind BindingsPage::Provider::RowKind(int row) const
{
    if (row == 0)
        return OptionsScrollList::KindStepper;
    if (IsActionRow(row))
        return OptionsScrollList::KindBinding;
    if (IsResetRow(row))
        return OptionsScrollList::KindAction;
    return OptionsScrollList::Provider::RowKind(row);
}

namespace
{
// Format a (code, modifier) pair as "Ctrl+W" / "W" / "" for display in
// the binding cell.  Modifier scancode of -1 (or out of keyboard range)
// drops the prefix.
std::string FormatBinding(int code, int modifier)
{
    if (code < 0)
        return "";
    std::string out;
    if (modifier >= 0)
    {
        out += (const char*)GetKeyName(modifier);
        out += "+";
    }
    out += (const char*)GetKeyName(code);
    return out;
}
} // namespace

const char* BindingsPage::Provider::BindingPrimary(int row) const
{
    m_primaryBuf.clear();
    if (!IsActionRow(row))
        return "";
    int actionIdx = ActionIndex(row);
    if (actionIdx < 0 || actionIdx >= UAN)
        return "";
    if (const char* display = m_owner->BindingDisplayOverride((UserAction)actionIdx, m_owner->m_category, 0))
        return display;
    auto& sub = InputSubsystem::Instance();
    const auto& bindings =
        sub.GetProfile(PrimaryContextForCategory(m_owner->m_category)).GetBindingEntries((UserAction)actionIdx);
    int seen = 0;
    for (const InputBinding& binding : bindings)
    {
        if (!m_owner->DeviceFilter(binding.code.toLegacy()))
            continue;
        if (seen == 0)
        {
            int slotMod = binding.modifier.valid() ? binding.modifier.toLegacy() : -1;
            m_primaryBuf = FormatBinding(binding.code.toLegacy(), slotMod);
            return m_primaryBuf.c_str();
        }
        seen++;
    }
    return "";
}

const char* BindingsPage::Provider::BindingAlt(int row) const
{
    m_altBuf.clear();
    if (!IsActionRow(row))
        return "";
    int actionIdx = ActionIndex(row);
    if (actionIdx < 0 || actionIdx >= UAN)
        return "";
    if (const char* display = m_owner->BindingDisplayOverride((UserAction)actionIdx, m_owner->m_category, 1))
        return display;
    auto& sub = InputSubsystem::Instance();
    const auto& bindings =
        sub.GetProfile(PrimaryContextForCategory(m_owner->m_category)).GetBindingEntries((UserAction)actionIdx);
    int seen = 0;
    for (const InputBinding& binding : bindings)
    {
        if (!m_owner->DeviceFilter(binding.code.toLegacy()))
            continue;
        if (seen == 1)
        {
            int slotMod = binding.modifier.valid() ? binding.modifier.toLegacy() : -1;
            m_altBuf = FormatBinding(binding.code.toLegacy(), slotMod);
            return m_altBuf.c_str();
        }
        seen++;
    }
    return "";
}

void BindingsPage::Provider::OnBindingClicked(int row, int slot, Display& host)
{
    if (!IsActionRow(row))
        return;
    auto* shell = dynamic_cast<OptionsShell*>(&host);
    if (!shell || !m_owner)
        return;

    int actionIdx = ActionIndex(row);
    if (actionIdx < 0 || actionIdx >= UAN)
        return;

    BindingsPage* page = m_owner;
    auto onSave = [page, actionIdx, slot](int packedCode, int modifier, bool replaceConflict)
    {
        page->ApplyCapture(actionIdx, slot, packedCode, modifier, replaceConflict);
        page->RefreshAfterCapture();
    };
    auto onConflict = [page, actionIdx, slot](int packedCode, int modifier) -> UserAction
    {
        auto& sub = InputSubsystem::Instance();
        const InputProfile& profile = sub.GetProfile(PrimaryContextForCategory(page->m_category));
        for (int i = 0; i < UAN; ++i)
        {
            int visibleSlot = 0;
            const auto& bindings = profile.GetBindingEntries(static_cast<UserAction>(i));
            for (const InputBinding& binding : bindings)
            {
                if (!page->DeviceFilter(binding.code.toLegacy()))
                    continue;
                if (!BindingMatches(binding, packedCode, modifier))
                {
                    visibleSlot++;
                    continue;
                }
                if (i == actionIdx && visibleSlot == slot)
                    continue;
                return static_cast<UserAction>(i);
            }
        }
        return UAN;
    };

    shell->PushPage(m_owner->MakeCaptureModal(ActionLabel((UserAction)actionIdx), slot == 0 ? "Primary" : "Alt",
                                              std::move(onSave), std::move(onConflict)));
}

void BindingsPage::Provider::OnRowAction(int row, Display& host)
{
    if (!IsResetRow(row))
        return;
    auto* shell = dynamic_cast<OptionsShell*>(&host);
    if (!shell || !m_owner)
        return;

    BindingsPage* page = m_owner;
    char title[160];
    snprintf(title, sizeof(title), "Reset \"%s\" bindings?",
             (const char*)LocalizeString(kCategoryStrIds[m_owner->m_category]));
    char body[160];
    snprintf(body, sizeof(body), "All current %s bindings in this category will be replaced with defaults.",
             m_owner->DeviceNoun());
    auto onYes = [page]()
    {
        if (!page->ResetCategoryOverride(page->m_category))
            page->ResetCurrentCategoryToDefaults();
        page->RefreshAfterCapture();
    };
    shell->PushPage(std::make_unique<ConfirmPage>(title, body, std::move(onYes), "Reset", "Cancel"));
}

const char* BindingsPage::Provider::FindBindingConflict(const char* /*formatted*/, int /*excludeRow*/,
                                                        int /*excludeSlot*/) const
{
    // Conflict scan happens in the capture modal's onConflict callback,
    // not here.  Returning empty keeps the scroll-list happy.
    return "";
}

int BindingsPage::Provider::VisibleActionCount() const
{
    int count = 0;
    const UserAction* list = GetControlsCategoryActions(m_owner->m_category);
    for (int i = 0; list[i] != UAN; ++i)
        if (m_owner->IsActionVisible(list[i], m_owner->m_category))
            ++count;
    return count;
}

} // namespace Poseidon
