#pragma once

#include <Poseidon/Input/InputBinding.hpp>
#include <Poseidon/Input/InputCode.hpp>
#include <Poseidon/Input/UserAction.hpp>
#include <array>
#include <vector>

namespace Poseidon
{

// InputProfile — binding table mapping UserAction → list of InputCode.
// Each InputContext gets its own profile.
class InputProfile
{
  public:
    InputProfile() = default;

    void Bind(UserAction action, InputCode code);
    void Bind(UserAction action, InputBinding binding);
    void Unbind(UserAction action, InputCode code);
    void Unbind(UserAction action, InputBinding binding);
    void ClearBindings(UserAction action);
    void ClearAll();

    const std::vector<InputCode>& GetBindings(UserAction action) const;
    const std::vector<InputBinding>& GetBindingEntries(UserAction action) const;
    bool HasBinding(UserAction action, InputCode code) const;
    bool HasBinding(UserAction action, InputBinding binding) const;
    int BindingCount(UserAction action) const;

    // Populate from existing UserActionDesc defaults (for compatibility)
    void LoadDefaults();

    // Populate from an array of raw packed ints (legacy format)
    void SetBindingsFromLegacy(UserAction action, const int* keys, int count);

  private:
    std::array<std::vector<InputBinding>, UAN> bindings_;
    mutable std::array<std::vector<InputCode>, UAN> codeCache_;
    mutable std::array<bool, UAN> codeCacheDirty_{};
    static const std::vector<InputCode> emptyBindings_;
    static const std::vector<InputBinding> emptyBindingEntries_;

    void MarkDirty(UserAction action);
};

// Default modifier scancode for an action's default key, or -1 for none. Used at every
// site that rebuilds default bindings from the bare UserActionDesc key list, so a combo
// default (currently only the cheat-entry trigger) survives load and reset.
int DefaultModifierForDefaultKey(UserAction action, int packedKey);
} // namespace Poseidon

