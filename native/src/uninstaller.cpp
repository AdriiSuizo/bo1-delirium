#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include "mod_install.hpp"

static void requirePlutoniumClosed(){
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot check whether Plutonium is running.");
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    bool running=false;
    for(BOOL valid=Process32FirstW(snapshot,&entry);valid;valid=Process32NextW(snapshot,&entry))
        running|=!_wcsicmp(entry.szExeFile,L"plutonium-bootstrapper-win32.exe")||!_wcsicmp(entry.szExeFile,L"plutonium-launcher-win32.exe");
    CloseHandle(snapshot);
    if(running)throw std::runtime_error("Close Plutonium before uninstalling Delirium. No processes have been closed.");
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    bool quiet=false;
    try {
        int count=0;auto** arguments=CommandLineToArgvW(GetCommandLineW(),&count);
        if(!arguments)throw std::runtime_error("Cannot read uninstaller options.");
        quiet=count==2&&!wcscmp(arguments[1],L"--quiet");
        const bool valid=count==1||quiet;LocalFree(arguments);
        if(!valid)throw std::runtime_error("Unknown uninstaller option.");
        requirePlutoniumClosed();
        wchar_t executable[32768]{};
        const auto size=GetModuleFileNameW(nullptr,executable,32768);
        if(!size||size>=32768)throw std::runtime_error("Cannot locate this uninstaller.");
        const auto root=std::filesystem::path(executable).parent_path()/L"BO1Delirium";
        if(!quiet&&MessageBoxW(nullptr,
            L"Desinstalar BO1 Delirium de Plutonium?\n\nSe retiran el plugin, el script y la entrada del menu Mods.\nSe conservan tus partidas, ISO, cache local, otros mods y los archivos del paquete para poder reinstalarlo.",
            L"Desinstalar BO1 Delirium",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return 0;
        wchar_t local[32768]{};
        if(!GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768))throw std::runtime_error("LOCALAPPDATA is unavailable.");
        installation::uninstall(root,std::filesystem::path(local)/L"Plutonium");
        if(!quiet)MessageBoxW(nullptr,
            L"BO1 Delirium desinstalado de Plutonium.\n\nPara reinstalarlo, ejecuta Instalar-BO1Delirium.exe.",
            L"BO1 Delirium",MB_OK|MB_ICONINFORMATION);
        return 0;
    }catch(const std::exception& error){
        if(!quiet)MessageBoxA(nullptr,error.what(),"BO1 Delirium: uninstall failed",MB_OK|MB_ICONERROR);
        return 1;
    }
}
