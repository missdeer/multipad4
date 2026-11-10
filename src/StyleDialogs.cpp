#include "EditorDialogs.h"
#include "StyleManager.h"
#include "resource.h"
#include <array>
#include <climits>
#include <sstream>
namespace {
struct Context {EditorDocument& d; UINT resource;};
int Scheme(const EditorDocument& d){return d.style.appliedTabScheme>=0?d.style.appliedTabScheme:d.style.scheme;}
int globalTabWidth=4,globalIndentWidth=4;bool globalSpaces=false;
std::map<int,std::array<int,3>> schemeTabs;
std::wstring Text(HWND window,int id){auto control=GetDlgItem(window,id);int n=GetWindowTextLengthW(control);std::wstring s(n+1,L'\0');GetWindowTextW(control,s.data(),n+1);s.resize(n);return s;}
void ComboResource(HWND window,int id,UINT resource,int selection){
    wchar_t labels[4096]{};LoadStringW(_Module.GetModuleInstance(),resource,labels,4096);std::wistringstream stream(labels);std::wstring item;
    while(std::getline(stream,item,L'|'))if(!item.empty())SendDlgItemMessageW(window,id,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item.c_str()));
    SendDlgItemMessageW(window,id,CB_SETCURSEL,selection,0);
}
int Selected(HWND window,int id){return static_cast<int>(SendDlgItemMessageW(window,id,CB_GETCURSEL,0,0));}
void Check(HWND window,int id,bool yes){CheckDlgButton(window,id,yes?BST_CHECKED:BST_UNCHECKED);}
bool Checked(HWND window,int id){return IsDlgButtonChecked(window,id)==BST_CHECKED;}
constexpr int autoChecks[]={IDC_AUTO_COMPLETE_WORD,IDC_AUTOC_SCAN_DOCUMENT_WORDS,IDC_AUTOC_ONLY_DOCUMENT_WORDS,IDC_AUTOC_ENGLISH_IME_ONLY,IDC_AUTO_COMPLETE_INSIDE_COMMONT,IDC_AUTO_COMPLETE_INSIDE_STRING,IDC_AUTO_COMPLETE_INSIDE_PLAINTEXT,IDC_AUTOC_FILLUP_ENTER,IDC_AUTOC_FILLUP_TAB,IDC_AUTOC_FILLUP_SPACE,IDC_AUTOC_FILLUP_PUNCTUATION,IDC_AUTO_INDENT_TEXT,IDC_AUTO_CLOSE_TAGS,IDC_AUTO_INSERT_SPACE_COMMA,IDC_AUTO_INSERT_SPACE_COMMENT,IDC_AUTO_INSERT_PARENTHESIS,IDC_AUTO_INSERT_BRACE,IDC_AUTO_INSERT_SQUARE_BRACKET,IDC_AUTO_INSERT_ANGLE_BRACKET,IDC_AUTO_INSERT_DOUBLE_QUOTE,IDC_AUTO_INSERT_SINGLE_QUOTE,IDC_AUTO_INSERT_BACKTICK,IDC_SCAN_WORD_INSIDE_COMMONT,IDC_SCAN_WORD_INSIDE_STRING,IDC_SCAN_WORD_INSIDE_PLAINTEXT};
constexpr int autoNumbers[]={IDC_AUTOC_VISIBLE_ITEM_COUNT,IDC_AUTOC_MIN_WORD_LENGTH,IDC_AUTOC_MIN_NUMBER_LENGTH,IDC_AUTOC_SCAN_WORDS_TIMEOUT};
INT_PTR CALLBACK Procedure(HWND window,UINT message,WPARAM w,LPARAM l){
    try {
    auto* c=reinterpret_cast<Context*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG){c=reinterpret_cast<Context*>(l);SetWindowLongPtrW(window,DWLP_USER,l);auto& d=c->d;
        if(c->resource==IDD_TABSETTINGS){
            auto scheme=schemeTabs.contains(Scheme(d))?schemeTabs[Scheme(d)]:std::array<int,3>{4,4,1};
            for(auto entry:std::initializer_list<std::pair<int,int>>{{IDC_GLOBAL_TAB_WIDTH,globalTabWidth},{IDC_GLOBAL_INDENT_WIDTH,globalIndentWidth},{IDC_SCHEME_TAB_WIDTH,scheme[0]},{IDC_SCHEME_INDENT_WIDTH,scheme[1]},{IDC_FILE_TAB_WIDTH,static_cast<int>(d.Send(SCI_GETTABWIDTH))},{IDC_FILE_INDENT_WIDTH,static_cast<int>(d.Send(SCI_GETINDENT))}})SetDlgItemInt(window,entry.first,entry.second,FALSE);
            SetDlgItemTextW(window,IDC_SCHEME_TAB_GROUPBOX,d.language.c_str());SetDlgItemTextW(window,IDC_FILE_TAB_GROUPBOX,L"Current document");
            Check(window,IDC_GLOBAL_TAB_AS_SPACE,globalSpaces);Check(window,IDC_SCHEME_TAB_AS_SPACE,scheme[2]!=0);Check(window,IDC_FILE_TAB_AS_SPACE,!d.Send(SCI_GETUSETABS));
            Check(window,IDC_SCHEME_USE_GLOBAL_TAB,!schemeTabs.contains(Scheme(d)));Check(window,IDC_FILE_USE_SCHEME_TAB,d.style.useSchemeTabs);
            Check(window,IDC_TAB_INDENT,d.Send(SCI_GETTABINDENTS)!=0);Check(window,IDC_BACKSPACE_UNINDENT,(d.Send(SCI_GETBACKSPACEUNINDENTS)&1)!=0);Check(window,IDC_BACKSPACE_SMARTDEL,(d.Send(SCI_GETBACKSPACEUNINDENTS)&2)!=0);Check(window,IDC_DETECT_INDENTATION,d.style.detectIndentation);
        }else if(c->resource==IDD_WORDWRAP){
            ComboResource(window,IDC_WRAP_INDENT,IDS_WRAP_INDENT_OPTIONS,d.style.wrapIndent);
            ComboResource(window,IDC_WRAP_SYMBOL_BEFORE,IDS_WRAP_SYMBOL_BEFORE_OPTIONS,d.Send(SCI_GETWRAPVISUALFLAGS)?d.style.wrapSymbols%10:0);
            ComboResource(window,IDC_WRAP_SYMBOL_AFTER,IDS_WRAP_SYMBOL_AFTER_OPTIONS,d.Send(SCI_GETWRAPVISUALFLAGS)?d.style.wrapSymbols/10:0);
            ComboResource(window,IDC_WRAP_MODE,IDS_WRAP_MODE_OPTIONS,static_cast<int>(d.Send(SCI_GETWRAPMODE)));
            Check(window,IDC_WRAP_SELECT_SUBLINE,(d.Send(SCI_GETMARGINOPTIONS)&SC_MARGINOPTION_SUBLINESELECT)!=0);Check(window,IDC_WRAP_HIGHLIGHT_SUBLINE,d.Send(SCI_GETCARETLINEHIGHLIGHTSUBLINE)!=0);
        }else if(c->resource==IDD_AUTOCOMPLETION){
            for(int id:autoChecks)Check(window,id,d.style.completionOptions.contains(id)?d.style.completionOptions[id]!=0:id!=IDC_AUTOC_ONLY_DOCUMENT_WORDS&&id!=IDC_AUTOC_ENGLISH_IME_ONLY);
            Check(window,IDC_AUTO_COMPLETE_WORD,d.style.completion);Check(window,IDC_AUTO_INDENT_TEXT,d.style.autoIndent);
            int defaults[]={16,1,3,500};for(unsigned i=0;i<4;++i)SetDlgItemInt(window,autoNumbers[i],d.style.completionOptions.contains(autoNumbers[i])?d.style.completionOptions[autoNumbers[i]]:defaults[i],FALSE);
            auto timeout=std::to_wstring(d.style.completionOptions.contains(IDC_AUTOC_SCAN_WORDS_TIMEOUT)?d.style.completionOptions[IDC_AUTOC_SCAN_WORDS_TIMEOUT]:500)+L" ms";SetDlgItemTextW(window,IDC_AUTOC_SCAN_WORDS_TIMEOUT,timeout.c_str());
            SetDlgItemTextW(window,IDC_AUTOC_FILLUP_PUNCTUATION_LIST,d.style.completionFillups.c_str());CheckRadioButton(window,IDC_ASM_LINE_COMMENT_SEMICOLON,IDC_ASM_LINE_COMMENT_AT,d.style.completionOptions.contains(IDC_ASM_LINE_COMMENT_SEMICOLON)?d.style.completionOptions[IDC_ASM_LINE_COMMENT_SEMICOLON]:IDC_ASM_LINE_COMMENT_SEMICOLON);
        }else if(c->resource==IDD_CSV_OPTIONS){
            int option=d.style.csvOptions,delimiter=option&127;int selected=IDC_CSV_DELIMITER_OTHER;
            for(auto entry:std::initializer_list<std::pair<int,int>>{{',',IDC_CSV_DELIMITER_COMMA},{'\t',IDC_CSV_DELIMITER_TAB},{'|',IDC_CSV_DELIMITER_PIPE},{';',IDC_CSV_DELIMITER_SEMICOLON},{' ',IDC_CSV_DELIMITER_SPACE}})if(delimiter==entry.first)selected=entry.second;
            CheckRadioButton(window,IDC_CSV_DELIMITER_COMMA,IDC_CSV_DELIMITER_OTHER,selected);wchar_t text[2]={static_cast<wchar_t>(delimiter),0};SetDlgItemTextW(window,IDC_CSV_DELIMITER_OTHER_TEXT,text);
            CheckRadioButton(window,IDC_CSV_QUALIFIER_DOUBLE,IDC_CSV_QUALIFIER_NONE,((option>>8)&127)=='"'?IDC_CSV_QUALIFIER_DOUBLE:((option>>8)&127)=='\''?IDC_CSV_QUALIFIER_SINGLE:IDC_CSV_QUALIFIER_NONE);
            Check(window,IDC_CSV_BACKSLASH_ESCAPE,(option&(1<<15))!=0);Check(window,IDC_CSV_MERGE_DELIMITER,(option&(1<<16))!=0);
        }
        CWindow(window).CenterWindow(GetParent(window));return TRUE;
    }
    if(!c)return FALSE;
    if(message==WM_CLOSE||(message==WM_COMMAND&&LOWORD(w)==IDCANCEL)){EndDialog(window,IDCANCEL);return TRUE;}
    if(message!=WM_COMMAND||LOWORD(w)!=IDOK)return FALSE;auto& d=c->d;
    if(c->resource==IDD_TABSETTINGS){
        int ids[]={IDC_GLOBAL_TAB_WIDTH,IDC_GLOBAL_INDENT_WIDTH,IDC_SCHEME_TAB_WIDTH,IDC_SCHEME_INDENT_WIDTH,IDC_FILE_TAB_WIDTH,IDC_FILE_INDENT_WIDTH};int values[6]{};
        for(unsigned i=0;i<6;++i){BOOL valid=FALSE;values[i]=GetDlgItemInt(window,ids[i],&valid,FALSE);if(!valid||values[i]>(i%2?256:256)||values[i]<(i%2?0:1)){SetFocus(GetDlgItem(window,ids[i]));MessageBeep(MB_ICONWARNING);return TRUE;}}
        globalTabWidth=values[0];globalIndentWidth=values[1];globalSpaces=Checked(window,IDC_GLOBAL_TAB_AS_SPACE);
        if(Checked(window,IDC_SCHEME_USE_GLOBAL_TAB))schemeTabs.erase(Scheme(d));else schemeTabs[Scheme(d)]={values[2],values[3],Checked(window,IDC_SCHEME_TAB_AS_SPACE)?1:0};
        d.style.useSchemeTabs=Checked(window,IDC_FILE_USE_SCHEME_TAB);d.style.tabsOverride=false;d.style.detectIndentation=Checked(window,IDC_DETECT_INDENTATION);
        auto current=d.style.useSchemeTabs?(schemeTabs.contains(Scheme(d))?schemeTabs[Scheme(d)]:std::array<int,3>{globalTabWidth,globalIndentWidth,globalSpaces?1:0}):std::array<int,3>{values[4],values[5],Checked(window,IDC_FILE_TAB_AS_SPACE)?1:0};
        d.style.tabWidth=current[0];d.style.indentWidth=current[1];d.style.tabsAsSpaces=current[2]!=0;
        d.Send(SCI_SETTABWIDTH,current[0]);d.Send(SCI_SETINDENT,current[1]);d.Send(SCI_SETUSETABS,!current[2]);d.Send(SCI_SETTABINDENTS,Checked(window,IDC_TAB_INDENT));d.style.tabIndents=Checked(window,IDC_TAB_INDENT);d.style.backspaceMask=(Checked(window,IDC_BACKSPACE_UNINDENT)?1:0)|(Checked(window,IDC_BACKSPACE_SMARTDEL)?2:0);d.style.backspaceUnindents=(d.style.backspaceMask&1)!=0;d.Send(SCI_SETBACKSPACEUNINDENTS,d.style.backspaceMask);
    }else if(c->resource==IDD_WORDWRAP){
        d.style.wrapIndent=std::clamp(Selected(window,IDC_WRAP_INDENT),0,7);
        int before=std::clamp(Selected(window,IDC_WRAP_SYMBOL_BEFORE),0,2),after=std::clamp(Selected(window,IDC_WRAP_SYMBOL_AFTER),0,3);d.style.wrapSymbolsShown=before||after;if(before||after)d.style.wrapSymbols=before+10*after;
        int mode=std::clamp(Selected(window,IDC_WRAP_MODE),0,4);if(mode)d.style.wrapMode=mode;d.Send(SCI_SETWRAPMODE,mode);ApplyDocumentWrapSettings(d);
        d.Send(SCI_SETMARGINOPTIONS,Checked(window,IDC_WRAP_SELECT_SUBLINE)?SC_MARGINOPTION_SUBLINESELECT:SC_MARGINOPTION_NONE);d.style.highlightSubline=Checked(window,IDC_WRAP_HIGHLIGHT_SUBLINE);d.Send(SCI_SETCARETLINEHIGHLIGHTSUBLINE,d.style.highlightSubline);
    }else if(c->resource==IDD_AUTOCOMPLETION){
        auto options=d.style.completionOptions;
        for(int id:autoChecks)options[id]=Checked(window,id)?1:0;
        for(int id:autoNumbers){
            auto text=Text(window,id);wchar_t* end=nullptr;long long value=wcstoll(text.c_str(),&end,10);if(end==text.c_str() || value<0 || value>INT_MAX){SetFocus(GetDlgItem(window,id));return TRUE;}
            while(*end==L' ')++end;if(*end && !(id==IDC_AUTOC_SCAN_WORDS_TIMEOUT && wcscmp(end,L"ms")==0)){SetFocus(GetDlgItem(window,id));return TRUE;}
            int minimum=id==IDC_AUTOC_VISIBLE_ITEM_COUNT?8:id==IDC_AUTOC_SCAN_WORDS_TIMEOUT?50:id==IDC_AUTOC_MIN_NUMBER_LENGTH?0:1;options[id]=std::max(static_cast<int>(value),minimum);
        }
        d.style.completionOptions=std::move(options);
        d.style.completionFillups=Text(window,IDC_AUTOC_FILLUP_PUNCTUATION_LIST);d.style.completion=Checked(window,IDC_AUTO_COMPLETE_WORD);d.style.autoIndent=Checked(window,IDC_AUTO_INDENT_TEXT);
        for(int id=IDC_ASM_LINE_COMMENT_SEMICOLON;id<=IDC_ASM_LINE_COMMENT_AT;++id)if(Checked(window,id))d.style.completionOptions[IDC_ASM_LINE_COMMENT_SEMICOLON]=id;
        d.Send(SCI_AUTOCSETMAXHEIGHT,d.style.completionOptions[IDC_AUTOC_VISIBLE_ITEM_COUNT]);
        std::string fillups;if(Checked(window,IDC_AUTOC_FILLUP_PUNCTUATION))fillups=files::ToUtf8(d.style.completionFillups);
        if(Checked(window,IDC_AUTOC_FILLUP_SPACE))fillups+=' ';if(Checked(window,IDC_AUTOC_FILLUP_ENTER))fillups+='\n';if(Checked(window,IDC_AUTOC_FILLUP_TAB))fillups+='\t';
        d.Send(SCI_AUTOCSETFILLUPS,0,reinterpret_cast<LPARAM>(fillups.c_str()));if(!d.style.completion)d.Send(SCI_AUTOCCANCEL);
    }else if(c->resource==IDD_CSV_OPTIONS){
        int delimiter=0;for(auto entry:std::initializer_list<std::pair<int,int>>{{',',IDC_CSV_DELIMITER_COMMA},{'\t',IDC_CSV_DELIMITER_TAB},{'|',IDC_CSV_DELIMITER_PIPE},{';',IDC_CSV_DELIMITER_SEMICOLON},{' ',IDC_CSV_DELIMITER_SPACE}})if(Checked(window,entry.second))delimiter=entry.first;
        if(Checked(window,IDC_CSV_DELIMITER_OTHER)){auto text=Text(window,IDC_CSV_DELIMITER_OTHER_TEXT);if(text.size()!=1||text[0]>127){MessageBeep(MB_ICONWARNING);return TRUE;}delimiter=text[0];}
        d.style.csvConfigured=true;d.style.csvOptions=delimiter|((Checked(window,IDC_CSV_QUALIFIER_DOUBLE)?'"':Checked(window,IDC_CSV_QUALIFIER_SINGLE)?'\'':0)<<8)|(Checked(window,IDC_CSV_BACKSLASH_ESCAPE)?1<<15:0)|(Checked(window,IDC_CSV_MERGE_DELIMITER)?1<<16:0);ConfigureDocumentStyle(d);
    }
    EndDialog(window,IDOK);return TRUE;
    }catch(const std::exception& error){OutputDebugStringA(error.what());MessageBeep(MB_ICONERROR);return TRUE;}
}
}
void ApplyStyleTabSettings(EditorDocument& d,int scheme,int defaultWidth,int defaultIndent,bool spaces,bool global){
    d.Send(SCI_SETTABINDENTS,d.style.tabIndents);d.Send(SCI_SETBACKSPACEUNINDENTS,d.style.backspaceMask&3);
    if(d.style.appliedTabScheme==scheme)return;d.style.appliedTabScheme=scheme;
    if(!d.style.useSchemeTabs)return;
    auto current=schemeTabs.contains(scheme)?schemeTabs[scheme]:global?std::array<int,3>{globalTabWidth,globalIndentWidth,globalSpaces?1:0}:std::array<int,3>{defaultWidth,defaultIndent,spaces?1:0};
    if(d.style.tabsOverride)current[2]=d.style.tabsAsSpaces?1:0;
    d.style.tabWidth=current[0];d.style.indentWidth=current[1];d.style.tabsAsSpaces=current[2]!=0;
    d.Send(SCI_SETTABWIDTH,current[0]);d.Send(SCI_SETINDENT,current[1]);d.Send(SCI_SETUSETABS,!current[2]);
}
INT_PTR ShowStyleSettingsDialog(HWND owner,EditorDocument& document,UINT resource){Context context{document,resource};return DialogBoxParamW(_Module.GetModuleInstance(),MAKEINTRESOURCEW(resource),owner,Procedure,reinterpret_cast<LPARAM>(&context));}
void LoadStyleDialogPreferences(const std::filesystem::path& path){
    globalTabWidth=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Tabs",L"Width",4,path.c_str())),1,256);
    globalIndentWidth=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Tabs",L"Indent",4,path.c_str())),0,256);
    globalSpaces=GetPrivateProfileIntW(L"Tabs",L"Spaces",0,path.c_str())!=0;
    schemeTabs.clear();for(int scheme=0;scheme<128;++scheme){auto section=L"Tabs.Scheme."+std::to_wstring(scheme);if(GetPrivateProfileIntW(section.c_str(),L"Override",0,path.c_str()))schemeTabs[scheme]={std::clamp(static_cast<int>(GetPrivateProfileIntW(section.c_str(),L"Width",4,path.c_str())),1,256),std::clamp(static_cast<int>(GetPrivateProfileIntW(section.c_str(),L"Indent",4,path.c_str())),0,256),GetPrivateProfileIntW(section.c_str(),L"Spaces",1,path.c_str())?1:0};}
}
void WriteStyleDialogPreferencesChecked(const std::filesystem::path& path){
    auto write=[&](const wchar_t* section,const wchar_t* key,const wchar_t* value,const wchar_t* file){if(!WritePrivateProfileStringW(section,key,value,file))throw std::runtime_error("Cannot write tab settings.");};
    auto tab=std::to_wstring(globalTabWidth),indent=std::to_wstring(globalIndentWidth);
    write(L"Tabs",L"Width",tab.c_str(),path.c_str());write(L"Tabs",L"Indent",indent.c_str(),path.c_str());write(L"Tabs",L"Spaces",globalSpaces?L"1":L"0",path.c_str());
    for(int scheme=0;scheme<128;++scheme){auto section=L"Tabs.Scheme."+std::to_wstring(scheme);write(section.c_str(),nullptr,nullptr,path.c_str());}
    for(auto& [scheme,values]:schemeTabs){auto section=L"Tabs.Scheme."+std::to_wstring(scheme),width=std::to_wstring(values[0]),size=std::to_wstring(values[1]);write(section.c_str(),L"Override",L"1",path.c_str());write(section.c_str(),L"Width",width.c_str(),path.c_str());write(section.c_str(),L"Indent",size.c_str(),path.c_str());write(section.c_str(),L"Spaces",values[2]?L"1":L"0",path.c_str());}
}

// Original EditSetWrapStartIndent/Mode and Notepad4::SetWrapVisualFlags mappings.
void ApplyDocumentWrapSettings(EditorDocument& d){
    int indent=0,mode=SC_WRAPINDENT_FIXED;auto width=d.Send(SCI_GETINDENT);if(!width)width=d.Send(SCI_GETTABWIDTH);
    switch(d.style.wrapIndent){case 1:indent=1;break;case 2:indent=2;break;case 3:indent=static_cast<int>(width);break;case 4:indent=static_cast<int>(2*width);break;case 5:mode=SC_WRAPINDENT_SAME;break;case 6:mode=SC_WRAPINDENT_INDENT;break;case 7:mode=SC_WRAPINDENT_DEEPINDENT;break;}
    d.Send(SCI_SETWRAPSTARTINDENT,indent);d.Send(SCI_SETWRAPINDENTMODE,mode);
    int flags=0,location=0;
    if(d.style.wrapSymbolsShown){if(!d.style.wrapSymbols)d.style.wrapSymbols=2;int before=d.style.wrapSymbols%10,after=d.style.wrapSymbols/10;
        if(before){flags|=SC_WRAPVISUALFLAG_END;if(before==1)location|=SC_WRAPVISUALFLAGLOC_END_BY_TEXT;}
        if(after==3){flags|=SC_WRAPVISUALFLAG_MARGIN;if(!d.Send(SCI_GETMARGINWIDTHN,0))d.Send(SCI_SETMARGINWIDTHN,0,MulDiv(54,GetDpiForWindow(d.hwnd),96));}
        else if(after){flags|=SC_WRAPVISUALFLAG_START;if(after==1)location|=SC_WRAPVISUALFLAGLOC_START_BY_TEXT;}
    }
    d.Send(SCI_SETWRAPVISUALFLAGSLOCATION,location);d.Send(SCI_SETWRAPVISUALFLAGS,flags);
}

void SetDefaultTabsAsSpaces(bool spaces){globalSpaces=spaces;}
