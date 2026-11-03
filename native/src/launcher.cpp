#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <string>
#include <cstdio>
#include <stdexcept>
namespace fs=std::filesystem;
static void fail(const char* what){
    fprintf(stderr,"%s (Windows %lu)\n",what,GetLastError());
    throw std::runtime_error(what);
}
struct Handle {
    HANDLE value;
    ~Handle(){if(value)CloseHandle(value);}
};

static bool gameIsRunning(){
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};
    if(snapshot.value==INVALID_HANDLE_VALUE){
        snapshot.value=nullptr;
        fail("check for a running BlackOps.exe");
    }
    PROCESSENTRY32W entry{};
    entry.dwSize=sizeof(entry);
    for(BOOL valid=Process32FirstW(snapshot.value,&entry);valid;valid=Process32NextW(snapshot.value,&entry)){
        if(_wcsicmp(entry.szExeFile,L"BlackOps.exe")==0)return true;
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
        fs::remove(destination,error);
        if(error)fwprintf(stderr,L"Could not remove temporary WinMM bridge: %ls\n",destination.c_str());
    }
};
static BOOL CALLBACK describeWindow(HWND window,LPARAM){
    wchar_t title[2048]{},kind[128]{};DWORD_PTR result=0;
    GetClassNameW(window,kind,128);
    SendMessageTimeoutW(window,WM_GETTEXT,2048,reinterpret_cast<LPARAM>(title),SMTO_ABORTIFHUNG,100,&result);
    wprintf(L"Private test window %s: %s\n",kind,title);return TRUE;
}
static BOOL CALLBACK describeTopWindow(HWND window,LPARAM arg){
    describeWindow(window,arg);EnumChildWindows(window,describeWindow,arg);return TRUE;
}
#include "portable_paths.hpp"
int wmain(int argc,wchar_t**argv) try {
    wchar_t exe[MAX_PATH];GetModuleFileNameW(nullptr,exe,MAX_PATH);
    fs::path root=fs::path(exe).parent_path();
    fs::path game=root.parent_path(),adapter=root/L"DeliriumBO1.dll";
    fs::path cache=environment(L"BO1_DELIRIUM_CACHE_DIR");
    if(cache.empty())cache=fs::path(environment(L"LOCALAPPDATA"))/L"BO1Delirium/bo1-independent-v1";
    int seconds=0;bool diagnostics=false;std::wstring map;
    for(int i=1;i<argc;i++){
        if(!wcscmp(argv[i],L"--test-seconds")&&i+1<argc){seconds=_wtoi(argv[++i]);diagnostics=true;}
        else if(!wcscmp(argv[i],L"--smoke-seconds")&&i+1<argc){seconds=_wtoi(argv[++i]);diagnostics=false;}
        else if(!wcscmp(argv[i],L"--map")&&i+1<argc)map=argv[++i];
        else if(!wcscmp(argv[i],L"--game-dir")&&i+1<argc)game=fs::absolute(argv[++i]);
        else if(!wcscmp(argv[i],L"--resource-cache")&&i+1<argc)cache=fs::absolute(argv[++i]);
        else fail("unknown launcher option");
    }
    if(!fs::exists(game/L"BlackOps.exe")||!fs::exists(adapter))fail("Copy BO1Delirium into the original Steam BO1 folder first");
    if(!supportedGame(game/L"BlackOps.exe"))fail("Unsupported BlackOps.exe: requires original Steam Build 7.0.152 CL 966072");
    if(gameIsRunning())throw std::runtime_error("Close the running Black Ops game before launching Delirium");
    TemporaryWinmm winmm(game,root);
    prepareResources(root,game,cache);
    auto loader=prepareLoader(root,cache.parent_path());
    fs::path home=cache.parent_path()/L"profile",logs=cache.parent_path()/L"logs";
    fs::create_directories(home);fs::create_directories(logs);
    auto scripts=home/L"main";
    fs::create_directories(scripts);
    fs::copy_file(root/L"scripts/bo1delirium_controls.iwd",scripts/L"bo1delirium_controls.iwd",fs::copy_options::overwrite_existing);
    SetEnvironmentVariableW(L"BO1_DELIRIUM_CACHE_DIR",cache.c_str());
    SetEnvironmentVariableW(L"BO1_DELIRIUM_CHARACTER",(cache/L"bo1-character.json").c_str());

    SetEnvironmentVariableW(L"BO1_DELIRIUM_LOG_DIR",logs.c_str());
    SetEnvironmentVariableW(L"SteamAppId",L"42700");
    SetEnvironmentVariableW(L"BO1_STEAM_DELIRIUM_ROOT",root.c_str());
    SetEnvironmentVariableW(L"BO1_DELIRIUM_CONTINUOUS",L"0");
    SetEnvironmentVariableW(L"BO1_PHYSICS_BACKEND",L"delirium");
    SetEnvironmentVariableW(L"NM108_GETUP",L"1");
    SetEnvironmentVariableW(L"BO1_STEAM_DELIRIUM_TEST",diagnostics?L"1":nullptr);
    for(auto pair: {std::pair{L"NM108_RECOVERY_CACHE",L"bo1-getup.motion"}})
        SetEnvironmentVariableW(pair.first,(cache/pair.second).c_str());
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=seconds?SW_HIDE:SW_SHOWNORMAL;
    HDESK desktop=nullptr;std::wstring desktopName;
    if(seconds){
        desktopName=L"BO1SteamDelirium-"+std::to_wstring(GetCurrentProcessId());
        desktop=CreateDesktopW(desktopName.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
        if(!desktop)fail("private test desktop");si.lpDesktop=desktopName.data();
    }
    std::wstring args=L"\""+(game/L"BlackOps.exe").wstring()+L"\"";
    if(seconds)args+=L" allowdupe";
    args+=L" +set fs_homepath \""+home.wstring()+
        L"\" +set fs_basepath \""+game.wstring()+L"\" +set fs_game mods/bo1delirium +set scr_useFastFileOnly 0 +set logfile 2 +set r_fullscreen 0 +set r_d3d9ex 1 +set r_customMode 1280x720 +set com_maxfps 150 +set r_vsync 0 +set com_introPlayed 1 +set com_startupIntroPlayed 1";
    args+=diagnostics?L" +set developer 1":L" +set developer 0";
    if(!map.empty())args+=L" +devmap "+map;
    if (!SetEnvironmentVariableW(L"BO1_DELIRIUM_LOCAL_GAMEMOD", loader.c_str()) ||
        !SetEnvironmentVariableW(L"BO1_DELIRIUM_LOCAL_ADAPTER", adapter.c_str()))
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
    if(!CreateProcessW((game/L"BlackOps.exe").c_str(),args.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,game.c_str(),&si,&pi))fail("Steam BlackOps.exe launch");
    Handle gameProcess{pi.hProcess},gameThread{pi.hThread};
    if(!AssignProcessToJobObject(job.value,pi.hProcess)){
        TerminateProcess(pi.hProcess,1);
        fail("own process job assignment");
    }
    printf("Original Steam BlackOps.exe PID %lu, private test %d seconds\n",pi.dwProcessId,seconds);
    fflush(stdout);
    if(ResumeThread(pi.hThread)==DWORD(-1))fail("resume Steam game");
    HANDLE startup[]={boot.value,pi.hProcess};
    if(WaitForMultipleObjects(2,startup,FALSE,30000)!=WAIT_OBJECT_0){
        TerminateJobObject(job.value,1);
        WaitForSingleObject(pi.hProcess,5000);
        fail("Black Ops exited or the mod failed to attach before initialization");
    }
    puts("Delirium loaded in-process");
    DWORD wait=WaitForSingleObject(pi.hProcess,seconds?static_cast<DWORD>(seconds)*1000:INFINITE);
    if(wait==WAIT_TIMEOUT){printf("Owned test deadline reached\n");if(desktop)EnumDesktopWindows(desktop,describeTopWindow,0);TerminateJobObject(job.value,0);WaitForSingleObject(pi.hProcess,5000);}
    DWORD code=0;GetExitCodeProcess(pi.hProcess,&code);printf("Steam game exit 0x%08lX\n",code);
    if(desktop)CloseDesktop(desktop);
    return code?2:0;
} catch(const std::exception& error) {
    fprintf(stderr,"%s\n",error.what());
    return 1;
}
