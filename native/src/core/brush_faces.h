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
// Four non-coplanar vertices define a tetrahedron even when its volume is
// too thin for the runtime's small-hull tolerance. Double-precision side
// tests retain the exact supplied vertices and orient every boundary outward.
inline bool BrushTetraTriangles(const float* vertices,unsigned count,std::vector<unsigned>& indices){
    indices.clear();if(!vertices||count!=4)return false;
    for(unsigned i=0;i<12;++i)if(!std::isfinite(vertices[i]))return false;
    auto side=[&](unsigned a,unsigned b,unsigned c,unsigned opposite){
        double u[3],v[3],w[3];for(unsigned k=0;k<3;++k){
            u[k]=double(vertices[3*b+k])-vertices[3*a+k];
            v[k]=double(vertices[3*c+k])-vertices[3*a+k];
            w[k]=double(vertices[3*opposite+k])-vertices[3*a+k];
        }
        return (u[1]*v[2]-u[2]*v[1])*w[0]+(u[2]*v[0]-u[0]*v[2])*w[1]+(u[0]*v[1]-u[1]*v[0])*w[2];
    };
    if(side(0,1,2,3)==0.0)return false;
    for(unsigned opposite=0;opposite<4;++opposite){
        unsigned face[3],n=0;for(unsigned i=0;i<4;++i)if(i!=opposite)face[n++]=i;
        if(side(face[0],face[1],face[2],opposite)>0.0)std::swap(face[1],face[2]);
        indices.insert(indices.end(),face,face+3);
    }
    return true;
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
