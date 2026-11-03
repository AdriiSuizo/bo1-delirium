#pragma once
#include <algorithm>
#include <cmath>
// Narrow-phase geometry for host recovery clearance. Bounds alone overlap
// across empty space between diagonal limbs and can veto every get-up frame.
struct RecoveryShape {
    GuideV center,axis[3],half;float radius,length;bool box;
};
static float ShapeDot(GuideV a,GuideV b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static GuideV ShapeScale(GuideV a,float s){return {a.x*s,a.y*s,a.z*s};}
static GuideV ShapeCross(GuideV a,GuideV b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static RecoveryShape RecoveryGeometry(const float* pose,const nm108::Shape& s){
    auto q=MatrixQ(pose),axes=MultiplyQ(q,{s.orientation[0],s.orientation[1],s.orientation[2],s.orientation[3]});
    RecoveryShape r={};r.center=GuideAdd({pose[12],pose[13],pose[14]},GuideRotate(q,{s.offset[0],s.offset[1],s.offset[2]}));
    r.axis[0]=GuideRotate(axes,{1,0,0});r.axis[1]=GuideRotate(axes,{0,1,0});r.axis[2]=GuideRotate(axes,{0,0,1});
    r.half={s.size[0]*.5f,s.size[1]*.5f,s.size[2]*.5f};r.radius=s.radius;r.length=s.length;r.box=s.box!=0;return r;
}
static GuideV RecoveryExtent(const RecoveryShape& s){
    if(s.box)return {
        fabsf(s.axis[0].x)*s.half.x+fabsf(s.axis[1].x)*s.half.y+fabsf(s.axis[2].x)*s.half.z,
        fabsf(s.axis[0].y)*s.half.x+fabsf(s.axis[1].y)*s.half.y+fabsf(s.axis[2].y)*s.half.z,
        fabsf(s.axis[0].z)*s.half.x+fabsf(s.axis[1].z)*s.half.y+fabsf(s.axis[2].z)*s.half.z};
    return {fabsf(s.axis[1].x)*s.length*.5f+s.radius,
        fabsf(s.axis[1].y)*s.length*.5f+s.radius,fabsf(s.axis[1].z)*s.length*.5f+s.radius};
}
static float SegmentDistance(GuideV a,GuideV b,GuideV c,GuideV d){
    auto u=GuideSub(b,a),v=GuideSub(d,c),w=GuideSub(a,c);
    float aa=ShapeDot(u,u),bb=ShapeDot(u,v),cc=ShapeDot(v,v),dd=ShapeDot(u,w),ee=ShapeDot(v,w),s=0,t=0;
    auto clamp=[](float x){return (std::max)(0.f,(std::min)(1.f,x));};
    if(aa<1e-8f)t=cc>1e-8f?clamp(ee/cc):0;
    else if(cc<1e-8f)s=clamp(-dd/aa);
    else {float determinant=aa*cc-bb*bb;if(determinant>1e-8f)s=clamp((bb*ee-cc*dd)/determinant);
        t=(bb*s+ee)/cc;if(t<0){t=0;s=clamp(-dd/aa);}else if(t>1){t=1;s=clamp((bb-dd)/aa);}}
    auto delta=GuideSub(GuideAdd(w,ShapeScale(u,s)),ShapeScale(v,t));return sqrtf(ShapeDot(delta,delta));
}
static float SegmentBoxDistance(GuideV start,GuideV end,const RecoveryShape& box){
    auto a=GuideSub(start,box.center),d=GuideSub(end,start);float origin[3],direction[3],extent[]={box.half.x,box.half.y,box.half.z};
    float breaks[8]={0,1};unsigned count=2;
    for(unsigned i=0;i<3;++i){origin[i]=ShapeDot(a,box.axis[i]);direction[i]=ShapeDot(d,box.axis[i]);
        if(fabsf(direction[i])>1e-8f)for(float side:{-1.f,1.f}){float t=(side*extent[i]-origin[i])/direction[i];if(t>0&&t<1)breaks[count++]=t;}}
    std::sort(breaks,breaks+count);float best=1e9f;
    auto distance=[&](float t){float sum=0;for(unsigned i=0;i<3;++i){float e=(std::max)(0.f,fabsf(origin[i]+direction[i]*t)-extent[i]);sum+=e*e;}return sum;};
    for(unsigned n=1;n<count;++n){float mid=(breaks[n-1]+breaks[n])*.5f,aa=0,ab=0;
        for(unsigned i=0;i<3;++i){float x=origin[i]+direction[i]*mid;if(fabsf(x)<=extent[i])continue;
            aa+=direction[i]*direction[i];ab+=direction[i]*(origin[i]-(x>0?extent[i]:-extent[i]));}
        float t=aa>1e-8f?(std::max)(breaks[n-1],(std::min)(breaks[n],-ab/aa)):mid;
        best=(std::min)(best,distance(t));}
    return sqrtf(best);
}
static float ShapePenetration(const RecoveryShape& a,const RecoveryShape& b){
    if(!a.box&&!b.box){auto aa=ShapeScale(a.axis[1],a.length*.5f),bb=ShapeScale(b.axis[1],b.length*.5f);
        return a.radius+b.radius-SegmentDistance(GuideSub(a.center,aa),GuideAdd(a.center,aa),GuideSub(b.center,bb),GuideAdd(b.center,bb));}
    if(!a.box||!b.box){const auto& capsule=a.box?b:a;const auto& box=a.box?a:b;auto e=ShapeScale(capsule.axis[1],capsule.length*.5f);
        return capsule.radius-SegmentBoxDistance(GuideSub(capsule.center,e),GuideAdd(capsule.center,e),box);}
    float best=1e9f;auto delta=GuideSub(b.center,a.center);
    auto test=[&](GuideV axis){float len=sqrtf(ShapeDot(axis,axis));if(len<1e-6f)return;
        axis=ShapeScale(axis,1/len);float ra=0,rb=0;const float* ah=&a.half.x;const float* bh=&b.half.x;
        for(unsigned i=0;i<3;++i){ra+=ah[i]*fabsf(ShapeDot(axis,a.axis[i]));rb+=bh[i]*fabsf(ShapeDot(axis,b.axis[i]));}
        best=(std::min)(best,ra+rb-fabsf(ShapeDot(delta,axis)));};
    for(unsigned i=0;i<3;++i){test(a.axis[i]);test(b.axis[i]);for(unsigned j=0;j<3;++j)test(ShapeCross(a.axis[i],b.axis[j]));}return best;
}
