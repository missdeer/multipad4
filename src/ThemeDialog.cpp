#include "ThemeDialog.h"
#include "StyleManager.h"
#include "LanguageConfiguration.h"
#include "ConfigurationXml.h"
#include "resource.h"
#include <sstream>
#include <iomanip>
#include <set>
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
    int language = 0, style = 0, slot = -1;
    bool changing = false, advanced = false, scopeChanged = false;
    unsigned displayedDecorations = 0;
    std::vector<std::wstring> styleLabels;
    std::unique_ptr<EditorDocument> appearanceDocument;
    StylePatch appearance;

    std::vector<std::string> styleIds;
    std::map<int,std::wstring> displayed;
    std::map<std::filesystem::path,std::shared_ptr<const ThemeDefinition>> drafts;

};
const wchar_t* Text(const wchar_t* english,const wchar_t* chinese) {
    return PRIMARYLANGID(GetThreadUILanguage())==LANG_CHINESE?chinese:english;
}
std::wstring Label(UINT resource,const std::wstring& fallback) {
    auto instance=_Module.GetModuleInstance();
    auto block=FindResourceExW(instance,RT_STRING,MAKEINTRESOURCEW((resource>>4)+1),GetThreadUILanguage());
    auto loaded=block?LoadResource(instance,block):nullptr;
    auto cursor=loaded?static_cast<const wchar_t*>(LockResource(loaded)):nullptr;
    if(!cursor)return fallback;
    for(UINT i=0;i<(resource&15);++i)cursor+=*cursor+1;
    return *cursor?std::wstring(cursor+1,*cursor):fallback;
}
std::wstring CategoryLabel(const LanguageDefinition& language,const StyleCategoryBinding& category) {
    std::wstring group;
    for(const auto& style:language.Styles)if(ConfigurationUtf8(style.id)==category.group){group=Label(style.rid,style.name);break;}
    static const std::map<std::string,std::wstring> names={
        {"primary",L"Primary"},{"secondary",L"Secondary"},{"block",L"Block"},{"line",L"Line"},
        {"quoted",L"Quoted"},{"character",L"Character"},{"unterminated",L"Unterminated"},
        {"documentation",L"Documentation"},{"documentation.line",L"Documentation line"},
        {"reference",L"Reference"},{"definition",L"Definition"},{"commented_tag_xml",L"XML tag"}
    };
    auto suffix=category.id.starts_with(category.group+".")?category.id.substr(category.group.size()+1):category.id.substr(category.id.find('.')+1);
    auto found=names.find(suffix);auto detail=found!=names.end()?found->second:ConfigurationWide(suffix);
    for(auto& ch:detail)if(ch==L'.'||ch==L'_')ch=L' ';
    if(!detail.empty())detail.front()=static_cast<wchar_t>(towupper(detail.front()));
    return group.empty()?detail:group+L" ("+detail+L")";
}
std::wstring Field(HWND window,int id) {
    int length=GetWindowTextLengthW(GetDlgItem(window,id));
    std::wstring text(static_cast<size_t>(length)+1,L'\0');
    GetDlgItemTextW(window,id,text.data(),length+1);text.resize(length);return text;
}
const LanguageDefinition& SelectedLanguage(const Context& context){return *LanguageCatalog()[context.language];}
std::wstring HexColor(std::optional<uint32_t> value,bool clear){
    if(clear)return L"inherit";if(!value)return {};
    std::wostringstream out;out<<L"#"<<std::uppercase<<std::hex<<std::setw(6)<<std::setfill(L'0')<<*value;return out.str();
}
void ReadColor(std::wstring text,std::optional<uint32_t>& value,bool& clear){
    value.reset();clear=text==L"inherit";if(text.empty()||clear)return;
    if(text.front()==L'#')text.erase(0,1);
    if(text.size()!=6||text.find_first_not_of(L"0123456789abcdefABCDEF")!=std::wstring::npos)throw std::runtime_error("Use six RGB hex digits, inherit, or an empty colour.");
    value=static_cast<uint32_t>(std::stoul(text,nullptr,16));
}
void ShowAppearance(HWND window,Context& context,bool updateFields) {
    if(!context.appearanceDocument){
        context.appearanceDocument=std::make_unique<EditorDocument>();
        if(!context.appearanceDocument->Create(window))throw std::runtime_error("Cannot create style preview.");
    }
    auto& preview=*context.appearanceDocument;
    const auto& language=SelectedLanguage(context);
    const auto& id=context.styleIds.at(context.style);
    preview.style=context.document.style;
    preview.style.theme=context.candidate;
    preview.style.scheme=context.language?context.language:std::max(1,context.document.style.resolvedScheme);
    preview.style.dialect=context.language==context.document.style.resolvedScheme?context.document.style.dialect:0;
    preview.style.appliedRules=0;preview.style.appliedLexer=0;preview.style.appliedTabScheme=-1;
    if(id=="global.defaultcodestyle")preview.style.fontKind=0;
    if(id=="global.defaulttextstyle")preview.style.fontKind=1;
    ConfigureDocumentStyle(preview);
    unsigned native=STYLE_DEFAULT;
    for(const auto& style:language.Styles)if(ConfigurationUtf8(style.id)==id&&style.iStyle)native=style.iStyle&255;
    for(const auto& category:language.categories)if(category.id==id)native=category.nativeStyle;
    if(id=="global.matchingbrace")native=STYLE_BRACELIGHT;
    if(id=="global.matchingbraceerror")native=STYLE_BRACEBAD;
    auto color=[](LRESULT value){auto rgb=static_cast<COLORREF>(value);return uint32_t((GetRValue(rgb)<<16)|(GetGValue(rgb)<<8)|GetBValue(rgb));};
    auto& appearance=context.appearance;
    appearance.foreground=color(preview.Send(SCI_STYLEGETFORE,native));
    appearance.background=color(preview.Send(SCI_STYLEGETBACK,native));
    static const std::map<std::string,unsigned> elements={
        {"editor.selection",SC_ELEMENT_SELECTION_BACK},{"global.selectedtext",SC_ELEMENT_SELECTION_BACK},
        {"editor.selection.additional",SC_ELEMENT_SELECTION_ADDITIONAL_BACK},
        {"editor.caret",SC_ELEMENT_CARET},{"global.caret",SC_ELEMENT_CARET},
        {"editor.caret.additional",SC_ELEMENT_CARET_ADDITIONAL},
        {"global.currentline",SC_ELEMENT_CARET_LINE_BACK},{"global.whitespace",SC_ELEMENT_WHITE_SPACE}
    };
    auto element=elements.find(id);
    if(element!=elements.end()){
        auto value=color(preview.Send(SCI_GETELEMENTCOLOUR,element->second));
        if(id.find("caret")!=std::string::npos||id=="global.whitespace")appearance.foreground=value;else appearance.background=value;
    }
    char font[256]{};preview.Send(SCI_STYLEGETFONT,native,reinterpret_cast<LPARAM>(font));
    appearance.font=ConfigurationWide(font);appearance.size=static_cast<int>(preview.Send(SCI_STYLEGETSIZE,native));
    appearance.decorations=(preview.Send(SCI_STYLEGETBOLD,native)?1u:0)|(preview.Send(SCI_STYLEGETITALIC,native)?2u:0)|(preview.Send(SCI_STYLEGETUNDERLINE,native)?4u:0);
    if(updateFields){
        SetDlgItemTextW(window,IDC_THEME_FONT_NAME,appearance.font->c_str());
        SetDlgItemInt(window,IDC_THEME_FONT_SIZE,*appearance.size,FALSE);
        unsigned bit=1;
        for(int control:{IDC_THEME_BOLD,IDC_THEME_ITALIC,IDC_THEME_UNDERLINE}){CheckDlgButton(window,control,*appearance.decorations&bit?BST_CHECKED:BST_UNCHECKED);bit<<=1;}
        context.displayedDecorations=*appearance.decorations;
    }
    auto foreground=std::wstring(Text(L"Foreground colour ",L"前景色 "))+HexColor(appearance.foreground,false);
    auto background=std::wstring(Text(L"Background colour ",L"背景色 "))+HexColor(appearance.background,false);
    SetDlgItemTextW(window,IDC_THEME_PICK_FORE,foreground.c_str());SetDlgItemTextW(window,IDC_THEME_PICK_BACK,background.c_str());
    for(int control:{IDC_THEME_PICK_FORE,IDC_THEME_PICK_BACK,IDC_THEME_SAMPLE})InvalidateRect(GetDlgItem(window,control),nullptr,TRUE);
}
void DrawAppearance(const DRAWITEMSTRUCT& draw,const Context& context) {
    auto rgb=[](uint32_t value){return RGB((value>>16)&255,(value>>8)&255,value&255);};
    bool sample=draw.CtlID==IDC_THEME_SAMPLE;
    RECT rectangle=draw.rcItem;
    if(!sample){
        DrawFrameControl(draw.hDC,&rectangle,DFC_BUTTON,DFCS_BUTTONPUSH|(draw.itemState&ODS_SELECTED?DFCS_PUSHED:0));
        InflateRect(&rectangle,-3,-3);
    }
    auto value=draw.CtlID==IDC_THEME_PICK_FORE?context.appearance.foreground:context.appearance.background;
    auto brush=CreateSolidBrush(rgb(value.value_or(0xffffff)));FillRect(draw.hDC,&rectangle,brush);DeleteObject(brush);
    if(sample){
        SetTextColor(draw.hDC,rgb(context.appearance.foreground.value_or(0)));SetBkMode(draw.hDC,TRANSPARENT);
        auto flags=context.appearance.decorations.value_or(0);
        auto size=std::clamp(context.appearance.size.value_or(11),8,22);
        auto font=CreateFontW(-MulDiv(size,GetDpiForWindow(draw.hwndItem),72),0,0,0,flags&1?FW_BOLD:FW_NORMAL,flags&2,flags&4,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,context.appearance.font.value_or(L"Consolas").c_str());
        auto old=SelectObject(draw.hDC,font);DrawTextW(draw.hDC,L"AaBbYyZz  0123456789",-1,&rectangle,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        SelectObject(draw.hDC,old);DeleteObject(font);
    }else if(draw.itemState&ODS_FOCUS){InflateRect(&rectangle,-2,-2);DrawFocusRect(draw.hDC,&rectangle);}
}
INT_PTR CALLBACK ReportProcedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_INITDIALOG){SetDlgItemTextW(window,IDC_THEME_REPORT,reinterpret_cast<const wchar_t*>(l));CWindow(window).CenterWindow(GetParent(window));return TRUE;}
    if(message==WM_CLOSE||(message==WM_COMMAND&&(LOWORD(w)==IDOK||LOWORD(w)==IDCANCEL))){EndDialog(window,IDOK);return TRUE;}
    return FALSE;
}
void ShowAdvanced(HWND window,Context& context) {
    context.advanced=!context.advanced;
    constexpr int controls[]={IDC_THEME_KEYWORD_GROUP,IDC_THEME_KEYWORD_LABEL,IDC_THEME_KEYWORD_SLOT,IDC_THEME_KEYWORD_MODE,IDC_THEME_KEYWORD_DEFAULT_LABEL,IDC_THEME_KEYWORD_USER_LABEL,IDC_THEME_KEYWORD_DEFAULT,IDC_THEME_KEYWORDS,IDC_THEME_EXT_GROUP,IDC_THEME_EXT_DEFAULT_LABEL,IDC_THEME_EXT_DEFAULT,IDC_THEME_EXT_USER_LABEL,IDC_THEME_EXTENSIONS,IDC_THEME_REPORT_BUTTON};
    RECT delta{0,0,0,142};MapDialogRect(window,&delta);
    int shift=context.advanced?delta.bottom:-delta.bottom;
    for(int id:{IDC_THEME_ADVANCED,IDC_THEME_STYLE_RESET,IDOK,IDCANCEL}){
        auto control=GetDlgItem(window,id);RECT r{};GetWindowRect(control,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);
        SetWindowPos(control,nullptr,r.left,r.top+shift,0,0,SWP_NOSIZE|SWP_NOZORDER);
    }
    for(int id:controls)ShowWindow(GetDlgItem(window,id),context.advanced?SW_SHOW:SW_HIDE);
    RECT outer{};GetWindowRect(window,&outer);
    SetWindowPos(window,nullptr,0,0,outer.right-outer.left,outer.bottom-outer.top+shift,SWP_NOMOVE|SWP_NOZORDER);
    SetDlgItemTextW(window,IDC_THEME_ADVANCED,context.advanced?Text(L"Advanced <<",L"收起设置 <<"):Text(L"Advanced >>",L"更多设置 >>"));
    // Keep the expanded window within the monitor's work area.
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);GetWindowRect(window,&outer);
    if(outer.bottom>monitor.rcWork.bottom)SetWindowPos(window,nullptr,outer.left,std::max(monitor.rcWork.top,monitor.rcWork.bottom-(outer.bottom-outer.top)),0,0,SWP_NOSIZE|SWP_NOZORDER);
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
int CALLBACK AddFont(const LOGFONTW* font,const TEXTMETRICW*,DWORD,LPARAM data) {
    if(font->lfFaceName[0]==L'@')return 1;
    auto names=reinterpret_cast<std::set<std::wstring>*>(data);names->insert(font->lfFaceName);return 1;
}
void InitializeFonts(HWND window) {
    std::set<std::wstring> fonts;LOGFONTW filter{};filter.lfCharSet=DEFAULT_CHARSET;auto dc=GetDC(window);
    EnumFontFamiliesExW(dc,&filter,AddFont,reinterpret_cast<LPARAM>(&fonts),0);ReleaseDC(window,dc);
    SendDlgItemMessageW(window,IDC_THEME_FONT_NAME,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(Text(L"(Inherit)",L"（继承默认）")));
    for(const auto& font:fonts)SendDlgItemMessageW(window,IDC_THEME_FONT_NAME,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(font.c_str()));
    SendDlgItemMessageW(window,IDC_THEME_FONT_SIZE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(Text(L"(Inherit)",L"（继承）")));
    for(int size:{8,9,10,11,12,14,16,18,20,22,24,26,28,36,48,72}){auto text=std::to_wstring(size);SendDlgItemMessageW(window,IDC_THEME_FONT_SIZE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
}
void ApplyPreview(HWND window,Context& context){
    auto saved=context.document.style.theme;
    try{context.document.style.theme=context.candidate;ConfigureDocumentStyle(context.document);}
    catch(...){context.document.style.theme=saved;throw;}
    SetDlgItemTextW(window,IDC_THEME_REPORT,Report(context.candidate.get()).c_str());
    SetDlgItemTextW(window,IDC_THEME_STATUS,L"");
    EnableWindow(GetDlgItem(window,IDOK),context.candidate!=context.saved||context.scopeChanged);
}
void ShowFields(HWND window,Context& context){
    context.changing=true;
    const auto& language=SelectedLanguage(context);
    const auto& id=context.styleIds.at(context.style);
    StylePatch patch;
    if(context.candidate){auto found=context.candidate->styles.find(id);if(found!=context.candidate->styles.end())patch=found->second;}
    auto heading=(context.language?Label(language.rid,language.name):std::wstring(Text(L"Global Styles",L"全局样式")))+L": "+context.styleLabels.at(context.style);
    SetDlgItemTextW(window,IDC_THEME_STYLE_INFO,heading.c_str());
    SetDlgItemTextW(window,IDC_THEME_FOREGROUND,HexColor(patch.foreground,patch.clearForeground).c_str());
    SetDlgItemTextW(window,IDC_THEME_BACKGROUND,HexColor(patch.background,patch.clearBackground).c_str());
    ShowAppearance(window,context,true);
    CheckDlgButton(window,IDC_THEME_INHERIT_FORE,!patch.foreground&&!patch.clearForeground?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(window,IDC_THEME_INHERIT_BACK,!patch.background&&!patch.clearBackground?BST_CHECKED:BST_UNCHECKED);
    SetDlgItemTextW(window,IDC_THEME_EXT_DEFAULT,language.extensions.c_str());
    std::wstring extensions;
    if(context.candidate){auto found=context.candidate->extensions.find(language.id);if(found!=context.candidate->extensions.end())extensions=found->second;}
    SetDlgItemTextW(window,IDC_THEME_EXTENSIONS,extensions.c_str());EnableWindow(GetDlgItem(window,IDC_THEME_EXTENSIONS),context.language!=0);
    auto key=std::make_pair(language.id,static_cast<unsigned>(context.slot));
    std::string words;int mode=0;
    if(context.candidate){auto found=context.candidate->keywords.find(key);if(found!=context.candidate->keywords.end()){words=found->second.substr(found->second.find(':')+1);mode=found->second.starts_with("replace:")?2:1;}}
    SendDlgItemMessageW(window,IDC_THEME_KEYWORD_MODE,CB_SETCURSEL,mode,0);
    SetDlgItemTextW(window,IDC_THEME_KEYWORDS,ConfigurationWide(words).c_str());
    SetDlgItemTextW(window,IDC_THEME_KEYWORD_DEFAULT,context.slot>=0?ConfigurationWide(language.keywords[context.slot].value_or("")).c_str():L"");
    for(int control:{IDC_THEME_KEYWORD_MODE,IDC_THEME_KEYWORDS})EnableWindow(GetDlgItem(window,control),context.slot>=0);
    context.displayed.clear();
    for(int control:{IDC_THEME_FOREGROUND,IDC_THEME_BACKGROUND,IDC_THEME_FONT_NAME,IDC_THEME_FONT_SIZE,IDC_THEME_EXTENSIONS,IDC_THEME_KEYWORDS})context.displayed[control]=Field(window,control);
    bool fontEnabled=context.language!=0||id=="editor.default"||id=="global.defaultcodestyle"||id=="global.defaulttextstyle"||id=="global.marginlinenumber"||id=="global.matchingbrace"||id=="global.matchingbraceerror"||id=="global.controlcharacter"||id=="global.foldellipsis"||id=="global.calltip"||id=="global.link";
    for(int control:{IDC_THEME_FONT_NAME,IDC_THEME_FONT_SIZE,IDC_THEME_BOLD,IDC_THEME_ITALIC,IDC_THEME_UNDERLINE})EnableWindow(GetDlgItem(window,control),fontEnabled);
    context.changing=false;
}
void ShowLanguage(HWND window,Context& context){
    context.changing=true;
    const auto& language=SelectedLanguage(context);
    auto list=GetDlgItem(window,IDC_THEME_STYLES);SendMessageW(list,LB_RESETCONTENT,0,0);context.styleIds.clear();context.styleLabels.clear();
    auto add=[&](const std::string& id,const std::wstring& label){if(std::find(context.styleIds.begin(),context.styleIds.end(),id)!=context.styleIds.end())return;context.styleIds.push_back(id);context.styleLabels.push_back(label);SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));};
    if(context.language==0){
        add("editor.default",Text(L"Default Style",L"默认样式"));
        add("editor.selection",Text(L"Selected text",L"选中文本"));
        add("editor.selection.additional",Text(L"Additional selections",L"附加选区"));
        add("editor.caret",Text(L"Caret",L"光标"));
        add("editor.caret.additional",Text(L"Additional carets",L"附加光标"));
        add("editor.fold.margin",Text(L"Fold margin",L"折叠边栏"));
    }
    for(const auto& style:language.Styles){
        // The editor roles above also cover the legacy selection/caret aliases.
        if(style.id==L"global.selectedtext"||style.id==L"global.caret")continue;
        add(ConfigurationUtf8(style.id),Label(style.rid,style.name));
    }
    if(IsDlgButtonChecked(window,IDC_THEME_DETAILS)==BST_CHECKED)for(const auto& category:language.categories)add(category.id,CategoryLabel(language,category));
    context.style=0;SendMessageW(list,LB_SETCURSEL,0,0);
    auto slots=GetDlgItem(window,IDC_THEME_KEYWORD_SLOT);SendMessageW(slots,CB_RESETCONTENT,0,0);context.slot=-1;
    for(unsigned i=0;i<language.keywordCount;++i)if(!((language.keywordAttr>>(4*i))&KeywordAttr_NoLexer)){
        auto label=L"Set "+std::to_wstring(i+1);auto index=SendMessageW(slots,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));SendMessageW(slots,CB_SETITEMDATA,index,i);if(context.slot<0)context.slot=static_cast<int>(i);
    }
    SendMessageW(slots,CB_SETCURSEL,0,0);EnableWindow(slots,context.slot>=0);
    context.changing=false;ShowFields(window,context);
}
void ReadFields(HWND window,Context& context,bool resetStyle=false){
    if(context.changing||context.styleIds.empty())return;
    auto theme=context.candidate?std::make_shared<ThemeDefinition>(*context.candidate):std::make_shared<ThemeDefinition>();
    bool changed=false;
    auto edited=[&](int id){return Field(window,id)!=context.displayed.at(id);};
    if(!resetStyle){
        const auto& id=context.styleIds.at(context.style);
        StylePatch patch;auto found=theme->styles.find(id);if(found!=theme->styles.end())patch=found->second;
        if(edited(IDC_THEME_FOREGROUND)){ReadColor(Field(window,IDC_THEME_FOREGROUND),patch.foreground,patch.clearForeground);changed=true;}
        if(edited(IDC_THEME_BACKGROUND)){ReadColor(Field(window,IDC_THEME_BACKGROUND),patch.background,patch.clearBackground);changed=true;}
        if(edited(IDC_THEME_FONT_NAME)){
            auto font=Field(window,IDC_THEME_FONT_NAME);if(font==Text(L"(Inherit)",L"（继承默认）"))font.clear();if(font.size()>128||font.find_first_of(L";\r\n")!=std::wstring::npos)throw std::runtime_error("Invalid font name.");
            if(font.empty())patch.font.reset();else patch.font=font;changed=true;
        }
        if(edited(IDC_THEME_FONT_SIZE)){
            auto size=Field(window,IDC_THEME_FONT_SIZE);if(size==Text(L"(Inherit)",L"（继承）"))size.clear();patch.size.reset();
            if(!size.empty()){if(size.find_first_not_of(L"0123456789")!=std::wstring::npos||size.size()>3)throw std::runtime_error("Font size must be 2 to 512.");auto value=std::stoi(size);if(value<2||value>512)throw std::runtime_error("Font size must be 2 to 512.");patch.size=value;}changed=true;
        }
        unsigned flags=0,bit=1;
        for(int control:{IDC_THEME_BOLD,IDC_THEME_ITALIC,IDC_THEME_UNDERLINE}){auto state=IsDlgButtonChecked(window,control);if(state==BST_CHECKED)flags|=bit;bit<<=1;}
        if(flags!=context.displayedDecorations){patch.decorations=flags;changed=true;}
        if(changed)theme->styles[id]=patch;
    }
    const auto& language=SelectedLanguage(context);
    if(context.language!=0&&edited(IDC_THEME_EXTENSIONS)){
        auto text=Field(window,IDC_THEME_EXTENSIONS);
        if(text.size()>2048||text.find_first_of(L"<>|/\\:\r\n")!=std::wstring::npos)throw std::runtime_error("Use extensions separated by semicolons, without paths.");
        if(text.empty())theme->extensions.erase(language.id);else theme->extensions[language.id]=text;changed=true;
    }
    if(context.slot>=0){
        auto key=std::make_pair(language.id,static_cast<unsigned>(context.slot));auto mode=SendDlgItemMessageW(window,IDC_THEME_KEYWORD_MODE,CB_GETCURSEL,0,0);
        auto current=theme->keywords.find(key);std::optional<std::string> words;
        if(mode>0)words=(mode==2?"replace:":"append:")+ConfigurationUtf8(Field(window,IDC_THEME_KEYWORDS));
        if(words){if(current==theme->keywords.end()||current->second!=*words){theme->keywords[key]=*words;changed=true;}}
        else if(current!=theme->keywords.end()){theme->keywords.erase(current);changed=true;}
    }
    if(changed){theme->version=NextConfigurationVersion();if(theme->name.empty())theme->name=L"Custom theme";context.candidate=theme;}
    for(int control:{IDC_THEME_FOREGROUND,IDC_THEME_BACKGROUND,IDC_THEME_FONT_NAME,IDC_THEME_FONT_SIZE,IDC_THEME_EXTENSIONS,IDC_THEME_KEYWORDS})context.displayed[control]=Field(window,control);
    if(!resetStyle){unsigned flags=0,bit=1;for(int control:{IDC_THEME_BOLD,IDC_THEME_ITALIC,IDC_THEME_UNDERLINE}){if(IsDlgButtonChecked(window,control)==BST_CHECKED)flags|=bit;bit<<=1;}context.displayedDecorations=flags;}

}
void Preview(HWND window,Context& context) {
    context.candidate=context.source.empty()?nullptr:LoadTheme(context.source);
    ShowFields(window,context);ApplyPreview(window,context);
}
INT_PTR CALLBACK ThemeProcedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    auto* context=reinterpret_cast<Context*>(GetWindowLongPtrW(window,DWLP_USER));
    try {
        if(message==WM_INITDIALOG){
            context=reinterpret_cast<Context*>(l);SetWindowLongPtrW(window,DWLP_USER,l);
            auto combo=GetDlgItem(window,IDC_THEME_LIST);
            SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(Text(L"Default",L"默认主题")));
            auto add=[&](const std::filesystem::path& directory){std::error_code error;if(!std::filesystem::is_directory(directory,error))return;for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.path().extension()==L".xml"){context->files.push_back(entry.path());auto label=entry.path().stem().wstring();try{auto xml=ReadConfigurationXml(entry.path());label=xml.Attribute(L"name",label);if(label.size()>256)label.resize(256);}catch(const std::exception&){}SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}};
            add(ConfigurationDirectory()/L"themes");if(!preferencePath.empty())add(preferencePath.parent_path()/L"config/themes");
            int selected=0;if(context->saved&&!context->savedPath.empty()){context->files.emplace_back(context->savedPath);auto label=context->saved->name+L" (current)";selected=static_cast<int>(SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str())));}
            SendMessageW(combo,CB_SETCURSEL,selected,0);CheckDlgButton(window,IDC_THEME_ALL,BST_CHECKED);
            SetDlgItemTextW(window,IDC_THEME_REPORT,Report(context->saved.get()).c_str());
            context->changing=true;InitializeFonts(window);
            for(auto language:LanguageCatalog()){
                auto label=language->id=="global"?std::wstring(Text(L"Global Styles",L"全局样式")):Label(language->rid,language->name);
                SendDlgItemMessageW(window,IDC_THEME_LANGUAGE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            }
            context->language=std::max(0,LanguageIndex(context->document.style.languageId));
            SendDlgItemMessageW(window,IDC_THEME_LANGUAGE,CB_SETCURSEL,context->language,0);
            for(const wchar_t* mode:{Text(L"Inherit",L"继承默认"),Text(L"Append",L"追加"),Text(L"Replace",L"替换")})SendDlgItemMessageW(window,IDC_THEME_KEYWORD_MODE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(mode));
            context->changing=false;ShowLanguage(window,*context);EnableWindow(GetDlgItem(window,IDOK),FALSE);
            CWindow(window).CenterWindow(GetParent(window));return TRUE;
        }
        if(!context)return FALSE;
        if(message==WM_CLOSE||(message==WM_COMMAND&&LOWORD(w)==IDCANCEL)){
            context->document.style.theme=context->saved;context->document.style.themePath=context->savedPath;ConfigureDocumentStyle(context->document);EndDialog(window,IDCANCEL);return TRUE;
        }
        if(message==WM_DRAWITEM){auto& draw=*reinterpret_cast<DRAWITEMSTRUCT*>(l);if(draw.CtlID==IDC_THEME_PICK_FORE||draw.CtlID==IDC_THEME_PICK_BACK||draw.CtlID==IDC_THEME_SAMPLE){DrawAppearance(draw,*context);return TRUE;}}
        if(message==WM_CTLCOLORSTATIC&&reinterpret_cast<HWND>(l)==GetDlgItem(window,IDC_THEME_STYLE_INFO)){SetTextColor(reinterpret_cast<HDC>(w),GetSysColor(COLOR_HOTLIGHT));SetBkMode(reinterpret_cast<HDC>(w),TRANSPARENT);return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_3DFACE));}
        if(message==WM_TIMER&&w==1){KillTimer(window,1);ReadFields(window,*context);ApplyPreview(window,*context);ShowAppearance(window,*context,false);return TRUE;}
        if(message!=WM_COMMAND)return FALSE;
        unsigned id=LOWORD(w);
        if(context->changing)return FALSE;
        if(id==IDC_THEME_ALL){context->scopeChanged=true;EnableWindow(GetDlgItem(window,IDOK),TRUE);return TRUE;}
        if(id==IDC_THEME_ADVANCED){ShowAdvanced(window,*context);return TRUE;}
        if(id==IDC_THEME_REPORT_BUTTON){auto report=Field(window,IDC_THEME_REPORT);if(report.empty())report=Text(L"No theme warnings.",L"没有主题警告。");DialogBoxParamW(_Module.GetModuleInstance(),MAKEINTRESOURCEW(IDD_THEME_REPORT),window,ReportProcedure,reinterpret_cast<LPARAM>(report.c_str()));return TRUE;}
        if(id==IDC_THEME_ACTIONS){
            auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,IDC_THEME_RELOAD,Text(L"Reload theme",L"重新加载主题"));AppendMenuW(menu,MF_STRING,IDC_THEME_RESET,Text(L"Reset theme",L"恢复默认主题"));AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,IDC_THEME_REPORT_BUTTON,Text(L"Theme report...",L"主题报告..."));
            RECT r{};GetWindowRect(GetDlgItem(window,id),&r);auto command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN,r.right,r.bottom,0,window,nullptr);DestroyMenu(menu);if(command)SendMessageW(window,WM_COMMAND,command,0);return TRUE;
        }
        if(id==IDC_THEME_DETAILS){
            ReadFields(window,*context);auto selected=context->styleIds.at(context->style);ShowLanguage(window,*context);
            auto found=std::find(context->styleIds.begin(),context->styleIds.end(),selected);if(found!=context->styleIds.end()){context->style=static_cast<int>(found-context->styleIds.begin());SendDlgItemMessageW(window,IDC_THEME_STYLES,LB_SETCURSEL,context->style,0);ShowFields(window,*context);}return TRUE;
        }
        if(id==IDC_THEME_INHERIT_FORE||id==IDC_THEME_INHERIT_BACK){
            bool foreground=id==IDC_THEME_INHERIT_FORE;int field=foreground?IDC_THEME_FOREGROUND:IDC_THEME_BACKGROUND;
            SetDlgItemTextW(window,field,IsDlgButtonChecked(window,id)==BST_CHECKED?L"":HexColor(foreground?context->appearance.foreground:context->appearance.background,false).c_str());
            ReadFields(window,*context);ApplyPreview(window,*context);ShowFields(window,*context);return TRUE;
        }
        if((id==IDC_THEME_FONT_NAME||id==IDC_THEME_FONT_SIZE)&&(HIWORD(w)==CBN_SELCHANGE||HIWORD(w)==CBN_EDITCHANGE)){
            if(HIWORD(w)==CBN_SELCHANGE){auto index=SendDlgItemMessageW(window,id,CB_GETCURSEL,0,0);wchar_t text[256]{};SendDlgItemMessageW(window,id,CB_GETLBTEXT,index,reinterpret_cast<LPARAM>(text));context->changing=true;SetDlgItemTextW(window,id,text);context->changing=false;}
            SetTimer(window,1,180,nullptr);EnableWindow(GetDlgItem(window,IDOK),TRUE);return TRUE;
        }
        if(id==IDC_THEME_BOLD||id==IDC_THEME_ITALIC||id==IDC_THEME_UNDERLINE){ReadFields(window,*context);ApplyPreview(window,*context);ShowAppearance(window,*context,false);return TRUE;}

        if((id==IDC_THEME_LANGUAGE&&HIWORD(w)==CBN_SELCHANGE)||(id==IDC_THEME_STYLES&&HIWORD(w)==LBN_SELCHANGE)||(id==IDC_THEME_KEYWORD_SLOT&&HIWORD(w)==CBN_SELCHANGE)){
            try{ReadFields(window,*context);}catch(...){
                SendDlgItemMessageW(window,IDC_THEME_LANGUAGE,CB_SETCURSEL,context->language,0);
                SendDlgItemMessageW(window,IDC_THEME_STYLES,LB_SETCURSEL,context->style,0);
                auto combo=GetDlgItem(window,IDC_THEME_KEYWORD_SLOT);
                for(LRESULT i=0;i<SendMessageW(combo,CB_GETCOUNT,0,0);++i)if(SendMessageW(combo,CB_GETITEMDATA,i,0)==context->slot)SendMessageW(combo,CB_SETCURSEL,i,0);
                throw;
            }
            if(id==IDC_THEME_LANGUAGE){context->language=static_cast<int>(SendDlgItemMessageW(window,id,CB_GETCURSEL,0,0));ShowLanguage(window,*context);}
            else if(id==IDC_THEME_STYLES){context->style=static_cast<int>(SendDlgItemMessageW(window,id,LB_GETCURSEL,0,0));ShowFields(window,*context);}
            else {auto index=SendDlgItemMessageW(window,id,CB_GETCURSEL,0,0);context->slot=static_cast<int>(SendDlgItemMessageW(window,id,CB_GETITEMDATA,index,0));ShowFields(window,*context);}
            ApplyPreview(window,*context);return TRUE;
        }
        if(id==IDC_THEME_PICK_FORE||id==IDC_THEME_PICK_BACK){
            int field=id==IDC_THEME_PICK_FORE?IDC_THEME_FOREGROUND:IDC_THEME_BACKGROUND;
            static COLORREF colors[16]{};CHOOSECOLORW choose{sizeof(choose)};choose.hwndOwner=window;choose.lpCustColors=colors;choose.Flags=CC_FULLOPEN|CC_RGBINIT;
            std::optional<uint32_t> value;bool clear=false;ReadColor(Field(window,field),value,clear);if(!value)value=id==IDC_THEME_PICK_FORE?context->appearance.foreground:context->appearance.background;if(value)choose.rgbResult=RGB((*value>>16)&255,(*value>>8)&255,*value&255);
            if(ChooseColorW(&choose)){auto rgb=choose.rgbResult;SetDlgItemTextW(window,field,HexColor((GetRValue(rgb)<<16)|(GetGValue(rgb)<<8)|GetBValue(rgb),false).c_str());ReadFields(window,*context);ApplyPreview(window,*context);ShowFields(window,*context);}return TRUE;
        }
        if(id==IDC_THEME_KEYWORDS&&HIWORD(w)==EN_CHANGE&&context->slot>=0){
            if(SendDlgItemMessageW(window,IDC_THEME_KEYWORD_MODE,CB_GETCURSEL,0,0)==0&&!Field(window,id).empty())SendDlgItemMessageW(window,IDC_THEME_KEYWORD_MODE,CB_SETCURSEL,1,0);
            SetTimer(window,1,180,nullptr);EnableWindow(GetDlgItem(window,IDOK),TRUE);return TRUE;
        }
        if(id==IDC_THEME_KEYWORD_MODE&&HIWORD(w)==CBN_SELCHANGE){SetTimer(window,1,180,nullptr);EnableWindow(GetDlgItem(window,IDOK),TRUE);return TRUE;}
        if(id==IDC_THEME_EXTENSIONS&&HIWORD(w)==EN_CHANGE){SetTimer(window,1,180,nullptr);EnableWindow(GetDlgItem(window,IDOK),TRUE);return TRUE;}
        if(id==IDC_THEME_STYLE_RESET){
            ReadFields(window,*context,true);if(context->candidate){auto theme=std::make_shared<ThemeDefinition>(*context->candidate);auto key=context->styleIds.at(context->style);theme->styles.erase(key);theme->legacy.erase(key);theme->version=NextConfigurationVersion();context->candidate=theme;}ShowFields(window,*context);ApplyPreview(window,*context);return TRUE;
        }
        if(id==IDC_THEME_PREVIEW){ReadFields(window,*context);ApplyPreview(window,*context);ShowFields(window,*context);return TRUE;}
        if(id==IDC_THEME_LIST&&HIWORD(w)==CBN_SELCHANGE){
            int index=static_cast<int>(SendDlgItemMessageW(window,IDC_THEME_LIST,CB_GETCURSEL,0,0));
            try{ReadFields(window,*context);}catch(...){
                int previous=0;for(size_t i=0;i<context->files.size();++i)if(context->files[i]==context->source)previous=static_cast<int>(i+1);
                SendDlgItemMessageW(window,IDC_THEME_LIST,CB_SETCURSEL,previous,0);throw;
            }
            context->drafts[context->source]=context->candidate;
            auto source=index>0?context->files.at(index-1):std::filesystem::path{};
            auto draft=context->drafts.find(source);
            auto candidate=draft!=context->drafts.end()?draft->second:source.empty()?nullptr:LoadTheme(source);
            context->source=source;context->candidate=candidate;ShowFields(window,*context);ApplyPreview(window,*context);return TRUE;
        }
        if(id==IDC_THEME_IMPORT){
            CFileDialog file(TRUE,L"xml",nullptr,OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR,L"MultiPad4 theme XML (*.xml)\0*.xml\0\0",window);
            if(file.DoModal()==IDOK){
                auto candidate=LoadTheme(file.m_szFileName);ReadFields(window,*context);context->drafts[context->source]=context->candidate;
                context->source=file.m_szFileName;context->candidate=candidate;context->files.push_back(context->source);
                auto label=candidate->name;auto index=SendDlgItemMessageW(window,IDC_THEME_LIST,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));SendDlgItemMessageW(window,IDC_THEME_LIST,CB_SETCURSEL,index,0);
                ShowFields(window,*context);ApplyPreview(window,*context);
            }return TRUE;
        }
        if(id==IDC_THEME_RELOAD){
            if(context->source.empty())context->source=ConfigurationDirectory()/L"themes"/(context->document.style.dark?L"dark.xml":L"light.xml");
            Preview(window,*context);return TRUE;
        }
        if(id==IDC_THEME_RESET){context->source.clear();SendDlgItemMessageW(window,IDC_THEME_LIST,CB_SETCURSEL,0,0);Preview(window,*context);return TRUE;}
        if(id==IDOK){
            ReadFields(window,*context);
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
    }catch(const std::exception& error){SetDlgItemTextW(window,IDC_THEME_STATUS,ConfigurationWide(error.what()).c_str());SetDlgItemTextW(window,IDC_THEME_REPORT,ConfigurationWide(error.what()).c_str());}
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
