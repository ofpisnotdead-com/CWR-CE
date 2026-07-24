#include <Poseidon/UI/Settings/ControlsConfig.hpp>

#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/UserActionDesc.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/UI/Settings/SettingsFile.hpp>

#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

namespace
{
RString KeyName(const UserActionDesc& desc)
{
    return RString("key") + RString(desc.name);
}
RString ModName(const UserActionDesc& desc)
{
    return RString("key") + RString(desc.name) + RString("_mod");
}

// True if the packed binding belongs in controls.cfg (keyboard scancodes
// or mouse buttons).  Joystick-class entries (STICK / STICK_AXIS /
// STICK_POV) are persisted via GamepadConfig instead.
bool IsKbmCode(int packedCode)
{
    if (packedCode < 0)
        return false;
    int dev = packedCode & INPUT_DEVICE_MASK;
    return dev == INPUT_DEVICE_KEYBOARD || dev == INPUT_DEVICE_MOUSE;
}
} // namespace

void ControlsConfig::LoadDefaults()
{
    UserActionDesc* descs = InputSubsystem::GetUserActionDesc();
    for (int i = 0; i < UAN; i++)
    {
        // Pull only the keyboard / mouse defaults — joystick defaults
        // ride along in GamepadConfig::LoadDefaults.
        bindings[i].Resize(0);
        modifiers[i].Resize(0);
        const KeyList& src = descs[i].keys;
        for (int j = 0; j < src.Size(); j++)
        {
            if (IsKbmCode(src[j]))
            {
                bindings[i].Add(src[j]);
                modifiers[i].Add(-1);
            }
        }
        bindings[i].Compact();
        modifiers[i].Compact();
    }
}

bool ControlsConfig::Load(const std::string& path)
{
    ParamFile cfg;
    if (!ReadSettingsFile(path, cfg))
        return false;

    UserActionDesc* descs = InputSubsystem::GetUserActionDesc();
    for (int i = 0; i < UAN; i++)
    {
        const ParamEntry* entry = cfg.FindEntry(KeyName(descs[i]));
        if (entry)
        {
            AutoArray<int>& slot = bindings[i];
            slot.Resize(0);
            int n = entry->GetSize();
            for (int j = 0; j < n; j++)
            {
                int code = (*entry)[j];
                // Defensive: drop joystick-class entries that somehow
                // ended up in controls.cfg (legacy file pre-split).
                // They'll be re-loaded from gamepad.cfg.
                if (code != -1 && IsKbmCode(code))
                    slot.Add(code);
            }
            slot.Compact();
        }

        // Modifiers are optional in the file; default to -1 per binding
        // when missing so old configs (no _mod lines) still load.
        AutoArray<int>& mods = modifiers[i];
        mods.Resize(0);
        const ParamEntry* modEntry = cfg.FindEntry(ModName(descs[i]));
        if (modEntry)
        {
            int n = modEntry->GetSize();
            for (int j = 0; j < n; j++)
                mods.Add((int)(*modEntry)[j]);
        }
        // Pad / truncate so the parallel array exactly matches the
        // primary binding list length.
        while (mods.Size() < bindings[i].Size())
            mods.Add(-1);
        if (mods.Size() > bindings[i].Size())
            mods.Resize(bindings[i].Size());
        mods.Compact();
    }
    return true;
}

bool ControlsConfig::Save(const std::string& path) const
{
    ParamFile cfg;
    UserActionDesc* descs = InputSubsystem::GetUserActionDesc();
    for (int i = 0; i < UAN; i++)
    {
        ParamEntry* entry = cfg.AddArray(KeyName(descs[i]));
        entry->Clear();
        const AutoArray<int>& slot = bindings[i];
        for (int j = 0; j < slot.Size(); j++)
        {
            // Defensive: only emit keyboard / mouse entries.  Joystick
            // bindings live in gamepad.cfg and InputSubsystem::SaveKeys
            // is responsible for splitting GInput.userKeys[] by device
            // class — this filter is belt-and-suspenders.
            if (IsKbmCode(slot[j]))
                entry->AddValue(slot[j]);
        }

        // Only emit a modifier line when at least one slot has a real
        // modifier — keeps the on-disk file readable and matches the
        // forward-compat assumption (missing _mod = all -1).
        const AutoArray<int>& mods = modifiers[i];
        bool any = false;
        for (int j = 0; j < mods.Size(); j++)
            if (mods[j] >= 0)
            {
                any = true;
                break;
            }
        if (any)
        {
            ParamEntry* modEntry = cfg.AddArray(ModName(descs[i]));
            modEntry->Clear();
            for (int j = 0; j < slot.Size(); j++)
                modEntry->AddValue(j < mods.Size() ? mods[j] : -1);
        }
    }

    return WriteSettingsFile(path, cfg);
}

} // namespace Poseidon
