#include <Poseidon/UI/UITestEngine.hpp>
#include <Poseidon/UI/UIActiveDisplay.hpp>

#include <Poseidon/World/World.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>

#include <fstream>
#include <filesystem>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

namespace Poseidon
{

UITestEngine* GUIRecorder = nullptr;

// ── Display Navigation ──

int UITestEngine::GetActiveDisplayIDD() const
{
    auto* display = FindActiveDisplay();
    return display ? display->IDD() : -1;
}

bool UITestEngine::PollDisplayChanged(int& idd)
{
    int current = GetActiveDisplayIDD();
    if (current != _harnessLastIDD)
    {
        _harnessLastIDD = current;
        idd = current;
        return true;
    }
    return false;
}

ControlsContainer* UITestEngine::FindActiveDisplay() const
{
    return UIActiveDisplay::FindTopmost(GWorld);
}

// ── Control Introspection ──

const char* UITestEngine::GetControlTypeName(IControl* ctrl)
{
    if (!ctrl)
        return "null";
    if (dynamic_cast<CButton*>(ctrl))
        return "CButton";
    if (dynamic_cast<CEdit*>(ctrl))
        return "CEdit";
    if (dynamic_cast<CCombo*>(ctrl))
        return "CCombo";
    if (dynamic_cast<CListBox*>(ctrl))
        return "CListBox";
    if (dynamic_cast<CSlider*>(ctrl))
        return "CSlider";
    if (dynamic_cast<CProgressBar*>(ctrl))
        return "CProgressBar";
    if (dynamic_cast<CTree*>(ctrl))
        return "CTree";
    if (dynamic_cast<CHTML*>(ctrl))
        return "CHTML";
    if (dynamic_cast<CStaticTime*>(ctrl))
        return "CStaticTime";
    if (dynamic_cast<CStatic*>(ctrl))
        return "CStatic";
    if (dynamic_cast<CActiveText*>(ctrl))
        return "CActiveText";
    if (dynamic_cast<Control*>(ctrl))
        return "Control";
    return "IControl";
}

std::string UITestEngine::GetControlText(IControl* ctrl)
{
    if (!ctrl)
        return {};
    if (ctrl->HasSemanticTestText())
        return std::string((const char*)ctrl->GetSemanticTestText());

    // Try each known text-bearing type (RString has operator const char*)
    if (auto* b = dynamic_cast<CButton*>(ctrl))
        return std::string((const char*)b->GetText());
    if (auto* s = dynamic_cast<CStatic*>(ctrl))
        return std::string((const char*)s->GetText());
    if (auto* e = dynamic_cast<CEdit*>(ctrl))
        return std::string((const char*)e->GetText());
    if (auto* e3 = dynamic_cast<C3DEdit*>(ctrl))
        return std::string((const char*)e3->GetText());
    if (auto* cb = dynamic_cast<CCombo*>(ctrl))
    {
        int sel = cb->GetCurSel();
        return sel >= 0 ? std::string((const char*)cb->GetText(sel)) : std::string{};
    }
    if (auto* a = dynamic_cast<CActiveText*>(ctrl))
        return std::string((const char*)a->GetText());
    if (auto* s3 = dynamic_cast<C3DStatic*>(ctrl))
        return std::string((const char*)s3->GetText());
    if (auto* a3 = dynamic_cast<C3DActiveText*>(ctrl))
        return std::string((const char*)a3->GetText());
    return {};
}

std::string UITestEngine::GetHtmlText(IControl* ctrl)
{
    auto* html = dynamic_cast<CHTMLContainer*>(ctrl);
    if (!html)
        return {};

    // HTML controls (mission/overview preview) carry their text in parsed
    // section fields; concatenate them so a test can read the rendered content
    // (e.g. assert the wizard overview switched to the selected template).
    std::string text;
    for (int s = 0; s < html->NSections(); ++s)
    {
        const HTMLSection& section = html->GetSection(s);
        for (int f = 0; f < section.fields.Size(); ++f)
        {
            const char* fieldText = section.fields[f].text;
            if (!fieldText || !*fieldText)
                continue;
            if (!text.empty())
                text += ' ';
            text += fieldText;
        }
    }
    return text;
}

void UITestEngine::SetSemanticControlText(IControl* ctrl, const char* text)
{
    if (!ctrl)
        return;
    if (!text || !*text)
    {
        ClearSemanticControlText(ctrl);
        return;
    }
    ctrl->SetSemanticTestText(RString(text));
}

void UITestEngine::ClearSemanticControlText(IControl* ctrl)
{
    if (ctrl)
        ctrl->ClearSemanticTestText();
}

// ── Dump Mode ──

void UITestEngine::DumpControls(ControlsContainer* container, int depth)
{
    if (!container)
        return;

    std::string indent(depth * 2, ' ');
    LOG_INFO(Core, "{}[UI-DUMP] IDD={} objects={} alwaysShow={}", indent, container->IDD(),
             container->GetObjects().Size(), container->AlwaysShow() ? 1 : 0);

    // Dump 3D objects
    auto& objects = container->GetObjects();
    LOG_INFO(Core, "{}  [objects_count={}]", indent, objects.Size());
    for (int i = 0; i < objects.Size(); i++)
    {
        ControlObject* obj = objects[i];
        if (!obj)
        {
            LOG_INFO(Core, "{}  OBJ[{}] null", indent, i);
            continue;
        }
        auto* a = dynamic_cast<ControlObjectContainerAnim*>(obj);
        auto* z = dynamic_cast<ControlObjectWithZoom*>(obj);
        LOG_INFO(Core, "{}  OBJ[{}] IDC={} visible={} isContainerAnim={} isWithZoom={}{}{}", indent, i, obj->IDC(),
                 obj->IsVisible() ? 1 : 0, a ? 1 : 0, z ? 1 : 0,
                 a ? std::string(" anim=") + std::to_string(a->IsAnimating()) +
                         " phase=" + std::to_string(a->GetPhase())
                   : "",
                 z ? std::string(" zooming=") + std::to_string(z->IsZooming()) : "");
    }

    // Walk controls via brute-force IDC scan
    for (int idc = -1; idc < 2000; idc++)
    {
        IControl* ctrl = container->GetCtrl(idc);
        if (!ctrl)
            continue;
        auto* c = dynamic_cast<Control*>(ctrl);
        std::string text = GetControlText(ctrl);
        if (c)
        {
            LOG_INFO(Core, "{}  IDC={} type={} visible={} enabled={} pos=({},{},{},{}){}", indent, ctrl->IDC(),
                     GetControlTypeName(ctrl), ctrl->IsVisible() ? 1 : 0, ctrl->IsEnabled() ? 1 : 0, c->X(), c->Y(),
                     c->W(), c->H(), text.empty() ? "" : " text=\"" + text + "\"");
        }
        else
        {
            LOG_INFO(Core, "{}  IDC={} type={} visible={} enabled={}{}", indent, ctrl->IDC(), GetControlTypeName(ctrl),
                     ctrl->IsVisible() ? 1 : 0, ctrl->IsEnabled() ? 1 : 0,
                     text.empty() ? "" : " text=\"" + text + "\"");
        }
    }
}

void UITestEngine::Step()
{
    // Dump mode / Record mode: monitor display changes
    ControlsContainer* display = FindActiveDisplay();
    int currentIDD = display ? display->IDD() : -1;
    if (currentIDD != _lastDumpIDD)
    {
        _lastDumpIDD = currentIDD;
        if (!_recordPath.empty())
            RecordDisplayChange(currentIDD);
        if (_dumpMode)
        {
            if (display)
            {
                LOG_INFO(Core, "[UI-DUMP] Display changed to IDD={}", currentIDD);
                DumpControls(display);
            }
            else
            {
                LOG_INFO(Core, "[UI-DUMP] No active display");
            }
        }
    }
}

// ── Recording ──

void UITestEngine::SetRecordFile(const std::string& path)
{
    _recordPath = path;
    _recordedEvents.clear();
    GUIRecorder = this;
    LOG_INFO(Core, "UI record mode: events will be saved to '{}'", path);
}

void UITestEngine::RecordKey(int scancode, bool down)
{
    if (_recordPath.empty())
        return;
    int frame = _frameCounter ? *_frameCounter : 0;
    _recordedEvents.push_back({frame, "key", scancode, down ? 1 : 0, 0, 0});
}

void UITestEngine::RecordMouseButton(int button, bool down, float x, float y)
{
    if (_recordPath.empty())
        return;
    int frame = _frameCounter ? *_frameCounter : 0;
    _recordedEvents.push_back({frame, "mouse", button, down ? 1 : 0, x, y});
}

void UITestEngine::RecordDisplayChange(int idd)
{
    if (_recordPath.empty())
        return;
    int frame = _frameCounter ? *_frameCounter : 0;
    _recordedEvents.push_back({frame, "display", idd, 0, 0, 0});
}

void UITestEngine::FlushRecording()
{
    if (_recordPath.empty() || _recordedEvents.empty())
        return;

    std::filesystem::create_directories(std::filesystem::path(_recordPath).parent_path());
    std::ofstream out(_recordPath);
    if (!out.is_open())
    {
        LOG_ERROR(Core, "Failed to write recording to '{}'", _recordPath);
        return;
    }

    for (const auto& ev : _recordedEvents)
    {
        // JSONL format — one event per line
        if (ev.type == "key")
        {
            out << "{\"frame\":" << ev.frame << ",\"type\":\"key\",\"scancode\":" << ev.param1
                << ",\"down\":" << (ev.param2 ? "true" : "false") << "}\n";
        }
        else if (ev.type == "mouse")
        {
            out << "{\"frame\":" << ev.frame << ",\"type\":\"mouse\",\"button\":" << ev.param1
                << ",\"down\":" << (ev.param2 ? "true" : "false") << ",\"x\":" << ev.x << ",\"y\":" << ev.y << "}\n";
        }
        else if (ev.type == "display")
        {
            out << "{\"frame\":" << ev.frame << ",\"type\":\"display\",\"idd\":" << ev.param1 << "}\n";
        }
    }

    LOG_INFO(Core, "UI recording saved: {} events -> '{}'", _recordedEvents.size(), _recordPath);
}

} // namespace Poseidon
