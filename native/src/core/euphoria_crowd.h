#pragma once
#include <algorithm>
#include "euphoria_math.h"
namespace euphoria {
struct CrowdHit { float fraction,depth;Vec3 normal,point;Vec3 velocity;unsigned part; };
struct CrowdMotion { Vec3 center[16],linear[16],angular[16];float mass[16],totalMass; };
inline Vec3 crowdPointVelocity(const CrowdMotion& motion,unsigned part,Vec3 at){
    return motion.linear[part]+cross(motion.angular[part],at-motion.center[part]);
}
// The reference host records the contact's impulse, component and point
// (gta4-nm/bump.rs, 8247EAE0). Before the receiver has a dynamic body there is
// no solved pair impulse yet. Estimate the handover budget from measured
// point motion and loaded masses, then leave the real response to the solver.
inline float crowdPartMomentum(Vec3 comVelocity,Vec3 pointVelocity,Vec3 receiver,Vec3 normal,
    float totalMass,float partMass,float receiverMass){
    if(!finite3(comVelocity)||!finite3(pointVelocity)||!finite3(receiver)||!finite3(normal)
        ||!std::isfinite(totalMass)||!std::isfinite(partMass)||!std::isfinite(receiverMass)
        ||totalMass<=0||partMass<=0||receiverMass<=0)return 0.f;
    const float away=(std::max)(0.f,dot(receiver,normal));
    const float whole=(std::max)(0.f,dot(comVelocity,normal)-away)/39.37f;
    const float point=(std::max)(0.f,dot(pointVelocity,normal)-away)/39.37f;
    const float coherent=(std::min)(whole,point);
    const float part=(std::min)(partMass,totalMass);
    return coherent*(totalMass*receiverMass/(totalMass+receiverMass))
        +(point-coherent)*(part*receiverMass/(part+receiverMass));
}
// Animated locomotion ignores ordinary crowd overlap and settled corpses.
// Only the incoming physical body's momentum can request a handover. Running
// into a stationary ragdoll must not manufacture an incoming impact.
inline bool crowdReactiveImpact(bool physical,bool dead,float momentum){
    return physical&&!dead&&std::isfinite(momentum)&&momentum>=35.f;
}
// Equal humanoid masses share an incoming impact. A correction step is only
// insufficient when its capture point leaves the measured support footprint.
inline bool crowdImpactUnbalances(float incomingSpeed,float height,float support){
    return incomingSpeed>0&&.5f*incomingSpeed*sqrtf((std::max)(height,.1f)/9.81f)>support;
}
// Horizontal sweep of one measured limb against another. Unlike the navigation
// cylinder this leaves the empty space beside a waist or between legs empty.
inline bool crowdPartSweep(Vec3 ownLo,Vec3 ownHi,Vec3 delta,Vec3 lo,Vec3 hi,CrowdHit& hit,bool resolveCoreOverlap=false){
    if(ownHi.z<lo.z||ownLo.z>hi.z)return false;
    Vec3 center=(ownLo+ownHi)*.5f,extent=(ownHi-ownLo)*.5f;
    float lower[2]={lo.x-extent.x,lo.y-extent.y},upper[2]={hi.x+extent.x,hi.y+extent.y};
    float enter=0,leave=1,depth=0;Vec3 normal={};bool inside=true;
    for(unsigned axis=0;axis<2;++axis){
        float p=(&center.x)[axis],d=(&delta.x)[axis];
        if(p<lower[axis]||p>upper[axis])inside=false;
        if(fabsf(d)<1e-6f){if(p<lower[axis]||p>upper[axis])return false;continue;}
        float a=(lower[axis]-p)/d,b=(upper[axis]-p)/d,sign=-1;
        if(a>b){std::swap(a,b);sign=1;}
        if(a>enter){enter=a;normal=v3(0,0,0);(&normal.x)[axis]=sign;}
        leave=(std::min)(leave,b);if(enter>leave)return false;
    }
    if(inside){
        depth=1e30f;
        for(unsigned axis=0;axis<2;++axis)for(unsigned side=0;side<2;++side){
            float p=(&center.x)[axis],d=side?upper[axis]-p:p-lower[axis];
            if(d<depth){depth=d;normal=v3(0,0,0);(&normal.x)[axis]=side?1.f:-1.f;}
        }
    }
    // Existing overlap may separate or slide; never block escape from a crowd.
    float closing=dot(delta,normal);
    if(closing>1e-5f||(closing>=-1e-5f&&!(resolveCoreOverlap&&inside&&depth>1.f)))return false;
    Vec3 p=center+delta*enter;
    hit={enter,depth,normal,v3(clampf(p.x,lo.x,hi.x),clampf(p.y,lo.y,hi.y),
        .5f*((std::max)(ownLo.z,lo.z)+(std::min)(ownHi.z,hi.z)))};
    return true;
}
inline void crowdMotionBounds(Vec3& lo,Vec3& hi,Vec3 motion){
    for(unsigned axis=0;axis<3;++axis){(&lo.x)[axis]+=(std::min)(0.f,(&motion.x)[axis]);(&hi.x)[axis]+=(std::max)(0.f,(&motion.x)[axis]);}
}
inline float crowdStopFraction(const CrowdHit& hit,Vec3 move){return (std::max)(0.f,hit.fraction-.1f/(length(move)+.001f));}
inline bool crowdMoveAccepted(Vec3 expected,Vec3 actual){
    Vec3 error=actual-expected;return finite3(actual)&&error.x*error.x+error.y*error.y<=4.f&&fabsf(error.z)<=18.f;
}
// Upright movement volume against an articulated neighbour's world bounds.
// The legacy caller can exclude ordinary step-height scenery. Physical bodies
// use foot-level clearance instead, including low settled corpses.
inline bool crowdSweep(Vec3 origin,Vec3 delta,float radius,float height,Vec3 lo,Vec3 hi,CrowdHit& hit,float stepClearance=18.f){
    if(hi.z<origin.z+stepClearance||lo.z>origin.z+height)return false;
    float lower[2]={lo.x-radius-1.f,lo.y-radius-1.f},upper[2]={hi.x+radius+1.f,hi.y+radius+1.f};
    float enter=0,leave=1;Vec3 normal={};bool inside=true;
    for(unsigned axis=0;axis<2;++axis){
        float p=(&origin.x)[axis],d=(&delta.x)[axis];if(p<lower[axis]||p>upper[axis])inside=false;
        if(fabsf(d)<1e-6f){if(p<lower[axis]||p>upper[axis])return false;continue;}
        float a=(lower[axis]-p)/d,b=(upper[axis]-p)/d;float sign=-1;
        if(a>b){float swap=a;a=b;b=swap;sign=1;}
        if(a>enter){enter=a;normal=v3(0,0,0);(&normal.x)[axis]=sign;}
        leave=(std::min)(leave,b);if(enter>leave)return false;
    }
    float depth=0;
    if(inside){float best=1e30f;for(unsigned axis=0;axis<2;++axis){
        float p=(&origin.x)[axis];for(int side=0;side<2;++side){float d=side?upper[axis]-p:p-lower[axis];
        if(d<best){best=d;normal=v3(0,0,0);(&normal.x)[axis]=side?1.f:-1.f;}}}depth=best;
        Vec3 center=v3((lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,origin.z);
        if(dot(delta,normal)>=-1e-5f&&dot(delta,center-origin)>=0){
            unsigned axis=fabsf(delta.x)>fabsf(delta.y)?0:1;
            normal=v3(0,0,0);(&normal.x)[axis]=(&delta.x)[axis]>0?-1.f:1.f;
        }
    }
    if(dot(delta,normal)>=-1e-5f)return false;
    Vec3 center=origin+delta*enter;hit={enter,depth,normal,
        v3(clampf(center.x,lo.x,hi.x),clampf(center.y,lo.y,hi.y),clampf(origin.z+35.f,lo.z,hi.z))};
    return true;
}
}
