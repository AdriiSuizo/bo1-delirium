#pragma once
#include "euphoria_math.h"
#include <algorithm>
#include <vector>
namespace euphoria {
struct BrushPlane {Vec3 normal;float distance;};
// Recover the exact compiled brush faces, including large hulls which exceed
// the runtime's small-convex limit. Emit outward CCW triangles, not a box.
inline bool BrushFaceTriangles(const float* vertices,unsigned count,const BrushPlane* planes,unsigned planeCount,std::vector<unsigned>& indices){
    indices.clear();if(!vertices||count<3||!planes||!planeCount)return false;
    for(unsigned i=0;i<count;++i)if(!finite3(v3(vertices[3*i],vertices[3*i+1],vertices[3*i+2])))return false;
    struct P {float x,y;unsigned index;};
    unsigned faces=0;
    for(unsigned face=0;face<planeCount;++face){
        auto n=planes[face].normal;float nl=length(n);if(!finite3(n)||nl<.01f||!std::isfinite(planes[face].distance))return false;
        n=n*(1.f/nl);float d=planes[face].distance/nl;bool duplicate=false;
        for(unsigned old=0;old<face;++old){float l=length(planes[old].normal);if(l>.01f&&dot(n,planes[old].normal*(1.f/l))>.99999f&&fabsf(d-planes[old].distance/l)<.025f){duplicate=true;break;}}
        if(duplicate)continue;
        Vec3 u=normalize(cross(fabsf(n.z)<.9f?v3(0,0,1):v3(0,1,0),n)),v=cross(n,u);
        std::vector<P> points;
        for(unsigned i=0;i<count;++i){auto p=v3(vertices[3*i],vertices[3*i+1],vertices[3*i+2]);if(fabsf(dot(n,p)-d)<=.025f)points.push_back({dot(p,u),dot(p,v),i});}
        if(points.size()<3)continue;
        std::sort(points.begin(),points.end(),[](P a,P b){return a.x<b.x||(a.x==b.x&&a.y<b.y);});
        points.erase(std::unique(points.begin(),points.end(),[](P a,P b){return fabsf(a.x-b.x)<.0001f&&fabsf(a.y-b.y)<.0001f;}),points.end());
        if(points.size()<3)continue;
        auto turn=[](P a,P b,P c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);};
        std::vector<P> hull;
        for(auto p:points){while(hull.size()>1&&turn(hull[hull.size()-2],hull.back(),p)<=.000001f)hull.pop_back();hull.push_back(p);}
        unsigned lower=unsigned(hull.size());
        for(int i=int(points.size())-2;i>=0;--i){auto p=points[i];while(hull.size()>lower&&turn(hull[hull.size()-2],hull.back(),p)<=.000001f)hull.pop_back();hull.push_back(p);}
        if(hull.size()<4)continue;hull.pop_back();++faces;
        for(unsigned i=1;i+1<hull.size();++i){indices.push_back(hull[0].index);indices.push_back(hull[i].index);indices.push_back(hull[i+1].index);}
    }
    return faces>=2&&!indices.empty();
}
inline bool BrushPlanarTriangles(const float* vertices,unsigned count,std::vector<unsigned>& indices){
    if(!vertices||count<3)return false;
    for(unsigned i=0;i<count;++i)if(!finite3(v3(vertices[3*i],vertices[3*i+1],vertices[3*i+2])))return false;
    Vec3 origin=v3(vertices[0],vertices[1],vertices[2]),normal=v3(0,0,0);float best=0;
    for(unsigned i=1;i<count;++i)for(unsigned j=i+1;j<count;++j){
        auto a=v3(vertices[i*3],vertices[i*3+1],vertices[i*3+2])-origin,b=v3(vertices[j*3],vertices[j*3+1],vertices[j*3+2])-origin;
        auto n=cross(a,b);float area=lengthSq(n);if(area>best){best=area;normal=n;}
    }
    if(best<1e-12f){indices={0,1,2};return true;} // The runtime already rejects zero-area triangles.
    normal=normalize(normal);float distance=dot(normal,origin);
    for(unsigned i=0;i<count;++i)if(fabsf(dot(normal,v3(vertices[i*3],vertices[i*3+1],vertices[i*3+2]))-distance)>.001f)return false;
    BrushPlane planes[]={{normal,distance},{normal*-1.f,-distance}};
    return BrushFaceTriangles(vertices,count,planes,2,indices);
}
}
