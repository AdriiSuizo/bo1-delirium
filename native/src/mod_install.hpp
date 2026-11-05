#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>
#include <windows.h>

namespace installation {
namespace fs=std::filesystem;
inline fs::path localData(){
    wchar_t path[32768]{};
    if(!GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768)||!path[0])throw std::runtime_error("LOCALAPPDATA is unavailable");
    return fs::path(path)/L"BO1PlutoniumDelirium";
}
inline fs::path configuration(){return localData()/L"installation.ini";}
inline fs::path registeredRoot(){
    wchar_t path[32768]{};
    GetPrivateProfileStringW(L"Delirium",L"root",L"",path,32768,configuration().c_str());
    return path;
}
inline void registerRoot(const fs::path& root){
    fs::create_directories(localData());
    const auto config=configuration(),temporary=fs::path(config.wstring()+L".new");
    // A Unicode INI preserves Steam library paths outside the ANSI code page.
    {std::ofstream output(temporary,std::ios::binary|std::ios::trunc);const unsigned char bom[]={0xff,0xfe};output.write(reinterpret_cast<const char*>(bom),2);if(!output)throw std::runtime_error("Cannot create installation configuration");}
    if(!WritePrivateProfileStringW(L"Delirium",L"root",root.c_str(),temporary.c_str()))
        throw std::runtime_error("Cannot write Delirium installation path");
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,temporary.c_str());
    if(!MoveFileExW(temporary.c_str(),config.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot register Delirium installation");
}
inline void publishFile(const fs::path& source,const fs::path& destination,const fs::path& backup){
    if(!fs::is_regular_file(source))throw std::runtime_error("Installation payload is incomplete");
    fs::create_directories(destination.parent_path());
    if(fs::exists(destination)){
        fs::create_directories(backup);
        fs::copy_file(destination,backup/destination.filename(),fs::copy_options::none);
    }
    const auto temporary=fs::path(destination.wstring()+L".new");
    fs::copy_file(source,temporary,fs::copy_options::overwrite_existing);
    if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot install file; close Plutonium and BO1 first");
}
inline void install(const fs::path& root,const fs::path& plutonium){
    if(!fs::is_regular_file(root/L"scripts/bo1delirium_controls.iwd")||!fs::is_regular_file(root/L"runtime/BO1DeliriumPlugin.dll"))
        throw std::runtime_error("Extract the complete mod archive beside BlackOps.exe before installing");
    const auto storage=plutonium/L"storage/t5",mod=storage/L"mods/bo1delirium";
    const auto backup=localData()/L"installation-backups"/std::to_wstring(GetTickCount64());
    // Publish the loader last, after its configuration and script are ready.
    publishFile(root/L"scripts/bo1delirium_controls.iwd",storage/L"main/bo1delirium_controls.iwd",backup);
    fs::create_directories(mod);
    {std::ofstream description(mod/L"description.txt",std::ios::binary|std::ios::trunc);description<<"BO1 Delirium";if(!description)throw std::runtime_error("Cannot register the Mods menu entry");}
    registerRoot(root);
    publishFile(root/L"runtime/BO1DeliriumPlugin.dll",plutonium/L"plugins/BO1DeliriumPlugin.dll",backup);
}
inline void deployPayload(const fs::path& source,const fs::path& game){
    const auto target=game/L"BO1Delirium";
    if(fs::exists(target)&&fs::equivalent(source,target))return;
    const auto base=fs::weakly_canonical(target).wstring()+L"\\";
    if(!fs::equivalent(fs::weakly_canonical(target).parent_path(),game))
        throw std::runtime_error("BO1Delirium must be a folder inside the selected BO1 installation");
    const auto backup=localData()/L"installation-backups"/(L"payload-"+std::to_wstring(GetTickCount64()));
    for(const auto& entry:fs::recursive_directory_iterator(source)){
        if(GetFileAttributesW(entry.path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT)
            throw std::runtime_error("The mod package must contain regular files and folders");
        if(!entry.is_regular_file())continue;
        const auto relative=fs::relative(entry.path(),source),destination=target/relative;
        const auto resolved=fs::weakly_canonical(destination).wstring();
        if(resolved.size()<base.size()||CompareStringOrdinal(resolved.c_str(),static_cast<int>(base.size()),base.c_str(),static_cast<int>(base.size()),TRUE)!=CSTR_EQUAL)
            throw std::runtime_error("A mod destination points outside BO1Delirium");
        publishFile(entry.path(),destination,backup/relative.parent_path());
    }
    for(auto name:{L"Instalar - Install BO1 Delirium.exe",L"Desinstalar - Uninstall BO1 Delirium.exe",L"LEEME - Espanol.txt",L"README - English.txt",L"CREDITOS - CREDITS.txt",L"NOVEDADES - CHANGELOG.txt",L"VERSION.txt",L"Instalar-BO1Delirium.exe",L"Desinstalar-BO1Delirium.exe",L"LEEME-INSTALACION.txt",L"manifest.json"}){
        const auto original=source.parent_path()/name,destination=game/name;
        if(fs::is_regular_file(original)&&(!fs::exists(destination)||!fs::equivalent(original,destination)))publishFile(original,destination,backup);
    }
}
inline void uninstall(const fs::path& root,const fs::path& plutonium){
    const auto registered=registeredRoot();
    if(registered.empty())return;
    // Removal must still work if the user has already deleted the payload.
    // Resolve the existing prefix, then compare Windows paths case-insensitively.
    const auto owner=fs::weakly_canonical(registered).wstring(),requested=fs::weakly_canonical(root).wstring();
    if(CompareStringOrdinal(owner.c_str(),-1,requested.c_str(),-1,TRUE)!=CSTR_EQUAL)
        throw std::runtime_error("A different BO1 folder owns this Delirium installation");
    const auto storage=plutonium/L"storage/t5",mod=storage/L"mods/bo1delirium";
    fs::remove(plutonium/L"plugins/BO1DeliriumPlugin.dll");
    fs::remove(storage/L"main/bo1delirium_controls.iwd");
    fs::remove(mod/L"description.txt");
    if(fs::is_directory(mod)&&fs::is_empty(mod))fs::remove(mod);
    fs::remove(configuration());
    // Local disc resources, profiles, backups and all unrelated mods survive.
}
inline bool runtimeReady(const fs::path& root,const fs::path& cache){
    for(auto name:{L"DeliriumBO1.dll",L"runtime/nm108_native_bridge.dll",L"runtime/delirium_worker.exe"})
        if(!fs::is_regular_file(root/name))return false;
    for(auto name:{L"bo1-independent-v1.ready",L"bo1-character.json",L"bo1-getup.motion"})
        if(!fs::is_regular_file(cache/name)||!fs::file_size(cache/name))return false;
    for(auto name:{L"gta4-development-v1.ready",L"gta4-xbox360-v060.kv",L"original-getup.motion",L"original-fred.xrt",L"fred-build.txt",L"original-taskparams.txt",L"original-locomotion.motion"})
        if(!fs::is_regular_file(cache.parent_path()/name)||!fs::file_size(cache.parent_path()/name))return false;
    return true;
}
}
