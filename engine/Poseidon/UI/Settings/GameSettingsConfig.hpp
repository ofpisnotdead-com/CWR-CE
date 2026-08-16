#pragma once

#include <string>
#include <optional>

namespace Poseidon
{
class GameSettingsConfig
{
  public:
    struct Environment
    {
        virtual ~Environment() = default;
        virtual std::string DetectSystemLanguage() const = 0;
    };

    std::string textLanguage;
    std::string voiceLanguage;
    std::string activeProfile;
    bool blood = true;
    float preferredViewDistance = 900.0f;
    bool respectMissionViewDistance = true;

    static constexpr float kMinViewDistance = 100.0f;
    static constexpr float kMaxViewDistance = 5000.0f;

    void LoadDefaults();
    void LoadDefaults(const Environment& env);
    bool Normalize();
    bool Normalize(const Environment& env);
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
};

bool EnsureGameSettingsFile(GameSettingsConfig& cfg, const std::string& path,
                            const GameSettingsConfig::Environment& env, bool* created = nullptr);
void LoadGameSettings();
void SaveGameSettings();
std::string LoadActiveProfile();
void SaveActiveProfile(const std::string& name);

const std::string& GetSelectedVoiceLanguage();
void SetSelectedVoiceLanguage(const std::string& language);
float GetSelectedPreferredViewDistance();
void SetSelectedPreferredViewDistance(float distance);
bool GetRespectMissionViewDistance();
void SetRespectMissionViewDistance(bool enabled);
float ClampPreferredViewDistance(float distance);
float ResolveEffectiveViewDistance(float preferredViewDistance, bool respectMissionViewDistance,
                                   const std::optional<float>& missionViewDistance);

} // namespace Poseidon
