#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include "mod_install.hpp"
#include "product_info.hpp"
#include "user_interface.hpp"

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
        ui::loadLanguage(installation::configuration().wstring());
        int count=0;auto** arguments=CommandLineToArgvW(GetCommandLineW(),&count);
        if(!arguments)throw std::runtime_error("Cannot read uninstaller options.");
        bool valid=true,languageSupplied=false;
        for(int i=1;i<count;++i){
            if(!wcscmp(arguments[i],L"--quiet"))quiet=true;
            else if(!wcscmp(arguments[i],L"--language")&&i+1<count){languageSupplied=true;valid=ui::setLanguage(arguments[++i]);}
            else valid=false;
            if(!valid)break;
        }
        LocalFree(arguments);
        if(!valid)throw std::runtime_error("Unknown uninstaller option.");
        if(!quiet&&!languageSupplied&&!ui::chooseLanguage())return 0;
        requirePlutoniumClosed();
        wchar_t executable[32768]{};
        const auto size=GetModuleFileNameW(nullptr,executable,32768);
        if(!size||size>=32768)throw std::runtime_error("Cannot locate this uninstaller.");
        const auto root=std::filesystem::path(executable).parent_path()/L"BO1Delirium";
        if(!quiet&&MessageBoxW(nullptr,ui::text(
            L"¿Desinstalar BO1 Delirium de Plutonium?\n\nSe retirarán el plugin, el script y la entrada de Mods.\nSe conservarán tus partidas, ISO, caché local, otros mods y los archivos del paquete para reinstalarlo.",
            L"Uninstall BO1 Delirium from Plutonium?\n\nThis removes the plugin, script and Mods entry.\nYour saves, ISO, local cache, other mods and package files will be kept for reinstallation."),
            product::title,MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return 0;
        wchar_t local[32768]{};
        if(!GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768))throw std::runtime_error("LOCALAPPDATA is unavailable.");
        installation::uninstall(root,std::filesystem::path(local)/L"Plutonium");
        if(!quiet)MessageBoxW(nullptr,ui::text(
            L"BO1 Delirium desinstalado de Plutonium.\n\nPara reinstalarlo, ejecuta Instalar - Install BO1 Delirium.exe.",
            L"BO1 Delirium has been removed from Plutonium.\n\nTo reinstall, run Instalar - Install BO1 Delirium.exe."),
            product::title,MB_OK|MB_ICONINFORMATION);
        return 0;
    }catch(const std::exception& error){
        if(!quiet)ui::error(error.what());
        return 1;
    }
}
