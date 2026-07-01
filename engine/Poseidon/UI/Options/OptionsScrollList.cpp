#include <Poseidon/Foundation/Framework/AppFrame.hpp>

namespace Poseidon
{

} // namespace Poseidon
#include <Poseidon/UI/Options/OptionsScrollList.hpp>
#include <Poseidon/UI/Options/MeterWidget.hpp>
#include <Poseidon/UI/Options/NotebookTheme.hpp>
#include <Poseidon/UI/UITestEngine.hpp>

#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace Poseidon
{

namespace
{
PackedColor ModAlpha(PackedColor color, int percent)
{
    return PackedColorRGB(color, color.A8() * percent / 100);
}
} // namespace

OptionsScrollList::OptionsScrollList(Display& host, Provider& provider)
    : m_host(host), m_provider(provider), m_notebook(dynamic_cast<ControlObjectContainer*>(host.GetCtrl(kIdcNotebook)))
{
    for (int slot = 0; slot < kVisibleSlots; ++slot)
    {
        m_slotLabelColor[slot] = NotebookTheme::ResourceColorOr(ControlColor(SlotIdcLabel(slot), PackedWhite));
        m_slotValueStepColor[slot] = NotebookTheme::ResourceColorOr(ControlColor(SlotIdcValStep(slot), m_slotLabelColor[slot]));
        m_slotValueBarColor[slot] = NotebookTheme::ResourceColorOr(ControlColor(SlotIdcValBar(slot), m_slotValueStepColor[slot]));
        m_slotRowBgColor[slot] = NotebookTheme::ResourceColorOr(ControlColor(kIdcRowBgBase + slot, m_slotLabelColor[slot]));
        m_slotRowBgFillColor[slot] = NotebookTheme::ResourceColorOr(ControlBgColor(kIdcRowBgBase + slot, ModAlpha(m_slotRowBgColor[slot], 65)));
        m_slotTrackFillColor[slot] = NotebookTheme::ResourceColorOr(ControlBgColor(SlotIdcTrack(slot), ModAlpha(m_slotLabelColor[slot], 35)));
        m_slotBarFillColor[slot] = NotebookTheme::ResourceColorOr(ControlBgColor(SlotIdcFill(slot), ModAlpha(m_slotLabelColor[slot], 95)));
        m_slotPeakFillColor[slot] = NotebookTheme::ResourceColorOr(ControlBgColor(SlotIdcPeak(slot), m_slotLabelColor[slot]));
    }
    m_scrollbarColor = NotebookTheme::ResourceColorOr(m_slotLabelColor[0]);
}

OptionsScrollList::Kind OptionsScrollList::Provider::RowKind(int row) const
{
    int c = RowFor(row).count;
    if (c == -1)
        return KindSlider;
    if (c == -2)
        return KindMeter;
    return KindStepper;
}

void OptionsScrollList::WithCloseRow::OnRowAction(int row, Display& host)
{
    if (IsCloseRow(row))
    {
        // Dispatch through the standard button-click path so the
        // active page's IDC_CANCEL handler decides what cancelling
        // means in context — popping a modal, popping a settings
        // page back to the index, or (for the index itself) exiting
        // the shell.  Calling Display::Exit directly would close the
        // whole shell from any depth, skipping the page stack.
        host.OnButtonClicked(IDC_CANCEL);
        return;
    }
    m_base.OnRowAction(row, host);
}

// Static layout helpers.

float OptionsScrollList::SlotTrackY(int slot)
{
    // Mirrors the resource-side S{N}Track entries — bar h=0.018
    // vertically centred in row h=0.075 → trackY = rowY + 0.0285.
    static const float k[kVisibleSlots] = {0.149f, 0.234f, 0.319f, 0.404f, 0.489f, 0.574f, 0.659f, 0.744f, 0.829f};
    if (slot < 0 || slot >= kVisibleSlots)
        return -1.0f;
    return k[slot];
}

int OptionsScrollList::SlotForControlIdc(int idc)
{
    if (idc < 500 || idc >= 500 + kVisibleSlots * 10)
        return -1;
    return (idc - 500) / 10;
}

bool OptionsScrollList::CanRowReceiveFocus(Kind kind)
{
    return kind != KindHeader;
}

bool OptionsScrollList::CanRowAdjustValue(Kind kind, bool disabled)
{
    if (disabled)
        return false;
    return kind == KindStepper || kind == KindBoolean || kind == KindSlider;
}

bool OptionsScrollList::CanRowInvokeAction(Kind kind, bool disabled)
{
    return kind == KindAction && !disabled;
}

bool OptionsScrollList::CanRowOpenBinding(Kind kind, bool disabled)
{
    return kind == KindBinding && !disabled;
}

int OptionsScrollList::Utf8Length(const char* s)
{
    return CountUtf8Codepoints(s);
}

void OptionsScrollList::FormatTruncated(const char* val, int innerChars, char* out, size_t outsz)
{
    // Truncate on codepoint boundaries, not bytes. innerChars is a codepoint
    // count; "%.*s" with a byte width would slice mid-sequence on multi-byte
    // UTF-8 (e.g. Russian "Полный экран") and leave a dangling lead byte that
    // renders as a missing-glyph box. FormatMarquee already walks codepoints.
    if (CountUtf8Codepoints(val) <= innerChars)
    {
        snprintf(out, outsz, "%s", val);
        return;
    }
    const char* end = Utf8AdvanceCodepoints(val, innerChars);
    size_t nbytes = (size_t)(end - val);
    if (nbytes >= outsz)
        nbytes = outsz - 1;
    memcpy(out, val, nbytes);
    out[nbytes] = '\0';
}

int OptionsScrollList::MarqueeOffset(DWORD elapsedMs, int valLen, int /*innerChars*/)
{
    // Right-to-left continuous scroll.  Cycle:
    //   pause kPauseMs at offset 0 (text fully visible at left)
    //   scroll forward — text shifts leftward; the next iteration's
    //   leading characters appear at the right edge after a small
    //   kMarqueeSepLen-char gap so the loop reads as continuous repeat
    //   rather than a fade-out and re-entry
    // Repeat.  Keep wrap brief so the user perceives one rolling string.
    if (valLen <= 0)
        return 0;
    const int wrapLen = valLen + kMarqueeSepLen;
    const DWORD scrollMs = (DWORD)wrapLen * (DWORD)kScrollPeriodMs;
    const DWORD cycleMs = (DWORD)kPauseMs + scrollMs;
    const DWORD t = elapsedMs % cycleMs;
    if (t < (DWORD)kPauseMs)
        return 0;
    return int((t - (DWORD)kPauseMs) / (DWORD)kScrollPeriodMs);
}

void OptionsScrollList::FormatMarquee(const char* val, int offset, int innerChars, char* out, size_t outsz)
{
    // Walks codepoints, not bytes — the doubled-string trick (val +
    // spaces + val) breaks on multi-byte UTF-8 input if you shift by
    // bytes, because at certain offsets the buffer would contain a
    // partial UTF-8 sequence and the engine would render garbage where
    // the codepoint should be.  Any text containing an em-dash, accented
    // character, etc. would visibly stutter at the edge it crosses.
    //
    // Cost: a Utf8AdvanceCodepoints walk per output codepoint (O(innerChars * srcCp)
    // per call).  innerChars and valCp are both small (<60 each), so
    // once-per-frame is fine.
    const int valCpCount = CountUtf8Codepoints(val);
    int wrapLen = valCpCount + kMarqueeSepLen;
    if (wrapLen <= 0)
        wrapLen = 1;
    offset = ((offset % wrapLen) + wrapLen) % wrapLen;

    char* o = out;
    const char* oend = out + outsz - 1; // reserve null terminator

    for (int i = 0; i < innerChars && o < oend; ++i)
    {
        int srcCp = (offset + i) % wrapLen;
        if (srcCp < valCpCount)
        {
            const char* src = Utf8AdvanceCodepoints(val, srcCp);
            int cpLen = Utf8CodepointBytes(src);
            if (o + cpLen > oend)
                break;
            for (int b = 0; b < cpLen; ++b)
                *o++ = *src++;
        }
        else
        {
            *o++ = ' ';
        }
    }
    *o = '\0';
}

int OptionsScrollList::RowLabelInnerChars(int row) const
{
    Kind kind = m_provider.RowKind(row);
    if (kind == KindBinding && m_provider.BindingUsesChevronUi(row))
        return kBindingLabelInnerChars;
    return kLabelInnerChars;
}

bool OptionsScrollList::RowLabelNeedsMarquee(int row) const
{
    if (row < 0 || row >= m_provider.RowCount())
        return false;
    Kind kind = m_provider.RowKind(row);
    if (kind == KindHeader || kind == KindAction)
        return false;
    const char* label = m_provider.RowLabel(row);
    if (!label)
        label = "";
    return Utf8Length(label) > RowLabelInnerChars(row);
}

bool OptionsScrollList::FocusedStepperValueNeedsMarquee() const
{
    if (m_rowFocus < 0 || m_rowFocus >= m_provider.RowCount())
        return false;
    if (m_provider.RowKind(m_rowFocus) != KindStepper)
        return false;
    RowDef row = m_provider.RowFor(m_rowFocus);
    if (row.count <= 0)
        return false;
    int idx = m_provider.RowValue(m_rowFocus) % row.count;
    const char* val = (row.options && row.count > 0) ? row.options[idx] : "";
    return Utf8Length(val) > kInnerChars;
}

// Control-update helpers.

void OptionsScrollList::SetRowValue(int labelIdc, const char* text, const char* semanticText)
{
    IControl* ctrl = m_notebook ? m_notebook->GetCtrl(labelIdc) : nullptr;
    if (!ctrl)
        ctrl = m_host.GetCtrl(labelIdc);

    if (auto* s3d = dynamic_cast<C3DStatic*>(ctrl))
        s3d->SetText(RString(text));
    else if (auto* s2d = dynamic_cast<CStatic*>(ctrl))
        s2d->SetText(RString(text));

    if (semanticText)
        UITestEngine::SetSemanticControlText(ctrl, semanticText);
    else
        UITestEngine::ClearSemanticControlText(ctrl);
}

void OptionsScrollList::SetCtrlVisible(int idc, bool visible)
{
    if (auto* ctrl = m_host.GetCtrl(idc))
        ctrl->ShowCtrl(visible);
}

void OptionsScrollList::SetCtrlEnabled(int idc, bool enabled)
{
    if (auto* ctrl = m_host.GetCtrl(idc))
        ctrl->EnableCtrl(enabled);
}

void OptionsScrollList::SetLabelColor(int idc, PackedColor color)
{
    if (auto* s3d = dynamic_cast<C3DStatic*>(m_host.GetCtrl(idc)))
        s3d->SetColor(color);
}

PackedColor OptionsScrollList::ControlColor(int idc, PackedColor fallback) const
{
    IControl* ctrl = m_notebook ? m_notebook->GetCtrl(idc) : nullptr;
    if (!ctrl)
        ctrl = m_host.GetCtrl(idc);
    if (auto* s3d = dynamic_cast<C3DStatic*>(ctrl))
        return s3d->GetColor();
    return fallback;
}

PackedColor OptionsScrollList::ControlBgColor(int idc, PackedColor fallback) const
{
    IControl* ctrl = m_notebook ? m_notebook->GetCtrl(idc) : nullptr;
    if (!ctrl)
        ctrl = m_host.GetCtrl(idc);
    if (auto* s3d = dynamic_cast<C3DStatic*>(ctrl))
        return s3d->GetBgColor();
    return fallback;
}

void OptionsScrollList::SetStaticColors(int idc, PackedColor color, PackedColor bgColor)
{
    IControl* ctrl = m_notebook ? m_notebook->GetCtrl(idc) : nullptr;
    if (!ctrl)
        ctrl = m_host.GetCtrl(idc);
    if (auto* s3d = dynamic_cast<C3DStatic*>(ctrl))
    {
        s3d->SetColor(color);
        s3d->SetBgColor(bgColor);
    }
}

namespace
{
const PackedColor kRowColorRed = PackedColor(Color(0.95f, 0.30f, 0.30f, 1.0f));
} // namespace

void OptionsScrollList::SetSliderBar(int fillIdc, int percent, float trackX, float trackY, float trackH,
                                     float trackWidth)
{
    percent = std::clamp(percent, 0, 100);
    float w = trackWidth * (percent / 100.0f);
    if (m_notebook)
        m_notebook->SetSubControlPos(fillIdc, trackX, trackY, w, trackH);
}

// Slot binding — paint one visible slot from one logical row.

void OptionsScrollList::RenderSlot(int slot, int logicalRow)
{
    int idcLabel = SlotIdcLabel(slot);
    int idcValStep = SlotIdcValStep(slot);
    int idcValBar = SlotIdcValBar(slot);
    int idcHover = SlotIdcHover(slot);
    int idcTrack = SlotIdcTrack(slot);
    int idcFill = SlotIdcFill(slot);
    int idcPeak = SlotIdcPeak(slot);
    int idcBarClick = SlotIdcBarClick(slot);
    int idcPrev = SlotIdcPrev(slot);
    int idcNext = SlotIdcNext(slot);

    int rowCount = m_provider.RowCount();
    if (logicalRow < 0 || logicalRow >= rowCount)
    {
        SetRowValue(idcLabel, "");
        SetRowValue(idcValStep, "");
        SetRowValue(idcValBar, "");
        SetCtrlVisible(idcHover, false);
        SetCtrlVisible(idcValStep, false);
        SetCtrlVisible(idcValBar, false);
        SetCtrlVisible(idcTrack, false);
        SetCtrlVisible(idcFill, false);
        SetCtrlVisible(idcPeak, false);
        SetCtrlVisible(idcBarClick, false);
        SetCtrlVisible(idcPrev, false);
        SetCtrlVisible(idcNext, false);
        return;
    }

    Kind kind = m_provider.RowKind(logicalRow);
    bool isStepper = (kind == KindStepper);
    bool isBoolean = (kind == KindBoolean);
    bool isSlider = (kind == KindSlider);
    bool isVU = (kind == KindMeter);
    bool isHeader = (kind == KindHeader);
    bool isAction = (kind == KindAction);
    bool isBinding = (kind == KindBinding);
    bool isPending = m_provider.IsPending(logicalRow);
    bool isDisabled = m_provider.IsDisabled(logicalRow);
    bool focused = (m_rowFocus == logicalRow);
    bool bindingChevronUi = isBinding && m_provider.BindingUsesChevronUi(logicalRow);
    PackedColor labelColor = m_slotLabelColor[slot];
    PackedColor valueStepColor = m_slotValueStepColor[slot];
    PackedColor valueBarColor = m_slotValueBarColor[slot];
    PackedColor disabledLabelColor = ModAlpha(labelColor, 45);
    PackedColor disabledValueStepColor = ModAlpha(valueStepColor, 45);
    PackedColor disabledValueBarColor = ModAlpha(valueBarColor, 45);
    SetStaticColors(idcTrack, ControlColor(idcTrack, PackedColor(0, 0, 0, 0)), m_slotTrackFillColor[slot]);
    SetStaticColors(idcFill, ControlColor(idcFill, PackedColor(0, 0, 0, 0)), m_slotBarFillColor[slot]);
    SetStaticColors(idcPeak, ControlColor(idcPeak, PackedColor(0, 0, 0, 0)), m_slotPeakFillColor[slot]);

    // Label colour is shared by left-column labels and centred
    // header/action text. The value-cell colour carries the focus cue.
    SetLabelColor(idcLabel, isDisabled ? disabledLabelColor : labelColor);

    // idcValStep is reused as Primary for binding rows.
    // idcValBar  is reused as Alt for binding rows.
    SetCtrlVisible(idcHover, true);
    SetCtrlVisible(idcValStep, isStepper || isBoolean || isBinding);
    SetCtrlVisible(idcValBar, isSlider || isVU || isBinding);
    SetCtrlVisible(idcTrack, isSlider || isVU);
    SetCtrlVisible(idcFill, isSlider || isVU);
    SetCtrlVisible(idcPeak, isVU);
    // Binding rows reuse the slot's overlays for two click hit-zones:
    //   Hover (5N3)    → primary cell click area
    //   BarClick (5N7) → alt cell click area
    // Both repositioned via SetSubControlPos below.
    SetCtrlVisible(idcBarClick, isSlider || isBinding);
    SetCtrlVisible(idcPrev, isStepper || isBoolean || bindingChevronUi);
    SetCtrlVisible(idcNext, isStepper || isBoolean || bindingChevronUi);
    // Presentation controls must never own hit-testing.  The row's live click
    // semantics belong to Hover / BarClick / chevrons; text and bar visuals are
    // display-only and should always let pointer input fall through.
    SetCtrlEnabled(idcLabel, false);
    SetCtrlEnabled(idcValStep, false);
    SetCtrlEnabled(idcValBar, false);
    SetCtrlEnabled(idcTrack, false);
    SetCtrlEnabled(idcFill, false);
    SetCtrlEnabled(idcPeak, false);
    SetCtrlEnabled(idcBarClick, !isDisabled);
    SetCtrlEnabled(idcPrev, !isDisabled);
    SetCtrlEnabled(idcNext, !isDisabled);

    if (isHeader || isAction)
    {
        // Spread Close / Reset / Header text across the full row width
        // so the user reads them as a row-level button rather than a
        // label tucked into the leftmost column.  ValueStep already has
        // ST_CENTER from OptListLabel; reposition it to span the full
        // row, hide the left-aligned idcLabel, and let SetRowValue
        // drive the centred text.
        SetCtrlVisible(idcLabel, false);
        SetCtrlVisible(idcValStep, true);
        SetCtrlVisible(idcValBar, false);
        // The stretched ValueStep is presentation only.  Mouse hits
        // must fall through to the row's live Hover overlay (5N3),
        // otherwise clicks on the visible centred action text land on a
        // static control and never dispatch.
        SetCtrlEnabled(idcValStep, false);
        SetRowValue(idcValStep, m_provider.RowLabel(logicalRow));
        SetLabelColor(idcValStep, isDisabled ? disabledValueStepColor : valueStepColor);
        float rowY = SlotTrackY(slot) - 0.0285f;
        if (m_notebook)
            m_notebook->SetSubControlPos(idcValStep, 0.02f, rowY, 0.96f, 0.075f);
        SetRowValue(idcValBar, "");
        return;
    }

    // Reset idcLabel + idcValStep to their default positions for value
    // rows.  Header / Action repositioned above and need to be reset
    // when the slot is rebound to a stepper / slider / binding row.
    {
        float rowY = SlotTrackY(slot) - 0.0285f;
        if (m_notebook)
        {
            if (bindingChevronUi)
            {
                m_notebook->SetSubControlPos(idcLabel, 0.04f, rowY, 0.26f, 0.075f);
                m_notebook->SetSubControlPos(idcValStep, 0.36f, rowY, 0.22f, 0.075f);
                m_notebook->SetSubControlPos(idcValBar, 0.64f, rowY, 0.22f, 0.075f);
            }
            else
            {
                m_notebook->SetSubControlPos(idcLabel, 0.04f, rowY, 0.34f, 0.075f);
                m_notebook->SetSubControlPos(idcValStep, 0.46f, rowY, 0.44f, 0.075f);
                m_notebook->SetSubControlPos(idcValBar, 0.81f, rowY, 0.12f, 0.075f);
            }
        }
    }
    SetCtrlVisible(idcLabel, true);

    const char* label = m_provider.RowLabel(logicalRow);
    if (!label)
        label = "";
    const int labelInnerChars = RowLabelInnerChars(logicalRow);
    const int labelCpLen = Utf8Length(label);
    char labelBuf[128];
    if (focused && labelCpLen > labelInnerChars)
    {
        DWORD elapsed = GlobalTickCount() - m_marqueeStartMs;
        int offset = MarqueeOffset(elapsed, labelCpLen, labelInnerChars);
        FormatMarquee(label, offset, labelInnerChars, labelBuf, sizeof(labelBuf));
    }
    else
    {
        FormatTruncated(label, labelInnerChars, labelBuf, sizeof(labelBuf));
    }
    SetRowValue(idcLabel, labelBuf, label);

    if (isBinding)
    {
        // Two value cells: Primary at the centre, Alt at the right.
        // Repurpose ValueStep / ValueBar templates — same template the
        // stepper/slider rows use.  Empty slot renders as "—".
        const char* primary = m_provider.BindingPrimary(logicalRow);
        const char* alt = m_provider.BindingAlt(logicalRow);
        const char* primaryDisplay = (primary && *primary) ? primary : "—";
        const char* altDisplay = (alt && *alt) ? alt : "—";
        SetRowValue(idcValStep, primaryDisplay);
        SetRowValue(idcValBar, altDisplay);

        // Highlight the slot Enter will target on the focused row.
        PackedColor primaryColor = isDisabled ? disabledValueStepColor : valueStepColor;
        PackedColor altColor = isDisabled ? disabledValueBarColor : valueBarColor;
        SetLabelColor(idcValStep, primaryColor);
        SetLabelColor(idcValBar, altColor);

        // Reposition click overlays so they cover only their cell:
        //   primary cell → Hover (idc 5N3) at x=0.34..0.65
        //   alt cell     → BarClick (idc 5N7) at x=0.65..0.95
        float rowY = SlotTrackY(slot) - 0.0285f; // back to row top
        if (m_notebook)
        {
            m_notebook->SetSubControlPos(idcBarClick, 0.65f, rowY, 0.30f, 0.075f);
            if (bindingChevronUi)
            {
                float prevX = (m_bindingSlot == 0) ? 0.30f : 0.58f;
                float nextX = (m_bindingSlot == 0) ? 0.58f : 0.86f;
                m_notebook->SetSubControlPos(idcPrev, prevX, rowY, 0.06f, 0.075f);
                m_notebook->SetSubControlPos(idcNext, nextX, rowY, 0.06f, 0.075f);
            }
        }
        // Hover stays at full row width but logically dispatches to
        // primary unless the click landed in the alt zone (x>=0.65).
        // The dispatch decision happens in OnButtonClicked based on
        // whether the click came in via Hover (5N3) or BarClick (5N7).
        return;
    }

    // Reset Hover / BarClick to default positions for non-binding rows.
    if (m_notebook)
    {
        float rowY = SlotTrackY(slot) - 0.0285f;
        m_notebook->SetSubControlPos(idcBarClick, 0.40f, rowY, 0.40f, 0.075f);
    }

    PackedColor valueColor = isDisabled ? disabledValueStepColor : valueStepColor;
    if (isBoolean && !isPending && !isDisabled)
        valueColor = (m_provider.RowValue(logicalRow) != 0) ? valueStepColor : kRowColorRed;
    SetLabelColor(idcValStep, valueColor);
    SetLabelColor(idcValBar, isDisabled ? disabledValueBarColor : valueBarColor);

    if (isStepper || isBoolean)
    {
        RowDef row = m_provider.RowFor(logicalRow);
        int idx = (row.count > 0) ? (m_provider.RowValue(logicalRow) % row.count) : 0;
        const char* val = (row.options && row.count > 0) ? row.options[idx] : "";
        char buf[80];
        int valCpLen = Utf8Length(val);
        if (isStepper && focused && valCpLen > kInnerChars)
        {
            DWORD elapsed = GlobalTickCount() - m_marqueeStartMs;
            int offset = MarqueeOffset(elapsed, valCpLen, kInnerChars);
            FormatMarquee(val, offset, kInnerChars, buf, sizeof(buf));
        }
        else
        {
            FormatTruncated(val, kInnerChars, buf, sizeof(buf));
        }
        SetRowValue(idcValStep, buf, val);
        SetRowValue(idcValBar, "");
    }
    else if (isSlider)
    {
        int p = m_provider.RowValue(logicalRow);
        const char* sliderText = m_provider.SliderValueText(logicalRow);
        if (sliderText && *sliderText)
        {
            SetRowValue(idcValBar, sliderText);
        }
        else
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%3d%%", p);
            SetRowValue(idcValBar, buf);
        }
        SetSliderBar(idcFill, p, 0.40f, SlotTrackY(slot), 0.018f, 0.40f);
        SetRowValue(idcValStep, "");
    }
    else if (isVU)
    {
        // VU rows are repainted each frame by UpdateMeter; clear stale
        // stepper text in case the slot was recently rebound.
        SetRowValue(idcValStep, "");
    }
}

int OptionsScrollList::ContentSlots() const
{
    // Pinned footer eats the last visible slot.
    return (m_provider.PinnedFooterRow() >= 0) ? (kVisibleSlots - 1) : kVisibleSlots;
}

int OptionsScrollList::ContentRowCount() const
{
    int total = m_provider.RowCount();
    return (m_provider.PinnedFooterRow() >= 0) ? (total - 1) : total;
}

int OptionsScrollList::MaxScrollOffset() const
{
    return std::max(0, ContentRowCount() - ContentSlots());
}

int OptionsScrollList::RowAtSlot(int slot) const
{
    if (slot < 0 || slot >= kVisibleSlots)
        return -1;
    int pinned = m_provider.PinnedFooterRow();
    if (pinned >= 0 && slot == kVisibleSlots - 1)
        return pinned;
    int row = m_scrollOffset + slot;
    int maxRow = ContentRowCount();
    if (row < 0 || row >= maxRow)
        return -1;
    // Skip the pinned row in the content stream when it lives mid-list
    // (today it's always the last row; this guards against future moves).
    if (pinned >= 0 && row >= pinned)
        ++row;
    return row;
}

int OptionsScrollList::SlotForRow(int row) const
{
    int pinned = m_provider.PinnedFooterRow();
    if (pinned >= 0 && row == pinned)
        return kVisibleSlots - 1;
    int adjusted = (pinned >= 0 && row > pinned) ? (row - 1) : row;
    int slot = adjusted - m_scrollOffset;
    int contentSlots = ContentSlots();
    if (slot < 0 || slot >= contentSlots)
        return -1;
    return slot;
}

void OptionsScrollList::RenderPage()
{
    m_scrollOffset = std::clamp(m_scrollOffset, 0, MaxScrollOffset());
    for (int slot = 0; slot < kVisibleSlots; ++slot)
    {
        int row = RowAtSlot(slot);
        // RenderSlot tolerates row == -1 by clearing the slot — we still
        // call it so any previously-bound stale text gets cleared when
        // content shrinks past a slot.
        RenderSlot(slot, row);
    }
    UpdateScrollbar();
    UpdateRowHighlight();
}

void OptionsScrollList::FocusInitial()
{
    // Skip only non-focusable row kinds.  Disabled rows still take focus
    // so the hint can explain why they are inert.
    int rowCount = m_provider.RowCount();
    int row = 0;
    while (row < rowCount && !CanRowReceiveFocus(m_provider.RowKind(row)))
        ++row;
    if (row >= rowCount)
        return;
    m_rowFocus = row;
    EnsureFocusVisible();
    FocusFocusedRow();
    RenderPage();
}

void OptionsScrollList::RestoreRetainedFocus()
{
    EnsureFocusOnFocusable();
    FocusFocusedRow();
}

void OptionsScrollList::EnsureFocusOnFocusable()
{
    int rowCount = m_provider.RowCount();
    if (m_rowFocus < 0 || m_rowFocus >= rowCount)
    {
        FocusInitial();
        return;
    }
    if (CanRowReceiveFocus(m_provider.RowKind(m_rowFocus)))
        return;
    // Current focus is on a Header row (e.g. one that just became
    // header-typed via a runtime row-kind change).  Walk forward to
    // the next non-header.
    int row = m_rowFocus + 1;
    while (row < rowCount && !CanRowReceiveFocus(m_provider.RowKind(row)))
        ++row;
    if (row >= rowCount)
    {
        FocusInitial();
        return;
    }
    m_rowFocus = row;
    EnsureFocusVisible();
    FocusFocusedRow();
    RenderPage();
}

void OptionsScrollList::UpdateScrollbar()
{
    auto* sb = dynamic_cast<C3DScrollBarStandalone*>(m_host.GetCtrl(kIdcScrollbar));
    if (!sb)
        return;
    sb->SetColor(m_scrollbarColor);
    int contentRows = ContentRowCount();
    int contentSlots = ContentSlots();
    bool needScroll = contentRows > contentSlots;
    SetCtrlVisible(kIdcScrollbar, needScroll);
    if (!needScroll)
        return;
    sb->SetScrollRange(0.0f, float(contentRows), float(contentSlots));
    sb->SetScrollSpeed(1.0f, float(contentSlots));
    sb->SetScrollPos(float(m_scrollOffset));
}

// Focus + scroll.

void OptionsScrollList::UpdateRowHighlight()
{
    int focusedSlot = SlotForRow(m_rowFocus);
    for (int s = 0; s < kVisibleSlots; ++s)
    {
        SetStaticColors(kIdcRowBgBase + s, m_slotRowBgColor[s], m_slotRowBgFillColor[s]);
        SetCtrlVisible(kIdcRowBgBase + s, s == focusedSlot);
    }

    // Hint is single-line; long descriptions marquee-scroll horizontally.
    // Pause for kPauseMs after the row focus changes (so the user reads
    // the start of the text first), then advance one shift per frame at
    // kScrollPeriodMs.  m_marqueeStartMs is reset by MoveFocus, so the
    // hint marquee shares the same timer as the stepper marquee.
    const char* desc = m_provider.RowDescription(m_rowFocus);
    if (!desc)
        desc = "";
    int descCpLen = Utf8Length(desc);
    if (descCpLen > kHintInnerChars)
    {
        DWORD elapsed = GlobalTickCount() - m_marqueeStartMs;
        int offset = MarqueeOffset(elapsed, descCpLen, kHintInnerChars);
        char buf[128];
        FormatMarquee(desc, offset, kHintInnerChars, buf, sizeof(buf));
        SetRowValue(kIdcHint, buf, desc);
    }
    else
    {
        SetRowValue(kIdcHint, desc);
    }
}

void OptionsScrollList::FocusFocusedRow()
{
    // Engine focus → SNHover (transparent CT_3DACTIVETEXT covering the
    // slot the focused row is currently mapped to).  Removes engine focus
    // from Close; SNHover renders no visible highlight (its colorActive
    // is transparent), so the row Bg ShowCtrl path is the only visible
    // signal of focus on a row.
    if (m_rowFocus != m_lastNotifiedFocus)
    {
        m_provider.OnRowFocusChanged(m_lastNotifiedFocus, m_rowFocus);
        m_lastNotifiedFocus = m_rowFocus;
    }
    int slot = SlotForRow(m_rowFocus);
    if (slot < 0)
        return;
    m_host.FocusCtrl(SlotIdcHover(slot));
}

void OptionsScrollList::EnsureFocusVisible()
{
    // Pinned rows are always visible; only content rows need scroll
    // adjustment.
    int pinned = m_provider.PinnedFooterRow();
    if (pinned >= 0 && m_rowFocus == pinned)
        return;
    int adjusted = (pinned >= 0 && m_rowFocus > pinned) ? (m_rowFocus - 1) : m_rowFocus;
    int contentSlots = ContentSlots();
    if (adjusted < m_scrollOffset)
        m_scrollOffset = adjusted;
    else if (adjusted >= m_scrollOffset + contentSlots)
        m_scrollOffset = adjusted - (contentSlots - 1);
}

void OptionsScrollList::MoveFocus(int direction)
{
    int rowCount = m_provider.RowCount();
    if (rowCount <= 0)
        return;

    // Disabled rows are focusable-but-inert: focus lands so the hint can
    // explain why; activation is gated separately.
    auto skipUnfocusable = [&](int start) -> int
    {
        int r = start;
        while (r >= 0 && r < rowCount && !CanRowReceiveFocus(m_provider.RowKind(r)))
            r += direction;
        return r;
    };

    int next = skipUnfocusable(m_rowFocus + direction);
    if (next < 0 || next >= rowCount)
    {
        // Wrap — Close is just another row in the list, no off-list cycle.
        next = skipUnfocusable((direction > 0) ? 0 : (rowCount - 1));
        if (next < 0 || next >= rowCount)
            return; // page is all unfocusable
    }
    m_rowFocus = next;
    m_bindingSlot = 0; // reset to primary on row change
    EnsureFocusVisible();
    FocusFocusedRow();
    m_marqueeStartMs = GlobalTickCount();
    RenderPage();
}

void OptionsScrollList::ScrollBy(int delta)
{
    int prev = m_scrollOffset;
    m_scrollOffset = std::clamp(m_scrollOffset + delta, 0, MaxScrollOffset());
    if (m_scrollOffset != prev)
        RenderPage();
}

void OptionsScrollList::CycleFocusedRowValue(int direction)
{
    Kind kind = m_provider.RowKind(m_rowFocus);
    if (!CanRowAdjustValue(kind, m_provider.IsDisabled(m_rowFocus)))
        return;
    RowDef row = m_provider.RowFor(m_rowFocus);
    int curr = m_provider.RowValue(m_rowFocus);
    if (row.count > 0)
    {
        m_provider.SetRowValue(m_rowFocus, (curr + direction + row.count) % row.count);
        m_marqueeStartMs = GlobalTickCount();
    }
    else if (row.count == -1)
    {
        m_provider.SetRowValue(m_rowFocus, std::clamp(curr + direction * 5, 0, 100));
    }
    else
    {
        return;
    }
    RenderPage();
}

// Per-frame polls.

void OptionsScrollList::PollWheelScroll()
{
    float dz = InputSubsystem::Instance().ConsumeCursorScroll();
    if (dz == 0.0f)
        return;
    int step = (dz > 0) ? -int(dz + 0.5f) : -int(dz - 0.5f);
    if (step < -3)
        step = -3;
    if (step > 3)
        step = 3;
    ScrollBy(step);
}

void OptionsScrollList::PollScrollbarDrag()
{
    auto* sb = dynamic_cast<C3DScrollBarStandalone*>(m_host.GetCtrl(kIdcScrollbar));
    if (!sb)
        return;
    int newOff = std::clamp(int(sb->GetScrollPos() + 0.5f), 0, MaxScrollOffset());
    if (newOff != m_scrollOffset)
    {
        m_scrollOffset = newOff;
        RenderPage();
    }
}

void OptionsScrollList::PollSliderDrag()
{
    if (!m_notebook)
        return;
    int pressed = m_notebook->GetLeftPressedIdc();
    if (pressed < 0)
        return;
    int slot = SlotForControlIdc(pressed);
    if (slot < 0)
        return;
    int digit = pressed - (500 + slot * 10);
    if (digit != 7)
        return;
    int rowIdx = RowAtSlot(slot);
    if (rowIdx < 0)
        return;
    if (rowIdx != m_rowFocus)
        return;
    RowDef row = m_provider.RowFor(rowIdx);
    if (row.count != -1)
        return;

    auto* clk = dynamic_cast<Control3D*>(m_notebook->GetCtrl(pressed));
    if (!clk)
        return;
    float u = clk->GetU();
    if (u != u)
        return;
    u = std::clamp(u, 0.0f, 1.0f);
    int p = std::clamp(int(u * 20.0f + 0.5f) * 5, 0, 100);
    if (m_provider.RowValue(rowIdx) == p)
        return;
    m_rowFocus = rowIdx;
    m_provider.SetRowValue(rowIdx, p);
    m_marqueeStartMs = GlobalTickCount();
    FocusFocusedRow();
    RenderPage();
}

bool OptionsScrollList::PollPointerActionClick()
{
    if (!m_notebook)
        return false;

    auto& in = InputSubsystem::Instance();
    const bool down = in.IsMouseLeftDown();
    float mouseX = 0.5f + in.GetCursorX() * 0.5f;
    float mouseY = 0.5f + in.GetCursorY() * 0.5f;

    int hoverRow = -1;
    if (IControl* hovered = m_notebook->GetCtrl(mouseX, mouseY); hovered && hovered != m_notebook)
    {
        int slot = SlotForControlIdc(hovered->IDC());
        if (slot >= 0)
            hoverRow = RowAtSlot(slot);
    }
    if (hoverRow < 0 && m_provider.RowKind(m_rowFocus) == KindAction)
        hoverRow = m_rowFocus;

    if (down && !m_pointerLeftWasDown)
    {
        m_pointerActionRow = -1;
        m_pointerActionCaptured = false;
        if (hoverRow >= 0 && m_provider.RowKind(hoverRow) == KindAction && !m_provider.IsDisabled(hoverRow))
            m_pointerActionRow = hoverRow;
    }
    else if (down && m_pointerActionRow >= 0)
    {
        if (m_notebook->GetLeftPressedIdc() >= 0)
            m_pointerActionCaptured = true;
    }
    else if (!down && m_pointerLeftWasDown)
    {
        int row = m_pointerActionRow;
        bool captured = m_pointerActionCaptured;
        m_pointerActionRow = -1;
        m_pointerActionCaptured = false;
        if (row >= 0 && !captured && hoverRow == row)
        {
            m_rowFocus = row;
            FocusFocusedRow();
            m_provider.OnRowAction(row, m_host);
            m_pointerLeftWasDown = down;
            return true;
        }
    }

    m_pointerLeftWasDown = down;
    return false;
}

void OptionsScrollList::UpdateHoverFocus()
{
    auto& in = InputSubsystem::Instance();
    float cx = in.GetCursorX();
    float cy = in.GetCursorY();
    // First call after construction: m_lastCursorX/Y are the sentinel
    // -2.0 which is guaranteed to differ from any real cursor reading.
    // Without this guard the first hover-focus update would move focus
    // off whatever FocusInitial set (typically row 0 / Output device)
    // to whichever row happens to be under the click position the user
    // arrived at the page from — confusing because subsequent arrow
    // keys would cycle the wrong row.
    bool firstSample = (m_lastCursorX < -1.0f);
    bool moved = !firstSample && ((cx != m_lastCursorX) || (cy != m_lastCursorY));
    m_lastCursorX = cx;
    m_lastCursorY = cy;
    if (!moved)
        return;
    if (!m_notebook)
        return;
    // Hit-test the cursor against the notebook FRESH this frame, the same way
    // FlatMenuPage does.  GetHoveredIdc() returns the notebook's _indexMove,
    // which is refreshed by the host container's mouse poll AFTER the page's
    // OnSimulate runs — so on the one frame `moved` is true it is still a step
    // stale (it holds the *previous* hovered control), and on the settled
    // frames `moved` is false so the fresh value is never read.  The net effect
    // was that scroll-list rows (and the pinned Close/Back footer) never took
    // hover focus.  A direct GetCtrl(x,y) sees the control under the cursor now.
    float mouseX = 0.5f + cx * 0.5f;
    float mouseY = 0.5f + cy * 0.5f;
    int hoveredIdc = -1;
    if (IControl* hovered = m_notebook->GetCtrl(mouseX, mouseY); hovered && hovered != m_notebook)
        hoveredIdc = hovered->IDC();
    int slot = SlotForControlIdc(hoveredIdc);
    // Any visible slot-local control should retarget the row under the
    // cursor — label/value statics as well as hover/click overlays.
    // Filtering to only 5N3/7/8/9 left focus stuck on slider rows when
    // the cursor moved through another row's value text area.
    if (slot < 0)
        return;
    int rowIdx = RowAtSlot(slot);
    if (rowIdx < 0)
        return;
    if (!CanRowReceiveFocus(m_provider.RowKind(rowIdx)))
        return;
    if (rowIdx != m_rowFocus)
    {
        m_rowFocus = rowIdx;
        m_marqueeStartMs = GlobalTickCount();
        FocusFocusedRow();
        RenderPage();
    }
}

void OptionsScrollList::UpdateMeter()
{
    // Find a meter row in the visible window and feed it from the provider.
    for (int slot = 0; slot < kVisibleSlots; ++slot)
    {
        int rowIdx = RowAtSlot(slot);
        if (rowIdx < 0)
            continue;
        RowDef row = m_provider.RowFor(rowIdx);
        if (row.count != -2)
            continue;

        float level = m_vuLevel;
        float peak = m_vuPeak;
        m_provider.TickMeter(rowIdx, level, peak);
        m_vuLevel = level;
        m_vuPeak = peak;

        int idcFill = SlotIdcFill(slot);
        int idcPeak = SlotIdcPeak(slot);
        int idcVal = SlotIdcValBar(slot);
        MeterBarLayout layout = {
            /* trackX */ 0.40f,
            /* trackY */ SlotTrackY(slot),
            /* trackW */ 0.40f,
            /* trackH */ 0.018f,
            /* peakW  */ 0.005f,
        };

        if (m_notebook)
        {
            RenderMeterBar(*m_notebook, idcFill, idcPeak, layout, m_vuLevel / 100.0f, m_vuPeak / 100.0f);
        }

        char buf[24];
        int lvl = int(m_vuLevel + 0.5f);
        if (lvl <= 0)
            snprintf(buf, sizeof(buf), "-inf");
        else
        {
            float db = 20.0f * log10f(lvl / 100.0f);
            snprintf(buf, sizeof(buf), "%+3.0fdB", db);
        }
        SetRowValue(idcVal, buf);
        return; // only one meter row supported per visible window
    }
}

void OptionsScrollList::UpdateMarquee()
{
    if (m_rowFocus != m_marqueeRowPrev)
    {
        m_marqueeRowPrev = m_rowFocus;
        m_marqueeStartMs = GlobalTickCount();
    }
    if (!RowLabelNeedsMarquee(m_rowFocus) && !FocusedStepperValueNeedsMarquee())
        return;
    int slot = SlotForRow(m_rowFocus);
    if (slot < 0)
        return;
    RenderSlot(slot, m_rowFocus);
}

// Frame orchestration + input.

void OptionsScrollList::OnSimulate()
{
    // PollWheelScroll runs BEFORE the host's Display::OnSimulate so the
    // page can intercept the wheel for row scrolling — otherwise the
    // notebook's OnMouseZChanged dollies the camera.
    PollWheelScroll();

    UpdateMeter();
    UpdateHoverFocus();
    if (PollPointerActionClick())
        return;
    PollScrollbarDrag();
    PollSliderDrag();
    UpdateMarquee();
    UpdateRowHighlight();
}

bool OptionsScrollList::OnKeyDown(unsigned nChar)
{
    const Kind kind = m_provider.RowKind(m_rowFocus);
    switch (nChar)
    {
        case SDLK_UP:
            MoveFocus(-1);
            return true;
        case SDLK_DOWN:
            MoveFocus(+1);
            return true;
        case SDLK_PAGEUP:
            MoveFocus(-kVisibleSlots);
            return true;
        case SDLK_PAGEDOWN:
            MoveFocus(+kVisibleSlots);
            return true;
        case SDLK_LEFT:
            if (kind == KindBinding)
            {
                if (m_bindingSlot != 0)
                {
                    m_bindingSlot = 0;
                    RenderPage();
                }
                return true;
            }
            CycleFocusedRowValue(-1);
            return true;
        case SDLK_RIGHT:
            if (kind == KindBinding)
            {
                if (m_bindingSlot != 1)
                {
                    m_bindingSlot = 1;
                    RenderPage();
                }
                return true;
            }
            CycleFocusedRowValue(+1);
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (kind == KindAction)
            {
                // Don't dispatch from here — return false so the engine
                // routes Enter to the focused slot Hover control (a
                // C3DActiveText), whose native Enter handler plays the
                // configured _clickSound and then fires
                // _parent->OnButtonClicked(IDC).  OptionsScrollList::
                // OnButtonClicked picks that up via the digit==3 +
                // KindAction branch and calls OnRowAction — same destination
                // as a mouse click, with the same audible feedback.
                // Disabled rows: still consume the key so the engine
                // doesn't fall back to some unrelated handler, but skip
                // the action.
                return !CanRowInvokeAction(kind, m_provider.IsDisabled(m_rowFocus));
            }
            if (kind == KindBinding)
            {
                if (!CanRowOpenBinding(kind, m_provider.IsDisabled(m_rowFocus)))
                    return true;
                m_provider.OnBindingClicked(m_rowFocus, m_bindingSlot, m_host);
                return true;
            }
            CycleFocusedRowValue(+1);
            return true;
    }
    return false;
}

bool OptionsScrollList::OnButtonClicked(int idc)
{
    // Slot click overlays — Hover (5N3), chevrons (5N8/5N9), bar click (5N7).
    if (idc < 500 || idc >= 500 + kVisibleSlots * 10)
        return false;
    int slot = SlotForControlIdc(idc);
    if (slot < 0)
        return false;
    int digit = idc - (500 + slot * 10);
    int rowIdx = RowAtSlot(slot);
    if (rowIdx < 0)
        return false;

    Kind kind = m_provider.RowKind(rowIdx);
    bool disabled = m_provider.IsDisabled(rowIdx);
    if (!CanRowReceiveFocus(kind))
        return false;

    if (digit == 3 || (digit == 1 && kind == KindAction))
    {
        // Full-row click — for Action rows, fire the action; for Boolean
        // rows toggle immediately; for the remaining value rows the click
        // already moved focus via UpdateHoverFocus.  Action rows render
        // their centred visible text through ValueStep (5N1), stretched
        // across the full row, so mouse clicks on that text must route
        // the same way as the hidden Hover overlay (5N3).
        if (kind == KindAction)
        {
            if (!CanRowInvokeAction(kind, disabled))
                return true;
            m_rowFocus = rowIdx;
            FocusFocusedRow();
            m_provider.OnRowAction(rowIdx, m_host);
            return true;
        }
        // Binding rows: full-row click defaults to primary slot.
        // (Alt slot reachable via the BarClick overlay at digit==7.)
        if (kind == KindBinding)
        {
            if (!CanRowOpenBinding(kind, disabled))
                return true;
            m_rowFocus = rowIdx;
            m_bindingSlot = 0;
            FocusFocusedRow();
            RenderPage();
            m_provider.OnBindingClicked(rowIdx, 0, m_host);
            return true;
        }
        if (kind == KindBoolean)
        {
            m_rowFocus = rowIdx;
            FocusFocusedRow();
            CycleFocusedRowValue(+1);
            return true;
        }
        return true;
    }
    if (digit == 8 || digit == 9)
    {
        if (kind == KindBinding && m_provider.BindingUsesChevronUi(rowIdx))
        {
            if (!CanRowOpenBinding(kind, disabled))
                return true;
            m_rowFocus = rowIdx;
            FocusFocusedRow();
            m_provider.OnBindingClicked(rowIdx, m_bindingSlot, m_host);
            return true;
        }
        if (kind != KindStepper && kind != KindBoolean)
            return false;
        m_rowFocus = rowIdx;
        FocusFocusedRow();
        CycleFocusedRowValue(digit == 9 ? +1 : -1);
        return true;
    }
    if (digit == 7)
    {
        if (kind == KindBinding)
        {
            if (!CanRowOpenBinding(kind, disabled))
                return true;
            m_rowFocus = rowIdx;
            m_bindingSlot = 1;
            FocusFocusedRow();
            RenderPage();
            m_provider.OnBindingClicked(rowIdx, 1, m_host);
            return true;
        }
        m_rowFocus = rowIdx;
        FocusFocusedRow();
        RowDef row = m_provider.RowFor(rowIdx);
        if (CanRowAdjustValue(kind, disabled) && row.count == -1 && m_notebook)
        {
            if (auto* clickCtrl = dynamic_cast<Control3D*>(m_notebook->GetCtrl(idc)))
            {
                float u = clickCtrl->GetU();
                int p = std::clamp(int(u * 20.0f + 0.5f) * 5, 0, 100);
                m_provider.SetRowValue(rowIdx, p);
                m_marqueeStartMs = GlobalTickCount();
                RenderPage();
            }
        }
        return true;
    }
    return false;
}

} // namespace Poseidon
