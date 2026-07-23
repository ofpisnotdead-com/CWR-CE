#pragma once

// Audio settings page for the production OptionsShell.
//
// Row layout:
//   0 Output device   stepper  (live OAL device picker)
//   1 Music           slider 0..100  (GSoundsys->Set/GetCDVolume)
//   2 Effects         slider 0..100  (Set/GetWaveVolume)
//   3 Speech / VoN    slider 0..100  (Set/GetSpeechVolume)
//   4 3D Audio (EAX)  stepper Off/On (EnableEAX; preview plays a
//                                       reverb sample on toggle)
//   5 Microphone      stepper  (VoNCapture device picker)
//   6 Input level     read-only meter  (live mic peak)
//   7 Test Microphone action  (pushes MicTestPage modal)
// + Close             action  (added by WithCloseRow wrapper)
//
// Mount/Unmount lifecycle:
//   - Mount  : ducks any playing music waves (SuppressMusicForPreview)
//              so previews don't fight menu BGM, opens VoNCapture for
//              the meter.
//   - Unmount: terminates any active preview, restores music gain,
//              closes the capture, persists the changed volumes.

#include <Poseidon/UI/Options/ScrollListPage.hpp>
#include <Poseidon/Audio/Voice/VonCapture.hpp>

#include <array>
#include <string>
#include <unordered_set>
#include <vector>


namespace Poseidon
{
class AudioPage : public ScrollListPage
{
public:
	const char* TitleText() const override;

    static int VolumeToPercent(float value);
    static float PercentToVolume(int percent);

	void Mount(OptionsShell& shell) override;
	void OnReshown(OptionsShell& shell) override;
	void Unmount(OptionsShell& shell) override;
	void OnSimulate(OptionsShell& shell) override;

protected:
	OptionsScrollList::Provider& ProviderRef() override { return m_provider; }

	private:
	static const char* CloseLabel();
	static const char* CloseDescription();

	VoNCapture m_capture;

private:
	class AudioProvider : public OptionsScrollList::Provider
	{
	public:
		enum : int {
			kRowOutputDev = 0,
			kRowMusic     = 1,
			kRowEffects   = 2,
			kRowSpeech    = 3,
			kRowEax       = 4,
			kRowMicDev    = 5,
			kRowInputLvl  = 6,
			kRowTestMic   = 7,
			kRowCount     = 8,
		};

		int  RowCount() const override         { return kRowCount; }
		const char* RowLabel(int row) const override;
		const char* RowDescription(int row) const override;
		OptionsScrollList::RowDef RowFor(int row) const override;
		int  RowValue(int row) const override;
		void SetRowValue(int row, int v) override;
		void TickMeter(int row, float& outLevel, float& outPeak) override;

		OptionsScrollList::Kind RowKind(int row) const override
		{
			if (row == kRowEax) return OptionsScrollList::KindBoolean;
			if (row == kRowTestMic) return OptionsScrollList::KindAction;
			return OptionsScrollList::Provider::RowKind(row);
		}

		bool IsDisabled(int /*row*/) const override
		{
			// The OpenAL backend exposes this as software EFX reverb when
			// ALC_EXT_EFX is available, so the row stays live even though
			// legacy hardware acceleration is unsupported.
			return false;
		}

		void OnRowAction(int row, Display& host) override;
		void OnRowFocusChanged(int prevRow, int newRow) override;

		// Per-frame tick from AudioPage::OnSimulate so the focused-row
		// loop timer can re-trigger the preview after its idle window.
		void TickPreviewLoop();

		// Capture handle for the input-level meter; AudioPage owns the
		// VoNCapture lifetime and the provider just reads peaks from it.
		void SetCapture(VoNCapture* cap) { m_capture = cap; }

		// Refresh the cached device lists (call from AudioPage::Mount).
		void ClearUnavailableOutputDevices() { m_unavailableOutputDevs.clear(); }
		void RebuildOutputDeviceList() const;
		void RebuildInputDeviceList() const;

		const std::string& SelectedInputDevice() const { return m_selectedInput; }
		void SetSelectedInputDevice(const std::string& name) { m_selectedInput = name; }

	private:
		void RefreshToggleTexts() const;

		// Volume rows (Music / Effects / Speech) read/write GSoundsys
		// live, no local cache.  Only the device pickers keep state
		// here for unwired-row fallback when GSoundsys is null.
		int m_values[kRowCount] = {0, 0, 0, 0, 0, 1, 0, 0};

		VoNCapture* m_capture = nullptr;
		mutable std::array<std::string, 2> m_toggleText;
		mutable std::array<const char*, 2> m_toggleOptions{};

		// Output / input device lists cached at Mount.  Entry 0 is
		// always "System default" (mapping to nullptr in the AL call).
		//
		// Two parallel arrays per channel because OpenAL Soft tags every
		// enumerated device with "OpenAL Soft on " — which the app has
		// to pass back verbatim to alcOpenDevice / alcCaptureOpenDevice
		// but which is pure noise for the user (we're the only OAL impl
		// on the system, so the prefix carries no information).
		//
		//   m_outputDevs       : full names — fed to SwitchOutputDevice
		//   m_outputDevsDisplay: prefix-stripped — what the picker shows
		//   m_outputDevCStrs   : c_str() into m_outputDevsDisplay
		mutable std::vector<std::string> m_outputDevs;
		mutable std::vector<std::string> m_outputDevsDisplay;
		mutable std::vector<const char*> m_outputDevCStrs;
		mutable std::unordered_set<std::string> m_unavailableOutputDevs;
		mutable std::vector<std::string> m_inputDevs;
		mutable std::vector<std::string> m_inputDevsDisplay;
		mutable std::vector<const char*> m_inputDevCStrs;
		std::string m_selectedInput;       // "" -> System default

		// Focus-driven preview state.
		//   m_previewActiveRow      : focused row whose preview will play
		//   m_previewLoopTick       : reference tick (focus change OR last fire)
		//   m_previewStartedThisFocus: false until first fire — controls
		//     whether the elapsed-threshold is the short dwell (avoid
		//     start-on-mount race) or the long inter-loop gap.
		uint32_t    m_previewLoopTick = 0;
		int         m_previewActiveRow = -1;
		bool        m_previewStartedThisFocus = false;
	};

	AudioProvider                   m_audio;
	OptionsScrollList::WithCloseRow m_provider{m_audio, CloseLabel(), CloseDescription()};
};

} // namespace Poseidon
