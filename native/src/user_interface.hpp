#pragma once
#include <windows.h>
#include <string>
#include <string_view>
#include <utility>
#include <stdexcept>
#include "product_info.hpp"

namespace ui {
enum class Language { Spanish,English };
inline Language language=PRIMARYLANGID(GetUserDefaultUILanguage())==LANG_SPANISH?Language::Spanish:Language::English;
inline const wchar_t* text(const wchar_t* spanish,const wchar_t* english){return language==Language::Spanish?spanish:english;}
inline const wchar_t* code(){return text(L"es",L"en");}
inline bool setLanguage(std::wstring_view value){
    if(value==L"es")language=Language::Spanish;
    else if(value==L"en")language=Language::English;
    else return false;
    return true;
}
inline void loadLanguage(const std::wstring& config){
    wchar_t value[8]{};GetPrivateProfileStringW(L"Delirium",L"language",L"",value,8,config.c_str());setLanguage(value);
}
inline INT_PTR CALLBACK languageDialog(HWND window,UINT message,WPARAM w,LPARAM){
    if(message==WM_INITDIALOG){SetWindowTextW(window,product::title);return TRUE;}
    if(message==WM_COMMAND){const auto id=LOWORD(w);if(id==1001||id==1002||id==IDCANCEL){EndDialog(window,id);return TRUE;}}
    return FALSE;
}
inline bool chooseLanguage(){
    const auto result=DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(100),nullptr,languageDialog,0);
    if(result==-1)throw std::runtime_error("The language selection window could not be opened");
    if(result==1001)language=Language::Spanish;
    else if(result==1002)language=Language::English;
    else return false;
    return true;
}
inline std::wstring wide(std::string_view value){
    UINT page=CP_UTF8;DWORD flags=MB_ERR_INVALID_CHARS;
    int size=MultiByteToWideChar(page,flags,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(!size){page=CP_ACP;flags=0;size=MultiByteToWideChar(page,flags,value.data(),static_cast<int>(value.size()),nullptr,0);}
    std::wstring result(size,L'\0');if(size)MultiByteToWideChar(page,flags,value.data(),static_cast<int>(value.size()),result.data(),size);return result;
}
inline void say(const wchar_t* message){
    const auto out=GetStdHandle(STD_OUTPUT_HANDLE);DWORD mode=0,written=0;
    const std::wstring line=std::wstring(message)+L"\r\n";
    if(GetConsoleMode(out,&mode)){WriteConsoleW(out,line.data(),static_cast<DWORD>(line.size()),&written,nullptr);return;}
    const int bytes=WideCharToMultiByte(CP_UTF8,0,line.data(),static_cast<int>(line.size()),nullptr,0,nullptr,nullptr);
    std::string utf8(bytes,'\0');WideCharToMultiByte(CP_UTF8,0,line.data(),static_cast<int>(line.size()),utf8.data(),bytes,nullptr,nullptr);WriteFile(out,utf8.data(),bytes,&written,nullptr);
}
inline void error(std::string_view detail){
    const std::pair<std::string_view,const wchar_t*> messages[]={
        {"Steam BO1 was not found. Select BlackOps.exe in the interactive installer.",L"No se encontró BO1 de Steam. Abre el instalador sin --quiet y selecciona BlackOps.exe."},
        {"The language selection window could not be opened",L"No se pudo abrir la ventana de selección de idioma."},
        {"Cannot save the installation language",L"No se pudo guardar el idioma de la instalación."},
        {"Cannot create BO1 LAN shortcut",L"No se pudo crear el acceso directo de BO1 en LAN."},
        {"Cannot locate the Desktop folder",L"No se pudo localizar la carpeta del escritorio."},
        {"Close Plutonium before installing or removing Delirium",L"Cierra Plutonium antes de instalar o desinstalar BO1 Delirium."},
        {"Close Plutonium before uninstalling Delirium. No processes have been closed.",L"Cierra Plutonium antes de desinstalar BO1 Delirium. No se ha cerrado ningún proceso."},
        {"Extract the complete mod package before installing",L"Extrae todo el contenido del paquete antes de abrir el instalador."},
        {"Install a compatible Plutonium T5 Zombies client first",L"Abre Plutonium una vez, configura BO1 Zombies y ciérralo. Después vuelve a abrir este instalador."},
        {"Unsupported BlackOps.exe: requires original Steam Build 7.0.152 CL 966072",L"Se necesita la versión de Steam de BO1: 7.0.152 CL 966072."},
        {"A different BO1 folder owns this Delirium installation",L"Esta instalación de Delirium pertenece a otra carpeta de BO1. Usa su desinstalador."},
        {"BO1 preparation failed",L"No se pudieron preparar los recursos. Consulta la guía de solución de problemas y el registro de instalación."},
        {"No GTA IV Xbox 360 ISO was found. Run the installer again and select your ISO.",L"No se encontró la ISO de GTA IV de Xbox 360. Vuelve a abrir el instalador y selecciona tu ISO."},
        {"The key that opens the GTA IV disc program could not be downloaded. Connect to the Internet and run the installer again. disc-setup.log explains the offline alternative.",L"No se pudo descargar la clave que abre el programa del disco de GTA IV. Conéctate a Internet y vuelve a abrir el instalador. disc-setup.log explica la alternativa sin conexión."},
        {"The ISO is not a supported base GTA IV Xbox 360 disc. Verified profiles: 4A53F9F6 and 6AC07221. Details: disc-setup.log.",L"La ISO no es un disco base de GTA IV de Xbox 360 compatible. Perfiles comprobados: 4A53F9F6 y 6AC07221. Detalles: disc-setup.log."},
        {"required local resource selection cancelled",L"Se canceló la selección del archivo necesario para instalar el mod."},
        {"Cannot install file; close Plutonium and BO1 first",L"No se pudo actualizar un archivo. Cierra Plutonium y BO1 y vuelve a intentarlo."},
        {"The selected folder does not contain BlackOps.exe",L"La carpeta seleccionada no contiene BlackOps.exe."},
        {"Local resources are incomplete. Run Instalar - Install BO1 Delirium.exe again.",L"Faltan recursos locales. Vuelve a ejecutar Instalar - Install BO1 Delirium.exe."},
        {"The BO1 Delirium adapter could not attach. Check plugin-loader.log and native-runtime.log.",L"No se pudo activar BO1 Delirium. Consulta plugin-loader.log y native-runtime.log."},
    };
    std::wstring message;
    if(language==Language::Spanish)for(const auto& entry:messages)if(entry.first==detail){message=entry.second;break;}
    if(message.empty())message=std::wstring(text(L"No se pudo completar la operación.\n\nDetalles:\n",L"The operation could not be completed.\n\nDetails:\n"))+wide(detail);
    MessageBoxW(nullptr,message.c_str(),product::title,MB_OK|MB_ICONERROR);
}
}
