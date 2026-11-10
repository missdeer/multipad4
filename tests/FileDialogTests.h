#pragma once
#include "../src/FileDialogs.h"
#include "../src/Printing.h"
#include "../src/FileCommands.h"
#include "../src/MainFrame.h"
#include <functional>
#include <cstdio>
#include <source_location>
#include "QAArtifacts.h"
namespace filedialogtest {
inline thread_local std::function<void(HWND)> action;
inline thread_local std::exception_ptr failure;
inline thread_local HWND owner=nullptr;
inline thread_local HWND active=nullptr;
inline thread_local unsigned ticks=0;
inline void CALLBACK Timer(HWND,UINT,UINT_PTR id,DWORD){
    ++ticks;HWND dialog=GetLastActivePopup(owner);wchar_t type[32]{};GetClassNameW(dialog,type,32);
    // Shell calls in WM_INITDIALOG may dispatch timers before setup finishes.
    if(action && dialog && dialog!=owner && wcscmp(type,L"#32770")==0 && IsWindowVisible(dialog)){
        wchar_t title[256]{};GetWindowTextW(dialog,title,256);fprintf(stderr,"FileDialog activate: %ls\n",title);fflush(stderr);
        active=dialog;auto callback=std::move(action);action={};
        try{callback(dialog);CaptureQAWindow(dialog,title);}catch(...){failure=std::current_exception();PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);}
    }
    if(ticks>200){
        KillTimer(nullptr,id);failure=std::make_exception_ptr(std::runtime_error("File dialog failed to close after action."));
        if(dialog && dialog!=owner && wcscmp(type,L"#32770")==0){PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);PostMessageW(dialog,WM_CLOSE,0,0);}
        if(active && IsWindow(active))PostMessageW(active,WM_COMMAND,IDCANCEL,0);
    }
}
inline void Run(const std::function<void(HWND)>& callback,const std::function<void()>& show) {
    failure=nullptr;action=callback;active=nullptr;ticks=0;
    auto timer=SetTimer(nullptr,0,10,Timer);if(!timer)throw std::runtime_error("Cannot install file dialog regression timer.");
    try{show();}catch(...){KillTimer(nullptr,timer);action={};throw;}
    KillTimer(nullptr,timer);action={};if(failure)std::rethrow_exception(failure);
}
inline INT_PTR CALLBACK PrintTemplate(HWND dialog,UINT message,WPARAM w,LPARAM l) {
    PrintSetupHook(dialog,message,w,l);
    if(message==WM_INITDIALOG)return TRUE;
    if(message==WM_COMMAND&&(LOWORD(w)==IDOK||LOWORD(w)==IDCANCEL)){EndDialog(dialog,LOWORD(w));return TRUE;}
    return FALSE;
}
}
inline void RunFileDialogTests(MainFrame& frame) {
    filedialogtest::owner=frame.m_hWnd;
    fprintf(stderr,"FileDialog tests begin\n");fflush(stderr);
    auto require=[](bool ok,std::source_location where=std::source_location::current()){if(!ok)throw std::runtime_error("File dialog regression failed at line "+std::to_string(where.line()));};
    filedialogtest::Run([](HWND dialog){
        CheckDlgButton(dialog,IDC_NOUNICODEDETECTION,BST_CHECKED);
        CheckDlgButton(dialog,IDC_ENCODINGFROMFILEVARS,BST_CHECKED);
        PostMessageW(dialog,WM_COMMAND,IDOK,0);
    },[&]{ConfigureDefaultEncoding(frame);});
    require(frame.settings.ReadString(L"File Settings",L"SkipUnicodeDetection")==L"1");
    require(frame.settings.ReadString(L"File Settings",L"IgnoreEncodingTags")==L"1");
    filedialogtest::Run([](HWND dialog){
        CheckDlgButton(dialog,IDC_NOUNICODEDETECTION,BST_UNCHECKED);
        PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);
    },[&]{ConfigureDefaultEncoding(frame);});
    require(frame.settings.ReadString(L"File Settings",L"SkipUnicodeDetection")==L"1");
    frame.settings.WriteString(L"File Settings",L"SkipUnicodeDetection",L"0");
    frame.settings.WriteString(L"File Settings",L"IgnoreEncodingTags",L"0");
    auto previousEol=frame.settings.Get(IDM_LINEENDINGS_SETDEFAULT,SC_EOL_CRLF);
    filedialogtest::Run([](HWND dialog){
        SendDlgItemMessageW(dialog,IDC_EOLMODELIST,CB_SETCURSEL,SC_EOL_LF,0);
        CheckDlgButton(dialog,IDC_AUTOSTRIPBLANKS,BST_CHECKED);
        PostMessageW(dialog,WM_COMMAND,IDOK,0);
    },[&]{ConfigureDefaultLineEndings(frame);});
    require(frame.settings.Get(IDM_LINEENDINGS_SETDEFAULT)==SC_EOL_LF);
    require(frame.settings.ReadString(L"File Settings",L"StripTrailingBlanks")==L"1");
    filedialogtest::Run([](HWND dialog){SendDlgItemMessageW(dialog,IDC_EOLMODELIST,CB_SETCURSEL,SC_EOL_CR,0);PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);},[&]{ConfigureDefaultLineEndings(frame);});
    require(frame.settings.Get(IDM_LINEENDINGS_SETDEFAULT)==SC_EOL_LF);
    frame.settings.Set(IDM_LINEENDINGS_SETDEFAULT,previousEol);
    frame.settings.WriteString(L"File Settings",L"StripTrailingBlanks",L"0");
    files::Encoding selected=files::Encoding::Ansi;
    filedialogtest::Run([](HWND dialog){
        HWND tree=GetDlgItem(dialog,IDC_ENCODINGLIST);
        std::function<HTREEITEM(HTREEITEM)> find=[&](HTREEITEM node)->HTREEITEM{for(;node;node=TreeView_GetNextSibling(tree,node)){TVITEMW item{};item.hItem=node;item.mask=TVIF_PARAM;TreeView_GetItem(tree,&item);if(item.lParam==static_cast<LPARAM>(files::Encoding::Utf16BE))return node;if(auto child=find(TreeView_GetChild(tree,node)))return child;}return nullptr;};
        auto selected=find(TreeView_GetRoot(tree));if(!selected)throw std::runtime_error("UTF16BE encoding leaf is missing.");TreeView_SelectItem(tree,selected);
        PostMessageW(dialog,WM_COMMAND,IDOK,0);
    },[&]{require(SelectFileEncoding(frame.m_hWnd,selected));});
    require(selected==files::Encoding::Utf16BE);
    filedialogtest::Run([](HWND dialog){PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);},[&]{require(!SelectFileEncoding(frame.m_hWnd,selected));});
    require(selected==files::Encoding::Utf16BE);
    auto oldCommand=frame.settings.ReadString(L"Run",L"Command");
    filedialogtest::Run([&](HWND dialog){
        BUTTON_IMAGELIST image{};
        require(SendDlgItemMessageW(dialog,IDC_SEARCHEXE,BCM_GETIMAGELIST,0,reinterpret_cast<LPARAM>(&image)) && image.himl && ImageList_GetImageCount(image.himl)==1);
        SetDlgItemTextW(dialog,IDC_COMMANDLINE,L"should-not-execute.exe");PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);
    },[&]{require(!RunFileDialog(frame));});
    require(frame.settings.ReadString(L"Run",L"Command")==oldCommand);
    filedialogtest::Run([&](HWND dialog){
        require(SendDlgItemMessageW(dialog,IDC_PAGESETUP_HEADER_LIST,CB_GETCOUNT,0,0)==4);
        require(SendDlgItemMessageW(dialog,IDC_PAGESETUP_FOOTER_LIST,CB_GETCOUNT,0,0)==2);
        SetDlgItemInt(dialog,IDC_PAGESETUP_ZOOMLEVEL,175,FALSE);
        SendDlgItemMessageW(dialog,IDC_PAGESETUP_HEADER_LIST,CB_SETCURSEL,3,0);
        PostMessageW(dialog,WM_COMMAND,IDOK,0);
    },[&]{require(DialogBoxParamW(_Module.GetModuleInstance(),MAKEINTRESOURCEW(IDD_PAGESETUP),frame.m_hWnd,filedialogtest::PrintTemplate,0)==IDOK);});
    require(CurrentPrintPreferences().zoom==175 && CurrentPrintPreferences().header==3);
    filedialogtest::Run([](HWND dialog){SetDlgItemInt(dialog,IDC_PAGESETUP_ZOOMLEVEL,999,FALSE);PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);},[&]{require(DialogBoxParamW(_Module.GetModuleInstance(),MAKEINTRESOURCEW(IDD_PAGESETUP),frame.m_hWnd,filedialogtest::PrintTemplate,0)==IDCANCEL);});
    require(CurrentPrintPreferences().zoom==175);
    auto* conversion=frame.NewDocument();require(conversion!=nullptr);conversion->Load({"中文 😀 café"});
    auto conversionText=conversion->Text();
    filedialogtest::Run([](HWND dialog){PostMessageW(dialog,WM_COMMAND,IDNO,0);},[&]{require(!SelectDocumentEncoding(frame,*conversion,files::Encoding::Ansi));});
    require(conversion->Text()==conversionText && conversion->encoding==files::Encoding::Utf8);
    auto ansiText=files::Decode(files::EncodeLossy({conversionText,files::Encoding::Ansi}),files::Encoding::Ansi).utf8;
    filedialogtest::Run([](HWND dialog){PostMessageW(dialog,WM_COMMAND,IDYES,0);},[&]{require(SelectDocumentEncoding(frame,*conversion,files::Encoding::Ansi));});
    require(conversion->Text()==ansiText && conversion->encoding==files::Encoding::Ansi);
    if(ansiText!=conversionText){conversion->Send(SCI_UNDO);require(conversion->Text()==conversionText);}
    conversion->Load({conversionText});conversion->SetEncoding(files::FromCodePage(1252));files::Text convertedOutput;
    filedialogtest::Run([](HWND dialog){PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);},[&]{require(!PrepareEncodingSave(frame,*conversion,convertedOutput));});
    require(conversion->Text()==conversionText);
    conversion->Send(SCI_SETREADONLY,TRUE);
    filedialogtest::Run([](HWND dialog){PostMessageW(dialog,WM_COMMAND,IDOK,0);},[&]{require(!PrepareEncodingSave(frame,*conversion,convertedOutput));});
    require(conversion->Text()==conversionText&&conversion->Send(SCI_GETREADONLY));
    filedialogtest::Run([](HWND dialog){PostMessageW(dialog,WM_COMMAND,IDOK,0);},[&]{require(PrepareEncodingSave(frame,*conversion,convertedOutput,true));});
    require(conversion->Text()==conversionText && convertedOutput.utf8!=conversionText&&conversion->Send(SCI_GETREADONLY));conversion->Send(SCI_SETREADONLY,FALSE);
    filedialogtest::Run([](HWND dialog){PostMessageW(dialog,WM_COMMAND,IDOK,0);},[&]{require(PrepareEncodingSave(frame,*conversion,convertedOutput));});
    require(conversion->Text()==convertedOutput.utf8);conversion->Send(SCI_UNDO);require(conversion->Text()==conversionText);
    conversion->Load({"text"});conversion->Send(SCI_CLEARALL);
    filedialogtest::Run([](HWND dialog){PostMessageW(dialog,WM_COMMAND,IDNO,0);},[&]{require(!SelectDocumentEncoding(frame,*conversion,files::Encoding::Ansi));});
    require(conversion->encoding==files::Encoding::Utf8 && conversion->Text().empty());
    conversion->encodingDirty=false;conversion->Send(SCI_SETSAVEPOINT);require(frame.Close(frame.tabs.GetActivePage()));
    auto directory=std::filesystem::temp_directory_path()/(L"MultiPad4-filedialogs-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    auto oldFavorites=frame.settings.ReadString(L"Files",L"FavoritesDirectory");auto oldOpenWith=frame.settings.ReadString(L"Files",L"OpenWithDirectory");
    try {
        frame.settings.WriteString(L"Files",L"FavoritesDirectory",(directory/L"Favorites").wstring());
        frame.settings.WriteString(L"Files",L"OpenWithDirectory",(directory/L"OpenWith").wstring());
        frame.NewDocument();auto* document=frame.Active();document->path=directory/L"target.txt";document->Load({"target"});require(frame.Save(*document));
        document->Load({"a\r\nb\nc\r"});
        frame.settings.WriteString(L"File Settings",L"WarnInconsistentEOL",L"1");
        filedialogtest::Run([](HWND dialog){SendDlgItemMessageW(dialog,IDC_EOLMODELIST,CB_SETCURSEL,SC_EOL_LF,0);PostMessageW(dialog,WM_COMMAND,IDOK,0);},[&]{InitializeFileDocument(frame,*document);});
        require(document->Text()=="a\nb\nc\n" && document->Send(SCI_GETEOLMODE)==SC_EOL_LF);
        document->Load({"a\r\nb\nc\r"});
        filedialogtest::Run([](HWND dialog){CheckDlgButton(dialog,IDC_WARNINCONSISTENTEOLS,BST_UNCHECKED);PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);},[&]{InitializeFileDocument(frame,*document);});
        require(document->Text()=="a\r\nb\nc\r" && frame.settings.ReadString(L"File Settings",L"WarnInconsistentEOL")==L"0");
        document->Send(SCI_SETREADONLY,TRUE);frame.settings.WriteString(L"File Settings",L"WarnInconsistentEOL",L"1");
        filedialogtest::Run([&](HWND dialog){require(!IsWindowEnabled(GetDlgItem(dialog,IDOK)));SendDlgItemMessageW(dialog,IDC_EOLMODELIST,CB_SETCURSEL,SC_EOL_LF,0);PostMessageW(dialog,WM_COMMAND,IDOK,0);PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);},[&]{InitializeFileDocument(frame,*document);});
        require(document->Text()=="a\r\nb\nc\r"&&document->Send(SCI_GETREADONLY));document->Send(SCI_SETREADONLY,FALSE);frame.settings.WriteString(L"File Settings",L"WarnInconsistentEOL",L"0");
        document->Load({"target"});require(frame.Save(*document));
        auto previousRecentCount=frame.settings.Get(IDM_FILE_RECENT,32);
        RecordOpenedFile(frame,document->path);
        filedialogtest::Run([&](HWND dialog){NMHDR notification{GetDlgItem(dialog,IDC_EMPTY_MRU),IDC_EMPTY_MRU,NM_CLICK};SendMessageW(dialog,WM_NOTIFY,IDC_EMPTY_MRU,reinterpret_cast<LPARAM>(&notification));require(ListView_GetItemCount(GetDlgItem(dialog,IDC_FILEMRU))==1);PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);},[&]{require(!SelectHistoryFile(frame,false));});
        require(frame.settings.ReadString(L"Recent Files",L"0")==document->path.wstring()&&frame.settings.ReadString(L"Recent Files",L"1").empty());
        filedialogtest::Run([](HWND dialog){PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);},[&]{require(!SelectHistoryFile(frame,false));});
        filedialogtest::Run([](HWND dialog){HWND list=GetDlgItem(dialog,IDC_FILEMRU);ListView_SetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);SetDlgItemInt(dialog,IDC_MRU_COUNT_VALUE,100,FALSE);PostMessageW(dialog,WM_COMMAND,IDOK,0);},[&]{require(SelectHistoryFile(frame,false));});
        fprintf(stderr,"Recent regression: active=%d maximum=%d remember=%d\n",frame.Active()==document,frame.settings.Get(IDM_FILE_RECENT),frame.settings.Get(IDM_VIEW_NOSAVERECENT));fflush(stderr);
        require(frame.Active()==document && frame.settings.Get(IDM_FILE_RECENT)==100);
        frame.settings.Set(IDM_FILE_RECENT,previousRecentCount);
        filedialogtest::Run([](HWND dialog){SetDlgItemTextW(dialog,IDC_FAVORITESFILE,L"should-not-save");PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);},[&]{require(!AddFavoriteFile(frame));});
        require(!std::filesystem::exists(directory/L"Favorites"/L"should-not-save.lnk"));
        filedialogtest::Run([](HWND dialog){SetDlgItemTextW(dialog,IDC_FAVORITESFILE,L"Regression Favorite");PostMessageW(dialog,WM_COMMAND,IDOK,0);},[&]{require(AddFavoriteFile(frame));});
        require(std::filesystem::exists(directory/L"Favorites"/L"Regression Favorite.lnk"));
        filedialogtest::Run([](HWND dialog){PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);},[&]{require(!SelectHistoryFile(frame,true));});
        filedialogtest::Run([](HWND dialog){PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);},[&]{require(!OpenWithFileDialog(frame));});
        auto count=frame.tabs.GetPageCount();
        filedialogtest::Run([](HWND dialog){HWND list=GetDlgItem(dialog,IDC_FAVORITESDIR);ListView_SetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);PostMessageW(dialog,WM_COMMAND,IDOK,0);},[&]{require(SelectHistoryFile(frame,true));});
        require(frame.Active()==document && frame.tabs.GetPageCount()==count);
        require(frame.Close(frame.tabs.GetActivePage()));
        frame.settings.WriteString(L"Files",L"FavoritesDirectory",oldFavorites);frame.settings.WriteString(L"Files",L"OpenWithDirectory",oldOpenWith);
        std::filesystem::remove_all(directory);
    }catch(...){frame.settings.WriteString(L"Files",L"FavoritesDirectory",oldFavorites);frame.settings.WriteString(L"Files",L"OpenWithDirectory",oldOpenWith);std::error_code error;std::filesystem::remove_all(directory,error);throw;}
}
