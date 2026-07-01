#pragma once

// OptionsScrollList — reusable scrolling-row runtime for OptionsTest
// settings pages.
//
// Hosts a virtual list of N logical rows displayed in K visible slots
// (default 7) on a 3D notebook surface.  Multiple Options screens share
// this shape — Audio, Graphics, Controls, Network — they only differ
// in the row labels, value tables, and read-only meter rows.  Each
// page provides a Provider implementation; this class owns scrolling,
// focus, marquee, mouse-wheel + scrollbar + slider drag, hover-to-
// focus, and the per-frame poll orchestration.
//
// Resource conventions the host's RscDisplay must satisfy (verbatim
// from the harness 3D Options shape):
//
//   Slot N (0..6) controls live on a Notebook (idc=105) with idcs:
//     5N0  Label        5N1  ValueStep    5N2  ValueBar
//     5N3  Hover        5N4  Track        5N5  Fill
//     5N6  Peak         5N7  BarClick
//     5N8  Prev (<)     5N9  Next (>)
//   700+N RowBg (one shown at a time to mark the focused row)
//   590   Hint label    850  ScrollBar (CT_3DSCROLLBAR)
//
// Close lives inside the row list as a KindAction row — every page in
// the new Options follows the same pattern, so the menu is the only
// focusable surface and the focus cycle is just rows-wrap.  Esc is
// still routed to IDC_CANCEL by Display::OnKeyDown, redundant with
// pressing Enter on the Close action row.

#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>


namespace Poseidon
{
class OptionsScrollList
{
public:
	enum : int {
		kVisibleSlots   = 9,
		kInnerChars     = 16,    // stepper value cell width in chars
		kLabelInnerChars = 18,   // left label column width in chars
		kBindingLabelInnerChars = 11, // narrower left label width when bindings show chevrons
		// Matches the hint control's visible glyph budget.  Keep close to
		// the real 3D width so long descriptions marquee before clipping.
		kHintInnerChars = 50,
		kPauseMs        = 1000,
		kScrollPeriodMs = 100,
		// Spaces between repeated iterations of a marquee'd string.
		// Trade-off:
		//   bigger → more obvious gap, but a longer "blank-edge" phase
		//             where new text suddenly materialises on the right
		//             reads as a jump
		//   smaller → continuous flow with end-of-text and start-of-
		//             next-text close together; no perceived jump
		// 1 space gives a sentence-boundary feel (". X" reads as
		// "sentence end. New sentence start") without the wrap looking
		// like a teleport.
		kMarqueeSepLen  = 1,
	};

	// IDCs from the shared scroll-list resource bundle (RscOptionsPageScrollList).
	// Slot N (0..6) controls live on the notebook with idcs:
	//     5N0 Label   5N1 ValueStep   5N2 ValueBar
	//     5N3 Hover   5N4 Track       5N5 Fill
	//     5N6 Peak    5N7 BarClick    5N8 Prev    5N9 Next
	enum : int {
		kIdcNotebook    = 105,   // host's ControlObjectContainer
		kIdcHint        = 590,   // bottom-of-page description label
		kIdcRowBgBase   = 700,   // 700+slot — focus row backdrop (one shown at a time)
		kIdcScrollbar   = 850,
	};
	static constexpr int SlotIdcLabel   (int slot) { return 500 + slot * 10 + 0; }
	static constexpr int SlotIdcValStep (int slot) { return 500 + slot * 10 + 1; }
	static constexpr int SlotIdcValBar  (int slot) { return 500 + slot * 10 + 2; }
	static constexpr int SlotIdcHover   (int slot) { return 500 + slot * 10 + 3; }
	static constexpr int SlotIdcTrack   (int slot) { return 500 + slot * 10 + 4; }
	static constexpr int SlotIdcFill    (int slot) { return 500 + slot * 10 + 5; }
	static constexpr int SlotIdcPeak    (int slot) { return 500 + slot * 10 + 6; }
	static constexpr int SlotIdcBarClick(int slot) { return 500 + slot * 10 + 7; }
	static constexpr int SlotIdcPrev    (int slot) { return 500 + slot * 10 + 8; }
	static constexpr int SlotIdcNext    (int slot) { return 500 + slot * 10 + 9; }
	static int SlotForControlIdc(int idc);

	// Per-row shape descriptor.  count encoding:
	//   > 0 — stepper/toggle, options[] has count strings to cycle
	//   -1 — slider, value is rowValueIdx as 0..100 percent
	//   -2 — read-only meter (VU); Provider::TickMeter feeds the bar
	struct RowDef
	{
		int valueIdc;
		const char* const* options;
		int count;
	};

	// Row kind drives the slot renderer + keyboard model:
	//   Stepper/Boolean/Slider/Meter — value-bearing, focusable, derived from
	//                          RowFor(row).count for backwards compat.
	//   Boolean — dedicated binary toggle presentation.  Reuses the
	//             stepper value cell, keeps chevrons visible, and also
	//             toggles on full-row click / Enter / Left / Right.
	//   Header — non-focusable visual divider, label only, no value cell.
	//            MoveFocus skips Header rows in both directions.
	//   Action — focusable, no value cell.  Enter / click invokes
	//            Provider::OnRowAction(row).  Used for "Apply", "Close",
	//            "Reset to defaults", "Run benchmark", etc.
	//   Binding — label + two value cells (Primary + Alt).  Right/Left
	//             toggle which cell Enter targets.  Click on the
	//             primary-cell click zone targets primary; alt-cell zone
	//             targets alt.  Provider::OnBindingClicked(row, slot)
	//             opens the page-specific capture modal.
	enum Kind { KindStepper, KindBoolean, KindSlider, KindMeter, KindHeader, KindAction, KindBinding };
	static bool CanRowReceiveFocus(Kind kind);
	static bool CanRowAdjustValue(Kind kind, bool disabled);
	static bool CanRowInvokeAction(Kind kind, bool disabled);
	static bool CanRowOpenBinding(Kind kind, bool disabled);

	// Page-specific data + value storage.  The list holds no row data
	// itself — every getter/setter delegates here.
	struct Provider
	{
		virtual ~Provider() = default;
		virtual int RowCount() const = 0;
		virtual const char* RowLabel(int row) const = 0;
		virtual const char* RowDescription(int /*row*/) const { return ""; }
		virtual RowDef RowFor(int row) const = 0;
		virtual int  RowValue(int row) const = 0;
		virtual void SetRowValue(int row, int value) = 0;

		// Optional: per-frame tick for read-only meter rows (count==-2).
		// Implementations write the new bar level + peak in 0..100.
		virtual void TickMeter(int /*row*/, float& /*outLevel*/, float& /*outPeak*/) {}

		// Optional: row kind override.  Default infers from RowFor(row).count
		// (>0 → Stepper, -1 → Slider, -2 → Meter).  Pages with section
		// headers / action rows return KindHeader / KindAction here.
		virtual Kind RowKind(int row) const;

		// Optional: per-row state flags.
		//   IsPending     — yellow value cell, gates Apply.
		//   IsDestructive — kept for future use by ConfirmRevertDialog.
		//   IsDisabled    — row is greyed and inert.  It remains focusable
		//                   so the hint can explain why it cannot be used,
		//                   but adjust / binding / action handlers must not
		//                   fire from any input source.  Used for cases
		//                   like Display refresh-rate outside exclusive
		//                   fullscreen, or "Apply" when nothing is pending.
		virtual bool IsPending(int /*row*/) const     { return false; }
		virtual bool IsDestructive(int /*row*/) const { return false; }
		virtual bool IsDisabled(int /*row*/) const    { return false; }

		// Invoked when the user activates an Action row (Enter or click).
		// The host Display is passed in so the implementation can call
		// Exit() / spawn child displays / etc. without needing to know
		// the page subclass.
		virtual void OnRowAction(int /*row*/, Display& /*host*/) {}

        // Optional: per-cell text for KindBinding rows (Primary + Alt).
        // Empty cell renders as the cleared-binding glyph (— by default).
        virtual const char* BindingPrimary(int /*row*/) const { return ""; }
        virtual const char* BindingAlt    (int /*row*/) const { return ""; }
        virtual const char* SliderValueText(int /*row*/) const { return nullptr; }
        virtual bool BindingUsesChevronUi(int /*row*/) const { return false; }

        // Invoked when the user activates a Binding row's primary
		// (slot=0) or alt (slot=1) cell.  Subclasses spawn the
		// per-device capture modal here.
		virtual void OnBindingClicked(int /*row*/, int /*slot*/, Display& /*host*/) {}

		// Invoked when focus moves between rows.  prevRow may be -1 on
		// first focus.  Audio uses this to start a category preview when
		// a slider is selected and stop it when focus leaves.
		virtual void OnRowFocusChanged(int /*prevRow*/, int /*newRow*/) {}

		// Optional: pin a row to the bottom-most visible slot regardless
		// of scroll offset — used by WithCloseRow so the Close action
		// stays reachable on pages whose content overflows the visible
		// slot count.  Returns the row index to pin, or -1 for no pin.
		virtual int PinnedFooterRow() const { return -1; }

		// Optional: capture-conflict lookup.  Capture modals call this
		// after a key is pressed to ask "is this combo already bound?"
		// — implementations search the page's binding tables and
		// return the conflicting action's label, or empty if free.
		// `excludeRow` / `excludeSlot` skip the cell currently being
		// re-bound (rebinding to the same value isn't a conflict).
		virtual const char* FindBindingConflict(const char* /*formatted*/,
		                                        int /*excludeRow*/,
		                                        int /*excludeSlot*/) const { return ""; }
	};

	// Mixin Provider: wraps a base Provider and appends a "Close" Action
	// row that dispatches IDC_CANCEL through the standard button-click
	// path (so the active page's IDC_CANCEL handler decides what
	// cancelling means in context — modal pop, settings-page pop, or
	// shell exit).  Every settings page in the new Options pattern
	// uses this — saves each page from hardcoding a positional
	// kCloseRow + KindAction override.  Custom label / description
	// optional.
		struct WithCloseRow : Provider
		{
			WithCloseRow(Provider& base,
			             const char* closeLabel = nullptr,
			             const char* closeDesc  = nullptr)
			: m_base(base),
			  m_label(closeLabel ? closeLabel : (const char*)LocalizeString("STR_DISP_CLOSE")),
			  m_desc(closeDesc ? closeDesc : (const char*)LocalizeString("STR_DISP_MAIN_OPT_CLOSE_DESC")) {}

		void SetCloseTexts(const char* label, const char* desc)
		{
			m_label = label ? label : "";
			m_desc = desc ? desc : "";
		}

		int  RowCount() const override            { return m_base.RowCount() + 1; }
		int  CloseRow() const                     { return m_base.RowCount(); }
		bool IsCloseRow(int row) const            { return row == CloseRow(); }

		const char* RowLabel(int row) const override
		    { return IsCloseRow(row) ? m_label : m_base.RowLabel(row); }
		const char* RowDescription(int row) const override
		    { return IsCloseRow(row) ? m_desc  : m_base.RowDescription(row); }
		RowDef RowFor(int row) const override
		    { return IsCloseRow(row) ? RowDef{-1, nullptr, 0} : m_base.RowFor(row); }
		int  RowValue(int row) const override
		    { return IsCloseRow(row) ? 0 : m_base.RowValue(row); }
		void SetRowValue(int row, int value) override
		    { if (!IsCloseRow(row)) m_base.SetRowValue(row, value); }
		void TickMeter(int row, float& outLevel, float& outPeak) override
		    { if (!IsCloseRow(row)) m_base.TickMeter(row, outLevel, outPeak); }

		Kind RowKind(int row) const override
		    { return IsCloseRow(row) ? KindAction : m_base.RowKind(row); }
		bool IsPending(int row) const override
		    { return !IsCloseRow(row) && m_base.IsPending(row); }
		bool IsDestructive(int row) const override
		    { return !IsCloseRow(row) && m_base.IsDestructive(row); }
		bool IsDisabled(int row) const override
		    { return !IsCloseRow(row) && m_base.IsDisabled(row); }

		const char* BindingPrimary(int row) const override
		    { return IsCloseRow(row) ? "" : m_base.BindingPrimary(row); }
		const char* BindingAlt(int row) const override
		    { return IsCloseRow(row) ? "" : m_base.BindingAlt(row); }
		const char* SliderValueText(int row) const override
		    { return IsCloseRow(row) ? nullptr : m_base.SliderValueText(row); }
		void OnBindingClicked(int row, int slot, Display& host) override
		    { if (!IsCloseRow(row)) m_base.OnBindingClicked(row, slot, host); }
		const char* FindBindingConflict(const char* fmt, int excludeRow, int excludeSlot) const override
		    { return m_base.FindBindingConflict(fmt, excludeRow, excludeSlot); }

		int  PinnedFooterRow() const override     { return CloseRow(); }
		void OnRowAction(int row, Display& host) override;
		void OnRowFocusChanged(int prevRow, int newRow) override
		    { m_base.OnRowFocusChanged(prevRow, newRow); }

	private:
		Provider&   m_base;
		const char* m_label;
		const char* m_desc;
	};

	OptionsScrollList(Display& host, Provider& provider);

	void RenderPage();
	// Set engine focus on the first focusable row (after any leading
	// Header rows).  Call once after Load so keyboard navigation has a
	// known starting point — RenderPage on its own doesn't touch engine
	// focus to avoid stealing it on every frame.
	void FocusInitial();
	// Reapply engine focus to the retained logical row after a modal
	// temporarily hid the list.  Does not change row selection.
	void RestoreRetainedFocus();
	// If the current focus is on a row that's now disabled (e.g. Apply
	// after a successful commit), move it to the next focusable row.
	// No-op when current focus is still focusable.
	void EnsureFocusOnFocusable();

	void OnSimulate();
	bool OnKeyDown(unsigned nChar);
	bool OnButtonClicked(int idc);

	int  Focus() const         { return m_rowFocus; }
	int  ScrollOffset() const  { return m_scrollOffset; }

private:
	void RenderSlot(int slot, int logicalRow);
	void UpdateScrollbar();
	void UpdateRowHighlight();
	void UpdateMeter();
	void UpdateMarquee();
	void UpdateHoverFocus();
	void PollWheelScroll();
	void PollScrollbarDrag();
	void PollSliderDrag();
	bool PollPointerActionClick();
	void MoveFocus(int direction);
	void ScrollBy(int delta);
	void CycleFocusedRowValue(int direction);
	void EnsureFocusVisible();

	// Slot ↔ row mapping that respects an optional pinned footer row.
	// When the provider pins a row (Provider::PinnedFooterRow() != -1)
	// the last visible slot always renders that row, and content rows
	// scroll across the remaining slots (kVisibleSlots - 1 of them).
	// Without a pin these reduce to the simple `m_scrollOffset + slot`
	// math.  RowAtSlot returns -1 if the slot is empty (e.g. a content
	// slot past the end of the content rows).  SlotForRow returns -1
	// if the row is not currently visible (off-screen above or below).
	int RowAtSlot(int slot) const;
	int SlotForRow(int row) const;
	int ContentSlots() const;     // number of slots used for scrolling content
	int ContentRowCount() const;  // total rows excluding the pinned one
	int MaxScrollOffset() const;

	void FocusFocusedRow();
	int  RowLabelInnerChars(int row) const;
	bool RowLabelNeedsMarquee(int row) const;
	bool FocusedStepperValueNeedsMarquee() const;

	void SetSliderBar(int fillIdc, int percent,
	                  float trackX, float trackY, float trackH, float trackWidth);
	void SetRowValue(int labelIdc, const char* text, const char* semanticText = nullptr);
	void SetLabelColor(int idc, PackedColor color);
	PackedColor ControlColor(int idc, PackedColor fallback) const;
	PackedColor ControlBgColor(int idc, PackedColor fallback) const;
	void SetStaticColors(int idc, PackedColor color, PackedColor bgColor);
	void SetCtrlEnabled(int idc, bool enabled);
	void SetCtrlVisible(int idc, bool visible);

	static float SlotTrackY(int slot);
	// UTF-8 codepoint count.  Strings with em-dashes, accented Czech chars,
	// etc. count as one logical character per codepoint instead of one per
	// byte — used so marquee math + width thresholds work in characters
	// the user actually sees, not raw bytes.
	static int  Utf8Length(const char* s);
	// Truncate or pass-through to fit `innerChars` in the output buffer.
	static void FormatTruncated(const char* val, int innerChars, char* out, size_t outsz);
	// Marquee window of `innerChars` codepoints over the input, advanced
	// by `offset`.  Multi-byte UTF-8 codepoints are treated atomically.
	static void FormatMarquee(const char* val, int offset, int innerChars,
	                          char* out, size_t outsz);
	// Time → right-to-left scrolling offset over [0, valLen+kMarqueeSepLen).
	// Pauses kPauseMs at offset 0 (text at start position), then scrolls
	// leftward continuously — the doubled-string trick (val + spaces +
	// val) means the next iteration's leading characters appear at the
	// right edge as the previous iteration's trailing characters fall
	// off the left, with a small visible gap between them.
	static int  MarqueeOffset(DWORD elapsedMs, int valLen, int innerChars);

	Display&                 m_host;
	Provider&                m_provider;
	ControlObjectContainer*  m_notebook = nullptr;   // resolved once in ctor
	PackedColor              m_slotLabelColor[kVisibleSlots];
	PackedColor              m_slotValueStepColor[kVisibleSlots];
	PackedColor              m_slotValueBarColor[kVisibleSlots];
	PackedColor              m_slotRowBgColor[kVisibleSlots];
	PackedColor              m_slotRowBgFillColor[kVisibleSlots];
	PackedColor              m_slotTrackFillColor[kVisibleSlots];
	PackedColor              m_slotBarFillColor[kVisibleSlots];
	PackedColor              m_slotPeakFillColor[kVisibleSlots];
	PackedColor              m_scrollbarColor;

	int   m_rowFocus       = 0;
	int   m_scrollOffset   = 0;
	// 0 = primary cell, 1 = alt cell.  Tracked for KindBinding rows
	// only — Right/Left toggle which cell Enter targets.  Reset to 0
	// whenever m_rowFocus changes.
	int   m_bindingSlot    = 0;
	int   m_marqueeRowPrev = -1;
	DWORD m_marqueeStartMs = 0;
	float m_vuLevel        = 0.0f;
	float m_vuPeak         = 0.0f;
	float m_lastCursorX    = -2.0f;
	float m_lastCursorY    = -2.0f;
	int   m_lastNotifiedFocus = -1;
	bool  m_pointerLeftWasDown = false;
	bool  m_pointerActionCaptured = false;
	int   m_pointerActionRow = -1;
};

} // namespace Poseidon
