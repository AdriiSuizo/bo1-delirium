#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <string>
#include <cstdio>
#include <stdexcept>
namespace fs=std::filesystem;
#include "product_info.hpp"
#ifdef BO1_DELIRIUM_INSTALLER
#include "user_interface.hpp"
#endif
#ifdef BO1_DELIRIUM_INSTALLER
static bool quietSetup;
#endif
static void fail(const char* what){
    fprintf(stderr,"%s (Windows %lu)\n",what,GetLastError());
    throw std::runtime_error(what);
}
struct Handle {
    HANDLE value;
    ~Handle(){if(value)CloseHandle(value);}
};

static bool gameIsRunning(bool plutoniumOnly=false){
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};
    if(snapshot.value==INVALID_HANDLE_VALUE){
        snapshot.value=nullptr;
        fail("check for a running BlackOps.exe");
    }
    PROCESSENTRY32W entry{};
    entry.dwSize=sizeof(entry);
    for(BOOL valid=Process32FirstW(snapshot.value,&entry);valid;valid=Process32NextW(snapshot.value,&entry)){
        if((!plutoniumOnly&&!_wcsicmp(entry.szExeFile,L"BlackOps.exe")) ||
           !_wcsicmp(entry.szExeFile,L"plutonium-bootstrapper-win32.exe") ||
           !_wcsicmp(entry.szExeFile,L"plutonium-launcher-win32.exe")) return true;
    }
    return false;
}

class TemporaryWinmm {
    fs::path source, destination;
    Handle exclusive{CreateMutexW(nullptr,TRUE,L"Local\\BO1DeliriumSteamLaunch")};
    bool installed=false;
public:
    TemporaryWinmm(const fs::path& game,const fs::path& mod)
        : source(mod/L"runtime/winmm.dll"),destination(game/L"winmm.dll"){
        if(!exclusive.value||GetLastError()==ERROR_ALREADY_EXISTS)
            fail("another BO1 Delirium launcher is running");
        // WinMM is a normal BO1 import. The proxy loads the mod only when
        // the launcher supplies the two explicit module paths.
        std::error_code error;
        installed=fs::copy_file(source,destination,fs::copy_options::none,error);
        if(!installed){
            fwprintf(stderr,L"Could not install temporary WinMM bridge at %ls: %hs\n",
                destination.c_str(),error.message().c_str());
            throw std::runtime_error("WinMM bridge installation failed");
        }
    }
    ~TemporaryWinmm(){
        if(!installed)return;
        std::error_code error;
        // A closing game can hold the bridge for a moment after its process ends.
        for(int attempt=0;attempt<40;++attempt){
            error.clear();fs::remove(destination,error);
            if(!error)return;
            Sleep(250);
        }
        fwprintf(stderr,L"Could not remove temporary WinMM bridge: %ls\n",destination.c_str());
    }
};
#include "portable_paths.hpp"
#include "mod_install.hpp"
#ifdef BO1_DELIRIUM_INSTALLER
#include "steam_discovery.hpp"
#include "lan_shortcut.hpp"
static fs::path findSteamGame(){
    wchar_t location[32768]{};DWORD bytes=sizeof(location);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,location,&bytes)!=ERROR_SUCCESS)return {};
    const auto games=steamDiscovery::installedGames(location);
    return games.size()==1?games.front():fs::path{};
}
#endif
int wmain(int argc,wchar_t**argv) try {
    wchar_t exe[MAX_PATH];GetModuleFileNameW(nullptr,exe,MAX_PATH);
    fs::path root=fs::path(exe).parent_path();
#ifdef BO1_DELIRIUM_INSTALLER
    root/=L"BO1Delirium";
#endif
    fs::path game=root.parent_path(),adapter=root/L"DeliriumBO1.dll";
    fs::path cache=environment(L"BO1_DELIRIUM_CACHE_DIR");
    if(cache.empty())cache=fs::path(environment(L"LOCALAPPDATA"))/L"BO1PlutoniumDelirium/bo1-character-v1";
    int seconds=0;bool diagnostics=false,quiet=false,uninstall=false,languageSupplied=false;std::wstring map;fs::path iso;
#ifdef BO1_DELIRIUM_INSTALLER
    ui::loadLanguage(installation::configuration().wstring());
#endif
    for(int i=1;i<argc;i++){
        if(!wcscmp(argv[i],L"--test-seconds")&&i+1<argc){seconds=_wtoi(argv[++i]);diagnostics=true;}
        else if(!wcscmp(argv[i],L"--smoke-seconds")&&i+1<argc){seconds=_wtoi(argv[++i]);diagnostics=false;}
        else if(!wcscmp(argv[i],L"--map")&&i+1<argc)map=argv[++i];
        else if(!wcscmp(argv[i],L"--game-dir")&&i+1<argc)game=fs::absolute(argv[++i]);
        else if(!wcscmp(argv[i],L"--resource-cache")&&i+1<argc)cache=fs::absolute(argv[++i]);
#ifdef BO1_DELIRIUM_INSTALLER
        else if(!wcscmp(argv[i],L"--quiet"))quiet=quietSetup=true;
        else if(!wcscmp(argv[i],L"--uninstall"))uninstall=true;
        else if(!wcscmp(argv[i],L"--iso")&&i+1<argc)iso=fs::absolute(argv[++i]);
        else if(!wcscmp(argv[i],L"--language")&&i+1<argc){if(!ui::setLanguage(argv[++i]))throw std::runtime_error("Use --language es or --language en");languageSupplied=true;}
#endif
        else fail("unknown launcher option");
    }
#ifdef BO1_DELIRIUM_INSTALLER
    if(!quiet&&!languageSupplied&&!ui::chooseLanguage())return 0;
    // Setup changes only mod files and Plutonium registration. Its read-only
    // resource capture has its own LAN profile, separate from stock Steam BO1.
    if(gameIsRunning(true))throw std::runtime_error("Close Plutonium before installing or removing Delirium");
#else
    if(gameIsRunning())throw std::runtime_error("Close Black Ops and the Plutonium launcher before starting Delirium");
#endif
    const fs::path plutonium=fs::path(environment(L"LOCALAPPDATA"))/L"Plutonium";
#ifdef BO1_DELIRIUM_INSTALLER
    const auto source=root;
    if(!fs::is_regular_file(game/L"BlackOps.exe")){
        game=findSteamGame();
        if(game.empty()){
            if(quiet)throw std::runtime_error("Steam BO1 was not found. Select BlackOps.exe in the interactive installer.");
            game=selectFile(ui::text(L"Selecciona BlackOps.exe de tu instalación de Steam",L"Select BlackOps.exe from your Steam installation"),L"BlackOps.exe\0BlackOps.exe\0\0").parent_path();
        }
    }
    root=game/L"BO1Delirium";adapter=root/L"DeliriumBO1.dll";
    if(uninstall){
        installation::uninstall(root,plutonium);
        ui::say(ui::text(L"Integración de Delirium retirada. Se conservan los recursos locales y los otros mods.",L"Delirium plugin and Mods registration removed. Local resources and other mods preserved."));
        if(!quiet)MessageBoxW(nullptr,ui::text(L"Delirium desinstalado de Plutonium.\nLa caché local y los otros mods se conservan.",L"Delirium has been removed from Plutonium.\nYour local cache and other mods have been kept."),product::title,MB_OK|MB_ICONINFORMATION);
        return 0;
    }
#endif
    if(!fs::exists(game/L"BlackOps.exe"))fail("The selected folder does not contain BlackOps.exe");
#ifndef BO1_DELIRIUM_INSTALLER
    if(!fs::exists(adapter))fail("Copy BO1Delirium into the original Steam BO1 folder first");
#endif
    if(!supportedGame(game/L"BlackOps.exe"))fail("Unsupported BlackOps.exe: requires original Steam Build 7.0.152 CL 966072");
    const auto bootstrap=plutonium/L"bin/plutonium-bootstrapper-win32.exe";
    if(!fs::is_regular_file(bootstrap)||!supportedGame(plutonium/L"games/t5sp.exe"))
        throw std::runtime_error("Install a compatible Plutonium T5 Zombies client first");
#ifdef BO1_DELIRIUM_INSTALLER
    installation::validatePayload(source);
    if(cache!=installation::localData()/L"bo1-character-v1")throw std::runtime_error("The normal installation requires the default resource cache");
    const auto& setupRoot=source;
#else
    const auto& setupRoot=root;
#endif
    TemporaryWinmm winmm(bootstrap.parent_path(),setupRoot);
    const auto tuning=cache.parent_path()/L"gta4-xbox360-v060.kv";
    fs::create_directories(cache.parent_path());
    const auto prepareDisc=[&](const fs::path& source){
        return runTool(setupRoot/L"runtime/delirium_worker.exe",
            L"--prepare-disc \""+source.wstring()+L"\" \""+tuning.wstring()+L"\"",setupRoot,cache.parent_path()/L"disc-setup.log");
    };
#ifdef BO1_DELIRIUM_INSTALLER
    // The worker validates the cached data before asking for a disc.
    const DWORD discStatus=installation::prepareDisc(game,iso,!quiet,prepareDisc,[]{
        return selectFile(ui::text(L"Selecciona tu ISO de GTA IV base de Xbox 360",L"Select your base GTA IV Xbox 360 ISO"),L"Xbox 360 ISO\0*.iso\0\0");
    });
#else
    const DWORD discStatus=prepareDisc(game);
#endif
    switch(discStatus){
    case 0:break;
    case installation::DiscImageMissing:throw std::runtime_error("No GTA IV Xbox 360 ISO was found. Run the installer again and select your ISO.");
    case 32:throw std::runtime_error("The key that opens the GTA IV disc program could not be downloaded. Connect to the Internet and run the installer again. disc-setup.log explains the offline alternative.");
    case 33:throw std::runtime_error("The ISO is not a supported base GTA IV Xbox 360 disc. Verified profiles: 4A53F9F6 and 6AC07221. Details: disc-setup.log.");
    default:throw std::runtime_error("BO1 preparation failed");
    }
#ifdef BO1_DELIRIUM_INSTALLER
    ui::say(ui::text(L"Recursos de GTA IV preparados. La ISO ya no hace falta para jugar.",L"GTA IV resources are ready. The ISO is no longer needed to play."));
#else
    puts("GTA IV tuning and get-up cache ready. You can remove the ISO.");
#endif
    fflush(stdout);
    if(!SetEnvironmentVariableW(L"BO1_DELIRIUM_GTA_CACHE",tuning.c_str())||
       !SetEnvironmentVariableW(L"BO1_DELIRIUM_PLUTONIUM_LAN",L"1")) fail("set Plutonium Delirium environment");
    prepareResources(setupRoot,game,cache,plutonium);
#ifdef BO1_DELIRIUM_INSTALLER
    installation::deployPayload(source,game);
    installation::install(root,plutonium);
    if(!WritePrivateProfileStringW(L"Delirium",L"language",ui::code(),installation::configuration().c_str()))throw std::runtime_error("Cannot save the installation language");
    lanShortcut::create(game/product::shortcut,plutonium,game);
    lanShortcut::create(lanShortcut::desktop()/product::shortcut,plutonium,game);
    ui::say(ui::text(L"Instalación completada. Abre Jugar - Play BO1 (LAN) y selecciona BO1 Delirium en Mods.",L"Installation complete. Open Jugar - Play BO1 (LAN), then select BO1 Delirium in Mods."));
    if(!quiet)MessageBoxW(nullptr,ui::text(
        L"Instalación completada.\n\nAbre Jugar - Play BO1 (LAN) desde el escritorio. En Mods, selecciona BO1 Delirium.\n\nPuedes borrar la carpeta donde extrajiste el RAR. La ISO ya no hace falta para jugar.\n\nModo comprobado: un jugador.",
        L"Installation complete.\n\nOpen Jugar - Play BO1 (LAN) from your Desktop. Select BO1 Delirium in Mods.\n\nYou can delete the extracted download folder. The ISO is no longer needed to play.\n\nVerified mode: single-player."),product::title,MB_OK|MB_ICONINFORMATION);
    return 0;
#endif
    fs::path home=cache.parent_path()/L"profile",logs=cache.parent_path()/L"logs";
    fs::create_directories(home);fs::create_directories(logs);
    const auto mod=plutonium/L"storage/t5/mods/bo1delirium";
    const auto scripts=home/L"main";
    fs::create_directories(scripts);
    // T5's RunMod changes fs_game without mounting new IWDs in this session.
    // Stage our script in this private profile, then load it only on selection.
    fs::copy_file(root/L"scripts/bo1delirium_controls.iwd",scripts/L"bo1delirium_controls.iwd",fs::copy_options::overwrite_existing);
    // T5 lists Mods from fs_homepath. Register in both the private LAN profile
    // and Plutonium's normal storage so the menu always shows the same entry.
    for(const auto& folder:{mod,home/L"mods/bo1delirium"}){
        fs::create_directories(folder);
        std::ofstream description(folder/L"description.txt",std::ios::binary);
        description<<"BO1 Delirium";
        description.close();
        if(!description)throw std::runtime_error("Could not register BO1 Delirium in the Mods menu");
    }
    puts("Select BO1 Delirium from the Plutonium Mods menu.");
    SetEnvironmentVariableW(L"BO1_DELIRIUM_CACHE_DIR",cache.c_str());
    SetEnvironmentVariableW(L"BO1_DELIRIUM_CHARACTER",(cache/L"bo1-character.json").c_str());

    SetEnvironmentVariableW(L"BO1_DELIRIUM_LOG_DIR",logs.c_str());
    SetEnvironmentVariableW(L"SteamAppId",L"42700");
    SetEnvironmentVariableW(L"BO1_STEAM_DELIRIUM_ROOT",root.c_str());
    SetEnvironmentVariableW(L"BO1_DELIRIUM_CONTINUOUS",L"0");
    SetEnvironmentVariableW(L"BO1_PHYSICS_BACKEND",L"delirium");
    SetEnvironmentVariableW(L"NM108_GETUP",L"1");
    SetEnvironmentVariableW(L"BO1_STEAM_DELIRIUM_TEST",diagnostics?L"1":nullptr);
    if(!SetEnvironmentVariableW(L"NM108_RECOVERY_CACHE",(tuning.parent_path()/L"original-getup.motion").c_str()))
        fail("set GTA IV recovery cache");
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=seconds?SW_SHOWMINNOACTIVE:SW_SHOWNORMAL;
    std::wstring args=L"\""+bootstrap.wstring()+L"\" t5sp \""+game.wstring()+L"\" -lan";
    args+=L" +set fs_homepath \""+home.wstring()+
        L"\" +set fs_basepath \""+game.wstring()+L"\" +set fs_game \"\" +set scr_useFastFileOnly 0 +set logfile 2 +set r_d3d9ex 1 +set com_introPlayed 1 +set com_startupIntroPlayed 1";
    if(seconds)args+=L" +set r_fullscreen 0 +set r_customMode 1280x720 +set com_maxfps 150 +set r_vsync 0 +set com_skipMovies 1";
    args+=diagnostics?L" +set developer 1":L" +set developer 0";
    if(!map.empty())args+=L" +set fs_game mods/bo1delirium +devmap "+map;
    if (!SetEnvironmentVariableW(L"BO1_DELIRIUM_LOCAL_ADAPTER", adapter.c_str()))
        fail("set in-process mod paths");
    const auto eventName=L"Local\\BO1DeliriumBoot-"+std::to_wstring(GetCurrentProcessId());
    Handle boot{CreateEventW(nullptr,TRUE,FALSE,eventName.c_str())};
    if(!boot.value||!SetEnvironmentVariableW(L"BO1_DELIRIUM_BOOT_EVENT",eventName.c_str()))
        fail("create mod startup event");
    PROCESS_INFORMATION pi{};
    Handle job{CreateJobObjectW(nullptr,nullptr)};
    if(!job.value)fail("owned process job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))fail("job limits");
    if(!CreateProcessW(bootstrap.c_str(),args.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,plutonium.c_str(),&si,&pi))fail("Plutonium T5 LAN launch");
    Handle gameProcess{pi.hProcess},gameThread{pi.hThread};
    if(!AssignProcessToJobObject(job.value,pi.hProcess)){
        TerminateProcess(pi.hProcess,1);
        fail("own process job assignment");
    }
    printf("Plutonium T5 LAN PID %lu, test deadline %d seconds\n",pi.dwProcessId,seconds);
    fflush(stdout);
    if(ResumeThread(pi.hThread)==DWORD(-1))fail("resume Plutonium T5");
    HANDLE startup[]={boot.value,pi.hProcess};
    if(WaitForMultipleObjects(2,startup,FALSE,30000)!=WAIT_OBJECT_0){
        TerminateJobObject(job.value,1);
        WaitForSingleObject(pi.hProcess,5000);
        fail("Black Ops exited or the mod failed to attach before initialization");
    }
    puts("Delirium loaded in-process");
    DWORD wait=WaitForSingleObject(pi.hProcess,seconds?static_cast<DWORD>(seconds)*1000:INFINITE);
    if(wait==WAIT_TIMEOUT){printf("Owned test deadline reached\n");TerminateJobObject(job.value,0);WaitForSingleObject(pi.hProcess,5000);}
    DWORD code=0;GetExitCodeProcess(pi.hProcess,&code);printf("Plutonium T5 exit 0x%08lX\n",code);
    return code?2:0;
} catch(const std::exception& error) {
    fprintf(stderr,"%s\n",error.what());
#ifdef BO1_DELIRIUM_INSTALLER
    if(!quietSetup)ui::error(error.what());
#endif
    return 1;
}
