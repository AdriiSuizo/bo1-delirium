#pragma once
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <filesystem>
#include <stdexcept>

namespace lanShortcut {
inline std::filesystem::path desktop(){
    PWSTR value=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_Desktop,0,nullptr,&value)))throw std::runtime_error("Cannot locate the Desktop folder");
    const std::filesystem::path result(value);CoTaskMemFree(value);return result;
}
inline void create(const std::filesystem::path& shortcut,const std::filesystem::path& plutonium,const std::filesystem::path& game){
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    const bool cleanup=SUCCEEDED(initialized);
    if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE)throw std::runtime_error("Cannot initialize shortcut support");
    IShellLinkW* link=nullptr;IPersistFile* file=nullptr;
    HRESULT result=CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link));
    if(SUCCEEDED(result))result=link->SetPath((plutonium/L"bin/plutonium-bootstrapper-win32.exe").c_str());
    const auto arguments=L"t5sp \""+game.wstring()+L"\" -lan";
    if(SUCCEEDED(result))result=link->SetArguments(arguments.c_str());
    if(SUCCEEDED(result))result=link->SetWorkingDirectory(plutonium.c_str());
    if(SUCCEEDED(result))result=link->SetDescription(L"BO1 de Plutonium en modo LAN. Selecciona tu mod en Mods.");
    if(SUCCEEDED(result))result=link->SetIconLocation((game/L"BlackOps.exe").c_str(),0);
    if(SUCCEEDED(result))result=link->QueryInterface(IID_PPV_ARGS(&file));
    if(SUCCEEDED(result))result=file->Save(shortcut.c_str(),TRUE);
    if(file)file->Release();if(link)link->Release();if(cleanup)CoUninitialize();
    if(FAILED(result))throw std::runtime_error("Cannot create BO1 LAN shortcut");
}
}
