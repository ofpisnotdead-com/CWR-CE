#include <Poseidon/UI/Options/OptionsPage.hpp>
#include <Poseidon/UI/Options/OptionsShell.hpp>

#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/UserActionDesc.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <SDL3/SDL_keycode.h>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

const char* ControlActionLabel(UserAction action)
{
    return InputSubsystem::GetUserActionLabel(action);
}

void OptionsPage::SetCtrlText(Display& display, int idc, const char* text)
{
    IControl* c = display.GetCtrl(idc);
    if (!c)
        return;
    RString rs(text ? text : "");
    if (auto* s3d = dynamic_cast<C3DStatic*>(c))
    {
        s3d->SetText(rs);
        return;
    }
    if (auto* a3d = dynamic_cast<C3DActiveText*>(c))
    {
        a3d->SetText(rs);
        return;
    }
    if (auto* s2d = dynamic_cast<CStatic*>(c))
    {
        s2d->SetText(rs);
        return;
    }
}

bool OptionsPage::WrapFocus(OptionsShell& shell, unsigned nChar, const int* idcs, int count, WrapMode mode)
{
    if (count <= 0 || !idcs)
        return false;
    bool isPrev = nChar == SDLK_UP || (mode == AnyAxis && nChar == SDLK_LEFT);
    bool isNext = nChar == SDLK_DOWN || (mode == AnyAxis && (nChar == SDLK_RIGHT || nChar == SDLK_TAB));
    if (!isPrev && !isNext)
        return false;

    int cur = -1;
    const int focusedIdc = shell.GetFocusedNotebookIdc();
    for (int i = 0; i < count; ++i)
    {
        if (idcs[i] == focusedIdc)
        {
            cur = i;
            break;
        }
    }
    int dir = isNext ? 1 : -1;
    int next = (cur < 0) ? 0 : (cur + dir + count) % count;
    for (int steps = 0; steps < count; ++steps)
    {
        if (shell.FocusNotebookCtrl(idcs[next]))
            return true;
        next = (next + dir + count) % count;
    }
    return false;
}

bool OptionsPage::ContainsCycleIdc(int idc, const int* idcs, int count)
{
    if (idc < 0 || !idcs || count <= 0)
        return false;
    for (int i = 0; i < count; ++i)
    {
        if (idcs[i] == idc)
            return true;
    }
    return false;
}

bool OptionsPage::FocusCycleIdc(OptionsShell& shell, int idc, const int* idcs, int count)
{
    if (!ContainsCycleIdc(idc, idcs, count))
        return false;
    if (shell.GetFocusedNotebookIdc() != idc)
        return shell.FocusNotebookCtrl(idc);
    return true;
}

void OptionsPage::Mount(OptionsShell& shell)
{
    const char* className = ResourceClassName();
    if (className && *className)
        MountFromClass(shell, className);
}

void OptionsPage::Unmount(OptionsShell& shell)
{
    auto* nb = shell.GetNotebook();
    if (!nb)
        return;
    // Drop in reverse mount order so the notebook's tracking indices
    // shift by ones we've already removed, not ones still in use.
    for (auto it = m_mountedIdcs.rbegin(); it != m_mountedIdcs.rend(); ++it)
    {
        nb->RemoveControl(*it);
    }
    m_mountedIdcs.clear();
}

void OptionsPage::MountFromClass(OptionsShell& shell, const char* className)
{
    auto* nb = shell.GetNotebook();
    if (!nb)
        return;

    const ParamEntry& cls = Res >> className;
    const ParamEntry* cfg = cls.FindEntry("controls");
    if (!cfg)
        return;

    auto loadOne = [&](const ParamEntry& ctrlCls)
    {
        Control* c = nb->LoadControl(ctrlCls);
        if (c)
            m_mountedIdcs.push_back(c->IDC());
    };

    if (cfg->IsClass())
    {
        for (int i = 0; i < cfg->GetEntryCount(); ++i)
        {
            const ParamEntry& ctrlCls = cfg->GetEntry(i);
            if (!ctrlCls.IsClass())
                continue;
            if (!ctrlCls.FindEntry("type"))
                continue;
            if (!ctrlCls.FindEntry("idc"))
                continue;
            loadOne(ctrlCls);
        }
    }
    else
    {
        // `controls[]` is a name list — look each entry up under the
        // page class.
        for (int i = 0; i < cfg->GetSize(); ++i)
        {
            RString name = (*cfg)[i];
            loadOne(cls >> name);
        }
    }
}

} // namespace Poseidon
