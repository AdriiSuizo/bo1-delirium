#pragma once
#include "nm108_runtime_protocol.h"
#include <cmath>
namespace nm108 {
// Preserve separate point impulses, as the reference's per-projectile path
// does. Only parallel impulses can share a weighted point without changing
// angular momentum. Never saturate a combined impulse to the per-hit limit.
inline bool MergeHit(Hit& a,const Hit& b){
    if(a.actor!=b.actor||a.part!=b.part||a.normal[3]!=b.normal[3]||
       a.impulse[3]!=b.impulse[3]||a.point[3]!=b.point[3])return false;
    float aa=0,bb=0,ab=0;
    for(unsigned k=0;k<3;++k){aa+=a.impulse[k]*a.impulse[k];bb+=b.impulse[k]*b.impulse[k];ab+=a.impulse[k]*b.impulse[k];}
    const float x=std::sqrt(aa),y=std::sqrt(bb),sum=x+y;
    if(!std::isfinite(sum)||x<=0||y<=0||sum>128.f||ab<=0)return false;
    // Float roundoff only; divergent pellets must keep their own torque arms.
    float difference=0;
    for(unsigned k=0;k<3;++k){float d=a.impulse[k]/x-b.impulse[k]/y;difference+=d*d;}
    if(difference>1.e-12f)return false;
    for(unsigned k=0;k<3;++k){a.point[k]=(a.point[k]*x+b.point[k]*y)/sum;a.impulse[k]+=b.impulse[k];}
    if(y>x)for(unsigned k=0;k<3;++k){a.bulletVelocity[k]=b.bulletVelocity[k];a.normal[k]=b.normal[k];}
    return true;
}
}
