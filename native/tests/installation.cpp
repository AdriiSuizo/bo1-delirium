#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "mod_install.hpp"
namespace fs=std::filesystem;
static void require(bool ok,const char* reason){if(!ok){fprintf(stderr,"%s\n",reason);std::exit(1);}}
static void write(const fs::path& file,const char* text){fs::create_directories(file.parent_path());std::ofstream output(file,std::ios::binary);output<<text;}
int wmain() try {
    wchar_t temporary[32768]{};require(GetTempPathW(32768,temporary)>0,"temporary path missing");
    const auto home=fs::path(temporary)/(L"DeliriumInstallTest-"+std::to_wstring(GetCurrentProcessId()));
    require(!fs::exists(home),"test directory must be new");
    fs::create_directories(home);
    require(SetEnvironmentVariableW(L"LOCALAPPDATA",home.c_str()),"test environment setup failed");
    const auto root=home/L"Steam \u6e2c\u8a66/Call of Duty Black Ops/BO1Delirium",plutonium=home/L"Plutonium";
    require(installation::registeredRoot().empty(),"uninstalled root unexpectedly exists");
    write(root/L"scripts/bo1delirium_controls.iwd","own scripts");
    write(root/L"runtime/BO1DeliriumPlugin.dll","own loader");
    write(plutonium/L"plugins/OtherMod.dll","unrelated");
    installation::install(root,plutonium);
    require(installation::registeredRoot()==root,"Unicode installation path did not round trip");
    require(fs::is_regular_file(plutonium/L"plugins/BO1DeliriumPlugin.dll"),"native plugin not installed");
    require(fs::is_regular_file(plutonium/L"storage/t5/mods/bo1delirium/description.txt"),"Mods menu entry missing");
    require(fs::is_regular_file(plutonium/L"storage/t5/main/bo1delirium_controls.iwd"),"control script not installed");
    write(root/L"runtime/BO1DeliriumPlugin.dll","new loader");
    installation::install(root,plutonium);
    require(fs::is_regular_file(plutonium/L"plugins/OtherMod.dll"),"unrelated plugin removed");
    bool backup=false;for(const auto& file:fs::recursive_directory_iterator(installation::localData()/L"installation-backups"))if(file.path().filename()==L"BO1DeliriumPlugin.dll")backup=true;
    require(backup,"upgrade did not preserve previous plugin");
    require(!installation::runtimeReady(root,installation::localData()/L"bo1-character-v1"),"incomplete cache accepted");
    write(installation::localData()/L"gta4-development-v1.ready","local disc cache");
    write(plutonium/L"storage/t5/mods/bo1delirium/user-note.txt","user file");
    bool refused=false;
    try{installation::uninstall(home/L"OtherGame/BO1Delirium",plutonium);}catch(const std::exception&){refused=true;}
    require(refused&&fs::exists(plutonium/L"plugins/BO1DeliriumPlugin.dll"),"uninstaller accepted a different installation owner");
    // Simulate a partially removed portable package, not a recursive deletion.
    const auto preserved=root.parent_path()/L"preserved-payload";
    const auto prefix=fs::weakly_canonical(home).wstring()+L"\\";
    require(fs::weakly_canonical(root).wstring().starts_with(prefix)&&fs::weakly_canonical(preserved).wstring().starts_with(prefix),"unsafe partial-removal test paths");
    fs::rename(root,preserved);
    installation::uninstall(root,plutonium);
    require(!fs::exists(plutonium/L"plugins/BO1DeliriumPlugin.dll")&&installation::registeredRoot().empty(),"uninstall left native loader registered");
    require(fs::exists(plutonium/L"plugins/OtherMod.dll")&&fs::exists(installation::localData()/L"gta4-development-v1.ready"),"uninstall removed unrelated mod or local cache");
    require(fs::exists(plutonium/L"storage/t5/mods/bo1delirium/user-note.txt"),"uninstall removed user files from the mod folder");
    installation::uninstall(root,plutonium); // A second run is harmless.
    // Remove only this test's newly allocated directory after checking its root.
    require(fs::equivalent(home.parent_path(),fs::path(temporary))&&home.filename().wstring().find(L"DeliriumInstallTest-")==0,"unsafe test cleanup path");
    fs::remove_all(home);
    puts("Installation: Unicode registration, native loader, menu, upgrade backup and unrelated mod preservation passed");
}catch(const std::exception& error){fprintf(stderr,"Installation test failed: %s\n",error.what());return 1;}
