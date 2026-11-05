#include <windows.h>
#include <shellapi.h>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "plugin_policy.hpp"
#include "mod_install.hpp"
#include "user_interface.hpp"

// Wire ABI of the public Plutonium plugin SDK v1, checked against
// plutoniummod/plutonium-sdk revision 17e9a0a. Only the lifecycle interface is
// used; game integration remains in our existing version-checked BO1 adapter.
namespace pluginAbi {
enum class Game { iw5,t4,t5,t6 };
class Plugin {
public:
    virtual ~Plugin()=default;
    virtual std::uint32_t version(){return 1;}
    virtual const char* name()=0;
    virtual bool supports(Game){return true;}
    virtual void startup(void*,Game)=0;
    virtual void shutdown()=0;
};
}
namespace {
void note(const char* message){
    try {
        std::filesystem::create_directories(installation::localData());
        if(FILE* file=_wfopen((installation::localData()/L"plugin-loader.log").c_str(),L"a")){
            fprintf(file,"%llu PID %lu %s\n",GetTickCount64(),GetCurrentProcessId(),message);fclose(file);
        }
    }catch(...){}
}
void set(const wchar_t* key,const std::filesystem::path& value){
    if(!SetEnvironmentVariableW(key,value.c_str()))throw std::runtime_error("Cannot configure Delirium runtime");
}
class Delirium final : public pluginAbi::Plugin {
public:
    const char* name() override {return "BO1 Delirium LAN";}
    bool supports(pluginAbi::Game game) override {return game==pluginAbi::Game::t5;}
    void startup(void*,pluginAbi::Game game) override {
        if(!supports(game))return;
        int count=0;auto** raw=CommandLineToArgvW(GetCommandLineW(),&count);
        if(!raw)return;
        std::vector<std::wstring> owned;for(int i=0;i<count;++i)owned.emplace_back(raw[i]);LocalFree(raw);
        std::vector<std::wstring_view> args;for(const auto& value:owned)args.emplace_back(value);
        if(!pluginPolicy::permitted(args))return;
        wchar_t inherited[32768]{};
        // The old developer launcher and the read-only first-install capture
        // already own their adapter. Never attach a second copy during either.
        if(GetEnvironmentVariableW(L"BO1_DELIRIUM_LOCAL_ADAPTER",inherited,32768))return;
        try {
            namespace fs=std::filesystem;
            ui::loadLanguage(installation::configuration().wstring());
            const auto root=installation::registeredRoot();
            if(root.empty()||!fs::equivalent(fs::path(owned[2]),root.parent_path()))return;
            const auto data=installation::localData(),cache=data/L"bo1-character-v1",logs=data/L"logs";
            if(!installation::runtimeReady(root,cache))throw std::runtime_error("Local resources are incomplete. Run Instalar - Install BO1 Delirium.exe again.");
            fs::create_directories(logs);
            set(L"BO1_STEAM_DELIRIUM_ROOT",root);
            set(L"BO1_DELIRIUM_CACHE_DIR",cache);
            set(L"BO1_DELIRIUM_CHARACTER",cache/L"bo1-character.json");
            set(L"BO1_DELIRIUM_GTA_CACHE",data/L"gta4-xbox360-v060.kv");
            set(L"NM108_RECOVERY_CACHE",data/L"original-getup.motion");
            set(L"BO1_DELIRIUM_LOG_DIR",logs);
            SetEnvironmentVariableW(L"BO1_PHYSICS_BACKEND",L"delirium");
            SetEnvironmentVariableW(L"BO1_DELIRIUM_CONTINUOUS",L"0");
            SetEnvironmentVariableW(L"NM108_GETUP",L"1");
            const auto library=LoadLibraryExW((root/L"DeliriumBO1.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            using Attach=DWORD(WINAPI*)(void*);
            const auto attach=library?reinterpret_cast<Attach>(GetProcAddress(library,"DeliriumAttach")):nullptr;
            if(!attach||attach(nullptr)!=1)throw std::runtime_error("The BO1 Delirium adapter could not attach. Check plugin-loader.log and native-runtime.log.");
            // Keep the adapter resident. Map/mod changes release its physical
            // world through the existing script-load lifecycle.
            note("Loaded by the Plutonium plugin system; select BO1 Delirium in Mods.");
        }catch(const std::exception& error){
            note(error.what());ui::error(error.what());
        }
    }
    void shutdown() override {}
};
}
extern "C" __declspec(dllexport) pluginAbi::Plugin* on_initialize(){static Delirium plugin;return &plugin;}
