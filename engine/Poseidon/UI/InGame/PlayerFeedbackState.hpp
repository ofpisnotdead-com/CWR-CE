#pragma once

#include <algorithm>
#include <cmath>

namespace Poseidon
{
// Presentation state only. The mission's saved combat ledger owns the kill count.
class PlayerFeedbackState
{
  public:
    void Reset()
    {
        const bool visible = _visible;
        *this = PlayerFeedbackState();
        _visible = visible;
    }

    void SetVisible(bool visible)
    {
        _visible = visible;
        _killUntil = _damageUntil = 0.0f;
    }
    bool Visible() const { return _visible; }

    void Observe(float damage, bool alive, int kills, float now, bool showKillConfirmation)
    {
        const float health = alive && std::isfinite(damage) ? std::clamp(1.0f - damage, 0.0f, 1.0f) : 0.0f;
        kills = std::max(0, kills);
        if (!_initialized || now < _lastTime || kills < _kills)
        {
            _killUntil = _damageUntil = 0.0f;
        }
        else
        {
            if (kills > _kills && showKillConfirmation && _visible)
                _killUntil = now + 2.5f;
            if (health < _health - 0.001f && _visible)
                _damageUntil = now + 0.6f;
        }
        if (!showKillConfirmation || !_visible)
            _killUntil = 0.0f;
        _health = health;
        _healthPercent = alive && health > 0.0f ? std::max(1, static_cast<int>(std::lround(health * 100))) : 0;
        _kills = kills;
        _lastTime = now;
        _initialized = true;
    }

    bool Initialized() const { return _initialized; }
    float Health() const { return _health; }
    int HealthPercent() const { return _healthPercent; }
    int Kills() const { return _kills; }
    float KillOpacity(float now) const { return std::clamp((_killUntil - now) / 0.5f, 0.0f, 1.0f); }
    float DamageOpacity(float now) const { return std::clamp((_damageUntil - now) / 0.6f, 0.0f, 1.0f); }

  private:
    bool _initialized = false;
    bool _visible = true;
    float _health = 1.0f;
    int _healthPercent = 100;
    int _kills = 0;
    float _lastTime = 0.0f;
    float _killUntil = 0.0f;
    float _damageUntil = 0.0f;
};
} // namespace Poseidon
