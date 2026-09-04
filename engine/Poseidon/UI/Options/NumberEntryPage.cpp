#include <Poseidon/UI/Options/NumberEntryPage.hpp>

#include <Poseidon/UI/Options/OptionsShell.hpp>

#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <Poseidon/Foundation/Framework/AppFrame.hpp>

#include <SDL3/SDL_keycode.h>

#include <charconv>
#include <cmath>
#include <cstdio>
#include <utility>

namespace Poseidon
{
NumberEntryPage::NumberEntryPage(std::string title, float value, float minimum, float maximum, int decimalPlaces,
                                 std::string unit, ApplyCallback onApply)
    : m_title(std::move(title)), m_value(value), m_minimum(minimum), m_maximum(maximum), m_decimalPlaces(decimalPlaces),
      m_unit(std::move(unit)), m_onApply(std::move(onApply))
{
}

const char* NumberEntryPage::ResourceClassName() const
{
    const ParamEntry& primary = Res >> "RscOptionsPageNumberEntry";
    return primary.FindEntry("controls") ? "RscOptionsPageNumberEntry" : "RscOptionsPageNumberEntryFallback";
}

void NumberEntryPage::Mount(OptionsShell& shell)
{
    OptionsPage::Mount(shell);

    if (auto* entry = dynamic_cast<C3DEdit*>(shell.GetCtrl(kEntryIdc)))
    {
        entry->SetMaxChars(24);
        entry->SetText(RString(FormatValue(m_value).c_str()));
        entry->SelectAll();
    }
    if (auto* notebook = shell.GetNotebook())
        notebook->CaptureTextInputCtrl(kEntryIdc);
    ShowRangePrompt(shell);
}

void NumberEntryPage::Unmount(OptionsShell& shell)
{
    if (auto* notebook = shell.GetNotebook())
        notebook->ReleaseTextInputCtrl();
    OptionsPage::Unmount(shell);
}

std::optional<float> NumberEntryPage::ParseValue(const char* text, float minimum, float maximum)
{
    if (!text)
        return std::nullopt;

    const char* begin = text;
    while (*begin == ' ' || *begin == '\t')
        ++begin;
    const char* end = begin;
    while (*end)
        ++end;

    float value = 0.0f;
    auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc())
        return std::nullopt;
    while (result.ptr != end && (*result.ptr == ' ' || *result.ptr == '\t'))
        ++result.ptr;
    if (result.ptr != end || !std::isfinite(value) || value < minimum || value > maximum)
        return std::nullopt;
    return value;
}

bool NumberEntryPage::OnButtonClicked(OptionsShell& shell, int idc)
{
    if (idc == kApplyIdc)
        return Apply(shell);
    if (idc == kCancelIdc || idc == IDC_CANCEL)
    {
        shell.PopPage();
        return true;
    }
    return false;
}

bool NumberEntryPage::OnKeyDown(OptionsShell& shell, unsigned nChar)
{
    if (nChar == SDLK_ESCAPE)
    {
        shell.PopPage();
        return true;
    }
    if (nChar == SDLK_RETURN || nChar == SDLK_KP_ENTER)
    {
        if (shell.GetFocusedNotebookIdc() == kCancelIdc)
        {
            shell.PopPage();
            return true;
        }
        return Apply(shell);
    }

    const int actions[] = {kApplyIdc, kCancelIdc};
    if (nChar == SDLK_TAB)
        return WrapFocus(shell, SDLK_DOWN, actions, 2);
    if (WrapFocus(shell, nChar, actions, 2))
        return true;
    if (auto* entry = dynamic_cast<C3DEdit*>(shell.GetCtrl(kEntryIdc)))
        return entry->OnKeyDown(nChar, 0, 0);
    return false;
}

void NumberEntryPage::OnSimulate(OptionsShell& shell)
{
    if (m_invalidUntilMs != 0 && static_cast<int32_t>(Poseidon::Foundation::GlobalTickCount() - m_invalidUntilMs) >= 0)
    {
        ShowRangePrompt(shell);
        if (m_promptColorSaved)
        {
            if (auto* title = dynamic_cast<C3DStatic*>(shell.GetCtrl(kTitleIdc)))
                title->SetColor(m_promptColor);
        }
        m_invalidUntilMs = 0;
    }

    auto& input = InputSubsystem::Instance();
    float cursorX = input.GetCursorX();
    float cursorY = input.GetCursorY();
    bool firstSample = m_lastCursorX < -1.0f;
    bool moved = !firstSample && (cursorX != m_lastCursorX || cursorY != m_lastCursorY);
    m_lastCursorX = cursorX;
    m_lastCursorY = cursorY;
    if (!moved)
        return;

    auto* notebook = shell.GetNotebook();
    if (!notebook)
        return;
    float mouseX = 0.5f + cursorX * 0.5f;
    float mouseY = 0.5f + cursorY * 0.5f;
    IControl* hovered = notebook->GetCtrl(mouseX, mouseY);
    if (!hovered || hovered == notebook)
        return;

    const int actions[] = {kApplyIdc, kCancelIdc};
    FocusCycleIdc(shell, hovered->IDC(), actions, 2);
}

bool NumberEntryPage::Apply(OptionsShell& shell)
{
    auto* entry = dynamic_cast<C3DEdit*>(shell.GetCtrl(kEntryIdc));
    const std::optional<float> value = entry ? ParseValue(entry->GetText(), m_minimum, m_maximum) : std::nullopt;
    if (!value)
    {
        ShowInvalidPrompt(shell);
        if (entry)
            entry->SelectAll();
        return true;
    }

    if (m_onApply)
        m_onApply(*value);
    shell.PopPage();
    return true;
}

void NumberEntryPage::ShowRangePrompt(OptionsShell& shell)
{
    std::string prompt = m_title;
    prompt += " (";
    prompt += FormatValue(m_minimum);
    prompt += " - ";
    prompt += FormatValue(m_maximum);
    if (!m_unit.empty())
    {
        prompt += " ";
        prompt += m_unit;
    }
    prompt += ")";
    SetCtrlText(shell, kTitleIdc, prompt.c_str());
}

void NumberEntryPage::ShowInvalidPrompt(OptionsShell& shell)
{
    SetCtrlText(shell, kTitleIdc, LocalizeStringWithFallback("STR_DISP_OPT_INVALID_VALUE", "Invalid value"));
    if (auto* title = dynamic_cast<C3DStatic*>(shell.GetCtrl(kTitleIdc)))
    {
        if (!m_promptColorSaved)
        {
            m_promptColor = title->GetColor();
            m_promptColorSaved = true;
        }
        title->SetColor(PackedColor(242, 77, 77, 255));
    }
    m_invalidUntilMs = Poseidon::Foundation::GlobalTickCount() + 3000;
}

std::string NumberEntryPage::FormatValue(float value) const
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", m_decimalPlaces, value);
    return buffer;
}
} // namespace Poseidon
