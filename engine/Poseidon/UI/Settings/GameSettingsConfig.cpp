#include <Poseidon/UI/Settings/GameSettingsConfig.hpp>
#include <Poseidon/UI/Settings/ViewDistance.hpp>

#include <Poseidon/UI/Locale/SupportedLanguages.hpp>

#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/UI/Settings/SettingsFile.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>

#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/IO/Filesystem/Utf8Paths.hpp>

#include <cstdlib>
#include <fstream>

namespace Poseidon
{
RString GetUserParams();
}

namespace Poseidon
{

namespace
{
struct LiveEnvironment : GameSettingsConfig::Environment
{
    std::string DetectSystemLanguage() const override { return CfgLib::DetectSystemLanguage(); }
};

std::string& SelectedVoiceLanguageStorage()
{
    static std::string language = "English";
    return language;
}

float& SelectedPreferredViewDistanceStorage()
{
    static float distance = 900.0f;
    return distance;
}

bool& RespectMissionViewDistanceStorage()
{
    static bool enabled = true;
    return enabled;
}

std::string GameSettingsPath()
{
    const std::string& dir = GamePaths::Instance().UserDir();
    return dir + "game.cfg";
}

std::string JoinPath(std::string dir, const char* filename)
{
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
        dir += '/';
    return dir + filename;
}

std::string LegacyPlayerPrefsPath()
{
    if (const char* userDir = std::getenv("POSEIDON_USER_DIR"); userDir && userDir[0] != '\0')
        return JoinPath(userDir, "prefs.cfg");

#ifdef _WIN32
    return {};
#else
    std::string configDir;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0] != '\0')
        configDir = xdg;
    else if (const char* home = std::getenv("HOME"); home && home[0] != '\0')
        configDir = JoinPath(home, ".config");
    else
        configDir = "/tmp";
    return JoinPath(JoinPath(configDir, "ARMA:CWA-RE-CE"), "prefs.cfg");
#endif
}

std::string LoadLegacyActiveProfile()
{
    const std::string path = LegacyPlayerPrefsPath();
    if (path.empty())
        return {};

    std::ifstream file(Poseidon::FilesystemPathFromUtf8(path));
    std::string line;
    while (std::getline(file, line))
    {
        constexpr const char prefix[] = "PlayerName=";
        if (line.rfind(prefix, 0) == 0)
            return line.substr(sizeof(prefix) - 1);
    }
    return {};
}

void ApplyToRuntime(const GameSettingsConfig& cfg)
{
    SetLanguage(RString(cfg.textLanguage.c_str()));
    SelectedVoiceLanguageStorage() = cfg.voiceLanguage;
    ENGINE_CONFIG.blood = cfg.blood;
    SelectedPreferredViewDistanceStorage() = ClampPreferredViewDistance(cfg.preferredViewDistance);
    RespectMissionViewDistanceStorage() = cfg.respectMissionViewDistance;
    if (GScene)
        GScene->SetPreferredViewDistance(SelectedPreferredViewDistanceStorage());
}

void SaveLegacySinks(const GameSettingsConfig& cfg)
{
    // Mirror blood + viewDistance into UserParams so mission scripts
    // (which read that file directly) see the current values.
    ParamFile userCfg;
    userCfg.Parse(Poseidon::GetUserParams());
    userCfg.Add("blood", cfg.blood);
    userCfg.Add("viewDistance", cfg.preferredViewDistance);
    if (userCfg.Save(Poseidon::GetUserParams()) != LSOK)
        LOG_WARN(Config, "GameSettings: failed to write '{}'", (const char*)Poseidon::GetUserParams());
}
} // namespace

void GameSettingsConfig::LoadDefaults()
{
    LiveEnvironment env;
    LoadDefaults(env);
}

void GameSettingsConfig::LoadDefaults(const Environment& env)
{
    textLanguage = CfgLib::NormalizeSupportedLanguage(env.DetectSystemLanguage());
    voiceLanguage = textLanguage;
    activeProfile.clear();
    blood = true;
    preferredViewDistance = 900.0f;
    respectMissionViewDistance = true;
}

bool GameSettingsConfig::Normalize()
{
    LiveEnvironment env;
    return Normalize(env);
}

bool GameSettingsConfig::Normalize(const Environment& env)
{
    bool changed = false;
    const std::string fallback = CfgLib::NormalizeSupportedLanguage(env.DetectSystemLanguage());

    const std::string normalizedText = CfgLib::NormalizeSupportedLanguage(textLanguage, fallback);
    if (normalizedText != textLanguage)
    {
        textLanguage = normalizedText;
        changed = true;
    }

    const std::string normalizedVoice = CfgLib::NormalizeSupportedLanguage(voiceLanguage, textLanguage);
    if (normalizedVoice != voiceLanguage)
    {
        voiceLanguage = normalizedVoice;
        changed = true;
    }

    const float normalizedViewDistance = ClampPreferredViewDistance(preferredViewDistance);
    if (normalizedViewDistance != preferredViewDistance)
    {
        preferredViewDistance = normalizedViewDistance;
        changed = true;
    }

    return changed;
}

bool GameSettingsConfig::Load(const std::string& path)
{
    if (!SettingsFileExists(path))
        return false;

    ParamFile cfg;
    if (cfg.Parse(RString(path.c_str())) != LSOK)
        return false;

    if (auto* entry = cfg.FindEntry("textLanguage"))
        textLanguage = ((RString)*entry).Data();
    if (auto* entry = cfg.FindEntry("voiceLanguage"))
        voiceLanguage = ((RString)*entry).Data();
    if (auto* entry = cfg.FindEntry("activeProfile"))
        activeProfile = ((RString)*entry).Data();
    if (auto* entry = cfg.FindEntry("blood"))
        blood = (bool)*entry;
    if (auto* entry = cfg.FindEntry("preferredViewDistance"))
        preferredViewDistance = (float)*entry;
    if (auto* entry = cfg.FindEntry("respectMissionViewDistance"))
        respectMissionViewDistance = (bool)*entry;

    return true;
}

bool GameSettingsConfig::Save(const std::string& path) const
{
    ParamFile cfg;
    cfg.Add("textLanguage", RString(textLanguage.c_str()));
    cfg.Add("voiceLanguage", RString(voiceLanguage.c_str()));
    cfg.Add("activeProfile", RString(activeProfile.c_str()));
    cfg.Add("blood", blood);
    cfg.Add("preferredViewDistance", preferredViewDistance);
    cfg.Add("respectMissionViewDistance", respectMissionViewDistance);

    return WriteSettingsFile(path, cfg);
}

bool EnsureGameSettingsFile(GameSettingsConfig& cfg, const std::string& path,
                            const GameSettingsConfig::Environment& env, bool* created)
{
    bool didCreate = false;
    if (!cfg.Load(path))
    {
        cfg.LoadDefaults(env);
        cfg.Normalize(env);
        if (!cfg.Save(path))
            return false;
        didCreate = true;
    }

    if (created)
        *created = didCreate;
    return true;
}

void LoadGameSettings()
{
    const std::string path = GameSettingsPath();
    LiveEnvironment env;

    GameSettingsConfig cfg;
    bool created = false;
    if (!EnsureGameSettingsFile(cfg, path, env, &created))
    {
        cfg.LoadDefaults(env);
        cfg.Normalize(env);
    }

    if (created)
    {
        LOG_INFO(Config, "GameSettings: created defaults at '{}'", path);
    }
    else if (cfg.Normalize(env))
        LOG_INFO(Config, "GameSettings: normalized invalid fields (not persisted)");

    // The `--lang` command-line override wins over the persisted / auto-detected
    // game.cfg language. Applied at runtime only (not written back to game.cfg).
    const std::string& cliLang = AppConfig::Instance().GetLanguage();
    if (!cliLang.empty())
    {
        const std::string normalized = CfgLib::NormalizeSupportedLanguage(cliLang);
        cfg.textLanguage = normalized;
        cfg.voiceLanguage = normalized;
    }

    ApplyToRuntime(cfg);
    LOG_DEBUG(Config, "GameSettings: text='{}' voice='{}' blood={} viewDistance={} respectMissionViewDistance={}",
              cfg.textLanguage, cfg.voiceLanguage, cfg.blood, cfg.preferredViewDistance,
              cfg.respectMissionViewDistance);
}

void SaveGameSettings()
{
    const std::string path = GameSettingsPath();
    GameSettingsConfig cfg;
    cfg.Load(path);
    cfg.textLanguage = CfgLib::NormalizeSupportedLanguage((const char*)GLanguage);
    cfg.voiceLanguage = CfgLib::NormalizeSupportedLanguage(GetSelectedVoiceLanguage(), cfg.textLanguage);
    cfg.blood = ENGINE_CONFIG.blood;
    cfg.preferredViewDistance = GetSelectedPreferredViewDistance();
    cfg.respectMissionViewDistance = GetRespectMissionViewDistance();

    if (!cfg.Save(path))
    {
        LOG_WARN(Config, "GameSettings: failed to write '{}'", path);
        return;
    }

    SaveLegacySinks(cfg);
    LOG_DEBUG(Config, "GameSettings: saved text='{}' voice='{}' blood={} viewDistance={} respectMissionViewDistance={}",
              cfg.textLanguage, cfg.voiceLanguage, cfg.blood, cfg.preferredViewDistance,
              cfg.respectMissionViewDistance);
}

std::string LoadActiveProfile()
{
    GameSettingsConfig cfg;
    if (cfg.Load(GameSettingsPath()) && !cfg.activeProfile.empty())
        return cfg.activeProfile;

    const std::string legacyProfile = LoadLegacyActiveProfile();
    if (!legacyProfile.empty())
        SaveActiveProfile(legacyProfile);
    return legacyProfile;
}

void SaveActiveProfile(const std::string& name)
{
    const std::string path = GameSettingsPath();
    LiveEnvironment env;
    GameSettingsConfig cfg;
    if (!EnsureGameSettingsFile(cfg, path, env))
    {
        LOG_WARN(Config, "GameSettings: failed to prepare '{}' for active profile", path);
        return;
    }

    cfg.activeProfile = name;
    if (!cfg.Save(path))
        LOG_WARN(Config, "GameSettings: failed to save active profile to '{}'", path);
}

const std::string& GetSelectedVoiceLanguage()
{
    return SelectedVoiceLanguageStorage();
}

void SetSelectedVoiceLanguage(const std::string& language)
{
    SelectedVoiceLanguageStorage() = CfgLib::NormalizeSupportedLanguage(language, (const char*)GLanguage);
}

float GetSelectedPreferredViewDistance()
{
    return SelectedPreferredViewDistanceStorage();
}

void SetSelectedPreferredViewDistance(float distance)
{
    SelectedPreferredViewDistanceStorage() = ClampPreferredViewDistance(distance);
    if (GScene)
        GScene->SetPreferredViewDistance(SelectedPreferredViewDistanceStorage());
}

bool GetRespectMissionViewDistance()
{
    return RespectMissionViewDistanceStorage();
}

void SetRespectMissionViewDistance(bool enabled)
{
    RespectMissionViewDistanceStorage() = enabled;
}

float ClampPreferredViewDistance(float distance)
{
    if (!std::isfinite(distance))
        return 900.0f;
    saturate(distance, GameSettingsConfig::kMinViewDistance, GameSettingsConfig::kMaxViewDistance);
    return distance;
}

float ResolveEffectiveViewDistance(float preferredViewDistance, bool respectMissionViewDistance,
                                   const std::optional<float>& missionViewDistance)
{
    // Pick mission-vs-preferred + clamp in the pure, unit-tested resolver.
    return ViewDistanceResolver::EffectiveView(preferredViewDistance, respectMissionViewDistance, missionViewDistance);
}

} // namespace Poseidon
