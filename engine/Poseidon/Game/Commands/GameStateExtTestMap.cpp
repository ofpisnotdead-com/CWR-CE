#include <Evaluator/express.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

#include <cstdio>

using namespace Poseidon;

/// triOpenMap -> "OK" or "FAIL:<reason>"
GameValue TriOpenMap(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");
    GWorld->CreateMainMap();
    GWorld->ForceMap(true);
    LOG_INFO(Core, "[tri] triOpenMap");
    return GameValue("OK");
}

/// triShowMap <0|1> -> "OK" or "FAIL:<reason>"
GameValue TriShowMap(const GameState* /*state*/, GameValuePar arg)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");
    auto* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!map)
        return GameValue("FAIL:no_map");
    map->ShowMap((float)arg != 0.0f);
    LOG_INFO(Core, "[tri] triShowMap {}", (float)arg != 0.0f);
    return GameValue("OK");
}

/// triMapSetScale <scale> — set the in-mission map zoom (clamped to the map's own min/max).
GameValue TriMapSetScale(const GameState* /*state*/, GameValuePar arg)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");
    auto* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!map || !map->GetMap())
        return GameValue("FAIL:no_map");
    map->GetMap()->SetScale((float)arg);
    LOG_INFO(Core, "[tri] triMapSetScale {}", (float)arg);
    return GameValue("OK");
}

/// triShowVoiceOverlay <0|1> — create / destroy the CapsLock VoIP voice-chat
/// overlay (the cursor-less HUD that normally appears while UAVoiceOverNet is
/// held).  Lets tests reproduce the menu-cursor-vanishes-under-overlay case
/// without driving the live action binding.
GameValue TriShowVoiceOverlay(const GameState* /*state*/, GameValuePar arg)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");
    if ((float)arg != 0.0f)
        GWorld->CreateVoiceChat(false);
    else
        GWorld->DestroyVoiceChat(0);
    LOG_INFO(Core, "[tri] triShowVoiceOverlay {}", (float)arg != 0.0f);
    return GameValue("OK");
}

/// triClickBriefingLink "<href>" -> "OK:section=<name>,img=<n>" / "FAIL:<reason>".
/// Finds the note in-page link with this href in the in-mission map's briefing/
/// notes control and replays its click (the `#X` -> SwitchSection path), then
/// reports the resulting current section and how many image fields it holds.
/// Used to regression-test in-page briefing links.
GameValue TriClickBriefingLink(const GameState* /*state*/, GameValuePar arg)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");
    auto* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!map)
        return GameValue("FAIL:no_map");
    CHTML* html = map->GetBriefingControl();
    if (!html)
        return GameValue("FAIL:no_briefing");
    // Diagnostic: FindControl (the real click router) only delivers to a control
    // that is visible AND enabled; otherwise map foreground controls may receive
    // the click before the notes link does.
    LOG_INFO(Core, "[tri] briefing control: visible={} enabled={} idc={}", html->IsVisible(), html->IsEnabled(),
             html->IDC());
    GameStringType href = static_cast<GameStringType>(arg);
    RString result = html->ActivateHRef((const char*)href);
    LOG_INFO(Core, "[tri] triClickBriefingLink {} -> {}", (const char*)href, (const char*)result);
    return GameValue((const char*)result);
}

/// triProbeClickBriefingLink "<href>" -> "OK:hit=x,y,section=<name>" /
/// "FAIL:findfield_miss" / "FAIL:no_link". Like triClickBriefingLink but goes
/// through the REAL hit-test (FindField) + OnMouseMove/OnLButtonDown, to locate
/// where a real click on the notes link is routed.
GameValue TriProbeClickBriefingLink(const GameState* /*state*/, GameValuePar arg)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");
    auto* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!map)
        return GameValue("FAIL:no_map");
    CHTML* html = map->GetBriefingControl();
    if (!html)
        return GameValue("FAIL:no_briefing");
    GameStringType href = static_cast<GameStringType>(arg);
    RString result = html->ProbeClickHRef((const char*)href);
    LOG_INFO(Core, "[tri] triProbeClickBriefingLink {} -> {}", (const char*)href, (const char*)result);
    return GameValue((const char*)result);
}

/// triBriefingSection -> the current briefing/notes section name (test hook).
GameValue TriBriefingSection(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue("");
    auto* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!map)
        return GameValue("");
    CHTML* html = map->GetBriefingControl();
    if (!html)
        return GameValue("");
    RString name = html->CurrentSectionName();
    LOG_INFO(Core, "[tri] triBriefingSection -> {}", (const char*)name);
    return GameValue((const char*)name);
}

/// triBriefingSwitch "<section>" -> switch the briefing/notes to a named section;
/// returns the resulting section name.
GameValue TriBriefingSwitch(const GameState* /*state*/, GameValuePar arg)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");
    auto* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!map)
        return GameValue("FAIL:no_map");
    CHTML* html = map->GetBriefingControl();
    if (!html)
        return GameValue("FAIL:no_briefing");
    GameStringType sec = static_cast<GameStringType>(arg);
    html->SwitchSection((const char*)sec);
    return GameValue((const char*)html->CurrentSectionName());
}

/// triBriefingLinkRoute "<href>" -> "OK:linkpos=x,y,route_idc=<n>,briefing_idc=56"
/// / "FAIL:<reason>". Finds the on-screen centre of the notes link, then asks the
/// in-mission map's hit-test (GetCtrl — the same foreground-first order the real
/// click dispatch uses) which control a click there routes to. If route_idc is
/// not the briefing control's idc, another control (e.g. the full-screen map)
/// intercepts the click before the notes.
GameValue TriBriefingLinkRoute(const GameState* /*state*/, GameValuePar arg)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");
    auto* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!map)
        return GameValue("FAIL:no_map");
    CHTML* html = map->GetBriefingControl();
    if (!html)
        return GameValue("FAIL:no_briefing");
    GameStringType href = static_cast<GameStringType>(arg);
    float sx = 0, sy = 0;
    if (!html->FindLinkScreenPos((const char*)href, sx, sy))
        return GameValue("FAIL:no_link");
    IControl* hit = map->GetCtrl(sx, sy);
    int routeIdc = hit ? hit->IDC() : -1;
    char buf[160];
    snprintf(buf, sizeof(buf), "OK:linkpos=%.3f,%.3f,route_idc=%d,briefing_idc=%d", sx, sy, routeIdc, html->IDC());
    LOG_INFO(Core, "[tri] triBriefingLinkRoute {} -> {}", (const char*)href, buf);
    return GameValue(buf);
}

/// triBriefingClickAt ["<href>", "<expectedSection>"] -> "OK" / "FAIL:..". Drives
/// the REAL CHTML::OnLButtonDown at the link's on-screen position with NO latched
/// hover (clears _activeField first), then asserts the notes switched to the
/// expected section.
GameValue TriBriefingClickAt(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:need_href_and_section");
    RString href = (GameStringType)a[0];
    RString want = (GameStringType)a[1];
    if (!GWorld)
        return GameValue("FAIL:no_world");
    auto* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!map)
        return GameValue("FAIL:no_map");
    CHTML* html = map->GetBriefingControl();
    if (!html)
        return GameValue("FAIL:no_briefing");
    float sx = 0, sy = 0;
    if (!html->FindLinkScreenPos((const char*)href, sx, sy))
        return GameValue("FAIL:no_link");
    // Clear any latched hover field so this exercises a click that arrives without
    // a preceding OnMouseMove for this control — the #32 failure mode.
    html->OnMouseMove(sx, sy, false);
    html->OnLButtonDown(sx, sy);
    RString got = html->CurrentSectionName();
    LOG_INFO(Core, "[tri] triBriefingClickAt {} -> section={} (want {})", (const char*)href, (const char*)got,
             (const char*)want);
    if (stricmp((const char*)got, (const char*)want) != 0)
    {
        char buf[160];
        snprintf(buf, sizeof(buf), "FAIL:got=%s,want=%s", (const char*)got, (const char*)want);
        return GameValue(buf);
    }
    return GameValue("OK");
}
