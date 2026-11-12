#pragma once
#include "nm108_runtime_protocol.h"
#include <cmath>
#include <algorithm>
namespace nm108 {
// Dynamic body contacts and sparse world contacts retain the existing
// foot-centre test. A world footprint can decide both entry and retention.
inline bool HasSolePolygon(const ActorState& s) {
    return (s.supportMask & (SolePolygon | BodySupport)) == SolePolygon;
}
inline float SupportedFeetHeight(const ActorState& s) {
    const bool left = (s.supportMask & (1u << 3)) != 0;
    const bool right = (s.supportMask & (1u << 6)) != 0;
    if (left && right) return (s.poses[3][14] + s.poses[6][14]) * .5f;
    return s.poses[left ? 3 : 6][14];
}
// First stop asking the physical balancer to chase. It must still balance
// itself; no body transform, contact or velocity is overridden here.
inline bool CanSettleLocomotion(const ActorState& s){
    if(s.recoveryActive)return false;
    if(s.detachedMask&((1u<<2)|(1u<<3)|(1u<<5)|(1u<<6)))return false;
    if(!(s.supportMask&((1u<<3)|(1u<<6))))return false;
    const auto* p=s.poses[0];const auto* c=s.poses[10];
    float x=c[12]-p[12],y=c[13]-p[13],z=c[14]-p[14],length=std::sqrt(x*x+y*y+z*z);
    float feet=SupportedFeetHeight(s);
    return length>.01f&&z/length>.78f&&((s.detachedMask&(1u<<12))||s.poses[12][14]-feet>1.1f)&&
        std::fabs(s.velocity[2])<.7f&&s.trunkAngularSpeed<3.f;
}
// A completed recovery tolerates small foot corrections during the blend,
// but losing support, falling, spinning or moving beyond the support region
// revokes the handoff. Every new bullet also revokes it immediately.
inline bool MaintainLocomotion(const ActorState& s){
    if(s.detachedMask&((1u<<2)|(1u<<3)|(1u<<5)|(1u<<6)))return false;
    if(!CanSettleLocomotion(s)||
       std::fabs(s.velocity[2])>.45f||s.footSpeed>2.f||s.trunkAngularSpeed>1.8f)return false;
    if (HasSolePolygon(s)) return (s.supportMask & SoleCaptureBlend) != 0;
    const bool both=(s.supportMask&((1u<<3)|(1u<<6)))==((1u<<3)|(1u<<6));
    unsigned first=(s.supportMask&(1u<<3))?3:6,second=both?6:first;
    float feet=(s.poses[first][14]+s.poses[second][14])*.5f,height=s.centerOfMass[2]-feet;
    if(height<.2f)return false;
    float time=std::sqrt(height/9.81f);
    float x=s.centerOfMass[0]+s.velocity[0]*time-s.poses[first][12];
    float y=s.centerOfMass[1]+s.velocity[1]*time-s.poses[first][13];
    float dx=s.poses[second][12]-s.poses[first][12],dy=s.poses[second][13]-s.poses[first][13];
    float t=(std::max)(0.f,(std::min)(1.f,(x*dx+y*dy)/(dx*dx+dy*dy+1e-6f)));
    x-=t*dx;y-=t*dy;float radius=both?.22f:.12f;return x*x+y*y<radius*radius;
}
// Returning to locomotion requires ground support and a viable capture point,
// not merely a vertical spine while the character is still falling or spinning.
inline bool SafeLocomotion(const ActorState& s) {
    if(s.detachedMask&((1u<<2)|(1u<<3)|(1u<<5)|(1u<<6))){
        // Resume BO1's existing crawl only after the remaining torso/thighs
        // have real support and the impact has settled. Missing feet never
        // qualify as contacts and the standing get-up clip is not available.
        return !s.recoveryActive&&(s.supportMask&((1u<<0)|(1u<<1)|(1u<<4)|(1u<<7)|(1u<<10)))&&
            std::fabs(s.velocity[2])<.12f&&s.velocity[0]*s.velocity[0]+s.velocity[1]*s.velocity[1]<.09f&&s.trunkAngularSpeed<.6f;
    }
    if(s.recoveryActive || !(s.supportMask & ((1u<<3)|(1u<<6))))return false;
    // A stable planted foot, including one on a passive corpse, is real
    // support. Waiting forever for both soles froze otherwise balanced actors.
    const bool both=(s.supportMask&((1u<<3)|(1u<<6)))==((1u<<3)|(1u<<6));
    unsigned first=(s.supportMask&(1u<<3))?3:6,second=both?6:first;
    const auto* pelvis=s.poses[0];const auto* chest=s.poses[10];
    float x=chest[12]-pelvis[12],y=chest[13]-pelvis[13],z=chest[14]-pelvis[14];
    float length=std::sqrt(x*x+y*y+z*z);
    float feet=(s.poses[first][14]+s.poses[second][14])*.5f;
    // A bent spine can still be balanced (Fred's settled recovery posture
    // leans about 25 degrees). Support/capture point and motion decide safety.
    if(length<.01f || z/length<.85f || (!(s.detachedMask&(1u<<12))&&s.poses[12][14]-feet<1.25f) ||
       std::fabs(s.velocity[2])>.24f || s.footSpeed>1.1f || s.trunkAngularSpeed>1.25f)return false;
    if (HasSolePolygon(s)) return (s.supportMask & SoleCapture) != 0;
    float height=s.centerOfMass[2]-feet;if(height<.2f)return false;
    float time=std::sqrt(height/9.81f);
    float captureX=s.centerOfMass[0]+s.velocity[0]*time;
    float captureY=s.centerOfMass[1]+s.velocity[1]*time;
    float dx=s.poses[second][12]-s.poses[first][12],dy=s.poses[second][13]-s.poses[first][13];
    float t=((captureX-s.poses[first][12])*dx+(captureY-s.poses[first][13])*dy)/(dx*dx+dy*dy+1e-6f);
    t=(std::max)(0.f,(std::min)(1.f,t));
    x=captureX-s.poses[first][12]-dx*t;y=captureY-s.poses[first][13]-dy*t;
    // The feet's finite contact area surrounds their supporting segment.
    // Small settling corrections are valid; a completely motionless pose
    // would keep a breathing, balancing humanoid in physics indefinitely.
    float radius=both?.18f:.10f;return x*x+y*y<radius*radius;
}
}
