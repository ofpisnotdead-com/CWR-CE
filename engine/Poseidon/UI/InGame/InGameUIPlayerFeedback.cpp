#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/UI/InGame/InGameUIImpl.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/UI/Settings/GameSettingsConfig.hpp>

#include <cstdio>

using namespace Poseidon;

RString InGameUI::GetPlayerFeedbackStateText() const
{
    char text[128];
    std::snprintf(text, sizeof(text), "health=%d;kills=%d;notice=%d;healthEnabled=%d;killsEnabled=%d;visible=%d",
                  _playerFeedback.HealthPercent(), _playerFeedback.Kills(),
                  _playerFeedback.KillOpacity(Glob.uiTime.toFloat()) > 0.0f, GetShowPlayerHealth(),
                  GetShowKillConfirmation(), _playerFeedback.Visible());
    return text;
}

void InGameUI::UpdatePlayerFeedback()
{
    Person* player = GWorld->GetRealPlayer();
    if (_feedbackPlayer != player || !player)
    {
        _playerFeedback.Reset();
        _feedbackPlayer = player;
    }
    if (!player)
        return;

    int kills = 0;
    for (int i = 0; i < GStats._mission._events.Size(); ++i)
    {
        const AIStatsEvent& event = GStats._mission._events[i];
        if (event.type == SETKillsEnemyInfantry && event.killerPlayer)
            ++kills;
    }
    _playerFeedback.Observe(player->GetTotalDammage(), !player->IsDammageDestroyed(), kills, Glob.uiTime.toFloat(),
                            GetShowKillConfirmation());
}

void InGameUI::DrawPlayerFeedback()
{
    const bool healthEnabled = GetShowPlayerHealth();
    const bool killsEnabled = GetShowKillConfirmation();
    if (!_playerFeedback.Visible() || (!healthEnabled && !killsEnabled) || !_playerFeedback.Initialized() || !_font24)
        return;

    const float now = Glob.uiTime.toFloat();
    const float screenW = GEngine->Width2D();
    const float screenH = GEngine->Height2D();
    const MipInfo solid = GEngine->TextBank()->UseMipmap(GScene->Preloaded(TextureWhite), 0, 0);
    auto rectangle = [&](float x, float y, float w, float h, Color color)
    { GEngine->Draw2D(solid, PackedColor(color), Rect2DPixel(x * screenW, y * screenH, w * screenW, h * screenH)); };

    // Stay clear of weapon information (top left), compass (top centre) and squad bar.
    constexpr float x = 0.775f, y = 0.045f, w = 0.205f, pad = 0.012f, textSize = 0.024f;
    const float h = healthEnabled ? (killsEnabled ? 0.110f : 0.079f) : 0.055f;
    rectangle(x, y, w, h, Color(0.025f, 0.035f, 0.030f, 0.72f));
    const float hit = healthEnabled ? _playerFeedback.DamageOpacity(now) : 0.0f;
    if (hit > 0)
        rectangle(x, y, w, h, Color(0.65f, 0.08f, 0.04f, hit * 0.5f));

    const PackedColor textColor(Color(0.94f, 0.96f, 0.91f, 1.0f));
    float rowY = y + pad;
    if (healthEnabled)
    {
        const RString label = LocalizeStringWithFallback("STR_HUD_PLAYER_HEALTH", "HEALTH");
        GEngine->DrawText(Point2DFloat(x + pad, rowY), textSize, _font24, textColor, label);
        char percent[16];
        std::snprintf(percent, sizeof(percent), "%d%%", _playerFeedback.HealthPercent());
        const float textW = GEngine->GetTextWidth(textSize, _font24, percent);
        GEngine->DrawText(Point2DFloat(x + w - pad - textW, rowY), textSize, _font24, textColor, percent);
        const float barY = y + 0.050f, barW = w - 2 * pad;
        const float health = _playerFeedback.Health();
        const Color barColor = health > 0.5f    ? Color(0.42f, 0.76f, 0.38f, 1.0f)
                               : health > 0.25f ? Color(0.95f, 0.68f, 0.24f, 1.0f)
                                                : Color(0.92f, 0.25f, 0.19f, 1.0f);
        rectangle(x + pad, barY, barW, 0.009f, Color(0.2f, 0.23f, 0.2f, 1.0f));
        if (health > 0.0f)
            rectangle(x + pad, barY, barW * health, 0.009f, barColor);
        rowY = y + 0.073f;
    }
    if (killsEnabled)
    {
        const RString label = LocalizeStringWithFallback("STR_HUD_PLAYER_KILLS", "KILLS");
        GEngine->DrawText(Point2DFloat(x + pad, rowY), textSize, _font24, textColor, label);
        char count[24];
        std::snprintf(count, sizeof(count), "%d", _playerFeedback.Kills());
        const float textW = GEngine->GetTextWidth(textSize, _font24, count);
        GEngine->DrawText(Point2DFloat(x + w - pad - textW, rowY), textSize, _font24, textColor, count);

        const float alpha = _playerFeedback.KillOpacity(now);
        if (alpha > 0.0f)
        {
            const RString notice = LocalizeStringWithFallback("STR_HUD_KILL_CONFIRMED", "Kill confirmed");
            const float noticeW = GEngine->GetTextWidth(textSize, _font24, notice);
            rectangle(0.5f - noticeW / 2 - pad, 0.60f, noticeW + 2 * pad, 0.046f,
                      Color(0.025f, 0.035f, 0.030f, alpha * 0.75f));
            GEngine->DrawText(Point2DFloat(0.5f - noticeW / 2, 0.608f), textSize, _font24,
                              PackedColor(Color(0.8f, 0.95f, 0.65f, alpha)), notice);
        }
    }
}
