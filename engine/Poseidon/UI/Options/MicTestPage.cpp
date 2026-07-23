#include <Poseidon/UI/Options/MicTestPage.hpp>
#include <Poseidon/UI/Options/MeterWidget.hpp>
#include <Poseidon/UI/Options/NotebookTheme.hpp>
#include <Poseidon/UI/Options/OptionsShell.hpp>

#include <Poseidon/Core/Global.hpp>

#include <algorithm>
#include <cmath>

namespace Poseidon
{

namespace
{
// Mirrors OPT_MICTEST_METER_* in optionsUI.hpp.  Kept in sync by
// hand; the resource constants drive layout at parse time, these
// drive per-frame repaint.  If the layout shifts, update both.
constexpr MeterBarLayout kMeter = {
    /* trackX */ 0.05f + 0.10f, // OPT_MICTEST_METER_X
    /* trackY */ 0.30f + 0.23f, // OPT_MICTEST_METER_Y
    /* trackW */ 0.90f - 0.20f, // OPT_MICTEST_METER_W
    /* trackH */ 0.04f,         // OPT_MICTEST_METER_H
    /* peakW  */ 0.005f,        // OPT_MICTEST_PEAK_W
};
constexpr int kSampleRate = 16000;
constexpr int kCaptureSamples = 4096;
constexpr int kIdcMeterTrack = 9510;
constexpr int kIdcMeterFill = 9511;
constexpr int kIdcMeterPeak = 9512;

void ThemeMeterStatic(ControlObjectContainer& notebook, int idc)
{
    if (auto* s3d = dynamic_cast<C3DStatic*>(notebook.GetCtrl(idc)))
    {
        s3d->SetColor(NotebookTheme::ResourceColorOr(s3d->GetColor()));
        s3d->SetBgColor(NotebookTheme::ResourceColorOr(s3d->GetBgColor()));
    }
}
} // namespace

void MicTestPage::Mount(OptionsShell& shell)
{
    if (m_capture.open(nullptr, kSampleRate, kCaptureSamples))
    {
        m_capture.start();
        m_loopback.open(kSampleRate);
    }
    ButtonModalPage::Mount(shell);
    if (auto* nb = dynamic_cast<ControlObjectContainer*>(shell.GetCtrl(105)))
    {
        ThemeMeterStatic(*nb, kIdcMeterTrack);
        ThemeMeterStatic(*nb, kIdcMeterFill);
        ThemeMeterStatic(*nb, kIdcMeterPeak);
    }
}

void MicTestPage::Unmount(OptionsShell& shell)
{
    m_loopback.close();
    if (m_capture.isCapturing())
        m_capture.stop();
    if (m_capture.isOpen())
        m_capture.close();
    ButtonModalPage::Unmount(shell);
}

void MicTestPage::OnSimulate(OptionsShell& shell)
{
    // Pump capture -> loopback for sidetone.  tick() is also what
    // drives lastFramePeak() each frame, so the meter reads from the
    // same data the user is hearing.
    if (m_loopback.isOpen())
        m_loopback.tick(m_capture);

    float target = m_capture.isCapturing() ? m_capture.lastFramePeak() : 0.0f;

    AdvanceMeterBar(target, m_level, m_peak);

    auto* nb = dynamic_cast<ControlObjectContainer*>(shell.GetCtrl(105));
    if (!nb)
        return;
    RenderMeterBar(*nb, kIdcMeterFill, kIdcMeterPeak, kMeter, m_level / 100.0f, m_peak / 100.0f);
}

} // namespace Poseidon
