#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "mod_install.hpp"
#include "steam_discovery.hpp"
#include "lan_shortcut.hpp"
#include "product_info.hpp"
#include "user_interface.hpp"
namespace fs=std::filesystem;
static void require(bool ok,const char* reason){if(!ok){fprintf(stderr,"%s\n",reason);std::exit(1);}}
static void write(const fs::path& file,const char* text){fs::create_directories(file.parent_path());std::ofstream output(file,std::ios::binary);output<<text;}
int wmain() try {
    require(ui::setLanguage(L"es")&&std::wstring(ui::text(L"Instalar",L"Install"))==L"Instalar","Spanish UI selection failed");
    require(ui::setLanguage(L"en")&&std::wstring(ui::text(L"Instalar",L"Install"))==L"Install","English UI selection failed");
    require(!ui::setLanguage(L"other"),"unsupported UI language accepted");
    wchar_t temporary[32768]{};require(GetTempPathW(32768,temporary)>0,"temporary path missing");
    const auto home=fs::path(temporary)/(L"DeliriumInstallTest-"+std::to_wstring(GetCurrentProcessId()));
    require(!fs::exists(home),"test directory must be new");
    fs::create_directories(home);
    require(SetEnvironmentVariableW(L"LOCALAPPDATA",home.c_str()),"test environment setup failed");
    const auto game=home/L"Game",iso=home/L"External Disc.iso";
    std::vector<fs::path> attempted;
    DWORD gameResult=installation::DiscImageMissing;
    auto prepare=[&](const fs::path& source){
        attempted.push_back(source);
        return source==game?gameResult:0;
    };
    unsigned prompts=0;
    auto select=[&]{++prompts;return iso;};
    require(installation::prepareDisc(game,{},true,prepare,select)==0
        && attempted==std::vector<fs::path>{game,iso} && prompts==1,
        "missing disc after cache validation did not prompt and retry");
    attempted.clear();
    require(installation::prepareDisc(game,{},false,prepare,select)==installation::DiscImageMissing
        && attempted==std::vector<fs::path>{game} && prompts==1,"quiet setup opened the disc picker");
    attempted.clear();
    require(installation::prepareDisc(game,iso,true,prepare,select)==0
        && attempted==std::vector<fs::path>{iso} && prompts==1,"explicit ISO was not used directly");
    for(DWORD status:{0u,32u,33u}){
        attempted.clear();
        gameResult=status;
        require(installation::prepareDisc(game,{},true,prepare,select)==status
            && attempted==std::vector<fs::path>{game} && prompts==1,"disc picker hid a distinct setup result");
    }
    const auto steam=home/L"Steam",library=home/L"Other Library";
    const auto utf8=library.generic_u8string();
    write(steam/L"steamapps/libraryfolders.vdf",("// library list\n\"libraryfolders\" { \"1\" { \"path\" \""+std::string(utf8.begin(),utf8.end())+"\" } }").c_str());
    write(library/L"steamapps/appmanifest_42700.acf","\"AppState\" { \"installdir\" \"Black Ops\" }");
    write(library/L"steamapps/common/Black Ops/BlackOps.exe","test file");
    require(steamDiscovery::installedGames(steam)==std::vector<fs::path>{library/L"steamapps/common/Black Ops"},"secondary Steam library discovery failed");
    write(steam/L"steamapps/appmanifest_42700.acf","\"AppState\" { \"installdir\" \"../../OtherGame\" }");
    require(steamDiscovery::installedGames(steam).size()==1,"unsafe manifest path was accepted");
    require(steamDiscovery::values("\"path\" \"C:\\\\Games\\\\Steam\"","path")==std::vector<std::string>{"C:\\Games\\Steam"},"VDF path escapes changed");
    const auto root=home/L"Steam \u6e2c\u8a66/Call of Duty Black Ops/BO1Delirium",plutonium=home/L"Plutonium";
    const auto package=home/L"Downloaded package/BO1Delirium";
    for(const auto* name:installation::RequiredPayload)write(package/name,"new payload");
    write(package/L"runtime/delirium_worker.exe","new worker");
    write(package.parent_path()/product::installer,"installer");
    write(package.parent_path()/L"README - English.txt","English guide");
    write(root/L"runtime/delirium_worker.exe","old worker");
    write(root/L"custom.txt","preserve user file");
    const auto preparer=package/L"tools/PrepareBO1.exe";
    fs::remove(preparer);
    bool incomplete=false;
    try{installation::deployPayload(package,root.parent_path());}
    catch(const std::runtime_error&){incomplete=true;}
    require(incomplete&&steamDiscovery::read(root/L"runtime/delirium_worker.exe")=="old worker"
        &&!fs::exists(root.parent_path()/product::installer),"incomplete update changed the existing installation");
    write(preparer,"preparer");
    installation::deployPayload(package,root.parent_path());
    require(steamDiscovery::read(root/L"runtime/delirium_worker.exe")=="new worker"&&steamDiscovery::read(root/L"custom.txt")=="preserve user file","deployment lost payload or user files");
    require(steamDiscovery::read(root.parent_path()/product::installer)=="installer"&&steamDiscovery::read(root.parent_path()/L"README - English.txt")=="English guide","bilingual root files were not deployed");
    write(plutonium/L"bin/plutonium-bootstrapper-win32.exe","test launcher");
    require(installation::registeredRoot().empty(),"uninstalled root unexpectedly exists");
    write(root/L"scripts/bo1delirium_controls.iwd","own scripts");
    write(root/L"runtime/BO1DeliriumPlugin.dll","own loader");
    write(plutonium/L"plugins/OtherMod.dll","unrelated");
    installation::install(root,plutonium);
    require(!installation::testControls(),"test controls must be off in a new installation");
    require(WritePrivateProfileStringW(L"Delirium",L"test_controls",L"1",installation::configuration().c_str()),"cannot enable test controls");
    installation::registerRoot(root);
    require(installation::testControls(),"reinstalling lost the test controls choice");
    require(WritePrivateProfileStringW(L"Delirium",L"language",L"es",installation::configuration().c_str()),"cannot store language");
    ui::setLanguage(L"en");ui::loadLanguage(installation::configuration().wstring());
    require(ui::language==ui::Language::Spanish,"stored language did not round trip");
    const auto shortcut=home/product::shortcut;
    lanShortcut::create(shortcut,plutonium,root.parent_path());
    require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM verification setup failed");
    IShellLinkW* link=nullptr;IPersistFile* persisted=nullptr;
    require(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))),"shortcut inspection failed");
    require(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&persisted)))&&SUCCEEDED(persisted->Load(shortcut.c_str(),STGM_READ)),"shortcut could not be loaded");
    wchar_t target[32768]{},arguments[32768]{},working[32768]{};
    link->GetPath(target,32768,nullptr,0);link->GetArguments(arguments,32768);link->GetWorkingDirectory(working,32768);
    require(fs::path(target)==plutonium/L"bin/plutonium-bootstrapper-win32.exe"&&std::wstring(arguments)==L"t5sp \""+root.parent_path().wstring()+L"\" -lan"&&fs::path(working)==plutonium,"LAN shortcut did not target the official offline launcher");
    persisted->Release();link->Release();CoUninitialize();
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
