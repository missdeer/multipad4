#pragma once
#include "../plugins/PluginApi.h"
#include "../plugins/LSPClient/Protocol.h"
#include "QAArtifacts.h"
#include <source_location>
inline void RunLSPPluginTests(MainFrame& frame, const std::filesystem::path& directory) {
    namespace fs=std::filesystem;
    auto require=[](bool value,std::source_location where=std::source_location::current()){
        if(!value)throw std::runtime_error("LSP plugin check failed at line "+std::to_string(where.line()));
    };
    auto plugin=std::find_if(frame.plugins.plugins.begin(),frame.plugins.plugins.end(),[](const auto& p){return p.module==L"LSPClient.dll";});
    require(plugin!=frame.plugins.plugins.end());
    auto item=[&](std::wstring_view label)->FuncItem&{
        for(int i=0;i<plugin->count;++i)if(label==std::wstring_view(plugin->items[i]._itemName).substr(0,std::wstring_view(plugin->items[i]._itemName).find(L'\t'))) return plugin->items[i];
        throw std::runtime_error("Missing LSP command");
    };
    auto command=[&](std::wstring_view label){if(!frame.plugins.Execute(item(label)._cmdID)) throw std::runtime_error("Disabled LSP command: "+files::ToUtf8(std::wstring(label)));};
    struct Restore {PluginHost& host;fs::path config;PFUNCPLUGINCMD stop;~Restore(){stop();host.config=std::move(config);}} restore{frame.plugins,frame.plugins.config,item(L"Stop")._pFunc};
    fs::create_directories(directory);frame.plugins.config=directory;
    qaArtifactDirectory=directory/L"screenshots";
    wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,_countof(exe));
    wchar_t realServer[32768]{};
    const bool real=GetEnvironmentVariableW(L"MULTIPAD4_LSP_REAL_SERVER",realServer,_countof(realServer))>0;
    wchar_t projectFile[32768]{};
    const bool project=real && GetEnvironmentVariableW(L"MULTIPAD4_LSP_PROJECT_FILE",projectFile,_countof(projectFile))>0;
    const auto original=project?files::ReadWithEncoding(projectFile,files::Encoding::Utf8).utf8:std::string();
    const auto prefix=project?original+"\n":std::string();
    auto server=fs::path(exe).parent_path()/L"plugins/LSPClient/lsp_tests.exe";
    lsp::Json settings{{"servers",lsp::Json::array({{{"languageId","test"},{"extensions",lsp::Json::array({".lsptest"})},{"command",lsp::Json::array({files::ToUtf8(server.wstring()),"--mock-lsp"})}}})}};
    if(real) settings["servers"][0]["command"]=lsp::Json::array({files::ToUtf8(realServer),"--log=error"});
    if(project)settings["servers"][0]["command"][1]="--log=verbose";
    if(project)settings["servers"][0]["command"].push_back("--compile-commands-dir="+files::ToUtf8(fs::path(exe).parent_path().wstring()));
    files::WriteAtomic(directory/L"LSPClient.json",{settings.dump()});
    const auto source=project?fs::path(projectFile):directory/(real?L"real.cpp":L"unicode-中文.lsptest");
    const auto second=directory/L"second.lsptest";
    if(real){settings["servers"][0]["extensions"]=lsp::Json::array({".cpp"});settings["servers"][0]["languageId"]="cpp";files::WriteAtomic(directory/L"LSPClient.json",{settings.dump()});}
    const std::string initial=real?prefix+"int main() { int number = 0; num }\n":"he";
    if(!project)files::WriteAtomic(source,{initial});
    files::WriteAtomic(second,{"other"});
    require(frame.Open(source));auto* document=frame.Active();
    if(project)document->Send(SCI_SETTEXT,0,reinterpret_cast<LPARAM>(initial.c_str()));
    frame.ShowWindow(SW_SHOWNORMAL);SetForegroundWindow(frame);SetFocus(document->hwnd);
    auto pump=[&](auto predicate){
        const auto end=GetTickCount64()+10000;
        do {MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}if(predicate())return true;MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);}while(GetTickCount64()<end);
        return false;
    };
    auto main=frame.runtime.mainMenu;
    bool topLevel=false;
    for (int i=0;i<GetMenuItemCount(main);++i) topLevel |= GetSubMenu(main,i)==plugin->submenu;
    require(topLevel);
    auto options=GetSubMenu(plugin->submenu,GetMenuItemCount(plugin->submenu)-1);
    require(options && GetMenuItemCount(options)>=11);
    require(GetMenuState(options,item(L"Show hover information")._cmdID,MF_BYCOMMAND)&MF_CHECKED);
    frame.plugins.UpdateMenu(plugin->submenu);
    require(GetMenuState(plugin->submenu,item(L"Go to Declaration")._cmdID,MF_BYCOMMAND)&MF_GRAYED);
    CaptureQAPopup(frame,plugin->submenu,L"LSP-menu");
    CaptureQAPopup(frame,options,L"LSP-options");
    document->Send(SCI_AUTOCSETCHOOSESINGLE,TRUE);
    // The old completion list must not survive an LSP ownership transfer.
    document->Send(SCI_AUTOCSETSEPARATOR,'\n');
    document->Send(SCI_AUTOCSHOW,0,reinterpret_cast<LPARAM>("oldOne\noldTwo"));
    require(document->Send(SCI_AUTOCACTIVE)!=0);
    command(L"Restart All LSP Servers");require(pump([&]{return document->externalCompletion;}));
    require(!document->Send(SCI_AUTOCACTIVE));
    require(document->Send(SCI_GETCODEPAGE)==SC_CP_UTF8);
    if(real) {
        auto caret=initial.find("num }")+3;document->Send(SCI_GOTOPOS,caret);SetFocus(document->hwnd);
        command(L"Complete");require(pump([&]{return document->Send(SCI_AUTOCACTIVE)!=0;}));
        CaptureQAWindow(frame,L"LSP-clangd-completion");
        document->Send(SCI_AUTOCSELECT,0,reinterpret_cast<LPARAM>("number"));document->Send(SCI_AUTOCCOMPLETE);
        require(document->Text()==prefix+"int main() { int number = 0; number }\n");
        document->Send(SCI_UNDO);require(document->Text()==initial);
        const std::string members=prefix+"struct Sample { int member; void method(); };\nvoid test() { Sample object;  }\n";
        document->Send(SCI_SETTEXT,0,reinterpret_cast<LPARAM>(members.c_str()));
        document->Send(SCI_GOTOPOS,members.find("object;  }")+8);SetFocus(document->hwnd);
        for (char character : std::string_view("object")) {
            document->Send(WM_CHAR,character);
            const auto until=GetTickCount64()+260;
            require(pump([&]{return GetTickCount64()>=until;}));
            require(document->externalCompletion);
        }
        document->Send(WM_CHAR,'.'); // Exercise automatic triggering through Scintilla.
        require(pump([&]{
            if(!document->Send(SCI_AUTOCACTIVE))return false;
            document->Send(SCI_AUTOCSELECT,0,reinterpret_cast<LPARAM>("member"));
            if(!document->Send(SCI_AUTOCACTIVE))return false;
            char selected[4096]{};document->Send(SCI_AUTOCGETCURRENTTEXT,0,reinterpret_cast<LPARAM>(selected));
            return std::string_view(selected)=="member";
        }));
        document->Send(SCI_AUTOCCOMPLETE);
        require(document->Text().find("object.member }")!=std::string::npos);
        document->Send(SCI_UNDO);require(document->Text().find("object. }")!=std::string::npos);
        command(L"Stop");require(!document->externalCompletion);
        if(project)document->Send(SCI_SETTEXT,0,reinterpret_cast<LPARAM>(original.c_str()));
        document->Send(SCI_SETSAVEPOINT);return;
    }
    command(L"Format on save");
    require(GetMenuState(options,item(L"Format on save")._cmdID,MF_BYCOMMAND)&MF_CHECKED);
    require(frame.Save(*document));
    require(files::ReadWithEncoding(source,files::Encoding::Utf8).utf8=="formatted");
    command(L"Format on save");
    document->Send(SCI_UNDO);require(document->Text()=="he");
    command(L"Format");
    require(pump([&]{return document->Text()=="formatted";}));
    document->Send(SCI_UNDO);require(document->Text()=="he");
    document->Send(SCI_GOTOPOS,0);
    command(L"Expand Selection");
    require(pump([&]{return document->Send(SCI_GETSELECTIONEND)==2;}));
    command(L"Shrink Selection");require(document->Send(SCI_GETSELECTIONEMPTY));
    command(L"Find References");
    require(pump([&]{return frame.Active()!=document;}));
    require(frame.Active()->Text().find(".lsptest:1")!=std::string::npos);
    command(L"Close All Dynamic Reference Tabs");require(frame.Active()==document);
    command(L"Restart LSP Server");
    SetFocus(document->hwnd);
    require(pump([&]{return document->Send(SCI_INDICATORVALUEAT,frame.plugins.nextIndicator-1,0)!=0;}));
    document->Send(SCI_GOTOPOS,2);SetFocus(document->hwnd);
    command(L"Complete");
    SCNotification dwellEnd{};dwellEnd.nmhdr.hwndFrom=document->hwnd;dwellEnd.nmhdr.code=SCN_DWELLEND;
    frame.plugins.ScintillaNotification(dwellEnd); // Mouse movement must not cancel completion.
    require(pump([&]{return document->Send(SCI_AUTOCACTIVE)!=0;}));
    const auto separator=document->Send(SCI_AUTOCGETSEPARATOR);require(separator=='\n');
    require(!document->Send(SCI_AUTOCGETCHOOSESINGLE));
    CaptureQAWindow(frame,L"LSP-completion");
    auto capturePopup=[](const wchar_t* type,const wchar_t* label) {
        struct Capture {const wchar_t* type;const wchar_t* label;} capture{type,label};
        EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM data)->BOOL {
            auto& capture=*reinterpret_cast<Capture*>(data);wchar_t name[128];GetClassNameW(window,name,_countof(name));
            if(IsWindowVisible(window) && wcscmp(name,capture.type)==0)CaptureQAWindow(window,capture.label);
            return TRUE;
        },reinterpret_cast<LPARAM>(&capture));
    };
    capturePopup(L"ListBoxX",L"LSP-completion-popup");
    document->Send(SCI_AUTOCSELECT,0,reinterpret_cast<LPARAM>("hello"));
    require(document->Send(SCI_AUTOCACTIVE)!=0);
    document->Send(SCI_AUTOCCOMPLETE);
    require(document->Text()=="hello;");
    require(document->Send(SCI_AUTOCGETCHOOSESINGLE));
    require(document->Send(SCI_INDICATORVALUEAT,frame.plugins.nextIndicator-1,0)==0); // Native handler must not insert a second time.
    document->Send(SCI_UNDO);require(document->Text()=="he");
    require(pump([&]{return !document->style.completionPending;}));
    document->Send(SCI_GOTOPOS,2);SetFocus(document->hwnd);
    command(L"Complete");require(pump([&]{return document->Send(SCI_AUTOCACTIVE)!=0;}));
    document->Send(WM_CHAR,'2');
    require(!document->Send(SCI_AUTOCACTIVE));
    require(pump([&]{return document->Send(SCI_AUTOCACTIVE)!=0;}));
    document->Send(SCI_AUTOCCOMPLETE);require(document->Text()=="hello;");
    document->Send(SCI_UNDO);require(document->Text()=="he2");
    for(const auto& [typedText,expected] : {std::pair{"fb","fooBar"},std::pair{"alias","resolvedText"}}) {
        document->Send(SCI_SETTEXT,0,reinterpret_cast<LPARAM>(typedText));
        document->Send(SCI_GOTOPOS,document->Send(SCI_GETLENGTH));SetFocus(document->hwnd);
        command(L"Complete");require(pump([&]{return document->Send(SCI_AUTOCACTIVE)!=0;}));
        document->Send(SCI_AUTOCCOMPLETE);require(document->Text()==expected);
        document->Send(SCI_UNDO);require(document->Text()==typedText);
    }
    for(const auto& [setting,key] : {std::pair{IDC_AUTOC_FILLUP_ENTER,VK_RETURN},std::pair{IDC_AUTOC_FILLUP_TAB,VK_TAB}}) {
        document->style.completionOptions[setting]=0;
        document->Send(SCI_SETTEXT,0,reinterpret_cast<LPARAM>("he"));document->Send(SCI_GOTOPOS,2);SetFocus(document->hwnd);
        command(L"Complete");require(pump([&]{return document->Send(SCI_AUTOCACTIVE)!=0;}));
        document->Send(WM_KEYDOWN,key);
        require(!document->Send(SCI_AUTOCACTIVE));
        const auto edited=document->Text();
        require(edited.starts_with("he") && edited.size()>2 && edited.find("hello")==std::string::npos);
        require(edited[2]=='\r' || edited[2]=='\n' || edited[2]=='\t' || edited[2]==' ');
        document->style.completionOptions[setting]=1;
    }
    document->Send(SCI_SETTEXT,0,reinterpret_cast<LPARAM>("he"));document->Send(SCI_GOTOPOS,2);SetFocus(document->hwnd);
    command(L"Complete");
    document->Send(WM_IME_STARTCOMPOSITION);
    require(document->completionComposing);
    SCNotification tentative{};tentative.nmhdr.hwndFrom=document->hwnd;tentative.nmhdr.code=SCN_CHARADDED;
    tentative.ch='e';tentative.characterSource=SC_CHARACTERSOURCE_TENTATIVE_INPUT;
    frame.plugins.ScintillaNotification(tentative);
    command(L"Complete");
    const auto imeDeadline=GetTickCount64()+400;
    require(pump([&]{return GetTickCount64()>=imeDeadline;}));
    require(!document->Send(SCI_AUTOCACTIVE));
    document->Send(WM_IME_ENDCOMPOSITION);
    require(!document->completionComposing);
    command(L"Complete");require(pump([&]{return document->Send(SCI_AUTOCACTIVE)!=0;}));
    document->Send(SCI_AUTOCCANCEL);
    const std::string positioned = std::string(5, '\n') + std::string(22, ' ') + "hello(42) symbol";
    document->Send(SCI_SETTEXT,0,reinterpret_cast<LPARAM>(positioned.c_str()));
    const auto signaturePosition=static_cast<LRESULT>(positioned.find("42"));
    document->Send(SCI_GOTOPOS,signaturePosition);SetFocus(document->hwnd);
    auto tipWindow=[]() {
        HWND result=nullptr;
        EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM data)->BOOL {
            wchar_t type[64]{};GetClassNameW(window,type,_countof(type));
            if(IsWindowVisible(window) && wcscmp(type,L"CallTip")==0)*reinterpret_cast<HWND*>(data)=window;
            return TRUE;
        },reinterpret_cast<LPARAM>(&result));
        return result;
    };
    auto checkPosition=[&](LRESULT anchor) {
        auto window=tipWindow();require(window!=nullptr);
        POINT point{static_cast<LONG>(document->Send(SCI_POINTXFROMPOSITION,0,anchor)),static_cast<LONG>(document->Send(SCI_POINTYFROMPOSITION,0,anchor))};
        ClientToScreen(document->hwnd,&point);
        RECT rect{};GetWindowRect(window,&rect);
        const auto height=document->Send(SCI_TEXTHEIGHT,document->Send(SCI_LINEFROMPOSITION,anchor));
        require(std::abs(rect.left-point.x)<120);
        require((rect.top>=point.y+height && rect.top<point.y+height*2+10) || (rect.bottom<=point.y && rect.bottom>point.y-height));
    };
    command(L"Signature Help");
    require(pump([&]{return document->Send(SCI_CALLTIPACTIVE)!=0;}));
    checkPosition(signaturePosition);
    CaptureQAWindow(frame,L"LSP-signature");capturePopup(L"CallTip",L"LSP-signature-popup");
    SCNotification click{};click.nmhdr.hwndFrom=document->hwnd;click.nmhdr.code=SCN_CALLTIPCLICK;click.position=2;
    require(frame.plugins.ScintillaNotification(click));checkPosition(signaturePosition);
    capturePopup(L"CallTip",L"LSP-overload-popup");document->Send(SCI_CALLTIPCANCEL);
    // Hover must follow the requested symbol even when the caret is at file start.
    document->Send(SCI_GOTOPOS,0);SetFocus(document->hwnd);
    SCNotification dwell{};dwell.nmhdr.hwndFrom=document->hwnd;dwell.nmhdr.code=SCN_DWELLSTART;
    dwell.position=static_cast<LRESULT>(positioned.find("symbol"));
    frame.plugins.ScintillaNotification(dwell);
    require(pump([&]{return document->Send(SCI_CALLTIPACTIVE)!=0;}));
    checkPosition(dwell.position);
    CaptureQAWindow(frame,L"LSP-hover-anchor");
    capturePopup(L"CallTip",L"LSP-hover-position");
    document->Send(SCI_CALLTIPCANCEL);
    dwell.position=signaturePosition;
    frame.plugins.ScintillaNotification(dwell);
    require(pump([&]{return document->Send(SCI_CALLTIPACTIVE)!=0;}));
    checkPosition(dwell.position);
    document->Send(SCI_CALLTIPCANCEL);
    document->Send(SCI_SETTEXT,0,reinterpret_cast<LPARAM>("he"));document->Send(SCI_GOTOPOS,2);
    command(L"Go to Definition");require(pump([&]{return document->Send(SCI_GETCURRENTPOS)==0;}));
    document->Send(SCI_GOTOPOS,2);SetFocus(document->hwnd);command(L"Complete");
    // A delayed response for the first document must not affect the second.
    require(frame.Open(second));auto* other=frame.Active();SetFocus(other->hwnd);
    require(pump([&]{return other->externalCompletion;}));
    const auto end=GetTickCount64()+250;
    pump([&]{return GetTickCount64()>=end;});
    require(!other->Send(SCI_AUTOCACTIVE) && other->Text()=="other");
    require(document->externalCompletion && other->externalCompletion);
    SCNotification inactiveDwell{};inactiveDwell.nmhdr.hwndFrom=document->hwnd;
    inactiveDwell.nmhdr.code=SCN_DWELLSTART;inactiveDwell.position=0;
    frame.plugins.ScintillaNotification(inactiveDwell);
    const auto hoverDeadline=GetTickCount64()+250;
    require(pump([&]{return GetTickCount64()>=hoverDeadline;}));
    require(!other->Send(SCI_CALLTIPACTIVE));
    const auto clientLog=files::ReadWithEncoding(directory/L"LSPClient-client.log",files::Encoding::Utf8).utf8;
    require(clientLog.find("Ignored diagnostic:")!=std::string::npos);
    require(clientLog.find("Ignored LSP message window/logMessage")!=std::string::npos);
    const auto revision=document->style.revision;
    document->Send(SCI_APPENDTEXT,1,reinterpret_cast<LPARAM>("!"));
    require(document->style.revision>revision);
    other->Send(SCI_SETTEXT,0,reinterpret_cast<LPARAM>("exit-server"));
    require(pump([&]{return !other->externalCompletion && !document->externalCompletion;}));
    require(frame.Save(*document));
    require(frame.Save(*other));
    require(files::ReadWithEncoding(second,files::Encoding::Utf8).utf8=="exit-server");
    command(L"Stop");require(!document->externalCompletion && !other->externalCompletion);
    require(!document->Send(SCI_AUTOCACTIVE));
    settings["servers"][0]["initializationOptions"]={{"diagnosticsOnly",true}};
    files::WriteAtomic(directory/L"LSPClient.json",{settings.dump()});
    command(L"Restart All LSP Servers");
    require(pump([&]{return other->Send(SCI_INDICATORVALUEAT,frame.plugins.nextIndicator-1,0)!=0;}));
    require(!other->externalCompletion);
    SetFocus(other->hwnd);other->Send(SCI_GOTOPOS,other->Send(SCI_GETLENGTH));
    other->Send(WM_CHAR,'2');command(L"Complete");
    const auto diagnosticDeadline=GetTickCount64()+400;
    require(pump([&]{return GetTickCount64()>=diagnosticDeadline;}));
    require(!other->externalCompletion);
    const auto diagnosticLog=files::ReadWithEncoding(directory/L"LSPClient-client.log",files::Encoding::Utf8).utf8;
    require(diagnosticLog.find("0x00000026")==std::string::npos);
    command(L"Stop");
    document->Send(SCI_SETSAVEPOINT);other->Send(SCI_SETSAVEPOINT);
}
