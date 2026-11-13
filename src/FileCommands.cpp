#include "SimdText.h"
#include "FileCommands.h"
#include "FileDialogs.h"
#include "MainFrame.h"
#include <cwchar>
#include "Printing.h"
#include "CommandLine.h"
#include "FileVariables.h"
#include "StyleManager.h"
#include "FrameCommands.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <regex>
namespace {
bool ReloadAs(MainFrame& frame, EditorDocument& document, files::Encoding encoding, bool autoDetect=false, bool variables=true){
    if(document.path.empty())return false;
    if(document.Modified() && frame.MessageBox(L"Discard changes and reload this document?",document.Title().c_str(),MB_OKCANCEL|MB_ICONWARNING)!=IDOK)return false;

    auto position=document.Send(SCI_GETCURRENTPOS), anchor=document.Send(SCI_GETANCHOR);
    if (!LoadLargeFileForFrame(frame, document, document.path, autoDetect ? std::optional<files::Encoding>{} : encoding, variables))
        document.Load(autoDetect ? files::Read(document.path,{true,variables,encoding}) : files::ReadWithEncoding(document.path,encoding));
    RestoreFileVariables(document);
    document.ConfigureLexer();DetectDocumentIndentation(document);
    document.Send(SCI_SETSEL,(std::min)(anchor,document.Send(SCI_GETLENGTH)),(std::min)(position,document.Send(SCI_GETLENGTH)));if(variables)ApplyFileVariables(document);SnapshotDocument(document);frame.Refresh();return true;
}
void CopyText(EditorDocument& d,const std::wstring& text){auto utf8=files::ToUtf8(text);d.Send(SCI_COPYTEXT,utf8.size(),reinterpret_cast<LPARAM>(utf8.data()));}
void CreateShortcut(const std::filesystem::path& file,const std::filesystem::path& link){
    CComPtr<IShellLinkW> shortcut;HRESULT result=shortcut.CoCreateInstance(CLSID_ShellLink);
    if(SUCCEEDED(result))result=shortcut->SetPath(file.c_str());
    if(SUCCEEDED(result))result=shortcut->SetWorkingDirectory(file.parent_path().c_str());
    CComQIPtr<IPersistFile> persist(shortcut);
    if(SUCCEEDED(result) && persist)result=persist->Save(link.c_str(),TRUE);
    if(FAILED(result))throw std::runtime_error("Cannot create shortcut.");
}
}
CommandState FileCommandState(MainFrame& frame,UINT id){auto* d=frame.Active();bool path=d && !d->path.empty();
    if(id>=IDM_RECENT_HISTORY_START && id<IDM_RECENT_HISTORY_END)return {true,!frame.settings.ReadString(L"Recent Files",std::to_wstring(id-IDM_RECENT_HISTORY_START).c_str()).empty(),false};
    switch(id){
    case IDM_ENCODING_UTF8:case IDM_ENCODING_UTF8SIGN:case IDM_ENCODING_UNICODE:case IDM_ENCODING_UNICODEREV:return EditorCommandState(d,id);
    case IDM_FILE_NEWWINDOW2:case IDM_FILE_RELAUNCH_ELEVATED:case IDM_FILE_RESTART:return {true,true,false};
    case CMD_OPEN_PATH_OR_LINK:case CMD_OPEN_CONTAINING_FOLDER:case CMD_ONLINE_SEARCH_GOOGLE:case CMD_ONLINE_SEARCH_BING:case CMD_ONLINE_SEARCH_WIKI:return {true,d!=nullptr,false};
    case CMD_CUSTOM_ACTION1:case CMD_CUSTOM_ACTION2:return {true,d && !frame.settings.ReadString(L"Tools",id==CMD_CUSTOM_ACTION1?L"CustomAction1":L"CustomAction2").empty(),false};
    case IDM_FILE_LARGE_FILE_MODE:return {true,d && !(d->Send(SCI_GETDOCUMENTOPTIONS)&SC_DOCUMENTOPTION_TEXT_LARGE),d && (d->Send(SCI_GETDOCUMENTOPTIONS)&SC_DOCUMENTOPTION_TEXT_LARGE)!=0};
    case IDM_FILE_LARGE_FILE_MODE_RELOAD:return {true,path && !(d->Send(SCI_GETDOCUMENTOPTIONS)&SC_DOCUMENTOPTION_TEXT_LARGE),false};
    case IDM_ENCODING_ANSI:return {true,d && !d->Send(SCI_GETREADONLY),d && d->encoding==files::Encoding::Ansi};
    case IDM_ENCODING_SELECT:return {true,d && !d->Send(SCI_GETREADONLY),false};
    case IDM_ENCODING_SETDEFAULT:case IDM_LINEENDINGS_SETDEFAULT:case IDM_FILE_PAGESETUP:
    case IDM_FILE_RUN:case IDM_FILE_OPENFAV:case IDM_FILE_MANAGEFAV:case IDM_FILE_RECENT:case IDM_FILE_BROWSE:return {true,true,false};
    case IDM_RECODE_SELECT:case CMD_RELOADANSI:case CMD_RELOADOEM:case CMD_RELOADUTF8:case CMD_RELOADNOFILEVARS:case CMD_RECODEDEFAULT:
    case IDM_FILE_SAVEORIGINALTIMESTAMP:case IDM_FILE_UPDATE_TIMESTAMP:
    case IDM_FILE_PROPERTIES:case IDM_FILE_OPEN_CONTAINING_FOLDER:case IDM_FILE_LAUNCH:case IDM_FILE_OPENWITH:
    case IDM_FILE_CREATELINK:case IDM_FILE_ADDTOFAV:return {true,path,false};
    case IDM_FILE_READONLY_FILE:{DWORD attributes=path?GetFileAttributesW(d->path.c_str()):INVALID_FILE_ATTRIBUTES;return {true,path && attributes!=INVALID_FILE_ATTRIBUTES,path && attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_READONLY)!=0};}
    case IDM_FILE_SAVEBACKUP:return {true,d && d->Send(SCI_GETLENGTH)>0,false};
    case IDM_FILE_PRINT:case CMD_COPYFILENAME:case CMD_COPYFILENAME_NOEXT:case CMD_COPYPATHNAME:return {true,d!=nullptr,false};
    default:return {};
    }
}
bool ExecuteFileCommand(MainFrame& frame,UINT id){auto state=FileCommandState(frame,id);if(!state.supported)return false;if(!state.enabled)return true;auto* d=frame.Active();
    try{
        if(id>=IDM_RECENT_HISTORY_START && id<IDM_RECENT_HISTORY_END){frame.Open(frame.settings.ReadString(L"Recent Files",std::to_wstring(id-IDM_RECENT_HISTORY_START).c_str()));return true;}
        switch(id){
        case IDM_ENCODING_UTF8:SelectDocumentEncoding(frame,*d,files::Encoding::Utf8);break;
        case IDM_ENCODING_UTF8SIGN:SelectDocumentEncoding(frame,*d,files::Encoding::Utf8Bom);break;
        case IDM_ENCODING_UNICODE:SelectDocumentEncoding(frame,*d,files::Encoding::Utf16LE);break;
        case IDM_ENCODING_UNICODEREV:SelectDocumentEncoding(frame,*d,files::Encoding::Utf16BE);break;
        case IDM_FILE_NEWWINDOW2:frame.Relaunch(false,false,false);break;
        case IDM_FILE_RELAUNCH_ELEVATED:frame.Relaunch(true,false);break;
        case IDM_FILE_RESTART:frame.Relaunch(false,false);break;
        case CMD_OPEN_PATH_OR_LINK:case CMD_OPEN_CONTAINING_FOLDER:case CMD_ONLINE_SEARCH_GOOGLE:case CMD_ONLINE_SEARCH_BING:case CMD_ONLINE_SEARCH_WIKI:case CMD_CUSTOM_ACTION1:case CMD_CUSTOM_ACTION2:{
            auto start=d->Send(SCI_GETSELECTIONSTART),end=d->Send(SCI_GETSELECTIONEND);if(start==end){auto pos=d->Send(SCI_GETCURRENTPOS);start=d->Send(SCI_WORDSTARTPOSITION,pos,TRUE);end=d->Send(SCI_WORDENDPOSITION,pos,TRUE);}
            auto text=d->Text();
            if(d->Send(SCI_GETSELECTIONEMPTY) && (id==CMD_OPEN_PATH_OR_LINK||id==CMD_OPEN_CONTAINING_FOLDER)){
                const auto pos=static_cast<size_t>(d->Send(SCI_GETCURRENTPOS));auto left=pos,right=pos;
                while(left>0 && !strchr("\r\n\t \"'<>|",text[left-1]))--left;
                while(right<text.size() && !strchr("\r\n\t \"'<>|",text[right]))++right;
                start=static_cast<LRESULT>(left);end=static_cast<LRESULT>(right);
            }
            std::string selection=text.substr(start,end-start);auto value=files::FromUtf8(selection);if(value.empty())break;
            if(id==CMD_ONLINE_SEARCH_GOOGLE||id==CMD_ONLINE_SEARCH_BING||id==CMD_ONLINE_SEARCH_WIKI||id==CMD_CUSTOM_ACTION1||id==CMD_CUSTOM_ACTION2){std::wstring escaped;constexpr wchar_t hex[]=L"0123456789ABCDEF";for(unsigned char c:selection){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')escaped+=c;else{escaped+=L'%';escaped+=hex[c>>4];escaped+=hex[c&15];}}const wchar_t* key=id==CMD_ONLINE_SEARCH_GOOGLE?L"GoogleSearchUrl":id==CMD_ONLINE_SEARCH_BING?L"BingSearchUrl":id==CMD_ONLINE_SEARCH_WIKI?L"WikiSearchUrl":id==CMD_CUSTOM_ACTION1?L"CustomAction1":L"CustomAction2";
                auto fallback=id==CMD_ONLINE_SEARCH_GOOGLE?L"https://www.google.com/search?q=%s":id==CMD_ONLINE_SEARCH_BING?L"https://www.bing.com/search?q=%s":id==CMD_ONLINE_SEARCH_WIKI?L"https://en.wikipedia.org/wiki/Special:Search?search=%s":L"";
                auto pattern=frame.settings.ReadString(L"Tools",key,fallback);if(pattern.empty())break;size_t position=0;while((position=pattern.find(L"%s",position))!=std::wstring::npos){pattern.replace(position,2,escaped);position+=escaped.size();}ExecuteExternalCommand(frame,pattern);break;}

            else{
                while(!value.empty() && (iswspace(value.front())||value.front()==L'"'||value.front()==L'\''||value.front()==L'<'))value.erase(0,1);
                while(!value.empty() && (iswspace(value.back())||value.back()==L'"'||value.back()==L'\''||value.back()==L'>'))value.pop_back();
                if(value.starts_with(L"file://")){wchar_t path[32768]{};DWORD count=32768;if(SUCCEEDED(PathCreateFromUrlW(value.c_str(),path,&count,0)))value=path;}
                if(value.find(L"://")==std::wstring::npos){
                    std::wsmatch suffix;long long line=0,column=1;
                    if(std::regex_search(value,suffix,std::wregex(LR"(:(\d+)(?::(\d+))?$)")) && suffix.position()>1){line=std::wcstoll(suffix[1].str().c_str(),nullptr,10);if(suffix[2].matched)column=std::wcstoll(suffix[2].str().c_str(),nullptr,10);value.erase(static_cast<size_t>(suffix.position()));while(!value.empty() && (value.back()==L'"'||value.back()==L'\''))value.pop_back();}
                    auto expandedSize=ExpandEnvironmentStringsW(value.c_str(),nullptr,0);if(expandedSize){std::wstring expanded(expandedSize,L'\0');ExpandEnvironmentStringsW(value.c_str(),expanded.data(),expandedSize);expanded.resize(expandedSize-1);value=std::move(expanded);}
                    auto path=std::filesystem::path(value);if(path.is_relative())path=d->path.parent_path()/path;
                    if(id==CMD_OPEN_CONTAINING_FOLDER){OpenFolderRequest(frame,path,FolderOpenRequest::ContainingFolder);break;}
                    if(std::filesystem::is_directory(path)){OpenFolderRequest(frame,path,FolderOpenRequest::OpenFolder);break;}
                    value=path.wstring();
                    if(id==CMD_OPEN_PATH_OR_LINK && std::filesystem::is_regular_file(path)){if(frame.Open(path)&&line>0){auto* target=frame.Active();auto index=(std::clamp)(static_cast<LRESULT>(line-1),LRESULT(0),target->Send(SCI_GETLINECOUNT)-1);target->Send(SCI_GOTOPOS,target->Send(SCI_FINDCOLUMN,index,(std::max)(0LL,column-1)));target->Send(SCI_SCROLLCARET);}break;}
                }
            }

            if(reinterpret_cast<INT_PTR>(ShellExecuteW(frame.m_hWnd,L"open",value.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)throw std::runtime_error("Cannot open selected path or link.");break;
        }

        case IDM_FILE_LARGE_FILE_MODE:case IDM_FILE_LARGE_FILE_MODE_RELOAD:{
            if(id==IDM_FILE_LARGE_FILE_MODE_RELOAD && !frame.Reload(*d))break;
            if (d->LargeFile()) break;
            const auto position=d->Send(SCI_GETCURRENTPOS),anchor=d->Send(SCI_GETANCHOR),eol=d->Send(SCI_GETEOLMODE);const bool modified=d->Modified();
            const auto length = static_cast<size_t>(d->Send(SCI_GETLENGTH));
            d->LoadStream(length, SC_DOCUMENTOPTION_TEXT_LARGE | SC_DOCUMENTOPTION_STYLES_NONE, [&](const files::ChunkConsumer& consume) {
                for (size_t offset = 0; offset < length; offset += 16 * 1024 * 1024) consume(d->Range(offset, 16 * 1024 * 1024));
                return d->encoding;
            });
            d->encodingDirty=modified;d->Send(SCI_SETEOLMODE,eol);d->Send(SCI_SETSEL,anchor,position);break;
        }
        case IDM_ENCODING_ANSI:SelectDocumentEncoding(frame,*d,files::Encoding::Ansi);break;
        case IDM_ENCODING_SELECT:{auto encoding=d->encoding;if(SelectFileEncoding(frame.m_hWnd,encoding))SelectDocumentEncoding(frame,*d,encoding);break;}
        case IDM_ENCODING_SETDEFAULT:ConfigureDefaultEncoding(frame);break;
        case IDM_LINEENDINGS_SETDEFAULT:ConfigureDefaultLineEndings(frame);break;
        case IDM_RECODE_SELECT:{auto encoding=d->encoding;if(SelectFileEncoding(frame.m_hWnd,encoding))ReloadAs(frame,*d,encoding);break;}
        case CMD_RELOADANSI:ReloadAs(frame,*d,files::Encoding::Ansi);break;
        case CMD_RELOADOEM:ReloadAs(frame,*d,files::Encoding::Oem);break;
        case CMD_RELOADUTF8:ReloadAs(frame,*d,files::Encoding::Utf8);break;
        case CMD_RELOADNOFILEVARS:ReloadAs(frame,*d,d->encoding,true,false);break;
        case CMD_RECODEDEFAULT:ReloadAs(frame,*d,static_cast<files::Encoding>(frame.settings.Get(IDM_ENCODING_SETDEFAULT,static_cast<int>(files::Encoding::Ansi))));break;
        case IDM_FILE_SAVEBACKUP:SaveDocumentBackup(frame,*d);break;
        case IDM_FILE_SAVEORIGINALTIMESTAMP:{auto times=files::GetTimes(d->path);if(frame.Save(*d)){files::SetTimes(d->path,times);SnapshotDocument(*d);}break;}
        case IDM_FILE_UPDATE_TIMESTAMP:files::Touch(d->path);SnapshotDocument(*d);break;
        case IDM_FILE_READONLY_FILE:{auto attributes=GetFileAttributesW(d->path.c_str());if(!SetFileAttributesW(d->path.c_str(),attributes^FILE_ATTRIBUTE_READONLY))throw std::runtime_error("Cannot change file attributes.");d->Send(SCI_SETREADONLY,(attributes&FILE_ATTRIBUTE_READONLY)?FALSE:TRUE);break;}
        case IDM_FILE_PROPERTIES:{SHELLEXECUTEINFOW info{sizeof(info)};info.hwnd=frame.m_hWnd;info.fMask=SEE_MASK_INVOKEIDLIST;info.lpVerb=L"properties";info.lpFile=d->path.c_str();info.nShow=SW_SHOWNORMAL;if(!ShellExecuteExW(&info))throw std::runtime_error("Cannot open file properties.");break;}
        case IDM_FILE_OPEN_CONTAINING_FOLDER:OpenFolderRequest(frame,d->path,FolderOpenRequest::ContainingFolder);break;
        case IDM_FILE_LAUNCH:if(frame.settings.Get(IDM_VIEW_SAVEBEFORERUNNINGTOOLS,1) && d->Modified() && !frame.Save(*d))break;if(reinterpret_cast<INT_PTR>(ShellExecuteW(frame.m_hWnd,L"open",d->path.c_str(),nullptr,d->path.parent_path().c_str(),SW_SHOWNORMAL))<=32)throw std::runtime_error("Cannot launch this file.");break;
        case IDM_FILE_OPENWITH:OpenWithFileDialog(frame);break;
        case IDM_FILE_CREATELINK:{PWSTR desktop=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_Desktop,0,nullptr,&desktop)))throw std::runtime_error("Cannot find desktop folder.");auto link=std::filesystem::path(desktop)/(d->path.filename().wstring()+L".lnk");CoTaskMemFree(desktop);CreateShortcut(d->path,link);break;}
        case IDM_FILE_PRINT:PrintDocument(frame.m_hWnd,*d,&frame.settings);break;
        case IDM_FILE_PAGESETUP:ConfigurePrinting(frame.m_hWnd,&frame.settings);break;
        case IDM_FILE_RUN:RunFileDialog(frame);break;
        case IDM_FILE_BROWSE:OpenFolderRequest(frame,d?d->path:std::filesystem::path(),FolderOpenRequest::Browse);break;

        case IDM_FILE_OPENFAV:SelectHistoryFile(frame,true);break;
        case IDM_FILE_MANAGEFAV:ManageFavoriteFiles(frame);break;
        case IDM_FILE_ADDTOFAV:AddFavoriteFile(frame);break;
        case IDM_FILE_RECENT:SelectHistoryFile(frame,false);break;
        case CMD_COPYFILENAME:CopyText(*d,d->path.filename().wstring());break;
        case CMD_COPYFILENAME_NOEXT:CopyText(*d,d->path.stem().wstring());break;
        case CMD_COPYPATHNAME:CopyText(*d,d->path.wstring());break;
        }
        frame.Refresh();
    }catch(const std::exception& error){frame.Error(error);}
    return true;
}
void RestoreFileVariables(EditorDocument& document){
    auto& state=document.fileVariables;if(!state.active)return;state.active=false;
    auto restore=[&](auto& value,const auto& applied,const auto& baseline){if(value==applied)value=baseline;};
    restore(document.style.scheme,state.applied.scheme,state.baseline.scheme);restore(document.style.dialect,state.applied.dialect,state.baseline.dialect);
    restore(document.style.tabWidth,state.applied.tabWidth,state.baseline.tabWidth);restore(document.style.indentWidth,state.applied.indentWidth,state.baseline.indentWidth);
    restore(document.style.tabsAsSpaces,state.applied.tabsAsSpaces,state.baseline.tabsAsSpaces);restore(document.style.useSchemeTabs,state.applied.useSchemeTabs,state.baseline.useSchemeTabs);
    restore(document.style.tabIndents,state.applied.tabIndents,state.baseline.tabIndents);
    int wrap=static_cast<int>(document.Send(SCI_GETWRAPMODE));if(wrap==state.appliedWrap)document.Send(SCI_SETWRAPMODE,state.baselineWrap);
    int edge=static_cast<int>(document.Send(SCI_GETEDGECOLUMN));if(edge==state.appliedEdge)document.Send(SCI_SETEDGECOLUMN,state.baselineEdge);
    document.ConfigureLexer();
    document.Send(SCI_SETTABWIDTH,document.style.tabWidth);
    document.Send(SCI_SETINDENT,document.style.indentWidth);
    document.Send(SCI_SETUSETABS,!document.style.tabsAsSpaces);
    document.Send(SCI_SETTABINDENTS,document.style.tabIndents);
    ApplyDocumentWrapSettings(document);
    if (document.LargeFile()) document.Send(SCI_SETWRAPMODE, SC_WRAP_NONE);
}
void ApplyFileVariables(EditorDocument& document){
    if (document.FastView()) return;
    RestoreFileVariables(document);
    auto sample = document.Range(0, 4096);
    auto length = static_cast<size_t>(document.Send(SCI_GETLENGTH));
    if (length > 4096) sample += document.Range((std::max)(size_t(4096), length - (std::min)(length, size_t(4096))), 4096);
    auto region=filevars::Region(sample);int value=0;
    if(filevars::Integer(region,"enable-local-variables",value)&&!value)return;
    auto& baseline=document.fileVariables;baseline.baseline=document.style;baseline.baselineWrap=static_cast<int>(document.Send(SCI_GETWRAPMODE));baseline.baselineEdge=static_cast<int>(document.Send(SCI_GETEDGECOLUMN));
    auto mode=filevars::String(region,"mode");if(!mode.empty())SelectDocumentScheme(document,files::FromUtf8(mode));
    int tab=0,indent=0;
    const bool hasTab=filevars::Integer(region,"tab-width",tab);
    const bool hasIndent=filevars::Integer(region,"*basic-indent",indent)||filevars::Integer(region,"*basic-offset",indent);
    if(hasTab||hasIndent){if(!hasTab)tab=indent;if(!hasIndent)indent=tab;tab=(std::clamp)(tab,1,256);indent=(std::clamp)(indent,0,256);
        document.style.tabWidth=tab;document.style.indentWidth=indent;document.style.useSchemeTabs=false;document.Send(SCI_SETTABWIDTH,tab);document.Send(SCI_SETINDENT,indent);}
    if(filevars::Integer(region,"indent-tabs-mode",value)){document.style.tabsAsSpaces=!value;document.style.useSchemeTabs=false;document.Send(SCI_SETUSETABS,value!=0);}
    if(filevars::Integer(region,"*tab-always-indent",value)){document.style.tabIndents=value!=0;document.Send(SCI_SETTABINDENTS,value!=0);}
    if(filevars::Integer(region,"truncate-lines",value))document.Send(SCI_SETWRAPMODE,value?SC_WRAP_NONE:document.style.wrapMode);
    if(filevars::Integer(region,"fill-column",value))document.Send(SCI_SETEDGECOLUMN,(std::clamp)(value,0,1000000));
    ApplyDocumentWrapSettings(document);
    if (document.LargeFile()) document.Send(SCI_SETWRAPMODE, SC_WRAP_NONE);
    baseline.applied=document.style;baseline.appliedWrap=static_cast<int>(document.Send(SCI_GETWRAPMODE));baseline.appliedEdge=static_cast<int>(document.Send(SCI_GETEDGECOLUMN));baseline.active=filevars::Recognized(region);
}

bool LoadLargeFileForFrame(MainFrame& frame, EditorDocument& document, const std::filesystem::path& path, std::optional<files::Encoding> forced, bool encodingTags) {
    if (std::filesystem::file_size(path) < files::LargeFileThreshold) return false;
    files::ReadOptions options;
    options.unicodeDetection = frame.settings.ReadString(L"File Settings", L"SkipUnicodeDetection", L"0") != L"1";
    options.fileVariables = encodingTags && frame.settings.ReadString(L"File Settings", L"IgnoreEncodingTags", L"0") != L"1";
    auto fallback = static_cast<files::Encoding>(frame.settings.Get(IDM_ENCODING_SETDEFAULT, static_cast<int>(files::Encoding::Ansi)));
    if (fallback == files::Encoding::Ansi || fallback == files::Encoding::Oem || static_cast<unsigned>(fallback) >= static_cast<unsigned>(files::Encoding::CodePageBase)) options.fallback = fallback;
    document.OpenFastFile(path, options, forced);
    for (int i = 0; i < frame.tabs.GetPageCount(); ++i) {
        if (frame.tabs.GetPageData(i) == &document) {
            auto* child = frame.tabs.Child(i);
            SetParent(document.ViewWindow(), child->m_hWnd);
            child->m_hWndClient = document.ViewWindow(); ShowWindow(document.ViewWindow(), SW_SHOW); child->UpdateLayout(); break;
        }
    }
    return true;
}
files::Text ReadFileForFrame(MainFrame& frame,const std::filesystem::path& path){
    auto option=[&](const wchar_t* key,const wchar_t* fallback=L"0"){return frame.settings.ReadString(L"File Settings",key,fallback)==L"1";};
    auto fallback=static_cast<files::Encoding>(frame.settings.Get(IDM_ENCODING_SETDEFAULT,static_cast<int>(files::Encoding::Ansi)));
    auto extension=path.extension().wstring();
    const bool nfo=option(L"NFOAsOEM") && (_wcsicmp(extension.c_str(),L".nfo")==0 || _wcsicmp(extension.c_str(),L".diz")==0);
    std::optional<files::Encoding> preferred;
    if(nfo)preferred=files::FromCodePage(437);
    else if(_wcsicmp(extension.c_str(),L".bat")==0||_wcsicmp(extension.c_str(),L".cmd")==0)preferred=files::Encoding::Ansi;
    else if(_wcsicmp(extension.c_str(),L".vhd")==0||_wcsicmp(extension.c_str(),L".vhdl")==0)preferred=files::FromCodePage(1252);
    const bool unicodeDefault=fallback==files::Encoding::Utf16LE||fallback==files::Encoding::Utf16BE||fallback==files::Encoding::Utf16LEPlain||fallback==files::Encoding::Utf16BEPlain;
    auto detectionFallback=preferred.value_or(unicodeDefault||fallback==files::Encoding::Utf8||fallback==files::Encoding::Utf8Bom?files::Encoding::Ansi:fallback);
    auto text=files::Read(path,{!option(L"SkipUnicodeDetection"),!option(L"IgnoreEncodingTags"),detectionFallback});
    const bool ascii=simd::IsAscii(text.utf8);
    if(!preferred&&(extension==L".sh"||text.utf8.starts_with("#!/")))preferred=files::Encoding::Utf8;
    bool declared=false;auto region=filevars::Region(text.utf8);int enabled=1;
    if(!option(L"IgnoreEncodingTags")&&(!filevars::Integer(region,"enable-local-variables",enabled)||enabled))for(auto key:{"encoding","charset","coding","fileencoding","/*!40101 SET NAMES "}){auto name=filevars::String(region,key);files::Encoding encoding=files::Encoding::Utf8;if(!name.empty()&&files::ParseEncodingName(files::FromUtf8(name),encoding)){declared=encoding!=files::Encoding::Utf16LE&&encoding!=files::Encoding::Utf16BE&&encoding!=files::Encoding::Utf16LEPlain&&encoding!=files::Encoding::Utf16BEPlain;if(declared)break;}}
    if(text.encoding==files::Encoding::Utf8&&ascii){
        if(preferred)text.encoding=*preferred;
        else if(!declared&&!option(L"ASCIIAsUTF8",L"1"))text.encoding=unicodeDefault?files::Encoding::Ansi:fallback==files::Encoding::Utf8Bom&&!text.utf8.empty()?files::Encoding::Utf8:fallback;
    }
    if(!preferred&&!declared&&option(L"ANSIAsUTF8")&&text.encoding==detectionFallback)text.encoding=files::Encoding::Utf8;
    return text;
}
void PrepareFileSave(MainFrame& frame,EditorDocument& document){
    if(document.Send(SCI_GETREADONLY))return;
    const bool normalize=frame.settings.ReadString(L"File Settings",L"ConsistentEOL",L"0")==L"1";
    const bool strip=frame.settings.ReadString(L"File Settings",L"StripTrailingBlanks",L"0")==L"1";
    if(!normalize && !strip)return;
    document.Send(SCI_BEGINUNDOACTION);
    if(strip){auto lines=document.Send(SCI_GETLINECOUNT);for(auto line=lines;line-->0;){auto end=document.Send(SCI_GETLINEENDPOSITION,line),start=document.Send(SCI_POSITIONFROMLINE,line);auto position=end;
        while(position>start){auto c=document.Send(SCI_GETCHARAT,position-1);if(c!=' '&&c!='\t')break;--position;}
        if(position!=end){document.Send(SCI_SETTARGETRANGE,position,end);document.Send(SCI_REPLACETARGET,0,reinterpret_cast<LPARAM>(""));}
    }}
    if(normalize)document.Send(SCI_CONVERTEOLS,document.Send(SCI_GETEOLMODE));
    document.Send(SCI_ENDUNDOACTION);
}
namespace {
struct EolWarning { EditorDocument& document; size_t crlf=0,cr=0,lf=0; bool always=true; };
INT_PTR CALLBACK EolProcedure(HWND window,UINT message,WPARAM w,LPARAM l){
    auto* context=reinterpret_cast<EolWarning*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG){context=reinterpret_cast<EolWarning*>(l);SetWindowLongPtrW(window,DWLP_USER,l);
        SetDlgItemTextW(window,IDC_EOL_SUM_CRLF,(std::to_wstring(context->crlf)+L" line(s).").c_str());SetDlgItemTextW(window,IDC_EOL_SUM_CR,(std::to_wstring(context->cr)+L" line(s).").c_str());SetDlgItemTextW(window,IDC_EOL_SUM_LF,(std::to_wstring(context->lf)+L" line(s).").c_str());
        for(auto label:{L"Windows (CR+LF)",L"Classic Mac OS (CR)",L"Unix/macOS (LF)"})SendDlgItemMessageW(window,IDC_EOLMODELIST,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendDlgItemMessageW(window,IDC_EOLMODELIST,CB_SETCURSEL,context->document.Send(SCI_GETEOLMODE),0);CheckDlgButton(window,IDC_WARNINCONSISTENTEOLS,BST_CHECKED);if(context->document.Send(SCI_GETREADONLY)){EnableWindow(GetDlgItem(window,IDOK),FALSE);EnableWindow(GetDlgItem(window,IDC_EOLMODELIST),FALSE);SetWindowTextW(window,L"Inconsistent Line Endings — Read Only");}CWindow(window).CenterWindow(GetParent(window));return TRUE;}
    if(!context || message!=WM_COMMAND)return FALSE;if(LOWORD(w)==IDOK&&context->document.Send(SCI_GETREADONLY))return TRUE;
    if(LOWORD(w)==IDOK||LOWORD(w)==IDCANCEL){context->always=IsDlgButtonChecked(window,IDC_WARNINCONSISTENTEOLS)!=0;
        if(LOWORD(w)==IDOK){auto mode=SendDlgItemMessageW(window,IDC_EOLMODELIST,CB_GETCURSEL,0,0);context->document.Send(SCI_CONVERTEOLS,mode);context->document.Send(SCI_SETEOLMODE,mode);}EndDialog(window,LOWORD(w));return TRUE;}return FALSE;
}
}
void InitializeFileDocument(MainFrame& frame,EditorDocument& document){
    if (document.FastView()) return;
    DetectDocumentIndentation(document);
    if(frame.settings.ReadString(L"File Settings",L"IgnoreFileVariables",L"0")!=L"1")ApplyFileVariables(document);
    if(document.LargeFile()){document.Send(SCI_SETWRAPMODE, SC_WRAP_NONE);return;}
    if(frame.settings.ReadString(L"File Settings",L"WarnInconsistentEOL",L"0")!=L"1")return;
    EolWarning warning{document};
    auto length = static_cast<size_t>(document.Send(SCI_GETLENGTH));
    auto data = reinterpret_cast<const char*>(document.Send(SCI_GETCHARACTERPOINTER));
    const auto endings = simd::AnalyzeLineEndings(std::string_view(data, length));
    warning.crlf = endings.linesCount[0]; warning.cr = endings.linesCount[1]; warning.lf = endings.linesCount[2];
    if((warning.crlf!=0)+(warning.cr!=0)+(warning.lf!=0)<=1)return;
    DialogBoxParamW(_Module.GetModuleInstance(),MAKEINTRESOURCEW(IDD_WARNLINEENDS),frame.m_hWnd,EolProcedure,reinterpret_cast<LPARAM>(&warning));
    frame.settings.WriteString(L"File Settings",L"WarnInconsistentEOL",warning.always?L"1":L"0");
}

std::filesystem::path SaveDocumentBackup(MainFrame& frame,EditorDocument& document){
    if(!document.Send(SCI_GETLENGTH))return {};
    auto record=[&](){document.runtime.lastRecoveryRevision=document.style.revision;document.runtime.lastRecoveryEncoding=static_cast<unsigned>(document.encoding);document.runtime.lastRecoveryEol=static_cast<int>(document.Send(SCI_GETEOLMODE));};
    SYSTEMTIME now{};GetLocalTime(&now);wchar_t timestamp[64]{};
    swprintf_s(timestamp,L" %04u%02u%02u %02u%02u%02u",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond);
    auto name=document.path.empty()?std::wstring(L"Untitled"):document.path.filename().wstring();
    auto dot=name.find(L'.');auto extension=dot==std::wstring::npos?std::wstring(L".bak"):name.substr(dot);
    auto backupName=name+timestamp+extension;
    files::Text text{document.Text(),files::Encoding::Utf8};
    if(!document.path.empty()){
        auto path=document.path.parent_path()/backupName;
        try{files::WriteNew(path,text);record();return path;}catch(const std::exception&){}
    }
    auto directory=frame.settings.path.parent_path()/L"AutoSave";std::filesystem::create_directories(directory);
    if(!document.path.empty()){
        auto eol=document.Send(SCI_GETEOLMODE);auto ending=eol==SC_EOL_LF?"\n":eol==SC_EOL_CR?"\r":"\r\n";
        text.utf8="AutoSave for "+files::ToUtf8(document.path.wstring())+ending+text.utf8;
    }
    auto path=directory/backupName;files::WriteNew(path,text);record();return path;
}
#include "InfoDialog.h"
namespace {
bool LegacyEncoding(files::Encoding encoding){return encoding!=files::Encoding::Utf8&&encoding!=files::Encoding::Utf8Bom&&encoding!=files::Encoding::Utf16LE&&encoding!=files::Encoding::Utf16BE&&encoding!=files::Encoding::Utf16LEPlain&&encoding!=files::Encoding::Utf16BEPlain;}
void ReplaceEncodingText(EditorDocument& document,const std::string& text){
    auto current=document.Send(SCI_GETCURRENTPOS),anchor=document.Send(SCI_GETANCHOR);
    document.Send(SCI_BEGINUNDOACTION);document.Send(SCI_SETTARGETRANGE,0,document.Send(SCI_GETLENGTH));document.Send(SCI_REPLACETARGET,text.size(),reinterpret_cast<LPARAM>(text.data()));document.Send(SCI_ENDUNDOACTION);
    auto length=document.Send(SCI_GETLENGTH);document.Send(SCI_SETSEL,(std::min)(anchor,length),(std::min)(current,length));
}
std::wstring EncodingMessage(UINT resource){wchar_t text[4096]{};LoadStringW(_Module.GetModuleInstance(),resource,text,4096);return text;}
}
bool SelectDocumentEncoding(MainFrame& frame,EditorDocument& document,files::Encoding encoding){
    if(document.encoding==encoding)return true;if(document.Send(SCI_GETREADONLY))return false;
    const bool ansiChange=document.encoding==files::Encoding::Ansi||encoding==files::Encoding::Ansi;
    if (!ansiChange) { document.SetEncoding(encoding); return true; }
    const auto original=document.Text();
    if(ansiChange && (!original.empty()||document.Send(SCI_GETUNDOACTIONS))){
        auto key=original.empty()?L"MsgConv2":L"MsgConv1";
        auto message=original.empty()?L"Change the encoding of this empty document? Text edits remain available in undo history.":L"Switching between ANSI and Unicode may replace unsupported characters with default placeholders. Text conversion can be undone. Continue?";
        if(ShowInfoDialog(frame.m_hWnd,frame.settings,message,L"Encoding",MB_YESNO|MB_ICONWARNING,key)!=IDYES)return false;
    }
    if(encoding==files::Encoding::Ansi){auto converted=files::Decode(files::EncodeLossy({original,encoding}),encoding).utf8;if(converted!=original)ReplaceEncodingText(document,converted);}
    document.SetEncoding(encoding);return true;
}
bool PrepareEncodingSave(MainFrame& frame,EditorDocument& document,files::Text& output,bool copy){
    output={document.Text(),document.encoding};
    try{files::Encode(output);return true;}catch(const std::runtime_error&){
        if(!LegacyEncoding(output.encoding)||!IsValidCodePage(files::CodePage(output.encoding)))throw;
        auto converted=files::Decode(files::EncodeLossy(output),output.encoding).utf8;
        if(converted==output.utf8)throw;
        if(!copy&&document.Send(SCI_GETREADONLY)){
            ShowInfoDialog(frame.m_hWnd,frame.settings,L"This document is read only. Turn off Read Only Mode before converting unsupported characters, or use Save Copy.",L"Encoding",MB_OK|MB_ICONINFORMATION);
            return false;
        }
        if(ShowInfoDialog(frame.m_hWnd,frame.settings,EncodingMessage(IDS_ERR_UNICODE2),L"Encoding",MB_OKCANCEL|MB_ICONWARNING,L"MsgConv3")!=IDOK)return false;
        output.utf8=converted;if(!copy)ReplaceEncodingText(document,converted);return true;
    }
}
files::Text ReadFileForReload(MainFrame& frame,const EditorDocument& document){
    files::ReadOptions options;
    options.unicodeDetection=frame.settings.ReadString(L"File Settings",L"SkipUnicodeDetection",L"0")!=L"1";
    options.fileVariables=frame.settings.ReadString(L"File Settings",L"IgnoreEncodingTags",L"0")!=L"1";
    options.fallback=static_cast<files::Encoding>(frame.settings.Get(IDM_ENCODING_SETDEFAULT,static_cast<int>(files::Encoding::Ansi)));
    if(!LegacyEncoding(options.fallback))options.fallback=files::Encoding::Ansi;
    auto extension=document.path.extension().wstring();
    if(frame.settings.ReadString(L"File Settings",L"NFOAsOEM",L"0")==L"1"&&(_wcsicmp(extension.c_str(),L".nfo")==0||_wcsicmp(extension.c_str(),L".diz")==0))options.fallback=files::FromCodePage(437);
    else if(_wcsicmp(extension.c_str(),L".vhd")==0||_wcsicmp(extension.c_str(),L".vhdl")==0)options.fallback=files::FromCodePage(1252);
    else if(_wcsicmp(extension.c_str(),L".bat")==0||_wcsicmp(extension.c_str(),L".cmd")==0)options.fallback=files::Encoding::Ansi;
    return files::ReadForReload(document.path,document.encoding,options);
}
