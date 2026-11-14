#pragma once
#include "../src/LanguageConfiguration.h"
#include "../src/ConfigurationXml.h"
#include "../src/ThemeDialog.h"
#include "../src/LexerMetrics.h"
#include "LexerBaselineTests.h"
#include "StyleDialogTests.h"
#include <source_location>
#include <fstream>
inline void RunConfigurationFeatureTests(MainFrame& frame,const std::filesystem::path& directory,const std::filesystem::path& fixture) {
    auto require=[](bool ok,std::source_location location=std::source_location::current()){if(!ok)throw std::runtime_error("Configuration assertion at "+std::to_string(location.line()));};
    std::filesystem::create_directories(directory);
    auto write=[&](const wchar_t* name,const std::string& data){auto path=directory/name;std::ofstream out(path,std::ios::binary);out<<data;out.close();require(bool(out));return path;};
    auto rejects=[&](const std::string& data){auto path=write(L"invalid.xml",data);try{LoadTheme(path);return false;}catch(const std::exception&){return true;}};
    require(rejects("<!DOCTYPE theme SYSTEM 'file:///nonexistent'><theme version='1'/>"));
    require(rejects("<theme version='99'/>"));
    require(rejects("<theme version='1'><style id='cpp.keyword' fg='zz0000'/></theme>"));
    require(rejects("<theme version='1'><style id='cpp.keyword' foreground='112233'/></theme>"));
    require(rejects("<theme version='1'><style id='cpp.keyword' size='-1'/></theme>"));
    require(rejects("<theme version='1'><style id='cpp.keyword' font='bad;font'/></theme>"));
    require(rejects("<theme version='1'><style id='cpp.keyword'/><style id='cpp.keyword'/></theme>"));
    require(rejects("<theme version='1'><keywords language='cpp' slot='15'>bad</keywords></theme>"));
    auto native=LoadTheme(write(L"清除.xml","<theme version='1' name='test'><style id='cpp.interface' decorations='0'/><style id='cpp.number' clear='fg,bg,font,size,decorations'/><x-optional/></theme>"));
    require(native->styles.at("cpp.interface").decorations==0&&native->styles.at("cpp.number").clearForeground);
    SaveNativeTheme(*native,directory/L"roundtrip.xml");auto roundtrip=LoadTheme(directory/L"roundtrip.xml");require(ThemeStyleValue(*roundtrip,"cpp.number")==ThemeStyleValue(*native,"cpp.number"));
    HANDLE locked=CreateFileW((directory/L"roundtrip.xml").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);require(locked!=INVALID_HANDLE_VALUE);
    bool saveRejected=false;try{SaveNativeTheme(*native,directory/L"roundtrip.xml");}catch(const std::exception&){saveRejected=true;}CloseHandle(locked);require(saveRejected);require(LoadTheme(directory/L"roundtrip.xml")->styles.contains("cpp.interface"));
    const std::wstring wide=L"<?xml version='1.0' encoding='UTF-16'?><theme version='1' name='中文'><style id='cpp.number' fg='112233'/></theme>";
    std::string utf16="\xff\xfe";utf16.append(reinterpret_cast<const char*>(wide.data()),wide.size()*sizeof(wchar_t));
    require(LoadTheme(write(L"utf16.xml",utf16))->name==L"中文");
    auto theme=LoadTheme(fixture);require(theme->styles.at("cpp.keyword").foreground==0x123456);
    auto noClass=LoadTheme(write(L"no-keyword-class.xml","<NotepadPlus><LexerStyles><LexerType name='cpp'><WordsStyle name='INSTRUCTION WORD' styleID='5'>arbitrary_body</WordsStyle></LexerType></LexerStyles><GlobalStyles/></NotepadPlus>"));require(noClass->keywords.empty());
    require(theme->styles.at("cpp.comment.line").foreground==0x993399);
    require(theme->keywords.at({"cpp",0}).find("multipad_keyword")!=std::string::npos);
    bool unsupported=false;for(const auto& diagnostic:theme->diagnostics)unsupported|=diagnostic.status=="unsupported";require(unsupported);
    auto base=CurrentConfiguration();auto changed=ConfigurationWithTheme(base,*theme);
    require(changed->languages[3]->rulesVersion!=base->languages[3]->rulesVersion);
    require(std::string(changed->languages[3]->pszKeyWords[0]).find("not_a_keyword")==std::string::npos);
    auto oldOwner=GetCompletionCache(*base->languages[3]).owner;
    auto newOwner=GetCompletionCache(*changed->languages[3]).owner;require(oldOwner!=newOwner&&oldOwner==base->languages[3]);
    auto first=frame.NewDocument();auto second=frame.NewDocument();require(first&&second);
    first->path=L"one.cpp";first->Load({"int value = 42; // line\n/* block */\n"});ConfigureDocumentStyle(*first);
    second->path=L"two.py";second->Load({"def example():\n    return 42\n"});ConfigureDocumentStyle(*second);auto isolated=LexerDocumentBaseline(*second);
    auto secondColor=second->Send(SCI_STYLEGETFORE,SCE_PY_NUMBER);
    first->style.semanticOverrides["editor.selection"]=L"back:#070809";ConfigureDocumentStyle(*first);require((first->Send(SCI_GETELEMENTCOLOUR,SC_ELEMENT_SELECTION_BACK)&0xffffff)==RGB(7,8,9));first->style.semanticOverrides.clear();
    first->style.theme=theme;ConfigureDocumentStyle(*first);
    char face[256]{};first->Send(SCI_STYLEGETFONT,STYLE_DEFAULT,reinterpret_cast<LPARAM>(face));require(std::string(face)=="Courier New");
    require(first->Send(SCI_STYLEGETFORE,SCE_C_WORD)==RGB(0x12,0x34,0x56));
    require(first->Send(SCI_STYLEGETFORE,SCE_C_NUMBER)==RGB(0x65,0x43,0x21));
    require(first->Send(SCI_STYLEGETFORE,SCE_C_COMMENTLINE)==RGB(0x99,0x33,0x99));
    require(first->Send(SCI_STYLEGETFORE,SCE_C_COMMENT)==RGB(0x33,0x99,0x33));
    require((first->Send(SCI_GETELEMENTCOLOUR,SC_ELEMENT_SELECTION_BACK)&0xffffff)==RGB(0x44,0x66,0x88));
    require((first->Send(SCI_GETELEMENTCOLOUR,SC_ELEMENT_CARET)&0xffffff)==RGB(0xff,0xaa,0));
    require(first->Send(SCI_STYLEGETFORE,STYLE_BRACELIGHT)==RGB(0x11,0x22,0x33));require(first->Send(SCI_STYLEGETBACK,STYLE_BRACELIGHT)==RGB(0x44,0xaa,0x66));
    first->style.overrides[(static_cast<unsigned>(NP2LEX_GLOBAL)<<8)|7]=L"back:#010203";ConfigureDocumentStyle(*first);require((first->Send(SCI_GETELEMENTCOLOUR,SC_ELEMENT_SELECTION_BACK)&0xffffff)==RGB(1,2,3));
    first->style.semanticOverrides["editor.selection"]=L"back:#040506";ConfigureDocumentStyle(*first);require((first->Send(SCI_GETELEMENTCOLOUR,SC_ELEMENT_SELECTION_BACK)&0xffffff)==RGB(4,5,6));
    auto validStyleSnapshot=first->style.configuration;auto validSelection=first->Send(SCI_GETELEMENTCOLOUR,SC_ELEMENT_SELECTION_BACK);bool invalidStyleRejected=false;
    first->style.semanticOverrides["cpp.number"]=L"unknown-property:1";try{ConfigureDocumentStyle(*first);}catch(const std::exception&){invalidStyleRejected=true;}
    require(invalidStyleRejected&&first->style.configuration==validStyleSnapshot&&first->Send(SCI_GETELEMENTCOLOUR,SC_ELEMENT_SELECTION_BACK)==validSelection);
    first->style.overrides.clear();first->style.semanticOverrides.clear();ConfigureDocumentStyle(*first);
    require(second->Send(SCI_STYLEGETFORE,SCE_PY_NUMBER)==secondColor&&LexerDocumentBaseline(*second)==isolated);
    second->style.theme=theme;ConfigureDocumentStyle(*second);require(second->Send(SCI_STYLEGETFORE,SCE_PY_NUMBER)==RGB(0xab,0xcd,0xef));
    require(second->Send(SCI_STYLEGETFORE,SCE_PY_IDENTIFIER)==RGB(0x77,0x88,0x99));require(second->Send(SCI_STYLEGETFORE,SCE_PY_DEFAULT)!=second->Send(SCI_STYLEGETFORE,SCE_PY_IDENTIFIER));
    second->path=L"two.js";second->Load({"const value = 42;\n"});ConfigureDocumentStyle(*second);require(second->Send(SCI_STYLEGETFORE,SCE_JS_NUMBER)==RGB(0xfe,0xdc,0xba));
    require(second->Send(SCI_STYLEGETFORE,SCE_JS_TEMPLATELITERAL)==RGB(0x22,0xaa,0x44));
    second->style.theme.reset();second->path=L"two.py";second->Load({"def example():\n    return 42\n"});ConfigureDocumentStyle(*second);
    first->style.theme=native;ConfigureDocumentStyle(*first);require(!first->Send(SCI_STYLEGETBOLD,SCE_C_INTERFACE));
    auto before=LexerDocumentBaseline(*first);lexerMetrics::Reset();
    first->style.theme=LoadTheme(write(L"color-only.xml","<theme version='1'><style id='cpp.number' fg='010203'/></theme>"));ConfigureDocumentStyle(*first);
    require(lexerMetrics::counters.calls==0&&LexerDocumentBaseline(*first)==before);
    lexerMetrics::Reset();first->style.appliedRules=0;ConfigureDocumentStyle(*first);require(lexerMetrics::counters.calls>0);
#ifdef MULTIPAD4_LEXER_METRICS
    require(lexerMetrics::counters.fetchedBytes>0);
#endif
    auto previous=CurrentConfiguration();require(!ReloadLanguageConfiguration(write(L"invalid-language.xml","<languages version='99'/>")));require(CurrentConfiguration()==previous);
    auto defaultXml=EmbeddedConfiguration(5520);auto location=defaultXml.find("cppm; hxx");require(location!=std::string::npos);defaultXml.replace(location,4,"testextension");
    auto modified=write(L"languages.xml",defaultXml);require(ReloadLanguageConfiguration(modified));require(CurrentConfiguration()!=previous);require(CurrentConfiguration()->version>previous->version);
    require(std::wstring(previous->languages[3]->pszDefExt).find(L"testextension")==std::wstring::npos);
    require(first->style.configuration!=CurrentConfiguration());ConfigureDocumentStyle(*first);require(first->style.configuration->languages[3]==CurrentConfiguration()->languages[3]);
    require(ReloadLanguageConfiguration());
    // Partial overlays distinguish missing, explicit empty, append and inherit.
    auto user=directory/L"user";std::filesystem::create_directories(user/L"config/languages");SetConfigurationUserDirectory(user);
    auto overlay=user/L"config/languages/custom.xml";
    {std::ofstream file(overlay);file<<"<language-overrides version='1'><language id='cpp'><extensions>customext</extensions><slot index='0' mode='append'>overlay_keyword</slot><field name='defaultTabWidth' value='2'/></language></language-overrides>";}
    require(ReloadLanguageConfiguration());require(Language("cpp").defaultTabWidth==2);require(std::wstring(Language("cpp").pszDefExt)==L"customext");require(std::string(Language("cpp").pszKeyWords[0]).find("overlay_keyword")!=std::string::npos);
    auto overlaySnapshot=CurrentConfiguration();
    {std::ofstream file(overlay);file<<"<language-overrides version='1'><language id='cpp'><slot index='0' mode='replace'></slot></language></language-overrides>";}
    require(ReloadLanguageConfiguration());require(Language("cpp").pszKeyWords[0]&&!*Language("cpp").pszKeyWords[0]);require(overlaySnapshot->languages[3]->rulesVersion!=Language("cpp").rulesVersion);
    {std::ofstream file(overlay);file<<"<language-overrides version='1'><language id='cpp'><slot index='0' mode='inherit'/></language></language-overrides>";}
    require(ReloadLanguageConfiguration());require(std::string(Language("cpp").pszKeyWords[0])==previous->languages[3]->pszKeyWords[0]);
    SetConfigurationUserDirectory(directory);require(ReloadLanguageConfiguration());
    // The real modal UI previews one document and restores it on Cancel.
    LoadThemePreferences(directory/L"Settings.ini");first->style.theme.reset();ConfigureDocumentStyle(*first);auto originalColor=first->Send(SCI_STYLEGETBACK,STYLE_DEFAULT);
    qaArtifactDirectory=directory/L"visual";
    StyleDialogTest::RunScheme(frame,*first,true,[&](HWND dialog){
        auto index=SendDlgItemMessageW(dialog,IDC_THEME_LIST,CB_FINDSTRINGEXACT,static_cast<WPARAM>(-1),reinterpret_cast<LPARAM>(L"MultiPad4 Dark"));require(index>=0);
        SendDlgItemMessageW(dialog,IDC_THEME_LIST,CB_SETCURSEL,index,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_THEME_LIST,CBN_SELCHANGE),0);
        require(first->Send(SCI_STYLEGETBACK,STYLE_DEFAULT)!=originalColor);require(second->Send(SCI_STYLEGETFORE,SCE_PY_NUMBER)==secondColor);
    },IDCANCEL,ID_THEME_CONFIG);
    require(!first->style.theme&&first->Send(SCI_STYLEGETBACK,STYLE_DEFAULT)==originalColor);
    StyleDialogTest::RunScheme(frame,*first,true,[&](HWND dialog){
        auto index=SendDlgItemMessageW(dialog,IDC_THEME_LIST,CB_FINDSTRINGEXACT,static_cast<WPARAM>(-1),reinterpret_cast<LPARAM>(L"MultiPad4 Dark"));require(index>=0);
        SendDlgItemMessageW(dialog,IDC_THEME_LIST,CB_SETCURSEL,index,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_THEME_LIST,CBN_SELCHANGE),0);CheckDlgButton(dialog,IDC_THEME_ALL,BST_UNCHECKED);
    },IDOK,ID_THEME_CONFIG);
    require(first->style.theme&&std::filesystem::exists(first->style.themePath));require(!second->style.theme);
    // Restore mutations at the fixture level and compare incremental lexing to a fresh document.
    first->style.theme.reset();first->Load({"int main() {\n/* comment */\nreturn 42;\n}\n"});ConfigureDocumentStyle(*first);
    first->Send(SCI_SETTARGETSTART,14);first->Send(SCI_SETTARGETEND,15);first->Send(SCI_REPLACETARGET,1,reinterpret_cast<LPARAM>(" "));first->Send(SCI_COLOURISE,14,-1);
    second->path=first->path;second->style={};second->Load({first->Text()});ConfigureDocumentStyle(*second);require(LexerDocumentBaseline(*first)==LexerDocumentBaseline(*second));
    const std::pair<const wchar_t*,const char*> edits[]={
        {L"quote.cpp","int value;\nconst char* text = \"value\";\n/* nested\ncomment */\n"},
        {L"quote.py","def example(value):\n    return f\"outer {f'inner {value}'}\"\n"},
        {L"quote.js","const value = `outer ${`inner ${1}`}`;\n/* comment */\n"},
        {L"quote.html","<script>const value = `text ${1}`;</script>\n<!-- comment -->\n"}
    };
    for(const auto& [filename,text]:edits){
        first->path=filename;first->style={};first->Load({text});ConfigureDocumentStyle(*first);
        auto content=first->Text();auto at=content.find_first_of("\"`'");require(at!=std::string::npos);
        first->Send(SCI_SETTARGETSTART,at);first->Send(SCI_SETTARGETEND,at+1);first->Send(SCI_REPLACETARGET,0,reinterpret_cast<LPARAM>(""));first->Send(SCI_COLOURISE,at,-1);
        second->path=filename;second->style={};second->Load({first->Text()});ConfigureDocumentStyle(*second);require(LexerDocumentBaseline(*first)==LexerDocumentBaseline(*second));
    }
    first->path=L"visual.cpp";first->style={};first->Load({"int main() {\n    // line comment\n    /* block comment */\n    const char* text = \"example\";\n    return 42;\n}\n"});ConfigureDocumentStyle(*first);
    qaArtifactDirectory=directory/L"visual";frame.tabs.SetActivePage(frame.tabs.GetPageCount()-2);auto child=frame.tabs.GetPageHWND(frame.tabs.GetPageCount()-2);SendMessageW(frame.m_hWndMDIClient,WM_MDIACTIVATE,reinterpret_cast<WPARAM>(child),0);frame.MDIMaximize(child);frame.ShowWindow(SW_SHOWNORMAL);frame.Refresh();
    first->style.theme=DefaultTheme(false);ConfigureDocumentStyle(*first);CaptureQAWindow(first->hwnd,L"theme-light");
    first->style.theme=DefaultTheme(true);ConfigureDocumentStyle(*first);CaptureQAWindow(first->hwnd,L"theme-dark");
    first->style.theme=theme;ConfigureDocumentStyle(*first);first->Send(SCI_SETSEL,0,3);CaptureQAWindow(first->hwnd,L"theme-import");
    first->Send(SCI_SETZOOM,150);CaptureQAWindow(first->hwnd,L"theme-150-percent");first->Send(SCI_SETZOOM,100);
    qaArtifactDirectory.clear();first->Send(SCI_SETSAVEPOINT);second->Send(SCI_SETSAVEPOINT);lexerMetrics::enabled=false;
}
