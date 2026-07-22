#pragma once

// Bindings list page — base class for KB&M (and future Gamepad) binding
// browsers.  Owns:
//   - the 5-category model (On foot / Vehicles / Pilot / Gunner / Common)
//     drawn from ControlsCategory
//   - the scroll-list Provider that turns category contents into
//     Stepper (category picker) + Binding (per-action) + Action (reset)
//     rows
//   - the capture-modal pivot — clicking a binding row pushes the
//     subclass-supplied capture modal
//   - per-category Reset action — pushes a ConfirmPage
//
// Storage is InputSubsystem's per-context InputProfile table.  The current
// five user-facing categories are projected onto one or more InputContext
// profiles by BindingsPage.cpp.  Subclass supplies:
//   - DeviceNoun()        : "keyboard" / "gamepad" — used in confirm prompts
//   - DeviceFilter()      : returns true if a packed-int binding belongs
//                           to this device (so KbmPage shows keyboard
//                           bindings only, not the gamepad bits in the
//                           same userKeys[] slot)
//   - MakeCaptureModal()  : factory returning the Press*Page subclass

#include <Poseidon/UI/Options/ScrollListPage.hpp>

#include <Poseidon/Input/ControlsCategory.hpp>

#include <array>
#include <functional>
#include <memory>
#include <string>


namespace Poseidon
{
class BindingsPage : public ScrollListPage
{
  public:
    BindingsPage() = default;

    using SaveCallback     = std::function<void(int packedCode, int modifier, bool replaceConflict)>;
    using ConflictCallback = std::function<UserAction(int packedCode, int modifier)>;

    void RefreshAfterCapture();

  protected:
    // Per-device hooks.
    virtual const char* DeviceNoun() const = 0;
    virtual bool DeviceFilter(int packedCode) const = 0;
    virtual bool IsActionVisible(UserAction action, ControlsCategory category) const;
    virtual const char* ActionLabelOverride(UserAction action, ControlsCategory category) const;
    virtual const char* BindingDisplayOverride(UserAction action, ControlsCategory category, int slot) const;
    virtual bool ApplyCaptureOverride(ControlsCategory category,
                                      UserAction action,
                                      int slot,
                                      int packedCode,
                                      int modifier);
    virtual bool ClearCaptureOverride(ControlsCategory category, UserAction action, int slot);
    virtual bool ResetCategoryOverride(ControlsCategory category);
    virtual std::unique_ptr<OptionsPage> MakeCaptureModal(
        std::string actionLabel,
        std::string slotName,
        SaveCallback onSave,
        ConflictCallback onConflict) = 0;

    OptionsScrollList::Provider& ProviderRef() override final { return m_withClose; }

    // Binding rows can be cleared, so the list advertises the controller
    // Delete capability ("Y Delete") on top of the scroll-list defaults.
    ControllerUiScene GetControllerUiScene() const override
    {
        ControllerUiScene scene = ScrollListPage::GetControllerUiScene();
        scene.activeSection.capabilities |= CtrlDelete;
        scene.sceneCapabilities |= CtrlDelete;
        return scene;
    }

    void Mount(OptionsShell& shell) override;
    void Unmount(OptionsShell& shell) override;

    void ResetCurrentCategoryToDefaults();
    void ApplyCapture(int actionIdx, int slot, int packedCode, int modifier, bool replaceConflict);
    void ClearCapture(int actionIdx, int slot);

  private:
    class Provider : public OptionsScrollList::Provider
    {
      public:
        explicit Provider(BindingsPage* owner) : m_owner(owner) {}

        BindingsPage* const m_owner;

        int RowCount() const override
        {
            // 1 (category stepper) + N actions + 1 (reset)
            return 1 + VisibleActionCount() + 1;
        }

        const char* RowLabel(int row) const override;
        const char* RowDescription(int row) const override;
        OptionsScrollList::RowDef RowFor(int row) const override;
        int RowValue(int row) const override
        {
            return (row == 0) ? (int)m_owner->m_category : 0;
        }
        void SetRowValue(int row, int value) override;
        OptionsScrollList::Kind RowKind(int row) const override;
        const char* BindingPrimary(int row) const override;
        const char* BindingAlt(int row) const override;
        void OnBindingClicked(int row, int slot, Display& host) override;
        void OnBindingCleared(int row, int slot) override;
        void OnRowAction(int row, Display& host) override;
        const char* FindBindingConflict(const char* formatted,
                                        int excludeRow,
                                        int excludeSlot) const override;

      private:
        bool IsActionRow(int row) const
        {
            return row >= 1 && row <= VisibleActionCount();
        }
        int ActionIndex(int row) const
        {
            // Returns the UserAction enum value for a given row, or UAN if
            // out of range.  Row 0 is the category stepper; rows 1..N are
            // actions; row N+1 is the reset.
            if (!IsActionRow(row))
                return UAN;
            const UserAction* list = GetControlsCategoryActions(m_owner->m_category);
            int visible = 0;
            for (int i = 0; list[i] != UAN; ++i)
            {
                if (!m_owner->IsActionVisible(list[i], m_owner->m_category))
                    continue;
                if (visible == row - 1)
                    return (int)list[i];
                ++visible;
            }
            return UAN;
        }
        bool IsResetRow(int row) const
        {
            return row == 1 + VisibleActionCount();
        }
        int VisibleActionCount() const;

        // Buffer for the most recently formatted Primary / Alt cells —
        // BindingPrimary / BindingAlt return c_str() into these so the
        // caller can hold the pointer for one frame.
        mutable std::string m_primaryBuf;
        mutable std::string m_altBuf;
        mutable std::string m_resetDescBuf;

        // Cache for localized category names — refreshed on RowFor(0)
        // so OptionsScrollList can hold the c_str() pointers for the
        // stepper renderer without dangling.
        mutable std::array<std::string, ControlsCategoryCount> m_categoryText;
        mutable std::array<const char*, ControlsCategoryCount> m_categoryPtrs{};
        void RefreshCategoryNames() const;
    };

    ControlsCategory m_category = ControlsCategoryOnFoot;
    Provider m_provider{this};
    OptionsScrollList::WithCloseRow m_withClose{m_provider};

    friend class Provider;
};

} // namespace Poseidon
