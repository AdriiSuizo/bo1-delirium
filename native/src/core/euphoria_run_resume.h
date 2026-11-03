#pragma once
#include "euphoria_math.h"
namespace euphoria {
// Host animation timing only; impulses and the physical solver are untouched.
struct RunResume {
    int began;
    float initial, seconds;
    bool active;
    void begin(int now,float duration,float carriedSpeed,float authoredSpeed){
        began=now;seconds=duration;
        initial=authoredSpeed>1.f?clampf(carriedSpeed/authoredSpeed,.15f,1.f):.15f;
        active=duration>0.f&&initial<1.f;
    }
    float sample(int now){
        if(!active)return 1.f;
        if(now<began){active=false;return 1.f;}
        float t=clampf(float(now-began)/1000.f/seconds,0.f,1.f);
        if(t>=1.f)active=false;
        return initial+(1.f-initial)*t*t*(3.f-2.f*t);
    }
    void cancel(){active=false;}
};
}
