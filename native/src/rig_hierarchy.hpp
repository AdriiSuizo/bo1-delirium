#pragma once
#include <cstdint>
#include <span>

namespace rig {
inline bool aliases(std::span<const uint8_t> data, std::span<short> out) {
    for (auto& parent : out) parent = -1;
    if (data.size() < 21) return false;
    for (size_t at = 20; at < data.size(); at += 2) {
        if (!data[at]) return true;
        if (at + 1 >= data.size()) return false;
        const int child = data[at] - 1, parent = int(data[at + 1]) - 1;
        if (child >= int(out.size()) || parent < 0 || parent >= child) return false;
        out[child] = short(parent);
    }
    return false;
}

struct ModelHierarchy {
    unsigned bones, roots;
    const uint8_t* parents;
    unsigned attachment;
};

// XModel child offsets are local backward references. DObj model roots attach
// to an earlier model or use 255 for the world root; duplicate bones alias an
// earlier bone. Validate the complete graph before evaluating any transforms.
inline bool hierarchy(std::span<const ModelHierarchy> models,
                      std::span<const short> duplicate, std::span<short> out) {
    if (out.empty() || duplicate.size() != out.size()) return false;
    unsigned offset = 0;
    for (const auto& m : models) {
        if (m.roots > m.bones || m.bones > out.size() - offset ||
            (m.bones > m.roots && !m.parents) ||
            (m.attachment != 255 && m.attachment >= offset)) return false;
        for (unsigned j = 0; j < m.bones; ++j) {
            const auto i = offset + j;
            int parent = -1;
            if (j < m.roots) {
                if (m.attachment != 255) parent = int(m.attachment);
            } else {
                const unsigned back = m.parents[j - m.roots];
                if (!back || back > j) return false;
                parent = int(i - back);
            }
            if (duplicate[i] >= 0) parent = duplicate[i];
            if (parent < -1 || parent >= int(i)) return false;
            out[i] = short(parent);
        }
        offset += m.bones;
    }
    return offset == out.size();
}

// BO1 SP 7.0.152, ActorScr_SetSpecies 007C1100 writes actor+8.
// Actor_IsDogSpecies 005886B0 identifies 1 and 3 as quadrupeds.
constexpr bool humanoid(int species) { return species == 0 || species == 2; }
}
