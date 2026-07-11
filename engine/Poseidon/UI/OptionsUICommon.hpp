#pragma once
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/UI/DisplayUI.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/Chat.hpp>

// Shared utility functions

namespace Poseidon
{
RString GetUserDirectory();
RString GetUserMissionsBase(); // shared (non-roaming) base for editor missions
RString GetUserProfile();
RString GetUserParams();
RString GetCampaignDirectory(RString name);
RString GetCampaignSaveDirectory(RString campaign);
RString GetMissionDirectory();
RString GetMissionSaveDirectory(RString mission);
RString GetSaveDirectory();
RString GetSaveDirectoryCampaign(RString campaign);
RString GetSaveDirectoryMission(RString world, RString mission);
RString GetTmpSaveDirectory();
RString CreateSingleMissionBank(RString filename, bool uniquePrefix = false);
void CreatePath(RString path);
void SetBaseDirectory(RString dir);
void SetBaseSubdirectory(RString dir);
void SetCampaign(RString name);
void SetMission(RString world, RString mission, RString subdir);
void SetMission(RString world, RString mission);
void ApplyCurrentMissionViewDistance();
RString GetBaseDirectory();
RString GetBaseSubdirectory();
LSError SaveMission(const char* filename);
LSError LoadMission(const char* filename);
void CampaignSaveWeaponPool(WeaponsInfo& pool);
void CampaignLoadWeaponPool(WeaponsInfo& pool);
bool IsIdentityDead(RString identity);
void EmptyDeadIdentities();
void AddDeadIdentity(RString identity);
void SetMissionDisplayName(RString campaign, RString battle, RString mission, RString displayName);
void AddMission(RString campaign, RString battle, RString mission, RString displayName);
void StartRandomCutscene(RString world);
RString GetCampaignMissionDirectory(RString campaign, RString mission);

struct CountedString : SerializeClass
{
    RString name;
    int count;
    LSError Serialize(ParamArchive& ar) override;
};

struct MissionHistory
{
    RString missionName;
    RString displayName;
    bool completed;
    AIStatsCampaign stats;
    AutoArray<CountedString> weapons;
    AutoArray<CountedString> magazines;
    AutoArray<RString> dead;
    MissionHistory()
    {
        missionName = "";
        displayName = "";
        completed = false;
    }
    LSError Serialize(ParamArchive& ar);
    void AddWeapons(RString name, int count);
    void AddMagazines(RString name, int count);
};

struct BattleHistory
{
    RString battleName;
    AutoArray<MissionHistory> missions;
    BattleHistory() { battleName = ""; }
    LSError Serialize(ParamArchive& ar);
};

struct CampaignHistory : public SerializeClass
{
    RString campaignName;
    AutoArray<BattleHistory> battles;
    CampaignHistory() { campaignName = ""; }
    void Clear(RString campaign)
    {
        campaignName = campaign;
        battles.Clear();
    }
    MissionHistory* CurrentMission();
    void AddMission(RString campaign, RString battle, RString mission, RString displayName);
    void MissionCompleted(RString campaign, RString battle, RString mission);
    void LoadWeaponPool(WeaponsInfo& pool);
    void SaveWeaponPool(WeaponsInfo& pool);
    LSError Serialize(ParamArchive& ar) override;
};

struct ContinueInfo
{
    Rank rank;
    float time;
    RString island;
    RString mission;

    LSError Serialize(ParamArchive& ar);
};

// bool IsHeader();
// void SaveHeader();

class DisplaySingleMission : public Display
{
  protected:
    int _exitWhenClose;

    int _langCbToken = -1;

    RString _directory;

  public:
    bool _cadetMode;

  public:
    DisplaySingleMission(ControlsContainer* parent);
    ~DisplaySingleMission() override;
    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    void OnLBSelChanged(int idc, int curSel) override;
    void OnLBDblClick(int idc, int curSel) override;
    void OnButtonClicked(int idc) override;
    void OnCtrlClosed(int idc) override;
    RString GetDirectory() { return _directory; }

  protected:
    void OnChangeMission();
    void OnChangeDifficulty();
    void LoadParams();
    void SaveParams();
    void LoadDirectory();
    void RefreshLanguage();
};

// Revert display
class DisplayRevert : public Display
{
  protected:
    int _exitWhenClose;

  public:
    DisplayRevert(ControlsContainer* parent);

    void OnButtonClicked(int idc) override;
    void OnSimulate(EntityAI* vehicle) override;
};

class DisplayCampaignLoad : public Display
{
  protected:
    int _exitWhenClose;
    int _langCbToken = -1;
    ParamFile _cfg;

  public:
    AutoArray<CampaignHistory> _campaigns;
    int _currentCampaign;
    bool _cadetMode;
    bool _showStatistics;

  public:
    DisplayCampaignLoad(ControlsContainer* parent);
    ~DisplayCampaignLoad() override;

    void OnButtonClicked(int idc) override;
    void OnLBSelChanged(int idc, int curSel) override;
    void OnLBDblClick(int idc, int curSel) override;
    void OnCtrlClosed(int idc) override;
    void OnSimulate(EntityAI* vehicle) override;
    void OnChildDestroyed(int idd, int exit) override;

  protected:
    void LoadCampaigns();

    void OnChangeCampaign();
    //	void OnChangeBattle();
    void OnChangeMission();
    void OnChangeDifficulty();
    void LoadParams();
    void SaveParams();

    void OnCampaignCheat();
    friend bool DebugUnlockAllCampaigns();
    void RefreshLanguage();

    void ShowStatistics(bool show);

    void UpdateStatistics(AIStatsCampaign* begin, AIStatsCampaign* end);
};

// Game display
class DisplayGame : public Display
{
  public:
    DisplayGame(ControlsContainer* parent) : Display(parent) { Load("RscDisplayGame"); }

    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    bool CanDestroy() override;
};

class DisplaySave : public Display
{
  public:
    DisplaySave(ControlsContainer* parent) : Display(parent) { Load("RscDisplaySave"); }

    bool CanDestroy() override;
};

class DisplayOptions : public Display
{
  protected:
    int _exitWhenClose;

  public:
    /*
        float _oldBright;
        float _oldGamma;
        float _oldFrameRate;
        float _oldQuality;

        float _oldMusic;
        float _oldEffects;
        float _oldVoices;

        bool _oldTitles;
        bool _oldRadio;
    */

    DisplayOptions(ControlsContainer* parent, bool enableSimulation, bool credits);
    //	Control *OnCreateCtrl(int type, int idc, const ParamEntry &cls);
    void OnButtonClicked(int idc) override;
    void OnChildDestroyed(int idd, int exit) override;
    //	void OnSliderPosChanged(int idc, float pos);
    void OnCtrlClosed(int idc) override;
};

// True when CfgCredits defines a usable cutscene; drives whether the Credits
// button is offered (a package with no credits leaves the cutscene empty).
bool IsCreditsConfigured();
void PlayCreditsCutscene(Display* parent);

class DisplayCredits : public Display
{
  public:
    DisplayCredits(ControlsContainer* parent) : Display(parent) { Load("RscDisplayCredits"); }
};

class DisplayOptionsVideo : public Display
{
  public:
    float _oldBright;
    float _oldGamma;
    float _oldFrameRate;
    float _oldQuality;
    float _oldVisibility;
    float _oldTerrain;

    int _oldResX, _oldResY, _oldBpp;
    bool _oldWindowed;
    int _oldRefreshRate;

    bool _oldObjShadows;
    bool _oldVehShadows;
    bool _oldSmokes;
    bool _oldBlood;
    bool _oldOcclusions;
    bool _oldHWTL;
    bool _oldMultitexturing;
    bool _oldWBuffer;

    FindArray<ResolutionInfo> _resolutions;
    FindArray<int> _refreshRates;

    bool _initializingControls = false;

    DisplayOptionsVideo(ControlsContainer* parent, bool enableSimulation);
    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    void OnButtonClicked(int idc) override;
    void OnSliderPosChanged(int idc, float pos) override;
    void OnLBSelChanged(int idc, int curSel) override;

  protected:
    int InitResolutions();
    void UpdateRefreshRates(int res);
    void UpdateTerrainGrids();
};

class DisplayOptionsAudio : public Display
{
  public:
    float _oldMusic;
    float _oldEffects;
    float _oldVoices;
    // _oldSamplingRate;
    bool _oldHWAcc;
    bool _oldEAX;
    bool _oldSingleVoice;

    DisplayOptionsAudio(ControlsContainer* parent, bool enableSimulation);
    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    void OnButtonClicked(int idc) override;
    void OnSliderPosChanged(int idc, float pos) override;
};

class CDifficulties : public C3DListBox
{
  public:
    bool cadetDifficulty[DTN];
    bool veteranDifficulty[DTN];

  protected:
    int _column;

  public:
    CDifficulties(ControlsContainer* parent, int idc, const ParamEntry& cls);

    bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
    void OnLButtonDown(float x, float y) override;

    void DrawItem(Vector3Par position, Vector3Par down, int i, float alpha) override;

  protected:
    void Toggle(int column, int index)
    {
        if (column == 1)
        {
            cadetDifficulty[index] = !cadetDifficulty[index];
        }
        else if (column == 2 && Config::diffDesc[index].enabledInVeteran)
        {
            veteranDifficulty[index] = !veteranDifficulty[index];
        }
    }
};

class DisplayDifficulty : public Display
{
  public:
    CDifficulties* _diff;

    bool _oldTitles;
    bool _oldRadio;

    DisplayDifficulty(ControlsContainer* parent, bool enableSimulation);
    Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
    void OnButtonClicked(int idc) override;
};

} // namespace Poseidon

using ::Poseidon::EmptyDeadIdentities;
using ::Poseidon::GetCampaignSaveDirectory;
using ::Poseidon::GetTmpSaveDirectory;
using ::Poseidon::IsIdentityDead;
using ::Poseidon::SetBaseSubdirectory;
using ::Poseidon::SetCampaign;
using ::Poseidon::StartRandomCutscene;
