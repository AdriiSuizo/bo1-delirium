#include "rig_hierarchy.hpp"
#include "../../bridge/nm108_balance_state.h"
#include "damage_routing.hpp"
#include "plugin_policy.hpp"
#include "core/brush_faces.h"
#include "core/ragdoll.h"
#include <windows.h>
#include <array>
#include <filesystem>
#include <cstdio>
#include <cstdlib>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
static void checkWorkerPaths(const wchar_t* bridge, const wchar_t* worker) {
    namespace fs = std::filesystem;
    wchar_t temp[32768]{};
    require(GetTempPathW(32768, temp) > 0, "temporary path unavailable");
    const auto directory = fs::path(temp) / (L"BO1-worker-" + std::to_wstring(GetCurrentProcessId()) + L"-\u6e2c\u8a66");
    require(fs::create_directory(directory), "worker test directory must be new");
    const auto bridgeCopy = directory / L"nm108_native_bridge.dll";
    const auto workerCopy = directory / L"delirium_worker.exe";
    fs::copy_file(bridge, bridgeCopy);
    fs::copy_file(worker, workerCopy);
    require(SetEnvironmentVariableW(L"BO1_DELIRIUM_LOG_DIR", directory.c_str()), "cannot set worker log directory");
    require(SetEnvironmentVariableW(L"BO1_PHYSICS_BACKEND", L"delirium"), "cannot select worker");
    const auto library = LoadLibraryW(bridgeCopy.c_str());
    require(library != nullptr, "bridge could not be loaded from a Unicode directory");
    const auto create = reinterpret_cast<decltype(&rd_world_create_ex)>(GetProcAddress(library, "rd_world_create_ex"));
    const auto destroy = reinterpret_cast<decltype(&rd_world_destroy)>(GetProcAddress(library, "rd_world_destroy"));
    require(create && destroy, "bridge exports missing");
    const RdWorldDesc desc{39.37007874f, RD_AXES_Z_UP};
    auto* world = create(&desc);
    if (world) destroy(world);
    FreeLibrary(library);
    const auto log = directory / (L"delirium-worker-" + std::to_wstring(GetCurrentProcessId()) + L".log");
    const bool logged = fs::is_regular_file(log) && fs::file_size(log) > 0;
    // Every file is contained in this test's newly created directory.
    for (const auto& entry : fs::directory_iterator(directory)) fs::remove(entry.path());
    fs::remove(directory);
    require(world != nullptr && logged, "runtime failed to start or write its log in a Unicode directory");
    std::puts("BO1 worker started and logged through the bridge from a Unicode directory");
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 3) {
        checkWorkerPaths(argv[1], argv[2]);
        return 0;
    }
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
    nm108::ActorState standing{};
    standing.supportMask = (1u << 3) | nm108::SolePolygon | nm108::SoleCapture | nm108::SoleCaptureBlend;
    standing.poses[0][14] = .9f;
    standing.poses[10][14] = 1.3f;
    standing.poses[12][14] = 1.5f;
    standing.poses[6][14] = .9f;
    standing.centerOfMass[0] = .15f;
    standing.centerOfMass[2] = .9f;
    require(nm108::SafeLocomotion(standing) && nm108::MaintainLocomotion(standing), "planted toe rejected during BO1 handoff");
    standing.supportMask &= ~nm108::SoleCapture;
    require(!nm108::SafeLocomotion(standing) && nm108::MaintainLocomotion(standing), "handoff lost its support hysteresis");
    standing.supportMask &= ~nm108::SoleCaptureBlend;
    require(!nm108::MaintainLocomotion(standing), "left the footprint but still returned to animation");
    standing.supportMask |= nm108::BodySupport | nm108::SoleCapture | nm108::SoleCaptureBlend;
    require(!nm108::SafeLocomotion(standing), "static footprint used when supported by a body");
    standing.supportMask = 1u << 3;
    standing.centerOfMass[0] = 0;
    require(nm108::SafeLocomotion(standing), "older worker's foot-centre handoff regressed");
    standing.recoveryActive = 1;
    require(!nm108::SafeLocomotion(standing) && !nm108::MaintainLocomotion(standing), "ongoing get-up returned to animation");
    std::puts("BO1 compatibility: hierarchy, attachments, aliases, palette bounds and species passed");
}
