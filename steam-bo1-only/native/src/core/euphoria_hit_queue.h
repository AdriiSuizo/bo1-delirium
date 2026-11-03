#pragma once
#include "euphoria_math.h"

namespace euphoria {
// POD: the host zero-initializes actor storage. Never combine different points
// or opposite directions: that would lose torque and local muscle damage.
struct PendingBullet {
    Vec3 point, impulse;
    int body, time;
    float duration, bulletSpeed;
    unsigned profile;
};
struct BulletQueue {
    enum { Capacity = 64, LifetimeMs = 500 };
    PendingBullet hits[Capacity];
    int count;

    void expire(int now) {
        int keep = 0;
        for (int i = 0; i < count; ++i)
            if (now >= hits[i].time && now - hits[i].time < LifetimeMs)
                hits[keep++] = hits[i];
        count = keep;
    }
    bool push(Vec3 point, Vec3 impulse, int body, int now, float duration=0, float bulletSpeed=450, unsigned profile=0) {
        expire(now);
        if (count == Capacity) return false;
        hits[count++] = PendingBullet{point, impulse, body, now,duration,bulletSpeed,profile};
        return true;
    }
    bool ready(int now) { expire(now); return count != 0; }
};
}
