#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace steamDiscovery {
namespace fs=std::filesystem;
inline std::vector<std::string> tokens(std::string_view text){
    std::vector<std::string> result;
    for(size_t i=0;i<text.size();){
        const char c=text[i];
        if(c==' '||c=='\t'||c=='\r'||c=='\n'){++i;continue;}
        if(c=='/'&&i+1<text.size()&&text[i+1]=='/'){while(i<text.size()&&text[i]!='\n')++i;continue;}
        if(c=='{'||c=='}'){result.emplace_back(1,c);++i;continue;}
        std::string value;
        if(c=='"'){
            ++i;while(i<text.size()&&text[i]!='"'){
                if(text[i]=='\\'&&i+1<text.size()&&(text[i+1]=='\\'||text[i+1]=='"'))++i;
                value+=text[i++];
            }
            if(i==text.size())return {}; // Do not use a truncated manifest.
            ++i;
        }else while(i<text.size()&&text[i]!=' '&&text[i]!='\t'&&text[i]!='\r'&&text[i]!='\n'&&text[i]!='{'&&text[i]!='}')value+=text[i++];
        result.push_back(std::move(value));
    }
    return result;
}
inline std::vector<std::string> values(std::string_view text,std::string_view key){
    const auto fields=tokens(text);std::vector<std::string> result;
    for(size_t i=0;i+1<fields.size();++i)
        if(fields[i]==key&&fields[i+1]!="{"&&fields[i+1]!="}")result.push_back(fields[i+1]);
    return result;
}
inline std::string read(const fs::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
inline std::vector<fs::path> installedGames(const fs::path& steam){
    std::vector<fs::path> libraries{steam},result;
    for(const auto& path:values(read(steam/L"steamapps/libraryfolders.vdf"),"path"))libraries.push_back(fs::u8path(path));
    for(const auto& library:libraries){
        const auto dirs=values(read(library/L"steamapps/appmanifest_42700.acf"),"installdir");
        if(dirs.size()!=1)continue;
        const auto relative=fs::u8path(dirs[0]);
        bool unsafe=relative.empty()||relative.has_root_path();for(const auto& part:relative)unsafe|=part==L"..";
        if(unsafe)continue;
        const auto game=library/L"steamapps/common"/relative;
        if(!fs::is_regular_file(game/L"BlackOps.exe"))continue;
        bool found=false;for(const auto& previous:result)found|=fs::equivalent(previous,game);
        if(!found)result.push_back(game);
    }
    return result;
}
}
