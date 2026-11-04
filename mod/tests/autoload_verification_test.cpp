#include "../autoload.cpp"
#include <fstream>
#include <iostream>

static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void write(const std::filesystem::path& path,const std::string& text){std::ofstream(path,std::ios::binary)<<text;}
static std::string fileHash(const std::filesystem::path& path){
    auto file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    require(file!=INVALID_HANDLE_VALUE,"fixture read failed");auto result=sha256(file);CloseHandle(file);return result;
}
static bool denied(const std::filesystem::path& path,DWORD access){
    auto file=CreateFileW(path.c_str(),access,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_SHARING_VIOLATION;
    CloseHandle(file);return false;
}
int main(int argc,char**){
    const auto module=std::filesystem::path(moduleDirectory());
    if(argc>1){
        write(module/L"ready","1");
        for(unsigned i=0;i<1000&&!std::filesystem::exists(module/L"release");++i)Sleep(10);
        return 0;
    }
    wchar_t temporary[MAX_PATH]{};GetTempPathW(MAX_PATH,temporary);
    auto root=std::filesystem::path(temporary)/(L"lmu-helper-test-"+std::to_wstring(GetCurrentProcessId()));
    const auto record=module/L"launcher.sha256",configRecord=module/L"launcher-config.sha256";
    require(!std::filesystem::exists(record)&&!std::filesystem::exists(configRecord),"fixture hash records already exist");
    std::filesystem::create_directory(root);
    auto exe=root/L"LMU-QuadView-Launcher.exe",config=root/L"LMU-QuadView-Launcher.exe.config";
    wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);
    std::filesystem::copy_file(self,exe);write(config,"approved config");
    auto rejected=[&](){try{helper(root,L"--probe",false);return false;}catch(const std::runtime_error&){return true;}};
    try{
        bool readFailed=false;try{sha256(INVALID_HANDLE_VALUE);}catch(const std::runtime_error&){readFailed=true;}
        require(readFailed,"failed read was treated as EOF");
        require(rejected(),"missing verification records accepted");
        write(record,"invalid");write(configRecord,fileHash(config));
        require(rejected(),"malformed verification record accepted");
        write(record,fileHash(exe));write(config,"tampered");
        require(rejected(),"tampered .NET config accepted");write(config,"approved config");
        write(record,std::string(64,'0'));require(rejected(),"wrong EXE hash accepted");write(record,fileHash(exe));
        auto saved=root/L"saved.config";std::filesystem::rename(config,saved);
        require(rejected(),"missing config accepted");std::filesystem::rename(saved,config);
        require(!std::filesystem::exists(root/L"ready"),"unverified child was started");
        helper(root,L"--probe",false);
        for(unsigned i=0;i<500&&!std::filesystem::exists(root/L"ready");++i)Sleep(10);
        require(std::filesystem::exists(root/L"ready"),"verified child did not start");
        require(denied(exe,GENERIC_WRITE)&&denied(config,GENERIC_WRITE)&&denied(config,DELETE),"child lifetime did not retain file locks");
        write(root/L"release","1");
        for(unsigned i=0;i<500&&denied(config,GENERIC_WRITE);++i)Sleep(10);
        require(!denied(config,GENERIC_WRITE),"config lock leaked after child exit");
        std::filesystem::remove(record);std::filesystem::remove(configRecord);std::filesystem::remove_all(root);
        std::cout<<"Autoload verification tests passed: hashes, config, read failure, inherited file locks.\n";
    }catch(const std::exception& e){write(root/L"release","1");std::cerr<<e.what()<<'\n';return 1;}
}
