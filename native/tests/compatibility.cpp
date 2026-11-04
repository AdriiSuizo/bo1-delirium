#include "rig_hierarchy.hpp"
#include "damage_routing.hpp"
#include "plugin_policy.hpp"
#include "core/brush_faces.h"
#include <array>
#include <cstdio>
#include <cstdlib>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
    const std::array<std::wstring_view,4> lan{L"plutonium-bootstrapper-win32.exe",L"t5sp",L"C:\\Steam\\BO1",L"-lan"};
    require(pluginPolicy::permitted(lan), "normal Plutonium T5 LAN launch refused");
    auto online=lan;online[3]=L"-token";
    require(!pluginPolicy::permitted(online), "online launch enabled native adapter");
    auto mp=lan;mp[1]=L"t5mp";
    require(!pluginPolicy::permitted(mp), "T5 multiplayer enabled Zombies adapter");
    const std::array<std::wstring_view,5> dedicated{lan[0],lan[1],lan[2],lan[3],L"-dedicated"};
    require(!pluginPolicy::permitted(dedicated), "dedicated server enabled client rendering adapter");
    // A thin but non-planar brush must keep all four original boundary faces.
    const float thin[]{0,0,0, 100,0,0, 100,100,100, 0,100,100.0001220703125f};
    std::vector<unsigned> faces;
    require(euphoria::BrushTetraTriangles(thin,4,faces) && faces.size()==12, "thin tetrahedron lost its boundary");
    for (unsigned k=0;k<faces.size();k+=3) {
        auto at=[&](unsigned i){return euphoria::v3(thin[3*i],thin[3*i+1],thin[3*i+2]);};
        unsigned opposite=0;while(opposite==faces[k]||opposite==faces[k+1]||opposite==faces[k+2])++opposite;
        const auto normal=euphoria::cross(at(faces[k+1])-at(faces[k]),at(faces[k+2])-at(faces[k]));
        require(euphoria::dot(normal,at(opposite)-at(faces[k]))<=0, "brush boundary faces inward");
    }
    const float flat[]{0,0,0, 100,0,0, 100,100,0, 0,100,0};
    require(!euphoria::BrushTetraTriangles(flat,4,faces) && euphoria::BrushPlanarTriangles(flat,4,faces), "planar brush path changed");
    for (int cause : {0, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, -1, 99})
        require(bo1damage::route(cause) == bo1damage::Route::Environment, "environmental damage acquired a weapon impulse");
    for (int cause : {1, 2, 9})
        require(bo1damage::route(cause) == bo1damage::Route::Bullet, "native bullet/headshot lost weapon routing");
    for (int cause : {3, 4, 5, 6, 15})
        require(bo1damage::route(cause) == bo1damage::Route::Explosion, "native explosive lost blast routing");
    require(bo1damage::route(7) == bo1damage::Route::Melee && bo1damage::route(8) == bo1damage::Route::Melee, "knife/bayonet routing changed");
    std::array<uint8_t, 23> table{};
    table[20] = 5; table[21] = 2;
    std::array<short, 5> decoded{};
    require(rig::aliases(table, decoded) && decoded[4] == 1, "duplicate part table rejected");
    require(!rig::aliases(std::span(table).first(22), decoded), "unterminated alias table accepted");
    table[21] = 5;
    require(!rig::aliases(table, decoded), "cyclic duplicate table accepted");
    std::array<short, 5> duplicate{-1, -1, -1, -1, 1}, parents{};
    const uint8_t bodyParents[]{1, 1}, faceParents[]{1};
    std::array<rig::ModelHierarchy, 2> models{{{3, 1, bodyParents, 255}, {2, 1, faceParents, 2}}};
    require(rig::hierarchy(models, duplicate, parents), "body + attached face rejected");
    require(parents == std::array<short, 5>{-1, 0, 1, 2, 1}, "attachment/duplicate hierarchy changed");
    const std::array<rig::ModelHierarchy, 2> accessory{{{3, 1, bodyParents, 255}, {2, 2, nullptr, 1}}};
    require(rig::hierarchy(accessory, duplicate, parents), "root-only accessory needs no child parent array");
    models[1].attachment = 4;
    require(!rig::hierarchy(models, duplicate, parents), "forward root attachment accepted");
    models[1].attachment = 2;
    duplicate[4] = 4;
    require(!rig::hierarchy(models, duplicate, parents), "self-referencing duplicate accepted");
    duplicate[4] = -1;
    const uint8_t broken[]{0, 1};
    models[0].parents = broken;
    require(!rig::hierarchy(models, duplicate, parents), "self-referencing model child accepted");
    models[0].parents = bodyParents;
    models[1].bones = 1;
    require(!rig::hierarchy(models, duplicate, parents), "incomplete bone palette accepted");
    models[1].bones = 3;
    require(!rig::hierarchy(models, duplicate, parents), "oversized bone palette accepted");
    require(rig::humanoid(0) && rig::humanoid(2) && !rig::humanoid(1) && !rig::humanoid(3) && !rig::humanoid(-1), "species classification changed");
    std::puts("BO1 compatibility: hierarchy, attachments, aliases, palette bounds and species passed");
}
