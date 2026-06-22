#include <Poseidon/Input/InputProfile.hpp>
#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/UserActionDesc.hpp>
#include <algorithm>

namespace Poseidon
{

const std::vector<InputCode> InputProfile::emptyBindings_;
const std::vector<InputBinding> InputProfile::emptyBindingEntries_;

namespace
{
bool IsLegacyGamepadCode(int packedCode)
{
    const int dev = packedCode & INPUT_DEVICE_MASK;
    return dev == INPUT_DEVICE_STICK || dev == INPUT_DEVICE_STICK_AXIS || dev == INPUT_DEVICE_STICK_POV;
}
} // namespace

void InputProfile::Bind(UserAction action, InputCode code)
{
    Bind(action, InputBinding(code));
}

void InputProfile::Bind(UserAction action, InputBinding binding)
{
    int idx = static_cast<int>(action);
    if (idx < 0 || idx >= UAN)
        return;
    auto& binds = bindings_[idx];
    if (std::find(binds.begin(), binds.end(), binding) == binds.end())
    {
        binds.push_back(binding);
        MarkDirty(action);
    }
}

void InputProfile::Unbind(UserAction action, InputCode code)
{
    int idx = static_cast<int>(action);
    if (idx < 0 || idx >= UAN)
        return;
    auto& binds = bindings_[idx];
    auto oldSize = binds.size();
    binds.erase(std::remove_if(binds.begin(), binds.end(),
                               [code](const InputBinding& binding) { return binding.code == code; }),
                binds.end());
    if (binds.size() != oldSize)
        MarkDirty(action);
}

void InputProfile::Unbind(UserAction action, InputBinding binding)
{
    int idx = static_cast<int>(action);
    if (idx < 0 || idx >= UAN)
        return;
    auto& binds = bindings_[idx];
    auto oldSize = binds.size();
    binds.erase(std::remove(binds.begin(), binds.end(), binding), binds.end());
    if (binds.size() != oldSize)
        MarkDirty(action);
}

void InputProfile::ClearBindings(UserAction action)
{
    int idx = static_cast<int>(action);
    if (idx < 0 || idx >= UAN)
        return;
    bindings_[idx].clear();
    MarkDirty(action);
}

void InputProfile::ClearAll()
{
    for (int i = 0; i < UAN; ++i)
    {
        auto& binds = bindings_[i];
        binds.clear();
        codeCache_[i].clear();
        codeCacheDirty_[i] = false;
    }
}

const std::vector<InputCode>& InputProfile::GetBindings(UserAction action) const
{
    int idx = static_cast<int>(action);
    if (idx < 0 || idx >= UAN)
        return emptyBindings_;
    if (codeCacheDirty_[idx])
    {
        codeCache_[idx].clear();
        for (const InputBinding& binding : bindings_[idx])
            codeCache_[idx].push_back(binding.code);
        codeCacheDirty_[idx] = false;
    }
    return codeCache_[idx];
}

const std::vector<InputBinding>& InputProfile::GetBindingEntries(UserAction action) const
{
    int idx = static_cast<int>(action);
    if (idx < 0 || idx >= UAN)
        return emptyBindingEntries_;
    return bindings_[idx];
}

bool InputProfile::HasBinding(UserAction action, InputCode code) const
{
    int idx = static_cast<int>(action);
    if (idx < 0 || idx >= UAN)
        return false;
    const auto& binds = bindings_[idx];
    return std::find_if(binds.begin(), binds.end(),
                        [code](const InputBinding& binding) { return binding.code == code; }) != binds.end();
}

bool InputProfile::HasBinding(UserAction action, InputBinding binding) const
{
    int idx = static_cast<int>(action);
    if (idx < 0 || idx >= UAN)
        return false;
    const auto& binds = bindings_[idx];
    return std::find(binds.begin(), binds.end(), binding) != binds.end();
}

int InputProfile::BindingCount(UserAction action) const
{
    int idx = static_cast<int>(action);
    if (idx < 0 || idx >= UAN)
        return 0;
    return static_cast<int>(bindings_[idx].size());
}

void InputProfile::LoadDefaults()
{
    ClearAll();
    UserActionDesc* descs = InputSubsystem::GetUserActionDesc();
    for (int i = 0; i < UAN; i++)
    {
        const KeyList& keys = descs[i].keys;
        for (int j = 0; j < keys.Size(); j++)
        {
            if (IsLegacyGamepadCode(keys[j]))
                continue;
            InputCode code = InputCode::FromLegacy(keys[j]);
            if (code.valid())
                Bind(static_cast<UserAction>(i), code);
        }
    }
}

void InputProfile::SetBindingsFromLegacy(UserAction action, const int* keys, int count)
{
    int idx = static_cast<int>(action);
    if (idx < 0 || idx >= UAN)
        return;
    bindings_[idx].clear();
    for (int i = 0; i < count; i++)
    {
        InputCode code = InputCode::FromLegacy(keys[i]);
        if (code.valid())
            bindings_[idx].push_back(InputBinding(code));
    }
    MarkDirty(action);
}

void InputProfile::MarkDirty(UserAction action)
{
    int idx = static_cast<int>(action);
    if (idx < 0 || idx >= UAN)
        return;
    codeCacheDirty_[idx] = true;
}
} // namespace Poseidon
