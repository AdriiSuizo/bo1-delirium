#include "rig_hierarchy.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
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
