#pragma once
#include "euphoria_math.h"

namespace euphoria {
// Visual handover only: physics still decides whether the body has recovered.
// A successful return is latched; losing BALANCED alone is not a fresh fall.
inline bool animationReturnInterrupted(int flags, bool dead, bool pendingImpact) {
    return dead || pendingImpact || flags < 0 || (flags & (1 | 4 | 8)) != 0;
}
struct AnimationReturn {
    float elapsed, startWeight, weight;
    bool returning;
    void reset() { elapsed=0.f; startWeight=weight=1.f; returning=false; }
    float step(bool toAnimation, float dt) {
        dt=clampf(dt,0.f,.1f);
        if(toAnimation){
            if(!returning){elapsed=0.f;startWeight=weight;}
            elapsed+=dt;
            float t=clampf(elapsed/.35f,0.f,1.f);
            weight=startWeight*(1.f-t*t*(3.f-2.f*t));
        }else{
            // Interrupt from the visible blend, avoiding a one-frame pose jump.
            weight=clampf(weight+dt/.06f,0.f,1.f);
            elapsed=0.f;
        }
        returning=toAnimation;
        return weight;
    }
};
}
