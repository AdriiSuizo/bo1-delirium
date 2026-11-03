#pragma once
#include <cmath>

namespace euphoria {
// BO1 gameplay adaptation, not a recovered NaturalMotion constant. Require
// 5% of remaining health in recent damage before starting a full wound task.
// Weapon impulse, actual health/damage and lethal reactions remain unchanged.
struct WoundBudget {
    float recent;
    int time;
    bool hit(int damage, int healthBefore, int now) {
        if (damage <= 0) return false;
        if (now < time) recent = 0.f;
        else recent *= std::exp2(-float(now - time) / 1000.f);
        time = now;
        if (healthBefore <= damage) { recent = 0.f; return true; }
        recent += float(damage) / float(healthBefore);
        if (recent < .05f) return false;
        recent = 0.f;
        return true;
    }
};
}
