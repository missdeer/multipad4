#include "ThemeDialog.h"
#include "StyleManager.h"
#include "LanguageConfiguration.h"
#include "ConfigurationXml.h"
#include "resource.h"
#include <sstream>
namespace {
std::shared_ptr<const ThemeDefinition> selectedTheme;
std::wstring selectedPath;
std::filesystem::path preferencePath;
bool scopeAll = true;
std::wstring Report(const ThemeDefinition* theme) {
    std::wstring report;
    if(theme)for(const auto& item:theme->diagnostics)report+=L"["+ConfigurationWide(item.status)+L"] "+item.message+L" ("+std::to_wstring(item.line)+L":"+std::to_wstring(item.column)+L")\r\n";
    for(const auto& item:ConfigurationDiagnostics())report+=L"[configuration] "+item.source+L": "+item.message+L"\r\n";
    return report;
}
struct Context {
    EditorDocument& document;
    std::shared_ptr<const ThemeDefinition> saved;
    std::wstring savedPath;
    std::vector<std::filesystem::path> files;
    std::filesystem::path source;
    std::shared_ptr<const ThemeDefinition> candidate;
};
void Preview(HWND window,Context& context) {
    if(context.source.empty()){context.candidate.reset();}
    else context.candidate=LoadTheme(context.source);
    context.document.style.theme=context.candidate;
    ConfigureDocumentStyle(context.document);
    auto report=Report(context.candidate.get());SetDlgItemTextW(window,IDC_THEME_REPORT,report.c_str());
}
INT_PTR CALLBACK ThemeProcedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    auto* context=reinterpret_cast<Context*>(GetWindowLongPtrW(window,DWLP_USER));
    try {
        if(message==WM_INITDIALOG){
            context=reinterpret_cast<Context*>(l);SetWindowLongPtrW(window,DWLP_USER,l);
            auto combo=GetDlgItem(window,IDC_THEME_LIST);
            SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Internal default (inherit)"));
            auto add=[&](const std::filesystem::path& directory){std::error_code error;if(!std::filesystem::is_directory(directory,error))return;for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.path().extension()==L".xml"){context->files.push_back(entry.path());auto label=entry.path().stem().wstring();try{auto xml=ReadConfigurationXml(entry.path());label=xml.Attribute(L"name",label);if(label.size()>256)label.resize(256);}catch(const std::exception&){}SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}};
            add(ConfigurationDirectory()/L"themes");if(!preferencePath.empty())add(preferencePath.parent_path()/L"config/themes");
            int selected=0;if(context->saved&&!context->savedPath.empty()){context->files.emplace_back(context->savedPath);auto label=context->saved->name+L" (current)";selected=static_cast<int>(SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str())));}
            SendMessageW(combo,CB_SETCURSEL,selected,0);CheckDlgButton(window,IDC_THEME_ALL,BST_CHECKED);
            SetDlgItemTextW(window,IDC_THEME_REPORT,Report(context->saved.get()).c_str());
            CWindow(window).CenterWindow(GetParent(window));return TRUE;
        }
        if(!context)return FALSE;
        if(message==WM_CLOSE||(message==WM_COMMAND&&LOWORD(w)==IDCANCEL)){
            context->document.style.theme=context->saved;context->document.style.themePath=context->savedPath;ConfigureDocumentStyle(context->document);EndDialog(window,IDCANCEL);return TRUE;
        }
        if(message!=WM_COMMAND)return FALSE;
        unsigned id=LOWORD(w);
        if(id==IDC_THEME_LIST&&HIWORD(w)==CBN_SELCHANGE){
            int index=static_cast<int>(SendDlgItemMessageW(window,IDC_THEME_LIST,CB_GETCURSEL,0,0));
            context->source=index>0?context->files.at(index-1):std::filesystem::path{};Preview(window,*context);return TRUE;
        }
        if(id==IDC_THEME_IMPORT){
            CFileDialog file(TRUE,L"xml",nullptr,OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR,L"Theme XML (*.xml)\0*.xml\0\0",window);
            if(file.DoModal()==IDOK){auto candidate=LoadTheme(file.m_szFileName);context->source=file.m_szFileName;context->candidate=candidate;context->document.style.theme=candidate;ConfigureDocumentStyle(context->document);SetDlgItemTextW(window,IDC_THEME_REPORT,Report(candidate.get()).c_str());}return TRUE;
        }
        if(id==IDC_THEME_RELOAD||id==IDC_THEME_PREVIEW){
            if(id==IDC_THEME_RELOAD&&context->source.empty())context->source=ConfigurationDirectory()/L"themes"/(context->document.style.dark?L"dark.xml":L"light.xml");
            Preview(window,*context);return TRUE;
        }
        if(id==IDC_THEME_RESET){context->source.clear();SendDlgItemMessageW(window,IDC_THEME_LIST,CB_SETCURSEL,0,0);Preview(window,*context);return TRUE;}
        if(id==IDOK){
            std::wstring saved;
            if(context->candidate){
                auto directory=preferencePath.empty()?std::filesystem::temp_directory_path()/L"MultiPad4-themes":preferencePath.parent_path()/L"config/themes";
                GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("Cannot allocate theme identity");wchar_t name[40]{};StringFromGUID2(guid,name,40);
                auto file=directory/L"snapshots"/(std::wstring(name)+L".xml");SaveNativeTheme(*context->candidate,file);saved=file.wstring();
            }
            context->document.style.theme=context->candidate;context->document.style.themePath=saved;
            scopeAll=IsDlgButtonChecked(window,IDC_THEME_ALL)==BST_CHECKED;
            if(scopeAll){selectedTheme=context->candidate;selectedPath=saved;}
            ConfigureDocumentStyle(context->document);EndDialog(window,IDOK);return TRUE;
        }
    }catch(const std::exception& error){SetDlgItemTextW(window,IDC_THEME_REPORT,ConfigurationWide(error.what()).c_str());}
    return FALSE;
}
}
bool ShowThemeDialog(HWND owner,EditorDocument& document) {
    Context context{document,document.style.theme,document.style.themePath,{},document.style.themePath,document.style.theme};
    return DialogBoxParamW(_Module.GetModuleInstance(),MAKEINTRESOURCEW(IDD_THEME_CONFIG),owner,ThemeProcedure,reinterpret_cast<LPARAM>(&context))==IDOK;
}
bool ThemeScopeAll(){return scopeAll;}
void ApplyDefaultDocumentTheme(EditorDocument& document){document.style.theme=selectedTheme;document.style.themePath=selectedPath;}
void LoadThemePreferences(const std::filesystem::path& path){
    preferencePath=path;wchar_t value[32768]{};GetPrivateProfileStringW(L"Configuration",L"Theme",L"",value,32768,path.c_str());selectedPath=value;selectedTheme.reset();
    if(!selectedPath.empty()&&std::filesystem::path(selectedPath).is_relative())selectedPath=(path.parent_path()/selectedPath).lexically_normal().wstring();
    if(!selectedPath.empty())try{selectedTheme=LoadTheme(selectedPath);}catch(const std::exception& error){RecordConfigurationDiagnostic(selectedPath,error);}
}
void SaveThemePreferences(const std::filesystem::path& path){preferencePath=path;auto stored=selectedPath;if(!stored.empty()){auto relative=std::filesystem::path(stored).lexically_relative(path.parent_path());if(!relative.empty()&&*relative.begin()!=L"..")stored=relative.generic_wstring();}if(!WritePrivateProfileStringW(L"Configuration",L"Theme",stored.c_str(),path.c_str()))throw std::runtime_error("Cannot persist theme selection");}
void ApplyThemeEditorElements(EditorDocument& document){
    auto configuration=document.style.configuration;
    static const std::map<std::string,StylePatch> noTheme;const auto& styles=configuration&&configuration->theme?configuration->theme->styles:noTheme;
    const auto& semanticStyles=configuration?configuration->semanticStyles:document.style.semanticOverrides;
    auto mergeLegacy=[&](StylePatch& patch,const std::wstring& text){
        std::wistringstream fields(text);std::wstring field;
        while(std::getline(fields,field,L';')){field.erase(0,field.find_first_not_of(L" \t"));auto colon=field.find(L':');auto key=field.substr(0,colon);auto value=colon==std::wstring::npos?L"":field.substr(colon+1);while(!value.empty()&&iswspace(value.front()))value.erase(value.begin());while(!value.empty()&&iswspace(value.back()))value.pop_back();
            if(key==L"fore"||key==L"back"){auto& color=key==L"fore"?patch.foreground:patch.background;auto& clear=key==L"fore"?patch.clearForeground:patch.clearBackground;if(value==L"inherit"){color.reset();clear=true;}else{unsigned parsed=0;if(swscanf_s(value.c_str(),L"#%x",&parsed)==1){color=parsed;clear=false;}}}
            else if(key==L"font")patch.font=value==L"$(Code)"?(document.style.codeFont.empty()?document.style.font:document.style.codeFont):value==L"$(Text)"?(document.style.textFont.empty()?document.style.font:document.style.textFont):value;
            else if(key==L"size"){int size=_wtoi(value.c_str());if(!value.empty()&&(value[0]==L'+'||value[0]==L'-'))size+=static_cast<int>(document.Send(SCI_STYLEGETSIZE,STYLE_DEFAULT));patch.size=std::clamp(size,0,512);}
            else if(key==L"bold"||key==L"italic"||key==L"underline"){unsigned bit=key==L"bold"?1:key==L"italic"?2:4;auto flags=patch.decorations.value_or(0);patch.decorations=value==L"0"?flags&~bit:flags|bit;}
        }
    };
    auto effective=[&](const char* id,int index){
        StylePatch patch;auto found=styles.find(id);if(found!=styles.end())patch=found->second;
        if(index>=0){StylePatch legacy;legacy.decorations=patch.decorations;auto text=DocumentGlobalStyleOverrides(document,static_cast<unsigned>(index));auto group=ConfigurationUtf8(LanguageCatalog()[0]->Styles[index].id);if(group!=id){auto value=semanticStyles.find(group);if(value!=semanticStyles.end())text+=L"; "+value->second;}mergeLegacy(legacy,text);
            if(std::string(id)=="editor.selection"){legacy.foreground.reset();legacy.clearForeground=false;}
            else if(std::string(id)=="editor.selection.additional"){legacy.background=legacy.foreground;legacy.clearBackground=legacy.clearForeground;legacy.foreground.reset();legacy.clearForeground=false;}
            else if(std::string(id)=="global.foldingmarker"){std::swap(legacy.foreground,legacy.background);std::swap(legacy.clearForeground,legacy.clearBackground);}
            else if(std::string(id)=="global.currentline"&&document.style.lineHighlight==2&&legacy.foreground)legacy.background=legacy.foreground;
            if(legacy.foreground||legacy.clearForeground){patch.foreground=legacy.foreground;patch.clearForeground=legacy.clearForeground;}if(legacy.background||legacy.clearBackground){patch.background=legacy.background;patch.clearBackground=legacy.clearBackground;}if(legacy.font)patch.font=legacy.font;if(legacy.size)patch.size=legacy.size;if(legacy.decorations)patch.decorations=legacy.decorations;
        }
        auto local=semanticStyles.find(id);if(local!=semanticStyles.end())mergeLegacy(patch,local->second);
        return patch;
    };
    auto element=[&](const char* name,int index,unsigned target,bool background){auto patch=effective(name,index);auto color=background?patch.background:patch.foreground;if(!color)return;unsigned value=*color;unsigned alpha=document.Send(SCI_GETELEMENTISSET,target)?static_cast<unsigned>(document.Send(SCI_GETELEMENTCOLOUR,target))&0xff000000U:0xff000000U;document.Send(SCI_SETELEMENTCOLOUR,target,RGB((value>>16)&255,(value>>8)&255,value&255)|alpha);};
    element("editor.selection",7,SC_ELEMENT_SELECTION_BACK,true);element("editor.selection",7,SC_ELEMENT_SELECTION_TEXT,false);
    element("editor.selection.additional",7,SC_ELEMENT_SELECTION_ADDITIONAL_BACK,true);element("editor.selection.additional",7,SC_ELEMENT_SELECTION_ADDITIONAL_TEXT,false);
    element("editor.caret",10,SC_ELEMENT_CARET,false);element("editor.caret.additional",-1,SC_ELEMENT_CARET_ADDITIONAL,false);
    element("global.currentline",9,SC_ELEMENT_CARET_LINE_BACK,true);
    element("global.whitespace",8,SC_ELEMENT_WHITE_SPACE,false);element("global.whitespace",8,SC_ELEMENT_WHITE_SPACE_BACK,true);
    auto rgb=[](uint32_t value){return RGB((value>>16)&255,(value>>8)&255,value&255);};
    auto brace=[&](const char* id,unsigned style,unsigned indicator){
        auto selected=styles.find(id);bool structured=false;
        if(selected!=styles.end()){const auto& value=selected->second;structured=value.foreground||value.background||value.font||value.size||value.decorations||value.clearForeground||value.clearBackground;}
        if(!structured&&!semanticStyles.contains(id))return;
        auto patch=effective(id,style==STYLE_BRACELIGHT?3:4);
        if(!patch.foreground&&!patch.background&&!patch.font&&!patch.size&&!patch.decorations&&!patch.clearForeground&&!patch.clearBackground)return;
        document.Send(indicator,FALSE,0);
        if(patch.foreground)document.Send(SCI_STYLESETFORE,style,rgb(*patch.foreground));
        if(patch.background)document.Send(SCI_STYLESETBACK,style,rgb(*patch.background));
        if(patch.font){std::string font;if(patch.font->empty()){char inherited[256]{};document.Send(SCI_STYLEGETFONT,STYLE_DEFAULT,reinterpret_cast<LPARAM>(inherited));font=inherited;}else font=ConfigurationUtf8(*patch.font);document.Send(SCI_STYLESETFONT,style,reinterpret_cast<LPARAM>(font.c_str()));}
        if(patch.size)document.Send(SCI_STYLESETSIZE,style,*patch.size?*patch.size:document.Send(SCI_STYLEGETSIZE,STYLE_DEFAULT));
        if(patch.decorations){auto flags=*patch.decorations;document.Send(SCI_STYLESETBOLD,style,flags&1);document.Send(SCI_STYLESETITALIC,style,flags&2);document.Send(SCI_STYLESETUNDERLINE,style,flags&4);}
    };
    brace("global.matchingbrace",STYLE_BRACELIGHT,SCI_BRACEHIGHLIGHTINDICATOR);brace("global.matchingbraceerror",STYLE_BRACEBAD,SCI_BRACEBADLIGHTINDICATOR);
    auto fold=effective("global.foldingmarker",15);for(int marker=SC_MARKNUM_FOLDEREND;marker<=SC_MARKNUM_FOLDEROPEN;++marker){if(fold.foreground)document.Send(SCI_MARKERSETFORETRANSLUCENT,marker,rgb(*fold.foreground)|0xff000000U);if(fold.background)document.Send(SCI_MARKERSETBACKTRANSLUCENT,marker,rgb(*fold.background)|0xff000000U);}
    auto margin=effective("editor.fold.margin",-1);if(margin.background){document.Send(SCI_SETFOLDMARGINCOLOUR,TRUE,rgb(*margin.background));document.Send(SCI_SETFOLDMARGINHICOLOUR,TRUE,rgb(*margin.background));}if(margin.foreground)document.Send(SCI_SETFOLDMARGINHICOLOUR,TRUE,rgb(*margin.foreground));
}
