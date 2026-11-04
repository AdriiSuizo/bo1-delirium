#pragma once
namespace bo1damage {
// BO1's meansOfDeath_t, not the weapon profile used by Delirium.
enum class Route { Environment, Bullet, Explosion, Melee };
constexpr Route route(int mod) {
    switch (mod) {
    case 1: case 2: case 9: return Route::Bullet;
    case 3: case 4: case 5: case 6: case 15: return Route::Explosion;
    case 7: case 8: return Route::Melee;
    default: return Route::Environment;
    }
}
}
