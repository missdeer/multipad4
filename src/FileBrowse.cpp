#include "FileCommands.h"
#include "FileDialogs.h"
#include "MainFrame.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
namespace {
std::wstring QuoteArgument(const std::wstring& value) {
    std::wstring result=L"\"";size_t slashes=0;
    for(auto c:value){if(c==L'\\'){++slashes;continue;}result.append(c==L'"'?slashes*2+1:slashes,L'\\');result+=c;slashes=0;}
    result.append(slashes*2,L'\\');result+=L'"';return result;
}
}
FolderOpenPlan BuildFolderOpenPlan(MainFrame& frame,const std::filesystem::path& input,FolderOpenRequest request) {
    FolderOpenPlan plan;
    if(!input.empty())plan.target=std::filesystem::weakly_canonical(std::filesystem::absolute(input));
    std::error_code error;bool directory=!plan.target.empty()&&std::filesystem::is_directory(plan.target,error);
    plan.selectTarget=request==FolderOpenRequest::ContainingFolder&&!plan.target.empty()&&std::filesystem::exists(plan.target,error)&&!PathIsRootW(plan.target.c_str());
    plan.folder=plan.target.empty()?std::filesystem::current_path():directory&&!plan.selectTarget?plan.target:plan.target.parent_path();
    plan.useMatepath=request==FolderOpenRequest::Browse||(request==FolderOpenRequest::OpenFolder&&frame.settings.Get(IDM_SET_OPEN_FOLDER_MATEPATH,1));
    plan.warnIfUnavailable=request==FolderOpenRequest::Browse;
    if(!plan.useMatepath)return plan;
    auto configured=frame.settings.ReadString(L"Tools",L"filebrowser.exe");
    if(configured.empty())configured=frame.settings.ReadString(L"Settings2",L"filebrowser.exe",L"matepath.exe");
    DWORD size=ExpandEnvironmentStringsW(configured.c_str(),nullptr,0);
    if(size){std::wstring expanded(size,L'\0');ExpandEnvironmentStringsW(configured.c_str(),expanded.data(),size);expanded.resize(size-1);configured=std::move(expanded);}
    auto start=configured.find_first_not_of(L" \t");if(start==std::wstring::npos)configured=L"matepath.exe",start=0;
    size_t end;std::wstring executable;
    if(configured[start]==L'"'){end=configured.find(L'"',start+1);if(end==std::wstring::npos)throw std::runtime_error("Unmatched quote in file browser command.");executable=configured.substr(start+1,end-start-1);++end;}
    else{end=configured.find_first_of(L" \t",start);executable=configured.substr(start,end==std::wstring::npos?end:end-start);}
    auto arguments=end==std::wstring::npos?std::wstring():configured.substr(end);
    std::filesystem::path program=executable;
    if(program.is_relative()){
        wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);auto local=std::filesystem::path(module).parent_path()/program;
        if(std::filesystem::is_regular_file(local,error))program=local;
        else{wchar_t found[32768]{};auto length=SearchPathW(nullptr,executable.c_str(),L".exe",32768,found,nullptr);if(length&&length<32768)program=found;}
    }
    plan.pluginAvailable=std::filesystem::is_regular_file(program,error);
    plan.command=QuoteArgument(program.wstring())+arguments;
    if(!plan.target.empty())plan.command+=L" "+QuoteArgument(plan.target.wstring());
    return plan;
}
bool OpenFolderRequest(MainFrame& frame,const std::filesystem::path& path,FolderOpenRequest request) {
    auto plan=BuildFolderOpenPlan(frame,path,request);
    if(plan.useMatepath){
        if(plan.pluginAvailable){try{ExecuteExternalCommand(frame,plan.command);return true;}catch(const std::exception&){if(plan.warnIfUnavailable)throw;}}
        if(plan.warnIfUnavailable){
            if(frame.MessageBox(L"The matepath file browser plugin could not be started. Open the upstream download page?",L"File Browser",MB_YESNO|MB_ICONINFORMATION)==IDYES)
                ShellExecuteW(frame.m_hWnd,L"open",L"https://github.com/zufuliu/notepad4/releases",nullptr,nullptr,SW_SHOWNORMAL);
            return false;
        }
    }
    std::error_code error;if(!std::filesystem::is_directory(plan.folder,error))return false;
    if(plan.selectTarget){
        auto folder=ILCreateFromPathW(plan.folder.c_str()),item=ILCreateFromPathW(plan.target.c_str());
        if(!folder||!item){if(folder)ILFree(folder);if(item)ILFree(item);throw std::runtime_error("Cannot locate file in Explorer.");}
        PCUITEMID_CHILD child=ILFindLastID(item);
        auto result=SHOpenFolderAndSelectItems(folder,1,&child,0);ILFree(item);ILFree(folder);
        if(FAILED(result))throw std::runtime_error("Cannot open containing folder.");return true;
    }
    if(reinterpret_cast<INT_PTR>(ShellExecuteW(frame.m_hWnd,L"open",plan.folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
        throw std::runtime_error("Cannot open folder.");
    return true;
}
