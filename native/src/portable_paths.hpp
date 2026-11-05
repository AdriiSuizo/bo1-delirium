#pragma once
#include <commdlg.h>
#include <algorithm>
#include "native_entries.hpp"
#include <fstream>
#include <vector>
static std::wstring environment(const wchar_t* name){
 DWORD size=GetEnvironmentVariableW(name,nullptr,0);if(!size)return {};
 std::wstring result(size,L'\0');GetEnvironmentVariableW(name,result.data(),size);result.resize(size-1);return result;
}
static fs::path selectFile(const wchar_t* title,const wchar_t* filter){
 wchar_t name[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.lpstrFile=name;
 dialog.nMaxFile=32768;dialog.lpstrTitle=title;dialog.lpstrFilter=filter;
 dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
 if(!GetOpenFileNameW(&dialog))fail("required local resource selection cancelled");return name;
}
static bool cacheReady(const fs::path& cache){
 for(auto name:{L"bo1-independent-v1.ready",L"bo1-character.json",L"bo1-getup.motion"})
  if(!fs::is_regular_file(cache/name)||fs::file_size(cache/name)==0)return false;
 return true;
}
static DWORD runTool(const fs::path& program,std::wstring args,const fs::path& root,const fs::path& log){
 SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
 HANDLE output=CreateFileW(log.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(output==INVALID_HANDLE_VALUE)fail("preparation log");
 STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
 startup.hStdOutput=output;startup.hStdError=output;startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
 PROCESS_INFORMATION process{};std::wstring command=L"\""+program.wstring()+L"\" "+args;
 if(!CreateProcessW(program.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,root.c_str(),&startup,&process)){
  CloseHandle(output);fail("BO1-only preparation tool launch");
 }
 CloseHandle(process.hThread);
 unsigned seconds=0;
 while(WaitForSingleObject(process.hProcess,1000)==WAIT_TIMEOUT){
  if(++seconds%10==0){
#ifdef BO1_DELIRIUM_INSTALLER
   wchar_t status[160]{};swprintf_s(status,ui::text(L"Preparando recursos: %u segundos...",L"Preparing resources: %u seconds..."),seconds);ui::say(status);
#else
   printf("BO1 setup: working (%u s)...\n",seconds);fflush(stdout);
#endif
  }
 }
 DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);CloseHandle(output);
 if(code)fwprintf(stderr,L"BO1 preparation tool exited with code %lu. Details: %ls\n",code,log.c_str());
 return code;
}
static void setupTool(const fs::path& program,std::wstring args,const fs::path& root,const fs::path& log){
 if(runTool(program,std::move(args),root,log))throw std::runtime_error("BO1 preparation failed");
}
static void prepareResources(const fs::path& root,const fs::path& game,const fs::path& cache,const fs::path& plutonium){
 if(cacheReady(cache))return;
#ifdef BO1_DELIRIUM_INSTALLER
 ui::say(ui::text(L"Preparando los recursos de BO1. Kino se abrirá en segundo plano y se cerrará al terminar (hasta 3 minutos).",L"Preparing BO1 resources. Kino will open in the background and close when finished (up to 3 minutes)."));
#else
 fwprintf(stdout,L"Preparing physics from your BO1 installation. Waiting for Kino resources in the background (up to 3 minutes).\n");fflush(stdout);
#endif
 fs::create_directories(cache);auto raw=cache/L"raw";
 auto log=cache/L"preparation.log";
 setupTool(root/L"tools/CaptureBO1.exe",L"--game-dir \""+game.wstring()+L"\" --resource-cache \""+raw.wstring()+L"\" --plutonium-dir \""+plutonium.wstring()+L"\" --prepare-seconds 180 --map zombie_theater",root,log);
 setupTool(root/L"tools/PrepareBO1.exe",L"--capture \""+raw.wstring()+L"\" --output \""+cache.wstring()+L"\"",root,log);
 if(!cacheReady(cache))fail("BO1-only resources incomplete");
}
static bool supportedGame(const fs::path& path){
 std::ifstream input(path,std::ios::binary);if(!input)return false;
 std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
 if(data.size()<sizeof(IMAGE_DOS_HEADER))return false;
 auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(data.data());
 if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||size_t(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS32)>data.size())return false;
 auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(data.data()+dos->e_lfanew);
 if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||nt->OptionalHeader.ImageBase!=0x400000)return false;
 const char identity[]="CoDSteam_CEG_bin\\BlackOps.pdb";
 if(std::search(data.begin(),data.end(),identity,identity+sizeof(identity)-1)==data.end())return false;
 size_t at=size_t(dos->e_lfanew)+4+sizeof(IMAGE_FILE_HEADER)+nt->FileHeader.SizeOfOptionalHeader;
 if(at+size_t(nt->FileHeader.NumberOfSections)*sizeof(IMAGE_SECTION_HEADER)>data.size())return false;
 auto* sections=reinterpret_cast<const IMAGE_SECTION_HEADER*>(data.data()+at);
 for(const auto& entry:nativeEntries){
  bool matched=false;uint32_t rva=uint32_t(entry.address-0x400000);
  for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++){
   const auto& section=sections[i];
   if(rva<section.VirtualAddress||uint64_t(rva)+8>uint64_t(section.VirtualAddress)+section.SizeOfRawData)continue;
   uint64_t offset=uint64_t(section.PointerToRawData)+(rva-section.VirtualAddress);
   if(offset+8>data.size()||memcmp(data.data()+offset,entry.bytes,8))return false;
   matched=true;break;
  }
  if(!matched)return false;
 }
 return true;
}
