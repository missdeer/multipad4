#include "CommandLine.h"
#include "MainFrame.h"
#include "FrameCommands.h"
#include "StyleManager.h"
#include "FileCommands.h"
#include "FileDialogs.h"
#include "Printing.h"
#include "Session.h"
#include <cwctype>
#include <sstream>
#include <climits>
#include <shellapi.h>
#include <shobjidl.h>
namespace {
std::wstring Lower(std::wstring value){std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});return value;}
std::vector<long long> Numbers(std::wstring text){std::replace(text.begin(),text.end(),L',',L' ');std::wistringstream stream(text);std::vector<long long> numbers;long long value;while(stream>>value)numbers.push_back(value);return numbers;}
files::Encoding Encoding(std::wstring value,bool target=true){
    value=Lower(value);value.erase(std::remove(value.begin(),value.end(),L'-'),value.end());
    if(value==L"a"||value==L"ansi"||value==L"mbcs")return files::Encoding::Ansi;
    if(value==L"w")return files::Encoding::Utf16LE;
    bool utf16=value.starts_with(L"utf16")||value.starts_with(L"unicode");
    if(utf16){auto suffix=value.substr(value.starts_with(L"utf16")?5:7);bool reverse=false;
        if(suffix.starts_with(L"le"))suffix.erase(0,2);else if(suffix.starts_with(L"be")){reverse=true;suffix.erase(0,2);}
        const bool bom=!suffix.empty();if(bom && suffix!=L"bom"&&suffix!=L"sig"&&suffix!=L"signature")throw std::runtime_error("Unknown UTF-16 command-line encoding.");
        if(target||bom)return reverse?files::Encoding::Utf16BE:files::Encoding::Utf16LE;
        return reverse?files::Encoding::Utf16BEPlain:files::Encoding::Utf16LEPlain;
    }
    files::Encoding parsed;if(files::ParseEncodingName(value,parsed))return parsed;
    throw std::runtime_error("Unknown command-line encoding.");
}
std::wstring Quote(const std::wstring& value){std::wstring result=L"\"";size_t backslashes=0;for(wchar_t c:value){if(c==L'\\'){++backslashes;continue;}if(c==L'\"')result.append(backslashes*2+1,L'\\');else result.append(backslashes,L'\\');result+=c;backslashes=0;}result.append(backslashes*2,L'\\');result+=L'\"';return result;}
}
ParsedCommandLine ParseCommandLine(const std::wstring& fullCommandLine){
    ParsedCommandLine result;int count=0;auto args=CommandLineToArgvW(fullCommandLine.c_str(),&count);if(!args)throw std::runtime_error("Cannot parse command line.");
    bool filesOnly=false;
    try{
        for(int i=1;i<count;++i){std::wstring original=args[i],option=Lower(original);
            if(!filesOnly&&(option==L"+"||option==L"-")){result.multipleFiles=option==L"+";filesOnly=true;continue;}
            if(original.empty())continue;
            auto addFile=[&](){std::wstring path=original;if(!result.multipleFiles)while(i+1<count){path+=L" ";path+=args[++i];}result.files.push_back(path);filesOnly=true;};
            if(filesOnly || (option[0]!=L'/'&&option[0]!=L'-')){addFile();continue;}
            option.erase(0,1);std::wstring raw=original.substr(1);if(option.starts_with(L"-")){option.erase(0,1);raw.erase(0,1);}
            auto argument=[&](){auto colon=raw.find(L':');if(colon!=std::wstring::npos)return raw.substr(colon+1);if(i+1>=count)throw std::runtime_error("Missing command-line option argument.");return std::wstring(args[++i]);};
            if(option==L"?"||option==L"help"){result.help=true;continue;}
            if(option==L"session"||option.starts_with(L"session:")){result.sessionFile=argument();continue;}
            if(option==L"z"){if(i+1<count)++i;result.multipleFiles=false;result.notepadReplacement=true;continue;}
            if(option==L"e"||option.starts_with(L"e:")){result.sourceEncoding=Encoding(argument(),false);continue;}
            if(option==L"g"||option.starts_with(L"g:")){auto values=Numbers(argument());if(values.empty())throw std::runtime_error("Invalid line/column argument.");result.jump=true;result.line=values[0];result.column=values.size()>1?values[1]:1;result.visualColumn=raw[0]==L'G';if(values.size()>2){result.byteColumn=!result.visualColumn && (values[2]&1);result.tabWidth=result.visualColumn?static_cast<unsigned>((std::clamp)(values[2],0LL,256LL)):0;}continue;}
            if(option==L"s"||option.starts_with(L"s:")){result.scheme=argument();continue;}
            if(option==L"t"||option.starts_with(L"t:")){result.title=argument();continue;}
            if(option==L"m"||option==L"m-"||option==L"mr"||option==L"mr-"||option==L"mb"||option==L"mb-"){result.matchRegex=option.find(L'r')!=std::wstring::npos;result.matchBackslash=option.find(L'b')!=std::wstring::npos;result.matchBackwards=option.back()==L'-';result.match=argument();continue;}
            if(option==L"crlf"||option==L"cr-lf"){result.eol=SC_EOL_CRLF;continue;}if(option==L"lf"){result.eol=SC_EOL_LF;continue;}if(option==L"cr"){result.eol=SC_EOL_CR;continue;}
            if(option==L"ro"){result.readOnly=true;continue;}if(option==L"r"||option==L"rs"){result.reuseWindow=1;result.singleFileInstance=option==L"rs";continue;}if(option==L"n"||option==L"ns"){result.reuseWindow=0;result.singleFileInstance=option==L"ns";continue;}
            if(option==L"l"){result.changeNotify=2;continue;}if(option==L"lo"||option==L"l0"||option==L"l-"){result.changeNotify=0;continue;}
            if(option==L"o"){result.alwaysOnTop=1;continue;}if(option==L"o0"||option==L"o-"||option==L"oo"){result.alwaysOnTop=0;continue;}
            if(option==L"c"){result.fromClipboard=true;result.reuseWindow=0;continue;}if(option==L"b"){result.pasteBoard=true;result.reuseWindow=0;continue;}
            if(option==L"q"){result.quietCreate=true;continue;}if(option==L"i"){result.startTray=true;continue;}if(option==L"u"){result.elevate=true;continue;}
            if(option==L"d"){result.scheme=L"txt";continue;}if(option==L"h"){result.scheme=L"html";continue;}if(option==L"x"){result.scheme=L"xml";continue;}
            if(option==L"p"||(result.notepadReplacement&&option==L"pt")){result.print=true;result.printDialog=option==L"pt";if(result.printDialog&&i+1<count)++i;continue;}
            if(option.starts_with(L"appid=")){result.appId=raw.substr(6);if(result.appId.empty())result.appId=L"MultiPad4";continue;}
            if(option==L"sysmru=0"||option==L"sysmru=1"){result.useSystemMRU=option.back()==L'1';continue;}
            if(option.starts_with(L"p")){
                auto value=option.starts_with(L"pos")?option.substr(3):option.substr(1);if(value.starts_with(L":"))value.erase(0,1);
                if(value.empty())value=Lower(argument());
                if(value==L"0"||value==L"o"||value==L"d"||value==L"s"||value==L"dl"||value==L"sl"){result.positionFlags=value;continue;}
                bool flags=!value.empty()&&std::all_of(value.begin(),value.end(),[](wchar_t c){return wcschr(L"flrtbm",c)!=nullptr;});
                if(flags){result.positionFlags=value;continue;}
                auto values=Numbers(value);if(values.size()>=4){
                    LONG width=values[2]>0?static_cast<LONG>((std::min)(values[2],static_cast<long long>(INT_MAX))):CW_USEDEFAULT;
                    LONG height=values[3]>0?static_cast<LONG>((std::min)(values[3],static_cast<long long>(INT_MAX))):CW_USEDEFAULT;
                    auto coordinate=[](long long value){return static_cast<LONG>((std::clamp)(value,static_cast<long long>(LONG_MIN),static_cast<long long>(LONG_MAX)));};
                    LONG x=coordinate(values[0]),y=coordinate(values[1]);
                    result.position=RECT{x,y,width==CW_USEDEFAULT?CW_USEDEFAULT:coordinate(static_cast<long long>(x)+width),height==CW_USEDEFAULT?CW_USEDEFAULT:coordinate(static_cast<long long>(y)+height)};result.maximize=values.size()>4&&values[4]!=0;continue;
                }
                addFile();continue;
            }
            try{result.targetEncoding=Encoding(option);}catch(const std::exception&){addFile();}
        }
    }catch(...){LocalFree(args);throw;}LocalFree(args);return result;
}
std::wstring GetOriginalCommandLineArguments(){int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);std::wstring output;if(args){for(int i=1;i<count;++i){if(i>1)output+=L" ";output+=Quote(args[i]);}LocalFree(args);}return output;}
bool RelaunchElevatedIfRequested(const ParsedCommandLine& options){if(!options.elevate)return false;
    HANDLE token=nullptr;TOKEN_ELEVATION elevation{};DWORD length=0;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))throw std::runtime_error("Cannot query process elevation.");
    bool queried=GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&length)!=FALSE;CloseHandle(token);
    if(!queried)throw std::runtime_error("Cannot query process elevation.");if(elevation.TokenIsElevated)return false;
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);auto arguments=GetOriginalCommandLineArguments();SHELLEXECUTEINFOW info{sizeof(info)};info.lpVerb=L"runas";info.lpFile=executable;info.lpParameters=arguments.c_str();info.nShow=SW_SHOWNORMAL;if(!ShellExecuteExW(&info))throw std::runtime_error("Elevation was cancelled or failed.");return true;}
void ApplyCommandLine(MainFrame& frame,const ParsedCommandLine& options){
    if(!options.appId.empty())SetCurrentProcessExplicitAppUserModelID(options.appId.c_str());
    if(options.useSystemMRU>=0)frame.runtime.useSystemMRU=options.useSystemMRU;
    if(options.alwaysOnTop>=0)frame.SetWindowPos(options.alwaysOnTop?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    auto systemDefault=[&](){RECT rect{80,60,1280,860};HWND probe=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,nullptr,nullptr,_Module.GetModuleInstance(),nullptr);if(probe){GetWindowRect(probe,&rect);DestroyWindow(probe);}return rect;};
    if(options.position){auto r=*options.position;auto fallback=systemDefault();
        int x=r.left==CW_USEDEFAULT?fallback.left:r.left,y=r.top==CW_USEDEFAULT?fallback.top:r.top;
        int width=r.right==CW_USEDEFAULT?fallback.right-fallback.left:r.right-r.left,height=r.bottom==CW_USEDEFAULT?fallback.bottom-fallback.top:r.bottom-r.top;
        frame.SetWindowPos(nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    }else if(!options.positionFlags.empty()){
        auto flag=options.positionFlags;RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        const int width=work.right-work.left,height=work.bottom-work.top;RECT r=work;
        if(flag==L"0"||flag==L"o")r=systemDefault();
        else if(flag==L"d"||flag==L"s"||flag==L"dl"||flag==L"sl"){
            int cy=height-32,cx=(std::min)(width-32,cy);int x=(flag==L"dl"||flag==L"sl")?work.left+16:work.right-cx-16;r={x,work.top+16,x+cx,work.top+16+cy};
        }else{
            bool left=false,right=false,top=false,bottom=false,full=false,margin=false;
            for(wchar_t c:flag)switch(c){case L'f':left=right=top=bottom=false;full=true;break;case L'l':right=false;full=false;left=true;break;case L'r':left=false;full=false;right=true;break;case L't':bottom=false;full=false;top=true;break;case L'b':top=false;full=false;bottom=true;break;case L'm':if(!left&&!right&&!top&&!bottom&&!full)full=true;margin=true;break;}
            if(!full){if(left||right){if(right)r.left+=width/2;else r.right=r.left+width/2;}if(top||bottom){if(bottom)r.top+=height/2;else r.bottom=r.top+height/2;}}
            if(margin){r.left+=right?4:8;r.right-=left?4:8;r.top+=bottom?4:8;r.bottom-=top?4:8;}
        }
        frame.SetWindowPos(nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    auto applyDocument=[&](EditorDocument* d){
        if(options.targetEncoding)d->SetEncoding(*options.targetEncoding);if(options.eol>=0){d->Send(SCI_CONVERTEOLS,options.eol);d->Send(SCI_SETEOLMODE,options.eol);}
        if(!options.scheme.empty()&&!SelectDocumentScheme(*d,options.scheme))throw std::runtime_error("Unknown syntax scheme.");
        if(options.jump||options.line){
            LRESULT position=0;
            if(options.line<0||options.line>d->Send(SCI_GETLINECOUNT))position=d->Send(SCI_GETLENGTH);
            else{
                Sci_CharacterRangeFull range{static_cast<Sci_Position>(options.line?options.line-1:0),0};
                WPARAM kind=options.visualColumn?SC_COLUMN_DEFAULT|(options.tabWidth<<4):options.byteColumn?SC_COLUMN_BYTE:SC_COLUMN_CHARACTER;
                long long column=options.column;
                if(column<0){kind=SC_COLUMN_CHARACTER;column=column==LLONG_MIN?LLONG_MAX:-column;}
                range.cpMax=static_cast<Sci_Position>((std::max)(0LL,column-1));
                position=d->Send(SCI_FINDCOLUMNEX,kind,reinterpret_cast<LPARAM>(&range));
            }
            d->Send(SCI_ENSUREVISIBLE,d->Send(SCI_LINEFROMPOSITION,position));d->Send(SCI_GOTOPOS,position);d->Send(SCI_SCROLLCARET);
        }
        if(!options.match.empty()){d->search.findText=options.match;d->search.regex=options.matchRegex;d->search.wildcard=false;d->search.flags&=SCFIND_REGEXP-1;d->search.transformBackslashes=options.matchBackslash;if(!options.jump&&!options.line)d->Send(SCI_GOTOPOS,options.matchBackwards?d->Send(SCI_GETLENGTH):0);d->FindNext(options.matchBackwards);}
        if(options.readOnly)d->Send(SCI_SETREADONLY,TRUE);if(options.changeNotify>=0)d->runtime.monitorMode=options.changeNotify;if(options.print)PrintDocument(frame.m_hWnd,*d,&frame.settings,!options.printDialog);
    };
    for(const auto& input:options.files){
        auto path=std::filesystem::weakly_canonical(std::filesystem::absolute(input));
        if(std::filesystem::exists(path)){
            if(!frame.Open(path,options.sourceEncoding))continue;
        }else{
            if(!options.quietCreate && frame.MessageBox((L"Create new file?\n"+path.wstring()).c_str(),L"MultiPad4",MB_YESNO|MB_ICONQUESTION)!=IDYES)continue;
            auto* created=frame.NewDocument();if(!created)throw std::runtime_error("Cannot create new document.");created->path=path;created->ConfigureLexer();
        }
        auto* d=frame.Active();if(!d)continue;
        applyDocument(d);
    }
    if(options.fromClipboard||options.pasteBoard){auto* d=frame.NewDocument();if(!d)throw std::runtime_error("Cannot create clipboard document.");d->Send(SCI_PASTE);if(options.pasteBoard){frame.runtime.pasteBoard=d->hwnd;frame.runtime.clipboardSequence=GetClipboardSequenceNumber();}applyDocument(d);}
    else if(options.files.empty() && (options.targetEncoding||options.eol>=0||!options.scheme.empty()||(options.jump||options.line)||!options.match.empty()||options.readOnly||options.changeNotify>=0||options.print)){
        auto* d=frame.Active();if(!d)d=frame.NewDocument();if(!d)throw std::runtime_error("Cannot create command-line document.");applyDocument(d);
    }
    if(!options.title.empty())frame.runtime.titleOverride=options.title;
    if(!options.sessionFile.empty())RestoreRelaunchSession(frame,options.sessionFile);
    if(options.maximize)frame.ShowWindow(SW_MAXIMIZE);if(options.startTray){SetTrayVisible(frame,true);frame.ShowWindow(SW_HIDE);}frame.Refresh();
}
