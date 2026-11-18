#pragma once
#include "../src/EditorDialogs.h"
#include "../src/StyleManager.h"
#include "../src/resource.h"
#include <functional>
#include "QAArtifacts.h"
#include <stdexcept>
#include <sstream>
// Modal dialogs run on the UI thread, so drive only this process's known owned
// dialog from its nested message loop; no desktop/global input is synthesized.
namespace StyleDialogTest {
inline thread_local HWND owner=nullptr;
inline thread_local std::function<void(HWND)> fill;
inline thread_local bool activated=false;
inline thread_local unsigned ticks=0;
inline thread_local UINT finishCommand=IDOK;
inline thread_local std::exception_ptr callbackError;
inline void CALLBACK Timer(HWND,UINT,UINT_PTR id,DWORD){
    HWND dialog=GetLastActivePopup(owner);wchar_t name[32]{};GetClassNameW(dialog,name,32);
    if(dialog && dialog!=owner && wcscmp(name,L"#32770")==0){
        KillTimer(nullptr,id);activated=true;
        try{if(fill)fill(dialog);CaptureQAWindow(dialog,L"style-dialog");PostMessageW(dialog,WM_COMMAND,finishCommand,0);}
        catch(...){callbackError=std::current_exception();PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);}
    }else if(++ticks>100){KillTimer(nullptr,id);PostMessageW(owner,WM_CANCELMODE,0,0);}
}
inline void Run(HWND frame,EditorDocument& d,UINT resource,std::function<void(HWND)> callback){
    owner=frame;fill=std::move(callback);activated=false;ticks=0;callbackError=nullptr;
    UINT_PTR timer=SetTimer(nullptr,0,10,Timer);if(!timer)throw std::runtime_error("Cannot create dialog test timer");
    auto result=ShowEditorDialog(frame,d,resource);KillTimer(nullptr,timer);fill={};owner=nullptr;
    if(callbackError)std::rethrow_exception(callbackError);
    if(!activated||result!=IDOK)throw std::runtime_error("Style dialog could not apply resource "+std::to_string(resource));
}
inline void RunCommand(HWND frame,UINT command,std::function<void(HWND)> callback,UINT finish=IDOK){
    owner=frame;fill=std::move(callback);activated=false;ticks=0;callbackError=nullptr;finishCommand=finish;
    UINT_PTR timer=SetTimer(nullptr,0,10,Timer);if(!timer)throw std::runtime_error("Cannot create scheme test timer");
    SendMessageW(frame,WM_COMMAND,command,0);KillTimer(nullptr,timer);fill={};owner=nullptr;finishCommand=IDOK;
    if(callbackError)std::rethrow_exception(callbackError);
    if(!activated)throw std::runtime_error("Dialog did not open for command "+std::to_string(command));
}
}
inline void RunStyleDialogTests(HWND owner,EditorDocument& d){
    auto require=[](bool ok){if(!ok)throw std::runtime_error("Style dialog setting did not apply");};
    using StyleDialogTest::Run;
    Run(owner,d,IDD_WORDWRAP,[](HWND dialog){
        SendDlgItemMessageW(dialog,IDC_WRAP_MODE,CB_SETCURSEL,SC_WRAP_CHAR,0);
        SendDlgItemMessageW(dialog,IDC_WRAP_INDENT,CB_SETCURSEL,5,0);
        CheckDlgButton(dialog,IDC_WRAP_HIGHLIGHT_SUBLINE,BST_CHECKED);
        SendDlgItemMessageW(dialog,IDC_WRAP_SYMBOL_BEFORE,CB_SETCURSEL,2,0);SendDlgItemMessageW(dialog,IDC_WRAP_SYMBOL_AFTER,CB_SETCURSEL,3,0);
    });
    require(d.Send(SCI_GETWRAPMODE)==SC_WRAP_CHAR && d.Send(SCI_GETWRAPINDENTMODE)==SC_WRAPINDENT_SAME);
        require(d.Send(SCI_GETCARETLINEHIGHLIGHTSUBLINE)!=0);
    require(d.Send(SCI_GETWRAPVISUALFLAGS)==(SC_WRAPVISUALFLAG_END|SC_WRAPVISUALFLAG_MARGIN));
    require(d.Send(SCI_GETWRAPVISUALFLAGSLOCATION)==SC_WRAPVISUALFLAGLOC_DEFAULT);
    ExecuteStyleCommand(owner,d,IDM_VIEW_WORDWRAP);require(d.Send(SCI_GETWRAPMODE)==SC_WRAP_NONE);ExecuteStyleCommand(owner,d,IDM_VIEW_WORDWRAP);require(d.Send(SCI_GETWRAPMODE)==SC_WRAP_CHAR);
    Run(owner,d,IDD_TABSETTINGS,[](HWND dialog){
        CheckDlgButton(dialog,IDC_FILE_USE_SCHEME_TAB,BST_UNCHECKED);
        SetDlgItemInt(dialog,IDC_FILE_TAB_WIDTH,7,FALSE);SetDlgItemInt(dialog,IDC_FILE_INDENT_WIDTH,3,FALSE);
        CheckDlgButton(dialog,IDC_FILE_TAB_AS_SPACE,BST_CHECKED);
        CheckDlgButton(dialog,IDC_BACKSPACE_UNINDENT,BST_UNCHECKED);CheckDlgButton(dialog,IDC_BACKSPACE_SMARTDEL,BST_CHECKED);
    });
    require(d.Send(SCI_GETTABWIDTH)==7 && d.Send(SCI_GETINDENT)==3 && !d.Send(SCI_GETUSETABS) && d.Send(SCI_GETBACKSPACEUNINDENTS)==2);
    Run(owner,d,IDD_ZOOMLEVEL,[](HWND dialog){SetDlgItemInt(dialog,IDC_ZOOMLEVEL,175,FALSE);});
    require(d.Send(SCI_GETZOOM)==175);
    Run(owner,d,IDD_LONGLINES,[](HWND dialog){
        SetDlgItemInt(dialog,IDC_LONGLINE_LIMIT,120,FALSE);
        CheckRadioButton(dialog,IDC_LONGLINE_EDGE_LINE,IDC_LONGLINE_BACK_COLOR,IDC_LONGLINE_BACK_COLOR);
    });
    require(d.Send(SCI_GETEDGECOLUMN)==120 && d.edgeMode==EDGE_BACKGROUND && d.Send(SCI_GETEDGEMODE)==EDGE_BACKGROUND && d.style.longLineEnabled);
    Run(owner,d,IDD_AUTOCOMPLETION,[](HWND dialog){
        CheckDlgButton(dialog,IDC_AUTO_COMPLETE_WORD,BST_UNCHECKED);
        CheckDlgButton(dialog,IDC_AUTO_INDENT_TEXT,BST_UNCHECKED);
        SetDlgItemInt(dialog,IDC_AUTOC_VISIBLE_ITEM_COUNT,8,FALSE);SetDlgItemInt(dialog,IDC_AUTOC_MIN_NUMBER_LENGTH,0,FALSE);
    });
    require(!d.style.completion && !d.style.autoIndent && d.Send(SCI_AUTOCGETMAXHEIGHT)==8 && d.style.completionOptions[IDC_AUTOC_MIN_NUMBER_LENGTH]==0);
    Run(owner,d,IDD_CSV_OPTIONS,[](HWND dialog){
        CheckRadioButton(dialog,IDC_CSV_DELIMITER_COMMA,IDC_CSV_DELIMITER_OTHER,IDC_CSV_DELIMITER_TAB);
        CheckRadioButton(dialog,IDC_CSV_QUALIFIER_DOUBLE,IDC_CSV_QUALIFIER_NONE,IDC_CSV_QUALIFIER_SINGLE);
        CheckDlgButton(dialog,IDC_CSV_MERGE_DELIMITER,BST_CHECKED);
    });
    require((d.style.csvOptions&255)=='\t' && ((d.style.csvOptions>>8)&127)=='\'');
    require(d.style.csvOptions&(1<<16));
    using StyleDialogTest::RunCommand;
    auto oldCodeFont=d.style.codeFont,oldTextFont=d.style.textFont;auto oldCodeSize=d.style.codeFontSize,oldTextSize=d.style.textFontSize;
    RunCommand(owner,IDM_VIEW_DEFAULT_CODE_FONT,[](HWND){},IDCANCEL);
    require(d.style.codeFont==oldCodeFont && d.style.codeFontSize==oldCodeSize);
    RunCommand(owner,IDM_VIEW_DEFAULT_TEXT_FONT,[](HWND){},IDCANCEL);
    require(d.style.textFont==oldTextFont && d.style.textFontSize==oldTextSize);
    auto oldScheme=d.style.scheme;
    auto choosePython=[](HWND dialog){CTreeViewCtrl tree(GetDlgItem(dialog,IDC_STYLELIST));for(auto item=tree.GetRootItem();item;item=tree.GetNextSiblingItem(item)){wchar_t name[128]{};tree.GetItemText(item,name,128);if(wcscmp(name,L"Python Script")==0){tree.SelectItem(item);return;}}};
    RunCommand(owner,IDM_VIEW_SCHEME,choosePython,IDCANCEL);require(d.style.scheme==oldScheme);
    RunCommand(owner,IDM_VIEW_SCHEME,choosePython);require(d.Send(SCI_GETLEXER)==SCLEX_PYTHON);
    RunCommand(owner,IDM_VIEW_SCHEME_FAVORITE,[](HWND dialog){
        CTreeViewCtrl tree(GetDlgItem(dialog,IDC_STYLELIST));HTREEITEM selected=nullptr;
        for(auto item=tree.GetRootItem();item;item=tree.GetNextSiblingItem(item)){wchar_t name[128]{};tree.GetItemText(item,name,128);bool keep=wcscmp(name,L"Python Script")==0 || wcscmp(name,L"C/C++ Source")==0;tree.SetCheckState(item,keep);if(wcscmp(name,L"Python Script")==0)selected=item;}
        tree.SelectItem(selected);
        for(unsigned count=0;count<100 && tree.GetSelectedItem()!=tree.GetRootItem();++count)SendMessageW(dialog,WM_COMMAND,IDC_DEFAULTSCHEME,0);
    });
    auto profile=std::filesystem::temp_directory_path()/(L"MultiPad4-favorite-test-"+std::to_wstring(GetCurrentProcessId()))/L"Settings.ini";SaveStylePreferences(profile);
    wchar_t order[1024]{};GetPrivateProfileStringW(L"Style Preferences",L"Favorites",L"",order,1024,profile.c_str());std::wistringstream entries(order);int first=-1,second=-1;entries>>first>>second;
    require(first>0 && second>0 && first!=second);
    RunCommand(owner,IDM_VIEW_SCHEME,[&](HWND dialog){CTreeViewCtrl tree(GetDlgItem(dialog,IDC_STYLELIST));wchar_t name[128]{};tree.GetItemText(tree.GetRootItem(),name,128);require(wcscmp(name,L"Python Script")==0);},IDCANCEL);
    std::error_code cleanup;std::filesystem::remove_all(profile.parent_path(),cleanup);
}
