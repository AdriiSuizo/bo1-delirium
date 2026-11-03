#include <windows.h>
#include <filesystem>
#include <string>
#include <cstdio>
namespace fs=std::filesystem;
static void fail(const char* what){fprintf(stderr,"%s (Windows %lu)\n",what,GetLastError());ExitProcess(1);}
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
int wmain(int argc,wchar_t**argv){
    wchar_t exe[MAX_PATH];GetModuleFileNameW(nullptr,exe,MAX_PATH);
    fs::path root=fs::path(exe).parent_path();
    fs::path game=root.parent_path(),adapter=root/L"BO1CharacterCapture.dll";
    fs::path cache=environment(L"BO1_DELIRIUM_CACHE_DIR");
    if(cache.empty())cache=fs::path(environment(L"LOCALAPPDATA"))/L"BO1Delirium/bo1-only-capture";
    int seconds=0;bool diagnostics=false,preparing=false;std::wstring map;
    for(int i=1;i<argc;i++){
        if(!wcscmp(argv[i],L"--test-seconds")&&i+1<argc){seconds=_wtoi(argv[++i]);diagnostics=true;}
        else if(!wcscmp(argv[i],L"--smoke-seconds")&&i+1<argc){seconds=_wtoi(argv[++i]);diagnostics=false;}
        else if(!wcscmp(argv[i],L"--prepare-seconds")&&i+1<argc){seconds=_wtoi(argv[++i]);diagnostics=false;preparing=true;}
        else if(!wcscmp(argv[i],L"--map")&&i+1<argc)map=argv[++i];
        else if(!wcscmp(argv[i],L"--game-dir")&&i+1<argc)game=fs::absolute(argv[++i]);
        else if(!wcscmp(argv[i],L"--resource-cache")&&i+1<argc)cache=fs::absolute(argv[++i]);
        else fail("unknown launcher option");
    }
    if(!fs::exists(game/L"BlackOps.exe")||!fs::exists(adapter))fail("Copy BO1Delirium into the original Steam BO1 folder first");
    if(!supportedGame(game/L"BlackOps.exe"))fail("Unsupported BlackOps.exe: requires original Steam Build 7.0.152 CL 966072");
    fs::create_directories(cache);
    SetEnvironmentVariableW(L"BO1_DELIRIUM_CAPTURE_DIR",cache.c_str());
    auto loader=prepareLoader(root,cache.parent_path());
    fs::path home=cache.parent_path()/L"profile",logs=cache.parent_path()/L"logs";
    fs::create_directories(home);fs::create_directories(logs);
    SetEnvironmentVariableW(L"SteamAppId",L"42700");
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=seconds?SW_HIDE:SW_SHOWNORMAL;
    HDESK desktop=nullptr;std::wstring desktopName;
    if(seconds){
        desktopName=L"BO1SteamDelirium-"+std::to_wstring(GetCurrentProcessId());
        desktop=CreateDesktopW(desktopName.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
        if(!desktop)fail("private test desktop");si.lpDesktop=desktopName.data();
    }
    std::wstring args=L"\""+(game/L"BlackOps.exe").wstring()+L"\" allowdupe +set fs_homepath \""+home.wstring()+
        L"\" +set fs_basepath \""+game.wstring()+L"\" +set logfile 2 +set r_fullscreen 0 +set r_d3d9ex 1 +set r_customMode 1280x720 +set com_maxfps 150 +set r_vsync 0 +set com_introPlayed 1 +set com_startupIntroPlayed 1";
    args+=diagnostics?L" +set developer 1":L" +set developer 0";
    if(!map.empty())args+=L" +devmap "+map;
    if (!SetEnvironmentVariableW(L"BO1_DELIRIUM_LOCAL_GAMEMOD", loader.c_str()) ||
        !SetEnvironmentVariableW(L"BO1_DELIRIUM_LOCAL_ADAPTER", adapter.c_str()))
        fail("set in-process mod paths");
    PROCESS_INFORMATION pi{};HANDLE job=CreateJobObjectW(nullptr,nullptr);if(!job)fail("owned process job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))fail("job limits");
    if(!CreateProcessW((game/L"BlackOps.exe").c_str(),args.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,game.c_str(),&si,&pi))fail("Steam BlackOps.exe launch");
    if(!AssignProcessToJobObject(job,pi.hProcess))fail("own process job assignment");
    printf("Original Steam BlackOps.exe PID %lu, private test %d seconds\n",pi.dwProcessId,seconds);
    fflush(stdout);
    ResumeThread(pi.hThread);CloseHandle(pi.hThread);
    DWORD wait;bool captured=false;
    if(preparing){
        auto deadline=GetTickCount64()+static_cast<ULONGLONG>(seconds)*1000;
        for(;;){
            wait=WaitForSingleObject(pi.hProcess,500);if(wait!=WAIT_TIMEOUT)break;
            if(fs::is_regular_file(cache/L"capture.ready")){captured=true;break;}
            if(GetTickCount64()>=deadline)break;
        }
        if(captured)printf("BO1 resource capture complete; closing the owned preparation instance\n");
        else fprintf(stderr,"BO1 capture failed: resources were not ready before the game exited or the preparation deadline\n");
    }else wait=WaitForSingleObject(pi.hProcess,seconds?static_cast<DWORD>(seconds)*1000:INFINITE);
    if(wait==WAIT_TIMEOUT){
        if(!captured)printf("Owned preparation/test deadline reached\n");
        if(desktop&&!preparing)EnumDesktopWindows(desktop,describeTopWindow,0);
        TerminateJobObject(job,0);WaitForSingleObject(pi.hProcess,5000);
    }
    DWORD code=0;GetExitCodeProcess(pi.hProcess,&code);printf("Steam game exit 0x%08lX\n",code);
    CloseHandle(pi.hProcess);CloseHandle(job);if(desktop)CloseDesktop(desktop);return preparing?(captured?0:3):(code?2:0);
}
