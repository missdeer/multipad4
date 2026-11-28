#include "LanguageConfiguration.h"
#include "SimdGraphics.h"
#include "SimdText.h"
#include "StyleManager.h"
#include "ConfigurationXml.h"
#include "ThemeDialog.h"
#include <chrono>
#include "DialogResizeGrip.h"
#include "StyleState.h"
#include "CompletionEngine.h"
#include "LexerDetection.h"
#include "EditorDialogs.h"
#include "LaTeXInput.h"
#include "../3rdparty/notepad4/src/EditLexers/EditStyle.h"
#include <imm.h>
#include <shlwapi.h>
#include "resource.h"
#include "../3rdparty/notepad4/src/EditLexer.h"
#include "LexerCatalog.h"
#include <set>
#include <cwctype>
#include <sstream>
namespace {
COLORREF customColors[16]{};
constexpr size_t Count = sizeof(lexerCatalog) / sizeof(lexerCatalog[0]);
int defaultScheme = 1;
bool autoSelectScheme = true;
std::vector<int> favoriteSchemes;
bool FavoriteContains(int id){return std::find(favoriteSchemes.begin(),favoriteSchemes.end(),id)!=favoriteSchemes.end();}
std::wstring LocalizedLabel(UINT id,const wchar_t* fallback){
    auto instance=GetModuleHandleW(nullptr);auto resource=FindResourceExW(instance,RT_STRING,MAKEINTRESOURCEW((id>>4)+1),GetThreadUILanguage());
    if(!resource)return fallback;auto loaded=LoadResource(instance,resource);auto* cursor=static_cast<const wchar_t*>(LockResource(loaded));if(!cursor)return fallback;
    for(UINT index=0;index<(id&15);++index)cursor+=*cursor+1;
    return *cursor?std::wstring(cursor+1,*cursor):std::wstring(fallback);
}
bool Starts(const char* value,const char* prefix){return strncmp(value,prefix,strlen(prefix))==0;}
const LanguageDefinition* SniffShebang(char* pchText,int& dialect) {
	if (pchText[0] == '#' && pchText[1] == '!') {
		size_t len = 0;
		char *pch = pchText + CSTRLEN("#!");
		char *name;
		while (*pch == ' ' || *pch == '\t') {
			pch++;
		}
		name = pch;
		while (*pch && !isspace(static_cast<unsigned char>(*pch))) {
			len = *pch == '\\' || *pch == '/';
			pch++;
			name = len ? pch : name;
		}
		if (pch >= name + CSTRLEN("env") && (Starts(name, "env") || Starts(name, "winpty"))) {
			while (*pch == ' ' || *pch == '\t') {
				pch++;
			}
			name = pch;
			while (*pch && !isspace(static_cast<unsigned char>(*pch))) {
				len = *pch == '\\' || *pch == '/';
				pch++;
				name = len ? pch : name;
			}
		}

		*pch = '\0';
		len = pch - name;
		pch = name;
		while (*pch) {
			*pch = static_cast<char>(tolower(static_cast<unsigned char>(*pch)));
			if (*pch < 'a' || *pch > 'z') {
				*pch = '\0';
				len = pch - name;
				break;
			}
			pch++;
		}

		if (len >= 4) {
			if (len >= 5) {
				if (Starts(name, "python")) {
					return &Language("python");
				}
				if (Starts(name, "groovy")) {
					return &Language("groovy");
				}
				if (Starts(name, "scala")) {
					return &Language("scala");
				}
				if (Starts(name, "rscript")) {
					return &Language("r");
				}
				if (Starts(name, "escript")) {
					return &Language("erlang");
				}
			}

			if (Starts(name, "bash") || Starts(name, "dash")) {
				return &Language("bash");
			}
			if (Starts(name, "tcsh")) {
				dialect = IDM_LEXER_CSHELL;
				return &Language("bash");
			}
			if (Starts(name, "perl")) {
				return &Language("perl");
			}
			if (Starts(name, "ruby")) {
				return &Language("ruby");
			}
			//if (Starts(name, "rust")) {
			//	return &Language("rust");
			//}
			if (Starts(name, "gawk") || Starts(name, "nawk")) {
				return &Language("awk");
			}
			if (Starts(name, "node")) {
				return &Language("javascript");
			}
			if (Starts(name, "wish")) {
				return &Language("tcl");
			}
			if (Starts(name, "wlua")) {
				return &Language("lua");
			}
		}
		if (len >= 3) {
			if (Starts(name, "awk")) {
				return &Language("awk");
			}
			if (Starts(name, "lua")) {
				return &Language("lua");
			}
			if (Starts(name, "php")) {
				return &Language("php");
			}
			if (Starts(name, "tcl")) {
				return &Language("tcl");
			}
			if (Starts(name, "ash") || Starts(name, "zsh") || Starts(name, "ksh")) {
				return &Language("bash");
			}
			if (Starts(name, "csh")) {
				dialect = IDM_LEXER_CSHELL;
				return &Language("bash");
			}
			if (Starts(name, "ipy")) {
				return &Language("python");
			}
		}
		if (len >= 2) {
			if (Starts(name, "sh")) {
				return &Language("bash");
			}
			if (Starts(name, "py")) {
				return &Language("python");
			}
		}
	}
	return nullptr;
}
std::filesystem::path SchemeFilename(const EditorDocument& d){
    auto name=d.path.filename();
    while(_wcsicmp(name.extension().c_str(),L".in")==0)name=name.stem();
    return name;
}
struct AssociationIndex {
    std::shared_ptr<const ConfigurationSnapshot> base;
    std::shared_ptr<const ThemeDefinition> theme;
    std::map<std::wstring,int> schemes;
    std::map<std::pair<std::string,std::wstring>,int> dialects;
};
const AssociationIndex& Associations(const EditorDocument& d) {
    // Only the UI thread resolves document schemes; retain one configuration/theme pair.
    static AssociationIndex cache;
    auto base=CurrentConfiguration();
    if(cache.base==base && cache.theme==d.style.theme)return cache;
    AssociationIndex next;next.base=base;next.theme=d.style.theme;
    for(size_t i=1;i<Count;++i){
        const auto& lexer=*lexerCatalog[i];
        if(lexer.pszDefExt){
            std::wstring associations=lexer.pszDefExt;
            if(next.theme){auto extra=next.theme->extensions.find(lexer.id);if(extra!=next.theme->extensions.end())associations+=L";"+extra->second;}
            std::wistringstream fields(associations);std::wstring entry;
            while(std::getline(fields,entry,L';')){
                entry.erase(0,entry.find_first_not_of(L" \t"));auto end=entry.find_last_not_of(L" \t");if(end!=std::wstring::npos)entry.resize(end+1);
                for(auto& ch:entry)ch=static_cast<wchar_t>(towlower(ch));
                if(!entry.empty())next.schemes.try_emplace(entry,static_cast<int>(i));
            }
        }
        for(const auto& variant:lexer.variants){
            std::wistringstream fields(variant.extensions);std::wstring entry;
            while(std::getline(fields,entry,L';'))next.dialects.try_emplace(std::pair{lexer.id,entry},variant.command);
        }
    }
    cache=std::move(next);return cache;
}
int ResolveScheme(const EditorDocument& d) {
    if (d.style.scheme > 0 && d.style.scheme < static_cast<int>(Count)) return d.style.scheme;
    if (!autoSelectScheme) return defaultScheme;
    auto filename=SchemeFilename(d);auto name=filename.wstring(),extension=filename.extension().wstring();
    if (!extension.empty()) extension.erase(0, 1);
    auto lower = [](std::wstring& s) { for (auto& c : s) c = static_cast<wchar_t>(towlower(c)); };
    lower(name); lower(extension);
    if (name == L"makefile" || name == L"gnumakefile") extension = L"mak";
    if (name == L"cmakelists.txt" || name==L"cmakecache.txt") extension = L"cmake";
    const std::pair<const wchar_t*,const wchar_t*> names[]={{L"cakefile",L"coffee"},{L"rakefile",L"rb"},{L"podfile",L"rb"},{L"steepfile",L"rb"},{L"mozconfig",L"sh"},{L"apkbuild",L"sh"},{L"pkgbuild",L"sh"},{L"jamroot",L"jam"},{L"emakefile",L"erl"}};
    for(const auto& entry:names)if(name==entry.first){extension=entry.second;break;}
    if(name.starts_with(L"kbuild") || name.starts_with(L"makefile"))extension=L"mak";
    if(name.starts_with(L"jamfile"))extension=L"jam";
    if(name.starts_with(L"kconfig") || name.starts_with(L"doxyfile"))extension=L"conf";
    if(name.starts_with(L"readme"))extension=L"txt";
    if(extension==L"m" && d.hwnd){int dialect=0;auto sniffed=DetectObjCAndMatlab(d,dialect);if(sniffed)for(size_t i=1;i<Count;++i)if(lexerCatalog[i]->rid==sniffed->rid)return static_cast<int>(i);}
    if (name.starts_with(L".bash") || name == L".profile") extension = L"sh";
    int matched=0;
    if(extension==L"r" && d.hwnd){char prefix[10]{};d.Send(SCI_GETTEXT,sizeof(prefix)-1,reinterpret_cast<LPARAM>(prefix));if(_strnicmp(prefix,"rebol",5)==0 && (prefix[5]==' ' || prefix[5]=='\t' || prefix[5]=='['))for(size_t i=1;i<Count;++i)if(lexerCatalog[i]->rid==NP2LEX_REBOL)return static_cast<int>(i);}
    if(name.starts_with(L"my") && (extension==L"ini" || extension==L"cnf"))for(size_t i=1;i<Count;++i)if(lexerCatalog[i]->rid==NP2LEX_CONFIG){matched=static_cast<int>(i);break;}
    if(!matched && !extension.empty()){
        const auto& index=Associations(d).schemes;auto found=index.find(extension);if(found!=index.end())matched=found->second;
    }
    if(matched && lexerCatalog[matched]->iLexer!=SCLEX_CONFIG)return matched;
    if(d.hwnd){
        char prefix[512]{};d.Send(SCI_GETTEXT,sizeof(prefix)-1,reinterpret_cast<LPARAM>(prefix));int dialect=0;
        auto sniffed=SniffShebang(prefix,dialect);
        if(!sniffed){const char* first=prefix;while(*first&&isspace(static_cast<unsigned char>(*first)))++first;
            if(Starts(first,"<?xml"))sniffed=&Language("xml");
            else if(Starts(first,"<?php"))sniffed=&Language("php");
            else if(_strnicmp(first,"<html",5)==0 || (_strnicmp(first,"<!doctype",9)==0 && StrStrIA(first,"html")))sniffed=&Language("html");
            else if(*first=='<' && (isalpha(static_cast<unsigned char>(first[1])) || first[1]=='!' || first[1]=='?' || first[1]=='/'))sniffed=&Language("xml");
        }
        if(sniffed)for(size_t i=1;i<Count;++i)if(lexerCatalog[i]->rid==sniffed->rid)return static_cast<int>(i);
    }
    if(!matched && d.hwnd){auto detected=AutoDetectContent(d,name.starts_with(L"."));if(detected)for(size_t i=1;i<Count;++i)if(lexerCatalog[i]->id==detected->id)return static_cast<int>(i);}
    return matched?matched:defaultScheme;
}
int ActualScheme(const EditorDocument& d){return d.style.resolvedScheme>0 && d.style.resolvedScheme<static_cast<int>(Count)?d.style.resolvedScheme:ResolveScheme(d);}
std::wstring ResolveStyleValue(const EditorDocument& d, const LanguageDefinition& lexer, unsigned index) {
    std::wstring value=lexer.Styles[index].pszDefault;
    auto id=ConfigurationUtf8(lexer.Styles[index].id);
    value+=L"; "+ThemeStyleValue(*DefaultTheme(d.style.paletteDark),id);
    if(d.style.theme)value+=L"; "+ThemeStyleValue(*d.style.theme,id);
    auto local=d.style.overrides.find((static_cast<unsigned>(lexer.rid)<<8)|index);
    if(local!=d.style.overrides.end())value+=L"; "+local->second;
    auto semantic=d.style.semanticOverrides.find(id);if(semantic!=d.style.semanticOverrides.end())value+=L"; "+semantic->second;
    return value;
}
std::wstring Value(const EditorDocument& d,const LanguageDefinition& lexer,unsigned index){
    if(d.style.configuration){auto found=d.style.configuration->styleValues.find({lexer.id,index});if(found!=d.style.configuration->styleValues.end())return found->second;}
    return ResolveStyleValue(d,lexer,index);
}
std::wstring UserStyleValue(const EditorDocument& d,const LanguageDefinition& language,unsigned index){
    auto local=d.style.overrides.find((static_cast<unsigned>(language.rid)<<8)|index);
    return local!=d.style.overrides.end()?local->second:std::wstring{};
}
unsigned StyleProperty(const std::wstring& value,const wchar_t* name,unsigned fallback){
    auto at=value.rfind(name);if(at==std::wstring::npos)return fallback;auto* first=value.c_str()+at+wcslen(name);
    if(wcsncmp(first,L"inherit",7)==0)return fallback;
    if(*first=='#'){unsigned color=0;if(swscanf_s(first,L"#%x",&color)==1)return RGB((color>>16)&255,(color>>8)&255,color&255);return fallback;}
    return static_cast<unsigned>(wcstoul(first,nullptr,10));
}
void HighlightCurrentLine(EditorDocument& d){
    d.Send(SCI_SETCARETLINEHIGHLIGHTSUBLINE,d.style.highlightSubline);
    if(!d.style.lineHighlight){d.Send(SCI_RESETELEMENTCOLOUR,SC_ELEMENT_CARET_LINE_BACK);return;}
    auto value=Value(d,*lexerCatalog[0],9);bool outline=d.style.lineHighlight==2;
    auto color=StyleProperty(value,outline?L"fore:":L"back:",outline?RGB(194,192,195):RGB(255,255,0));
    int frame=outline?std::max(1,MulDiv(static_cast<int>(StyleProperty(value,L"size:",2)),GetDpiForWindow(d.hwnd)*static_cast<int>(d.Send(SCI_GETZOOM)),96*100)):0;
    d.Send(SCI_SETCARETLINEFRAME,frame);d.Send(SCI_SETCARETLINELAYER,SC_LAYER_UNDER_TEXT);
    auto alpha=std::min(StyleProperty(value,outline?L"outline:":L"alpha:",255),255u);d.Send(SCI_SETELEMENTCOLOUR,SC_ELEMENT_CARET_LINE_BACK,(color&0xFFFFFF)|(alpha<<24));
}
void ApplyStyle(EditorDocument& d, unsigned style, const std::wstring& value) {
    std::wistringstream fields(value); std::wstring entry;
    while (std::getline(fields, entry, L';')) {
        entry.erase(0, entry.find_first_not_of(L" \t"));
        auto colon = entry.find(L':'); auto key = entry.substr(0,colon);
        auto val = colon == std::wstring::npos ? L"" : entry.substr(colon+1);
        while(!key.empty() && iswspace(key.back()))key.pop_back();
        while(!val.empty() && iswspace(val.front()))val.erase(val.begin());while(!val.empty()&&iswspace(val.back()))val.pop_back();
        if (key == L"fore" || key == L"back") {
            unsigned color = 0; if(val==L"inherit"){d.Send(key==L"fore"?SCI_STYLESETFORE:SCI_STYLESETBACK,style,d.Send(key==L"fore"?SCI_STYLEGETFORE:SCI_STYLEGETBACK,STYLE_DEFAULT));}
            else if (swscanf_s(val.c_str(), L"#%x", &color) == 1) {
                auto rgb = RGB((color>>16)&255,(color>>8)&255,color&255);
                d.Send(key == L"fore" ? SCI_STYLESETFORE : SCI_STYLESETBACK,style,rgb);
            }
        } else if (key == L"font") {
            while(!val.empty() && iswspace(val.front()))val.erase(val.begin());while(!val.empty()&&iswspace(val.back()))val.pop_back();
            if(val==L"$(Code)")val=d.style.codeFont.empty()?d.style.font:d.style.codeFont;
            else if(val==L"$(Text)")val=d.style.textFont.empty()?d.style.font:d.style.textFont;
            if(val.empty()){if(style==STYLE_DEFAULT)val=d.style.font;else{char inherited[256]{};d.Send(SCI_STYLEGETFONT,STYLE_DEFAULT,reinterpret_cast<LPARAM>(inherited));val=files::FromUtf8(inherited);}}
            if (val.find(L"$(") == std::wstring::npos) { auto face = files::ToUtf8(val); d.Send(SCI_STYLESETFONT,style,reinterpret_cast<LPARAM>(face.c_str())); }
        } else if (key == L"size") {
            double size = wcstod(val.c_str(), nullptr);
            if(size==0)size=static_cast<double>(d.Send(SCI_STYLEGETSIZE,STYLE_DEFAULT));
            if (val.starts_with(L"+") || val.starts_with(L"-")) size += d.Send(SCI_STYLEGETSIZE,STYLE_DEFAULT);
            d.Send(SCI_STYLESETSIZEFRACTIONAL,style,static_cast<int>(std::clamp(size,2.0,512.0)*100));
        } else if (key == L"bold") d.Send(SCI_STYLESETBOLD,style,val!=L"0");
        else if (key == L"italic") d.Send(SCI_STYLESETITALIC,style,val!=L"0");
        else if (key == L"underline") d.Send(SCI_STYLESETUNDERLINE,style,val!=L"0");
        else if (key == L"strikeout") d.Send(SCI_STYLESETSTRIKE,style,TRUE);
        else if (key == L"overline") d.Send(SCI_STYLESETOVERLINE,style,TRUE);
        else if (key == L"eolfilled") d.Send(SCI_STYLESETEOLFILLED,style,TRUE);
        else if (key == L"charset") d.Send(SCI_STYLESETCHARACTERSET,style,wcstol(val.c_str(),nullptr,10));
        else if (key == L"case") d.Send(SCI_STYLESETCASE,style,val==L"u" ? SC_CASE_UPPER : val==L"l" ? SC_CASE_LOWER : SC_CASE_MIXED);
        else if (key == L"weight") d.Send(SCI_STYLESETWEIGHT,style,wcstol(val.c_str(),nullptr,10));
    }
}
int SchemeForCommand(UINT id) {
    if(id<IDM_LEXER_TEXTFILE||id>=IDM_LEXER_LEXER_COUNT)return 0;
    const auto& commands=CurrentConfiguration()->commandLanguages;auto found=commands.find(static_cast<int>(id));return found==commands.end()?0:found->second;
}
int Dialect(const EditorDocument& d, const LanguageDefinition& lexer,int* command=nullptr) {
    int id=d.style.dialect;
    if(!id){
        auto filename=SchemeFilename(d);auto extension=filename.extension().wstring();for(auto& ch:extension)ch=static_cast<wchar_t>(towlower(ch));
        const auto& index=Associations(d).dialects;auto found=index.find({lexer.id,extension});if(found!=index.end())id=found->second;
        auto name=filename.wstring();for(auto& ch:name)ch=static_cast<wchar_t>(towlower(ch));
        if(lexer.rid==NP2LEX_MATLAB && extension==L".m"){int sniffedDialect=0;DetectObjCAndMatlab(d,sniffedDialect);if(sniffedDialect)id=sniffedDialect;}
        if(lexer.rid==NP2LEX_CONFIG && (name.starts_with(L"httpd") || extension==L".htaccess"))id=IDM_LEXER_APACHE;
        if((extension==L".aspx" || extension==L".asp") && d.hwnd){char text[1024]{};d.Send(SCI_GETTEXT,sizeof(text)-1,reinterpret_cast<LPARAM>(text));for(auto& ch:text)ch=static_cast<char>(tolower(static_cast<unsigned char>(ch)));if(strstr(text,"language=\"vb") || strstr(text,"language='vb"))id=extension==L".aspx"?IDM_LEXER_ASPX_VB:IDM_LEXER_ASP_VBS;else if(strstr(text,"language=\"jscript")||strstr(text,"language='jscript")||strstr(text,"language=\"javascript"))id=IDM_LEXER_ASP_JS;}
    }
    if(!d.style.dialect && (lexer.rid==NP2LEX_HTML || lexer.rid==NP2LEX_XML)){auto markup=DetectDocumentMarkupDialect(d);if(markup)id=markup;}
    if(command)*command=id;
    if(lexer.rid==NP2LEX_CSV)return d.style.csvOptions;
    for(const auto& variant:lexer.variants)if(variant.command==id)return variant.parameter;
    return lexer.defaultDialect;
}

std::string Range(EditorDocument& d,LRESULT start,LRESULT end){
    if(end<start)return {};std::string text(static_cast<size_t>(end-start)+1,'\0');Sci_TextRangeFull range{{start,end},text.data()};d.Send(SCI_GETTEXTRANGEFULL,0,reinterpret_cast<LPARAM>(&range));text.resize(static_cast<size_t>(end-start));return text;
}
int CompletionOption(const EditorDocument& d,int id,int fallback=1) {
    auto found=d.style.completionOptions.find(id);return found==d.style.completionOptions.end()?fallback:found->second;
}
int StyleCategory(const LanguageDefinition& lexer,int style){
    const auto& cache=GetCompletionCache(lexer);
    if(cache.Test(cache.CommentStyleMask,style))return 1;
    if(cache.Test(cache.AllStringStyleMask,style))return 2;
    if(cache.Test(cache.PlainTextStyleMask,style))return 3;
    return 0;
}
void GotoBlock(EditorDocument& d,int menu) {
	const LRESULT iCurPos = d.Send(SCI_GETCURRENTPOS);
	const LRESULT iCurLine = d.Send(SCI_LINEFROMPOSITION,iCurPos);

	LRESULT iLine = iCurLine;
	int level = static_cast<int>(d.Send(SCI_GETFOLDLEVEL,iLine));
	if (!(level & SC_FOLDLEVELHEADERFLAG)) {
		iLine = d.Send(SCI_GETFOLDPARENT,iLine);
	}

	switch (menu) {
	case IDM_EDIT_GOTO_BLOCK_START:
		break;

	case IDM_EDIT_GOTO_BLOCK_END:
		if (iLine >= 0) {
			iLine = d.Send(SCI_GETLASTCHILD,iLine,-1);
		}
		break;

	case IDM_EDIT_GOTO_PREVIOUS_BLOCK:
	case IDM_EDIT_GOTO_PREV_SIBLING_BLOCK: {
		bool sibling = menu == IDM_EDIT_GOTO_PREV_SIBLING_BLOCK;
		LRESULT line = iCurLine - 1;
		LRESULT first = -1;
		level &= SC_FOLDLEVELNUMBERMASK;

		while (line >= 0) {
			const int lev = static_cast<int>(d.Send(SCI_GETFOLDLEVEL,line));
			 if ((lev & SC_FOLDLEVELHEADERFLAG) && line != iLine) {
				if (sibling) {
					if (first < 0) {
						first = line;
					}
					if (level >= (lev & SC_FOLDLEVELNUMBERMASK)) {
						iLine = line;
						sibling = false;
						break;
					}
					line = d.Send(SCI_GETFOLDPARENT,line);
					continue;
				}

				iLine = line;
				break;
			}
			--line;
		}
		if (sibling && first >= 0) {
			iLine = first;
		}
	}
	break;

	case IDM_EDIT_GOTO_NEXT_BLOCK:
	case IDM_EDIT_GOTO_NEXT_SIBLING_BLOCK: {
		const LRESULT lineCount = d.Send(SCI_GETLINECOUNT);
		if (iLine >= 0) {
			iLine = d.Send(SCI_GETLASTCHILD,iLine,-1);
		}

		bool sibling = menu == IDM_EDIT_GOTO_NEXT_SIBLING_BLOCK;
		LRESULT line = iCurLine + 1;
		LRESULT first = -1;
		if (sibling && iLine > 0 && (level & SC_FOLDLEVELHEADERFLAG)) {
			line = iLine + 1;
		}
		level &= SC_FOLDLEVELNUMBERMASK;

		while (line < lineCount) {
			const int lev = static_cast<int>(d.Send(SCI_GETFOLDLEVEL,line));
			if (lev & SC_FOLDLEVELHEADERFLAG) {
				if (sibling) {
					if (first < 0) {
						first = line;
					}
					if (level >= (lev & SC_FOLDLEVELNUMBERMASK)) {
						iLine = line;
						sibling = false;
						break;
					}
					line = d.Send(SCI_GETLASTCHILD,line,lev);
				} else {
					iLine = line;
					break;
				}
			}
			++line;
		}
		if (sibling && first >= 0) {
			iLine = first;
		}
	}
	break;
	}

	if (iLine >= 0 && iLine != iCurLine) {
		const LRESULT column = d.Send(SCI_GETCOLUMN,iCurPos);
		d.Send(SCI_GOTOPOS,d.Send(SCI_FINDCOLUMN,iLine,column));d.Send(SCI_SCROLLCARET);
	}
}
int FoldDepth(EditorDocument& d,LRESULT line){
    if(!(GetDocumentLexer(d)->lexerAttr&LexerAttr_IndentBasedFolding))return static_cast<int>((d.Send(SCI_GETFOLDLEVEL,line)&SC_FOLDLEVELNUMBERMASK)-SC_FOLDLEVELBASE);
    int depth=0;while((line=d.Send(SCI_GETFOLDPARENT,line))>=0)++depth;return depth;
}
void ToggleFoldLevels(EditorDocument& d,int wanted,bool defaults){
    if (!d.LargeFile()) d.Send(SCI_COLOURISE,0,-1);const auto* lexer=GetDocumentLexer(d);
    bool sniff=true,expand=false;int maxLevel=0;for(int i=0;i<8;++i)if(lexer->defaultFoldLevelMask&(1u<<i))maxLevel=i;
    for(LRESULT line=0;line<d.Send(SCI_GETLINECOUNT);++line){
        auto flags=d.Send(SCI_GETFOLDLEVEL,line);if(!(flags&SC_FOLDLEVELHEADERFLAG))continue;int depth=FoldDepth(d,line);
        if(defaults ? depth>=8 || !(lexer->defaultFoldLevelMask&(1u<<depth)) : depth!=wanted)continue;
        bool before=d.Send(SCI_GETFOLDEXPANDED,line)!=0;if(sniff)expand=!before;
        d.Send(SCI_FOLDLINE,line,expand?SC_FOLDACTION_EXPAND:SC_FOLDACTION_CONTRACT);
        if(before!=(d.Send(SCI_GETFOLDEXPANDED,line)!=0))sniff=false;
        bool ignoreInner=false;
        if(defaults && lexer->defaultFoldIgnoreInner){auto start=d.Send(SCI_POSITIONFROMLINE,line),end=std::min(d.Send(SCI_GETLINEENDPOSITION,line),start+1024);while(start<end)if(d.Send(SCI_GETSTYLEINDEXAT,start++)==lexer->defaultFoldIgnoreInner){ignoreInner=true;break;}}
        if(!defaults || depth==maxLevel || ignoreInner)line=d.Send(SCI_GETLASTCHILD,line,-1);
    }
    d.Send(SCI_SCROLLCARET);
}
bool LongCompletionLine(EditorDocument& d,LRESULT position) {
    return d.Send(SCI_LINELENGTH,d.Send(SCI_LINEFROMPOSITION,position))>64*1024;
}
void QueueCompletion(EditorDocument& d) {
    if(CompletionInputPending())return;
    const auto owner=GetAncestor(d.hwnd,GA_ROOT);MSG queued{};
    if(!PeekMessageW(&queued,owner,WM_COMPLETION_CONTINUE,WM_COMPLETION_CONTINUE,PM_NOREMOVE))
        PostMessageW(owner,WM_COMPLETION_CONTINUE,0,0);
}
void Complete(EditorDocument& d,bool autoInsert=false) {
    if (d.externalCompletion) return;
    if (d.Send(SCI_GETREADONLY) || d.Send(SCI_AUTOCACTIVE)) return;
    const auto requestRevision=d.style.revision,requestEpoch=d.completionEpoch;
    const auto requestConfiguration=d.style.configuration;const auto requestTheme=d.style.theme;const auto requestDialect=d.style.resolvedDialect;
    auto position=d.Send(SCI_GETCURRENTPOS), start=position;
    auto& lexer=*GetDocumentLexer(d);const auto& cache=GetCompletionCache(lexer);
    const auto lineStart=d.Send(SCI_POSITIONFROMLINE,d.Send(SCI_LINEFROMPOSITION,position));
    auto special=[](int ch){return ch=='.'||ch=='#'||ch=='@'||ch=='<'||ch=='\\'||ch==':'||ch=='$'||ch=='`'||ch=='\''||ch=='%';};
    do {
        auto before=start;start=d.Send(SCI_WORDSTARTPOSITION,before,TRUE);bool nonWord=start==before;before=d.Send(SCI_POSITIONBEFORE,start);
        if(nonWord){if(before+1!=start)break;auto ch=static_cast<unsigned>(d.Send(SCI_GETCHARAT,before));if(!cache.Word(ch)||special(static_cast<int>(ch)))break;start=before;}
        else {auto end=d.Send(SCI_WORDENDPOSITION,before,TRUE);if(end==start)break;}
    }while(start>lineStart);
    int minimum=CompletionOption(d,IDC_AUTOC_MIN_WORD_LENGTH,1);
    if(position-start<minimum || (!autoInsert && position-start>124)) { d.style.completionPending=false; return; }
    if(CompletionOption(d,IDC_AUTOC_ENGLISH_IME_ONLY,0)){
        auto context=ImmGetContext(d.hwnd);DWORD conversion=0,sentence=0;
        bool native=context && ImmGetOpenStatus(context) && ImmGetConversionStatus(context,&conversion,&sentence) && (conversion&IME_CMODE_NATIVE);
        if(context)ImmReleaseContext(d.hwnd,context);if(native)return;
    }
    auto prefix=Range(d,start,position);
    auto styled=d.Send(SCI_GETENDSTYLED);
    const bool continuePending=d.style.completionPending && d.style.completionStart==start;
    if (!autoInsert && styled<position && (continuePending || position-styled>64*1024 || LongCompletionLine(d,styled) || LongCompletionLine(d,position))) {
        auto& state=d.style;
        if (GetFocus()!=d.hwnd || d.completionComposing) { state.completionPending=false; return; }
        state.completionPending=true; state.completionStart=start; state.completionPosition=position;
        state.completionRevision=state.revision; state.completionConfiguration=state.configuration; state.completionLexer=&lexer;
        state.completionDialect=state.resolvedDialect; state.completionTheme=state.theme; state.completionEpoch=d.completionEpoch;
        PostMessageW(GetAncestor(d.hwnd,GA_ROOT),WM_DOCUMENT_CHANGED,1,0);QueueCompletion(d);return;
    }
    d.style.completionPending=false;
    if(styled<position)d.Send(SCI_COLOURISE,styled,position);
    const bool rootNumber=!prefix.empty() && isdigit(static_cast<unsigned char>(prefix[0]));
    const bool ignoreLexer=CompletionOption(d,IDC_AUTOC_ONLY_DOCUMENT_WORDS,0)!=0 || rootNumber;
    const bool ignoreCase=ignoreLexer || d.style.completionIgnoreCase;
    std::set<std::string> words,foldedWords;
    int category=StyleCategory(lexer,static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT,max<LRESULT>(0,position-1))));
    if(category==1 && !CompletionOption(d,IDC_AUTO_COMPLETE_INSIDE_COMMONT))return;
    if(category==2 && !CompletionOption(d,IDC_AUTO_COMPLETE_INSIDE_STRING))return;
    if((lexer.iLexer==SCLEX_NULL || category==3) && !CompletionOption(d,IDC_AUTO_COMPLETE_INSIDE_PLAINTEXT))return;
    if(rootNumber){int minimumNumber=CompletionOption(d,IDC_AUTOC_MIN_NUMBER_LENGTH,3);if(minimumNumber<=0)return;if(prefix.size()>1 && prefix[0]=='0' && (tolower(static_cast<unsigned char>(prefix[1]))=='x' || tolower(static_cast<unsigned char>(prefix[1]))=='b' || tolower(static_cast<unsigned char>(prefix[1]))=='o'))minimumNumber+=2;if(prefix.size()<static_cast<size_t>(minimumNumber))return;}
    auto accept=[&](const std::string& word) {
        if(word.size()<prefix.size()) return;
        bool matches=true;
        for(size_t i=0;i<prefix.size();++i) if(ignoreCase ? tolower(static_cast<unsigned char>(prefix[i]))!=tolower(static_cast<unsigned char>(word[i])) : prefix[i]!=word[i]) {matches=false;break;}
        if(matches && word.find_first_of("\r\n\t")==std::string::npos){
            if(ignoreCase){auto folded=word;for(auto& ch:folded)if(static_cast<unsigned char>(ch)<128)ch=static_cast<char>(tolower(static_cast<unsigned char>(ch)));if(!foldedWords.insert(std::move(folded)).second)return;}
            words.insert(word);
        }
    };
    auto& state=d.style;
    const auto keywordOwner=state.configuration?state.configuration:CurrentConfiguration();
    if(!state.completionKeywords || state.completionKeywords->configuration.lock()!=keywordOwner || state.completionKeywords->lexer!=&lexer || state.completionKeywords->dialect!=state.resolvedDialect){
        state.completionKeywords=std::make_shared<CompletionKeywordCache>(); state.completionKeywords->configuration=keywordOwner;
        state.completionKeywords->lexer=&lexer; state.completionKeywords->dialect=state.resolvedDialect;
    }
    auto& parsedKeywords=state.completionKeywords->words;
    auto addKeywordList=[&](const char* list){
        if(!list)return;
        auto found=parsedKeywords.find(std::string_view(list));
        if(found==parsedKeywords.end()){
            std::istringstream stream(list);std::string word;std::vector<std::string> parsed;
            while(stream>>word){for(auto& ch:word)if(ch=='^')ch=' ';auto open=word.find('(');if(open!=std::string::npos){word.resize(open+1);word+=')';}parsed.push_back(word);}
            found=parsedKeywords.emplace(std::string(list),std::move(parsed)).first;
        }
        for(const auto& word:found->second)accept(word);
    };
    auto addLexer=[&](const LanguageDefinition& source){for(unsigned i=0;i<min<unsigned>(source.keywordCount,16);++i)if(!((source.keywordAttr>>(i*4))&KeywordAttr_NoAutoComp))addKeywordList(source.pszKeyWords[i]);};
    int contextResult=0;char documentPrefix=0;
    if(!CompletionOption(d,IDC_AUTOC_ONLY_DOCUMENT_WORDS,0) && !prefix.empty() && !isdigit(static_cast<unsigned char>(prefix[0]))){
        int marker=start>lineStart?static_cast<int>(d.Send(SCI_GETCHARAT,start-1)):0;
        int previous=start>lineStart+1?static_cast<int>(d.Send(SCI_GETCHARAT,start-2)):0;
        if((lexer.lexerAttr&LexerAttr_CppPreprocessor) && (marker==' '||marker=='\t')){auto before=start-1;while(before>lineStart && (d.Send(SCI_GETCHARAT,before)==' '||d.Send(SCI_GETCHARAT,before)=='\t'))--before;if(d.Send(SCI_GETCHARAT,before)=='#'){marker='#';previous=0;}}
        if(special(marker) || (previous=='<'&&marker=='/') || (previous=='-'&&marker=='>')){
            auto at=marker==':'?d.Send(SCI_WORDSTARTPOSITION,start-1,FALSE):position;
            int previousStyle=marker==':'?static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT,at)):0;
            contextResult=AddContextCompletionWords(d,static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT,position)),previousStyle,marker,previous,addKeywordList);
            if(contextResult==2)documentPrefix=static_cast<char>((marker=='/'||marker=='>')?'<':marker);
        }
    }
    auto collectKeywords=[&](){if(contextResult || ignoreLexer)return;
        if(lexer.iLexer!=SCLEX_PHPSCRIPT)addLexer(lexer);
        const char* const* extra=DocumentCompletionExtraKeywords(d);if(!extra && category==1)extra=cache.extraKeywords;
        if(extra)for(unsigned i=0;i<4;++i)addKeywordList(extra[i]);
        int style=static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT,max<LRESULT>(0,position-1)));
        if(lexer.iLexer==SCLEX_HTML || lexer.iLexer==SCLEX_PHPSCRIPT){
            if(style>=SCE_STYLE_OFFSET_PHP)addLexer(DocumentLanguage(d,"php"));
            else if(style>=SCE_STYLE_OFFSET_CSS)addLexer(DocumentLanguage(d,"css"));
            else if(style>=SCE_STYLE_OFFSET_JS)addLexer(DocumentLanguage(d,"javascript"));
            else if(style>=SCE_HB_DEFAULT)addLexer(DocumentLanguage(d,"vbscript"));
        }
        if(lexer.rid==NP2LEX_TYPESCRIPT)addLexer(DocumentLanguage(d,"javascript"));
        if(lexer.iLexer==SCLEX_PHPSCRIPT || lexer.iLexer==SCLEX_JAVASCRIPT || lexer.iLexer==SCLEX_MARKDOWN)for(unsigned i=0;i<3 && i<DocumentLanguage(d,"html").keywordCount;++i)addKeywordList(DocumentLanguage(d,"html").pszKeyWords[i]);
    };
    collectKeywords();
    bool latexRoot=d.style.latex && start>0 && d.Send(SCI_GETCHARAT,start-1)=='\\';
    bool emojiRoot=d.style.latex && start>1 && d.Send(SCI_GETCHARAT,start-1)==':' && d.Send(SCI_GETCHARAT,start-2)=='\\';
    if(latexRoot || emojiRoot){words.clear();foldedWords.clear();addKeywordList(emojiRoot?EmojiInputSequenceString:LaTeXInputSequenceString);}
    auto collectDocument=[&](){
        if(contextResult==1 || latexRoot || emojiRoot || !CompletionOption(d,IDC_AUTOC_SCAN_DOCUMENT_WORDS))return;
        uint32_t ignored[8]{};
        for(unsigned i=0;i<8;++i){ignored[i]=cache.IgnoreWordStyleMask[i];if(category!=1&&!CompletionOption(d,IDC_SCAN_WORD_INSIDE_COMMONT))ignored[i]|=cache.CommentStyleMask[i];if(category!=2&&!CompletionOption(d,IDC_SCAN_WORD_INSIDE_STRING))ignored[i]|=cache.AllStringStyleMask[i];if(category!=3&&!CompletionOption(d,IDC_SCAN_WORD_INSIDE_PLAINTEXT))ignored[i]|=cache.PlainTextStyleMask[i];}
        const auto configured = static_cast<UINT>(CompletionOption(d,IDC_AUTOC_SCAN_WORDS_TIMEOUT,500));
        AddDocumentCompletionWords(d,prefix,ignoreCase,documentPrefix,autoInsert ? configured : (std::min)(configured, 4u),ignored,accept,autoInsert ? SIZE_MAX : 128 * 1024);
    };
    collectDocument();
    if(words.empty() && !latexRoot && !emojiRoot){
        auto separator=prefix.find_last_of(":.#@<\\/->$%");
        if(separator!=std::string::npos && separator+1<prefix.size()){
            start+=separator+1;prefix.erase(0,separator+1);documentPrefix=0;collectKeywords();collectDocument();
        }
    }
    std::string list; for(const auto& word:words) {if(!list.empty())list+='\n';list+=word;}
    if(!autoInsert && (d.style.revision!=requestRevision || d.completionEpoch!=requestEpoch || d.Send(SCI_GETCURRENTPOS)!=position || GetFocus()!=d.hwnd || d.completionComposing ||
        d.style.configuration!=requestConfiguration || d.style.theme!=requestTheme || d.style.resolvedDialect!=requestDialect || d.Send(SCI_GETSELECTIONS)!=1))return;
    if(!list.empty() && !(words.size()==1 && words.begin()->size()==prefix.size())) {d.Send(SCI_AUTOCSETSEPARATOR,'\n');d.Send(SCI_AUTOCSETIGNORECASE,ignoreCase);d.Send(SCI_AUTOCSETORDER,SC_ORDER_PERFORMSORT);d.Send(SCI_AUTOCSETCHOOSESINGLE,autoInsert);d.Send(SCI_AUTOCSHOW,prefix.size(),reinterpret_cast<LPARAM>(list.c_str()));}
}
}
size_t DocumentSchemeCount() { return Count-1; }
const LanguageDefinition* GetDocumentLexer(const EditorDocument& document) {return document.style.configuration?document.style.configuration->catalog[ActualScheme(document)]:lexerCatalog[ActualScheme(document)];}
const LanguageDefinition& DocumentLanguage(const EditorDocument& document,const std::string& id){const auto& catalog=document.style.configuration?document.style.configuration->catalog:LanguageCatalog();for(auto language:catalog)if(language->id==id)return *language;throw std::runtime_error("Unknown document language");}
const char* const* DocumentCompletionExtraKeywords(const EditorDocument& document){auto snapshot=document.style.configuration?document.style.configuration:CurrentConfiguration();auto found=snapshot->dialectPointers.find(document.style.resolvedDialect);return found==snapshot->dialectPointers.end()?nullptr:found->second.data();}
std::wstring DocumentGlobalStyleOverrides(const EditorDocument& document,unsigned index) {
    if(document.style.configuration){auto found=document.style.configuration->userStyleValues.find({"global",index});if(found!=document.style.configuration->userStyleValues.end())return found->second;}
    return UserStyleValue(document,Language("global"),index);
}
bool SelectDocumentScheme(EditorDocument& document,const std::wstring& name) {
    for(size_t i=1;i<Count;++i) {
        const auto& lexer=*lexerCatalog[i];
        bool matches=CompareStringOrdinal(lexer.pszName,-1,name.c_str(),-1,TRUE)==CSTR_EQUAL || (name.size()>=3 && _wcsnicmp(lexer.pszName,name.c_str(),name.size())==0);
        if(!matches && lexer.pszDefExt){std::wistringstream fields(lexer.pszDefExt);std::wstring extension;while(std::getline(fields,extension,L';')){extension.erase(0,extension.find_first_not_of(L" \t"));extension.erase(extension.find_last_not_of(L" \t")+1);if(CompareStringOrdinal(extension.c_str(),-1,name.c_str(),-1,TRUE)==CSTR_EQUAL){matches=true;break;}}}
        if(!matches){const auto& legacy=BuiltinLanguage(lexer.id);matches=CompareStringOrdinal(legacy.pszName,-1,name.c_str(),-1,TRUE)==CSTR_EQUAL||(name.size()>=3&&_wcsnicmp(legacy.pszName,name.c_str(),name.size())==0);}
        if(!matches)for(const auto& alias:lexer.aliases)if(CompareStringOrdinal(alias.c_str(),-1,name.c_str(),-1,TRUE)==CSTR_EQUAL){matches=true;break;}
        if(matches){document.style.scheme=static_cast<int>(i);document.style.dialect=0;ConfigureDocumentStyle(document);return true;}
    }
    return false;
}
void ForgetDocumentStyle(EditorDocument& d) {d.style.autoClosers.clear();d.style.colorStart=d.style.colorEnd=-1;d.style.calltipHex.clear();d.Send(SCI_AUTOCCANCEL);d.Send(SCI_CALLTIPCANCEL);}
void ApplyStyleTabSettings(EditorDocument&,int,int,int,bool,bool);
void ConfigureDocumentStyle(EditorDocument& d) {
    d.style.fontKind=std::clamp(d.style.fontKind,-1,1);
    d.style.paletteDark=d.style.dark;
    if(d.style.theme){auto root=d.style.theme->styles.find("editor.default");if(root!=d.style.theme->styles.end()&&root->second.background){auto color=*root->second.background;d.style.paletteDark=(((color>>16)&255)*299+((color>>8)&255)*587+(color&255)*114)<128000;}}
    const auto base=CurrentConfiguration();
    d.style.lineHighlight=std::clamp(d.style.lineHighlight,0,2);d.style.wrapMode=std::clamp(d.style.wrapMode,1,4);d.style.wrapIndent=std::clamp(d.style.wrapIndent,0,7);int symbols=std::clamp(d.style.wrapSymbols,0,32);d.style.wrapSymbols=std::clamp(symbols%10,0,2)+10*std::clamp(symbols/10,0,3);
    d.style.fontSize=std::clamp(d.style.fontSize,2,512);d.style.codeFontSize=std::clamp(d.style.codeFontSize,2,512);d.style.textFontSize=std::clamp(d.style.textFontSize,2,512);
    bool initializeAppearance=d.style.appliedTabScheme<0;
    const auto selected=d.LargeFile()?1:ResolveScheme(d);
    // Compare actual inputs, including overrides, rather than relying on a hash or revision hint.
    const auto fonts=std::tuple{d.style.font,d.style.codeFont,d.style.textFont,d.style.fontSize,d.style.codeFontSize,d.style.textFontSize,d.style.fontKind};
    auto cached=d.style.effectiveStyleInputs;
    bool reuse=cached && d.style.configuration && cached->base==base && cached->theme==d.style.theme && cached->scheme==selected && cached->paletteDark==d.style.paletteDark && cached->fonts==fonts && cached->overrides==d.style.overrides && cached->semanticOverrides==d.style.semanticOverrides;
    std::shared_ptr<const ConfigurationSnapshot> effectiveSnapshot=d.style.configuration;
    if(!reuse){
    auto candidate=d.style.theme?ConfigurationWithTheme(base,*d.style.theme):base;
    auto snapshot=std::make_shared<ConfigurationSnapshot>(*candidate);
    for(auto& [command,words]:snapshot->dialectWords){auto& pointers=snapshot->dialectPointers[command];for(size_t index=0;index<4;++index)pointers[index]=words[index]?words[index]->c_str():nullptr;}
    snapshot->version=NextConfigurationVersion();snapshot->theme=d.style.theme;snapshot->semanticStyles=d.style.semanticOverrides;
    std::set<std::string> sources{"global",snapshot->catalog[selected]->id};
    auto resource=snapshot->catalog[selected]->rid;
    if(resource==NP2LEX_HTML||resource==NP2LEX_XML||resource==NP2LEX_MARKDOWN||resource==NP2LEX_PHP)for(const char* id:{"html","javascript","css","vbscript","php"})sources.insert(id);
    for(auto language:snapshot->catalog)if(sources.contains(language->id))for(unsigned index=0;index<language->iStyleCount;++index){
        auto value=ResolveStyleValue(d,*language,index);ValidateLegacyStyle(value);snapshot->styleValues[{language->id,index}]=std::move(value);
        snapshot->userStyleValues[{language->id,index}]=UserStyleValue(d,*language,index);
    }
    for(const auto& [id,value]:snapshot->semanticStyles)ValidateLegacyStyle(value);
    snapshot->themeVersion=d.style.theme?d.style.theme->version:DefaultTheme(d.style.paletteDark)->version;
    auto hash=[&](const std::wstring& value){for(wchar_t character:value){snapshot->themeVersion^=static_cast<uint16_t>(character);snapshot->themeVersion*=1099511628211ULL;}};
    for(const auto& [key,value]:snapshot->styleValues){hash(ConfigurationWide(key.first)+std::to_wstring(key.second));hash(value);}
    for(const auto& [id,value]:snapshot->semanticStyles){hash(ConfigurationWide(id));hash(value);}
    hash(d.style.font+d.style.codeFont+d.style.textFont+std::to_wstring(d.style.fontSize)+std::to_wstring(d.style.codeFontSize)+std::to_wstring(d.style.textFontSize)+std::to_wstring(d.style.fontKind));
    effectiveSnapshot=std::move(snapshot);
    }
    // All validation and merging precedes publication and editor mutations.
    d.style.completionPending=false;d.style.completionKeywords.reset();
    d.style.configuration=std::move(effectiveSnapshot);d.style.resolvedScheme=selected;
    if(!reuse)d.style.effectiveStyleInputs=std::make_shared<EffectiveStyleInputs>(EffectiveStyleInputs{base,d.style.theme,selected,d.style.paletteDark,fonts,d.style.overrides,d.style.semanticOverrides});
    for(unsigned element:{SC_ELEMENT_SELECTION_TEXT,SC_ELEMENT_SELECTION_ADDITIONAL_TEXT,SC_ELEMENT_CARET_ADDITIONAL,SC_ELEMENT_WHITE_SPACE_BACK})d.Send(SCI_RESETELEMENTCOLOUR,element);
    auto& lexer=*d.style.configuration->catalog[selected];
    d.style.languageId=lexer.id;
    ApplyStyleTabSettings(d,selected,lexer.defaultTabWidth,lexer.defaultIndentWidth,(lexer.lexerAttr&LexerAttr_TabAsSpaces)!=0,!(lexer.lexerAttr&LexerAttr_NoGlobalTabSettings));
    d.language=lexer.pszName;
    if(lexer.rid==NP2LEX_CPP)d.language=L"C/C++";
    if(lexer.rid==NP2LEX_PYTHON)d.language=L"Python";
    if(lexer.rid==NP2LEX_JSON)d.language=L"JSON";
    if(lexer.rid==NP2LEX_TEXTFILE)d.language=L"Text";
    d.language=LocalizedLabel(lexer.rid,d.language.c_str());
    if(initializeAppearance){d.edgeMode=std::clamp(d.style.longLineMode,EDGE_LINE,EDGE_BACKGROUND);d.Send(SCI_SETEDGECOLUMN,std::clamp(d.style.longLineColumn,0,1000000));d.Send(SCI_SETEDGEMODE,d.style.longLineEnabled?d.edgeMode:EDGE_NONE);}
    ApplyDocumentWrapSettings(d);
    if (d.LargeFile()) d.Send(SCI_SETWRAPMODE, SC_WRAP_NONE);
    const auto previousDialect=d.style.resolvedDialect;
    if(lexer.rid==NP2LEX_CSV && !d.style.csvConfigured){auto ext=SchemeFilename(d).extension().wstring();int delimiter=_wcsicmp(ext.c_str(),L".tsv")==0?'\t':DetectDocumentCSVDelimiter(d);d.style.csvOptions=(static_cast<int>('"')<<8)|(delimiter?delimiter:',');}
    int effective=0;unsigned dialect=static_cast<unsigned>(Dialect(d,lexer,&effective));d.style.resolvedDialect=effective;
    // This fork stores HTML/CSV options as a little-endian four-byte property;
    // ordinary small dialects are ASCII digits (same representation as upstream).
    if(dialect<10 && lexer.rid!=NP2LEX_HTML && lexer.rid!=NP2LEX_XML && lexer.rid!=NP2LEX_CSV)dialect+='0';
    char dialectBytes[5]{};memcpy(dialectBytes,&dialect,sizeof(dialect));
    bool rulesChanged=d.style.appliedRules!=lexer.rulesVersion||d.style.appliedLexer!=lexer.iLexer||previousDialect!=effective||d.Send(SCI_GETLEXER)!=(d.LargeFile()?SCLEX_NULL:lexer.iLexer);
    if(rulesChanged){
    d.Send(SCI_SETLEXER,d.LargeFile()?SCLEX_NULL:lexer.iLexer);
    d.Send(SCI_SETPROPERTY,reinterpret_cast<WPARAM>("lexer.lang"),reinterpret_cast<LPARAM>(dialectBytes));
    d.Send(SCI_SETPROPERTY,reinterpret_cast<WPARAM>("fold"),reinterpret_cast<LPARAM>("1"));
    for(unsigned i=0;i<16;++i) {
        auto attr=(lexer.keywordAttr>>(i*4))&15;
        const char* words=i<lexer.keywordCount && !(attr&KeywordAttr_NoLexer) ? lexer.pszKeyWords[i] : "";
        d.Send(SCI_SETKEYWORDS,i|((attr&3)<<8),reinterpret_cast<LPARAM>(words?words:""));
    }
    }
    d.style.appliedRules=lexer.rulesVersion;d.style.appliedLexer=lexer.iLexer;
    d.Send(SCI_STYLERESETDEFAULT);
    bool codeFont=d.style.fontKind<0?lexer.bUseDefaultCodeStyle:d.style.fontKind==0;
    const auto& chosenFont=codeFont?d.style.codeFont:d.style.textFont;
    auto font=files::ToUtf8(chosenFont.empty()?d.style.font:chosenFont);
    d.Send(SCI_STYLESETFONT,STYLE_DEFAULT,reinterpret_cast<LPARAM>(font.c_str()));
    d.Send(SCI_STYLESETSIZE,STYLE_DEFAULT,chosenFont.empty()?d.style.fontSize:(codeFont?d.style.codeFontSize:d.style.textFontSize));
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};GetUserDefaultLocaleName(locale,LOCALE_NAME_MAX_LENGTH);auto fontLocale=files::ToUtf8(locale);d.Send(SCI_SETFONTLOCALE,0,reinterpret_cast<LPARAM>(fontLocale.c_str()));
    d.Send(SCI_STYLESETFORE,STYLE_DEFAULT,d.style.paletteDark?RGB(220,220,220):RGB(32,36,42));
    d.Send(SCI_STYLESETBACK,STYLE_DEFAULT,d.style.paletteDark?RGB(30,30,30):RGB(255,255,255));
    unsigned rootIndex=d.style.fontKind<0?(lexer.bUseDefaultCodeStyle?0:1):d.style.fontKind;
    const auto& global=*lexerCatalog[0];
    ApplyStyle(d,STYLE_DEFAULT,ThemeStyleValue(*DefaultTheme(d.style.paletteDark),"editor.default"));
    ApplyStyle(d,STYLE_DEFAULT,global.Styles[rootIndex].pszDefault);
    ApplyStyle(d,STYLE_DEFAULT,lexer.Styles[0].pszDefault);
    auto rootTheme=[&](const ThemeDefinition& theme){
        ApplyStyle(d,STYLE_DEFAULT,ThemeStyleValue(theme,ConfigurationUtf8(global.Styles[rootIndex].id)));
        ApplyStyle(d,STYLE_DEFAULT,ThemeStyleValue(theme,ConfigurationUtf8(lexer.Styles[0].id)));
    };
    rootTheme(*DefaultTheme(d.style.paletteDark));
    if(d.style.theme){ApplyStyle(d,STYLE_DEFAULT,ThemeStyleValue(*d.style.theme,"editor.default"));rootTheme(*d.style.theme);}
    auto rootOverrides=[&](const LanguageDefinition& language,unsigned index){
        auto found=d.style.configuration->userStyleValues.find({language.id,index});if(found!=d.style.configuration->userStyleValues.end())ApplyStyle(d,STYLE_DEFAULT,found->second);
        auto semantic=d.style.configuration->semanticStyles.find(ConfigurationUtf8(language.Styles[index].id));if(semantic!=d.style.configuration->semanticStyles.end())ApplyStyle(d,STYLE_DEFAULT,semantic->second);
    };
    rootOverrides(global,rootIndex);rootOverrides(lexer,0);
    if(!chosenFont.empty()){auto face=files::ToUtf8(chosenFont);d.Send(SCI_STYLESETFONT,STYLE_DEFAULT,reinterpret_cast<LPARAM>(face.c_str()));d.Send(SCI_STYLESETSIZE,STYLE_DEFAULT,codeFont?d.style.codeFontSize:d.style.textFontSize);}
    d.Send(SCI_STYLECLEARALL);
    for(unsigned index : {2u,5u,6u,16u,19u})ApplyStyle(d,lexerCatalog[0]->Styles[index].iStyle,Value(d,*lexerCatalog[0],index));
    auto applyAll=[&](const LanguageDefinition& styles,unsigned offset,unsigned limit=0){
        for(unsigned i=1;i<styles.iStyleCount;++i){auto packed=styles.Styles[i].iStyle;const auto value=Value(d,styles,i);const unsigned first=(packed&255)+offset;
            ApplyStyle(d,first,value);
            if(packed>255)d.Send(SCI_COPYSTYLES,first|(offset<<8),packed>>8);
            if(limit && styles.Styles[i].iStyle==limit)break;
        }
    };
    applyAll(lexer,0);
    ApplyStyle(d,STYLE_LINK,Value(d,*lexerCatalog[0],21));
    d.Send(SCI_STYLESETUNDERLINE,STYLE_LINK,TRUE);
    d.Send(SCI_STYLESETHOTSPOT,STYLE_LINK,TRUE);
    if(lexer.rid==NP2LEX_HTML || lexer.rid==NP2LEX_XML || lexer.rid==NP2LEX_MARKDOWN || lexer.rid==NP2LEX_PHP){
        if(lexer.rid!=NP2LEX_MARKDOWN){applyAll(Language("javascript"),SCE_STYLE_OFFSET_JS);applyAll(Language("css"),SCE_STYLE_OFFSET_CSS);}
        if(lexer.rid==NP2LEX_MARKDOWN || lexer.rid==NP2LEX_PHP)applyAll(Language("html"),0,SCE_H_QUESTION);
    }
    auto applyCategories=[&](const LanguageDefinition& language,unsigned offset=0){
        for(const auto& category:language.categories){
            unsigned target=category.nativeStyle+offset;
            ApplyStyle(d,target,ThemeStyleValue(*DefaultTheme(d.style.paletteDark),category.id));
            if(d.style.theme)ApplyStyle(d,target,ThemeStyleValue(*d.style.theme,category.id));
            for(unsigned i=0;i<language.iStyleCount;++i)if(ConfigurationUtf8(language.Styles[i].id)==category.group){
                auto user=d.style.configuration->userStyleValues.find({language.id,i});if(user!=d.style.configuration->userStyleValues.end())ApplyStyle(d,target,user->second);
                auto semantic=d.style.configuration->semanticStyles.find(category.group);if(semantic!=d.style.configuration->semanticStyles.end())ApplyStyle(d,target,semantic->second);
                break;
            }
            auto local=d.style.configuration->semanticStyles.find(category.id);if(local!=d.style.configuration->semanticStyles.end())ApplyStyle(d,target,local->second);
        }
    };
    applyCategories(lexer);
    if(lexer.rid==NP2LEX_HTML||lexer.rid==NP2LEX_XML||lexer.rid==NP2LEX_PHP){applyCategories(Language("javascript"),SCE_STYLE_OFFSET_JS);applyCategories(Language("css"),SCE_STYLE_OFFSET_CSS);}
    auto property=[&](unsigned index,const wchar_t* name,unsigned fallback){
        const auto value=Value(d,*lexerCatalog[0],index);const auto at=value.rfind(name);
        if(at==std::wstring::npos)return fallback;
        const auto first=value.c_str()+at+wcslen(name);
        if(wcsncmp(first,L"inherit",7)==0)return fallback;
        if(*first=='#'){unsigned color=0;if(swscanf_s(first,L"#%x",&color)==1)return static_cast<unsigned>(RGB((color>>16)&255,(color>>8)&255,color&255));}
        return static_cast<unsigned>(wcstoul(first,nullptr,10));
    };
    auto element=[&](int elementId,unsigned index,const wchar_t* key,unsigned fallback){unsigned color=property(index,key,fallback);unsigned alpha=std::min(property(index,L"alpha:",255),255u);d.Send(SCI_SETELEMENTCOLOUR,elementId,(color&0xFFFFFF)|(alpha<<24));};
    element(SC_ELEMENT_WHITE_SPACE,8,L"fore:",RGB(255,64,0));
    d.Send(SCI_SETSELECTIONLAYER,SC_LAYER_OVER_TEXT);
    element(SC_ELEMENT_SELECTION_BACK,7,L"back:",GetSysColor(COLOR_HIGHLIGHT));
    element(SC_ELEMENT_SELECTION_ADDITIONAL_BACK,7,L"fore:",GetSysColor(COLOR_HIGHLIGHT));
    auto caret = property(10, L"fore:", d.style.paletteDark ? RGB(220,220,220) : RGB(0,0,0));
    auto background = static_cast<COLORREF>(d.Send(SCI_STYLEGETBACK, STYLE_DEFAULT));
    if (!simd::VerifyContrast(caret, background)) caret = static_cast<COLORREF>(d.Send(SCI_STYLEGETFORE, STYLE_DEFAULT));
    d.Send(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET, caret | 0xff000000U);
    d.Send(SCI_SETWHITESPACESIZE,std::clamp(property(8,L"size:",2),1u,16u));
    d.Send(SCI_SETEDGECOLOUR,property(12,L"fore:",RGB(255,192,0)));
    auto spacing=std::clamp(property(13,L"size:",2),0u,32u);d.Send(SCI_SETEXTRAASCENT,spacing/2);d.Send(SCI_SETEXTRADESCENT,spacing-spacing/2);
    for(auto indicator: {std::pair{9,3u},std::pair{10,4u},std::pair{8,17u}}){
        d.Send(SCI_INDICSETSTYLE,indicator.first,INDIC_ROUNDBOX);d.Send(SCI_INDICSETFORE,indicator.first,property(indicator.second,L"fore:",RGB(0,150,100)));d.Send(SCI_INDICSETALPHA,indicator.first,property(indicator.second,L"alpha:",50));d.Send(SCI_INDICSETOUTLINEALPHA,indicator.first,property(indicator.second,L"outline:",150));
    }
    d.Send(SCI_BRACEHIGHLIGHTINDICATOR,TRUE,9);d.Send(SCI_BRACEBADLIGHTINDICATOR,TRUE,10);
    unsigned bookmark=property(18,L"back:",RGB(0,255,0))|(std::min(property(18,L"alpha:",255),255u)<<24);
    d.Send(SCI_MARKERSETBACKTRANSLUCENT,0,bookmark);
    d.Send(SCI_SETMARGINSENSITIVEN,1,TRUE);
    d.Send(SCI_SETMARGINWIDTHN,2,(d.style.folding && !d.LargeFile())?MulDiv(16,GetDpiForWindow(d.hwnd),96):0);
    d.Send(SCI_SETMARGINTYPEN,2,SC_MARGIN_SYMBOL);d.Send(SCI_SETMARGINMASKN,2,SC_MASK_FOLDERS);
    d.Send(SCI_SETMARGINSENSITIVEN,2,TRUE);
    d.Send(SCI_SETMARGINTYPEN,3,SC_MARGIN_SYMBOL);
    d.Send(SCI_SETMARGINMASKN,3,SC_MASK_HISTORY);
    d.Send(SCI_SETMARGINWIDTHN,3,(!d.LargeFile()&&(d.Send(SCI_GETCHANGEHISTORY)&SC_CHANGE_HISTORY_MARKERS))?MulDiv(8,GetDpiForWindow(d.hwnd),96):0);
    for(int marker=SC_MARKNUM_HISTORY_REVERTED_TO_ORIGIN;marker<=SC_MARKNUM_HISTORY_REVERTED_TO_MODIFIED;++marker){
        const auto color=property(19,marker<=SC_MARKNUM_HISTORY_SAVED?L"fore:":L"back:",marker<=SC_MARKNUM_HISTORY_SAVED?RGB(0,128,0):RGB(0,128,255))|0xFF000000u;
        d.Send(SCI_MARKERSETFORETRANSLUCENT,marker,color);
        d.Send(SCI_MARKERSETBACKTRANSLUCENT,marker,(marker==SC_MARKNUM_HISTORY_SAVED||marker==SC_MARKNUM_HISTORY_MODIFIED)?color:static_cast<unsigned>(d.Send(SCI_STYLEGETBACK,STYLE_LINENUMBER))|0xFF000000u);
    }
    const int folderShapes[]={SC_MARK_BOXPLUSCONNECTED,SC_MARK_BOXMINUSCONNECTED,SC_MARK_TCORNER,SC_MARK_LCORNER,SC_MARK_VLINE,SC_MARK_BOXPLUS,SC_MARK_BOXMINUS};
    for(int marker=SC_MARKNUM_FOLDEREND;marker<=SC_MARKNUM_FOLDEROPEN;++marker){
        d.Send(SCI_MARKERDEFINE,marker,folderShapes[marker-SC_MARKNUM_FOLDEREND]);d.Send(SCI_MARKERSETFORETRANSLUCENT,marker,property(15,L"back:",d.style.paletteDark?RGB(80,80,80):RGB(173,216,230))|0xFF000000u);d.Send(SCI_MARKERSETBACKTRANSLUCENT,marker,property(15,L"fore:",RGB(128,128,255))|0xFF000000u);
    }
    d.Send(SCI_SETFOLDMARGINCOLOUR,TRUE,d.Send(SCI_STYLEGETBACK,STYLE_DEFAULT));d.Send(SCI_SETFOLDMARGINHICOLOUR,TRUE,d.Send(SCI_STYLEGETBACK,STYLE_DEFAULT));
    d.Send(SCI_SETAUTOMATICFOLD,SC_AUTOMATICFOLD_SHOW|SC_AUTOMATICFOLD_CHANGE);
    d.Send(SCI_AUTOCSETIGNORECASE,d.style.completionIgnoreCase);
    d.Send(SCI_AUTOCSETMAXHEIGHT,CompletionOption(d,IDC_AUTOC_VISIBLE_ITEM_COUNT,16));
    std::string fillups;
    if(CompletionOption(d,IDC_AUTOC_FILLUP_SPACE))fillups+=' ';
    if(CompletionOption(d,IDC_AUTOC_FILLUP_ENTER))fillups+='\n';
    if(CompletionOption(d,IDC_AUTOC_FILLUP_TAB))fillups+='\t';
    if(CompletionOption(d,IDC_AUTOC_FILLUP_PUNCTUATION))fillups+=files::ToUtf8(d.style.completionFillups);
    d.Send(SCI_AUTOCSETFILLUPS,0,reinterpret_cast<LPARAM>(fillups.c_str()));
    HighlightCurrentLine(d);
    d.Send(SCI_SETMOUSEDWELLTIME,500);
    ApplyThemeEditorElements(d);
    if(!d.LargeFile()){
        auto start=rulesChanged?0:d.Send(SCI_GETENDSTYLED);
        if(rulesChanged||start<d.Send(SCI_GETLENGTH))d.Send(SCI_COLOURISE,start,-1);
    }
}
namespace {
struct SchemeDialogContext {
    EditorDocument& document;
    int scheme;
    bool favorites;
    struct Layout {HWND window;RECT rectangle;};
    std::vector<Layout> controls;
    int clientWidth=0,clientHeight=0;
};
HTREEITEM InsertSchemeItem(HWND tree, wchar_t* name, size_t index) {
    TVINSERTSTRUCTW insert{};
    insert.hParent = TVI_ROOT;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_PARAM;
    insert.item.pszText = name;
    insert.item.lParam = static_cast<LPARAM>(index);
    return TreeView_InsertItem(tree, &insert);
}
void MoveFavoriteItem(HWND window,int direction){
    HWND tree(GetDlgItem(window,IDC_STYLELIST));auto selected=TreeView_GetSelection(tree);if(!selected)return;
    auto neighbor=direction<0?TreeView_GetPrevSibling(tree, selected):TreeView_GetNextSibling(tree, selected);if(!neighbor)return;
    wchar_t first[256]{},second[256]{};TreeGetItemText(tree, selected,first,256);TreeGetItemText(tree, neighbor,second,256);
    auto data=TreeGetItemData(tree, selected),other=TreeGetItemData(tree, neighbor);bool checked=TreeView_GetCheckState(tree, selected)!=0,otherCheck=TreeView_GetCheckState(tree, neighbor)!=0;
    TreeSetItemText(tree, selected,second);TreeSetItemData(tree, selected,other);TreeView_SetCheckState(tree, selected,otherCheck);
    TreeSetItemText(tree, neighbor,first);TreeSetItemData(tree, neighbor,data);TreeView_SetCheckState(tree, neighbor,checked);TreeView_SelectItem(tree, neighbor);TreeView_EnsureVisible(tree, neighbor);
}
INT_PTR CALLBACK SchemeProcedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    try {
    auto* c=reinterpret_cast<SchemeDialogContext*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG) {
        c=reinterpret_cast<SchemeDialogContext*>(l);SetWindowLongPtrW(window,DWLP_USER,l);
        HWND tree(GetDlgItem(window,IDC_STYLELIST));HTREEITEM selected=nullptr;
        if(c->favorites){SetWindowLongPtrW(tree,GWL_STYLE,GetWindowLongPtrW(tree,GWL_STYLE)|TVS_CHECKBOXES);SetWindowTextW(window,L"Favorite Schemes (Ctrl+Up/Down reorders)");
            for(int control:{IDC_DEFAULTSCHEME,IDC_AUTOSELECT}){auto button=GetDlgItem(window,control);auto style=GetWindowLongPtrW(button,GWL_STYLE);SetWindowLongPtrW(button,GWL_STYLE,(style&~BS_TYPEMASK)|BS_PUSHBUTTON);SetWindowTextW(button,control==IDC_DEFAULTSCHEME?L"Move Up":L"Move Down");RECT placement{control==IDC_DEFAULTSCHEME?7:86,171,control==IDC_DEFAULTSCHEME?79:158,191};MapDialogRect(window,&placement);SetWindowPos(button,nullptr,placement.left,placement.top,placement.right-placement.left,placement.bottom-placement.top,SWP_NOZORDER);ShowWindow(button,SW_SHOW);}}
        std::vector<size_t> order;
        for(int i:favoriteSchemes)if(i>0 && i<static_cast<int>(Count))order.push_back(static_cast<size_t>(i));
        for(size_t i=1;i<Count;++i)if(!FavoriteContains(static_cast<int>(i)))order.push_back(i);
        for(size_t i:order) {
            auto name=LocalizedLabel(lexerCatalog[i]->rid,lexerCatalog[i]->pszName);auto root=InsertSchemeItem(tree, name.data(), i);
            if(static_cast<int>(i)==c->scheme)selected=root;
            if(c->favorites)TreeView_SetCheckState(tree, root,FavoriteContains(static_cast<int>(i)));
        }
        TreeView_SelectItem(tree, selected);
        if(!c->favorites) {
            CheckDlgButton(window,IDC_AUTOSELECT,autoSelectScheme?BST_CHECKED:BST_UNCHECKED);
            CheckDlgButton(window,IDC_DEFAULTSCHEME,c->scheme==defaultScheme?BST_CHECKED:BST_UNCHECKED);
        }
        PositionDialogResizeGrip(window,IDC_RESIZEGRIP3);
        RECT client{};GetClientRect(window,&client);c->clientWidth=client.right;c->clientHeight=client.bottom;
        for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){RECT rectangle{};GetWindowRect(child,&rectangle);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rectangle),2);c->controls.push_back({child,rectangle});}
        CenterWindow(window, GetParent(window));return TRUE;
    }
    if(!c)return FALSE;
    if(message==WM_GETMINMAXINFO && c->clientWidth){
        auto* bounds=reinterpret_cast<MINMAXINFO*>(l);RECT client{},outer{};GetClientRect(window,&client);GetWindowRect(window,&outer);bounds->ptMinTrackSize.x=c->clientWidth+outer.right-outer.left-client.right;bounds->ptMinTrackSize.y=c->clientHeight+outer.bottom-outer.top-client.bottom;return TRUE;
    }
    if(message==WM_SIZE && c->clientWidth){
        int dx=LOWORD(l)-c->clientWidth,dy=HIWORD(l)-c->clientHeight;
        for(const auto& item:c->controls){RECT r=item.rectangle;int id=GetDlgCtrlID(item.window);
            if(id==IDC_STYLELIST){r.bottom+=dy;r.right+=dx;}
            else {r.top+=dy;r.bottom+=dy;if(id==IDOK||id==IDCANCEL){r.left+=dx;r.right+=dx;}else if(c->favorites && (id==IDC_DEFAULTSCHEME||id==IDC_AUTOSELECT)){if(id==IDC_AUTOSELECT){r.left+=dx/2;r.right+=dx;}else r.right+=dx/2;}else if(id!=IDC_RESIZEGRIP3)r.right+=dx;}
            SetWindowPos(item.window,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
        }PositionDialogResizeGrip(window,IDC_RESIZEGRIP3);return TRUE;
    }
    if(message==WM_NOTIFY) {
        auto* n=reinterpret_cast<NMHDR*>(l);
        if(c->favorites && n->idFrom==IDC_STYLELIST && n->code==TVN_KEYDOWN){
            auto* key=reinterpret_cast<NMTVKEYDOWN*>(l);
            if((GetKeyState(VK_CONTROL)&0x8000) && (key->wVKey==VK_UP || key->wVKey==VK_DOWN)){
                MoveFavoriteItem(window,key->wVKey==VK_UP?-1:1);
                return TRUE;
            }
        }
        if(n->idFrom==IDC_STYLELIST && n->code==TVN_SELCHANGEDW) {
            auto* change=reinterpret_cast<NMTREEVIEWW*>(l);
            c->scheme=static_cast<int>(change->itemNew.lParam);return TRUE;
        }
        if(n->idFrom==IDC_STYLELIST && n->code==NM_DBLCLK)SendMessageW(window,WM_COMMAND,IDOK,0);
    }
    if(message==WM_CLOSE || (message==WM_COMMAND && LOWORD(w)==IDCANCEL)) {
        EndDialog(window,IDCANCEL);return TRUE;
    }
    if(message!=WM_COMMAND)return FALSE;
    UINT id=LOWORD(w);
    if(c->favorites && (id==IDC_DEFAULTSCHEME || id==IDC_AUTOSELECT)){MoveFavoriteItem(window,id==IDC_DEFAULTSCHEME?-1:1);return TRUE;}
    if(id==IDOK) {
        if(c->favorites){favoriteSchemes.clear();HWND tree(GetDlgItem(window,IDC_STYLELIST));for(auto item=TreeView_GetRoot(tree);item;item=TreeView_GetNextSibling(tree, item))if(TreeView_GetCheckState(tree, item) && favoriteSchemes.size()<31)favoriteSchemes.push_back(static_cast<int>(TreeGetItemData(tree, item)));EndDialog(window,IDOK);return TRUE;}
        c->document.style.scheme=c->scheme;autoSelectScheme=IsDlgButtonChecked(window,IDC_AUTOSELECT)!=0;if(IsDlgButtonChecked(window,IDC_DEFAULTSCHEME))defaultScheme=c->scheme;
        ConfigureDocumentStyle(c->document);EndDialog(window,IDOK);return TRUE;
    }
    return FALSE;
    }catch(const std::exception& error){OutputDebugStringA(error.what());MessageBeep(MB_ICONERROR);return TRUE;}
}
void SelectScheme(HWND owner,EditorDocument& document,bool favorites=false) {
    SchemeDialogContext c{document,ActualScheme(document),favorites};
    DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_STYLESELECT),owner,SchemeProcedure,reinterpret_cast<LPARAM>(&c));
}
}
namespace {
UINT DefaultSchemeCommand(const LanguageDefinition& lexer){return lexer.defaultCommand;}
}
CommandState StyleCommandState(const EditorDocument* d,UINT id) {
    int scheme=SchemeForCommand(id);
    if(scheme){UINT selected=d?static_cast<UINT>(d->style.dialect?d->style.dialect:d->style.resolvedDialect):0;if(d && !selected)selected=DefaultSchemeCommand(*GetDocumentLexer(*d));return {true,d!=nullptr,d && ActualScheme(*d)==scheme && selected==id};}
    if(id>=IDM_EDIT_GOTO_BLOCK_START && id<=IDM_EDIT_GOTO_NEXT_SIBLING_BLOCK)return {true,d!=nullptr,false};
    if(id>=IDM_VIEW_FOLD_LEVEL1 && id<=IDM_VIEW_FOLD_LEVEL10)return {true,d!=nullptr,false};
    switch(id){
    case ID_RELOAD_CONFIGURATION:case IDM_VIEW_SCHEME:case IDM_VIEW_SCHEME_CONFIG:case IDM_VIEW_SCHEME_FAVORITE:case IDM_VIEW_DEFAULT_CODE_FONT:case IDM_VIEW_DEFAULT_TEXT_FONT:
    case IDM_VIEW_FOLD_CURRENT_LEVEL:case IDM_VIEW_FOLD_CURRENT_BLOCK:case IDM_VIEW_FOLD_DEFAULT:case IDM_VIEW_FOLD_ALL:
    case IDM_VIEW_TABSETTINGS:case IDM_VIEW_WORDWRAPSETTINGS:case IDM_VIEW_AUTOCOMPLETION_SETTINGS:
    case IDM_EDIT_COMPLETEWORD:return {true,d!=nullptr,false};
    case CMD_TAB_COMPLETION:return {true,d && !d->Send(SCI_GETREADONLY),false};
    case IDM_SET_LATEX_INPUT_METHOD:return {true,d!=nullptr,d&&d->style.latex};
    case IDM_VIEW_SHOWCALLTIP_OFF:case IDM_VIEW_SHOWCALLTIP_RGBA:case IDM_VIEW_SHOWCALLTIP_ARGB:case IDM_VIEW_SHOWCALLTIP_BGRA:case IDM_VIEW_SHOWCALLTIP_ABGR:return {true,d!=nullptr,d&&d->style.colorCalltip==static_cast<int>(id-IDM_VIEW_SHOWCALLTIP_OFF)};
    case IDM_VIEW_USEDEFAULT_CODESTYLE:return {true,d!=nullptr,d&&(d->style.fontKind<0?lexerCatalog[ActualScheme(*d)]->bUseDefaultCodeStyle:d->style.fontKind==0)};
    case IDM_VIEW_HIGHLIGHTCURRENTLINE_NONE:case IDM_VIEW_HIGHLIGHTCURRENTLINE_BACK:case IDM_VIEW_HIGHLIGHTCURRENTLINE_FRAME:return {true,d!=nullptr,d&&d->style.lineHighlight==static_cast<int>(id-IDM_VIEW_HIGHLIGHTCURRENTLINE_NONE)};
    case IDM_VIEW_HIGHLIGHTCURRENTLINE_SUBLINE:return {true,d!=nullptr,d&&d->style.highlightSubline};
    case IDM_VIEW_CHANGE_HISTORY_MARKER:return {true,d&&!d->LargeFile()&&(d->Send(SCI_GETCHANGEHISTORY)||d->Send(SCI_GETUNDOCURRENT)==0),d&&(d->Send(SCI_GETCHANGEHISTORY)&SC_CHANGE_HISTORY_MARKERS)!=0};
    case IDM_VIEW_HIGHLIGHTCURRENT_BLOCK:return {true,d!=nullptr,d&&d->style.highlightBlock};
    case IDM_VIEW_LONGLINEMARKER:return {true,d!=nullptr,d&&d->Send(SCI_GETEDGEMODE)!=EDGE_NONE};
    case IDM_VIEW_WORDWRAP:return {true,d!=nullptr,d&&d->Send(SCI_GETWRAPMODE)!=SC_WRAP_NONE};
    case IDM_VIEW_WORDWRAPSYMBOLS:return {true,d!=nullptr,d&&d->Send(SCI_GETWRAPVISUALFLAGS)!=0};
    case IDM_VIEW_SHOW_FOLDING:return {true,d!=nullptr,d&&d->style.folding};
    case IDM_VIEW_MATCHBRACES:return {true,d!=nullptr,d&&d->style.matchBraces};
    case IDM_VIEW_AUTOCOMPLETION_IGNORECASE:return {true,d!=nullptr,d&&d->style.completionIgnoreCase};
    case IDM_VIEW_STYLE_THEME_DEFAULT:return {true,d!=nullptr,d&&!d->style.dark};
    case IDM_VIEW_STYLE_THEME_DARK:return {true,d!=nullptr,d&&d->style.dark};
    case IDM_VIEW_MARKOCCURRENCES_OFF:return {true,d!=nullptr,d&&d->style.markOccurrences==0};
    case IDM_VIEW_MARKOCCURRENCES_CASE:case IDM_VIEW_MARKOCCURRENCES_WORD:case IDM_VIEW_MARKOCCURRENCES_BOOKMARK:return {true,d!=nullptr,d&&CompletionOption(*d,id,0)!=0};
    default:return {};
    }
}
bool ExecuteStyleCommand(HWND owner,EditorDocument& d,UINT id) {
    if(!StyleCommandState(&d,id).supported)return false;
    if(int scheme=SchemeForCommand(id)){
        if(id==IDM_LEXER_CSV && ShowEditorDialog(owner,d,IDD_CSV_OPTIONS)!=IDOK)return false;
        d.style.scheme=scheme;d.style.dialect=id;ConfigureDocumentStyle(d);return true;
    }
    if(id>=IDM_EDIT_GOTO_BLOCK_START && id<=IDM_EDIT_GOTO_NEXT_SIBLING_BLOCK){GotoBlock(d,id);return true;}
    if(id>=IDM_VIEW_FOLD_LEVEL1 && id<=IDM_VIEW_FOLD_LEVEL10){ToggleFoldLevels(d,static_cast<int>(id-IDM_VIEW_FOLD_LEVEL1),false);return true;}
    switch(id){
    case ID_RELOAD_CONFIGURATION:if(!ReloadLanguageConfiguration())throw std::runtime_error("Configuration reload failed; previous snapshot retained. See theme diagnostics.");ConfigureDocumentStyle(d);break;
    case IDM_VIEW_SCHEME:SelectScheme(owner,d);break;
    case IDM_VIEW_SCHEME_FAVORITE:SelectScheme(owner,d,true);break;
    case IDM_VIEW_USEDEFAULT_CODESTYLE:d.style.fontKind=StyleCommandState(&d,id).checked?1:0;ConfigureDocumentStyle(d);break;
    case IDM_VIEW_DEFAULT_CODE_FONT:case IDM_VIEW_DEFAULT_TEXT_FONT:{
        LOGFONTW font{};const auto& selected=id==IDM_VIEW_DEFAULT_CODE_FONT?d.style.codeFont:d.style.textFont;
        wcsncpy_s(font.lfFaceName,selected.empty()?d.style.font.c_str():selected.c_str(),_TRUNCATE);int pointSize=selected.empty()?d.style.fontSize:id==IDM_VIEW_DEFAULT_CODE_FONT?d.style.codeFontSize:d.style.textFontSize;font.lfHeight=-MulDiv(pointSize,GetDpiForWindow(owner),72);
        CHOOSEFONTW choose{sizeof(choose)};choose.hwndOwner=owner;choose.lpLogFont=&font;choose.Flags=CF_SCREENFONTS|CF_INITTOLOGFONTSTRUCT;
        if(ChooseFontW(&choose)){
            if(id==IDM_VIEW_DEFAULT_CODE_FONT){d.style.codeFont=font.lfFaceName;d.style.codeFontSize=choose.iPointSize/10;}
            else {d.style.textFont=font.lfFaceName;d.style.textFontSize=choose.iPointSize/10;}
            ConfigureDocumentStyle(d);
        }break;
    }
    case IDM_VIEW_STYLE_THEME_DEFAULT:case IDM_VIEW_STYLE_THEME_DARK:d.style.dark=id==IDM_VIEW_STYLE_THEME_DARK;ConfigureDocumentStyle(d);break;
    case IDM_VIEW_SHOWCALLTIP_OFF:case IDM_VIEW_SHOWCALLTIP_RGBA:case IDM_VIEW_SHOWCALLTIP_ARGB:case IDM_VIEW_SHOWCALLTIP_BGRA:case IDM_VIEW_SHOWCALLTIP_ABGR:d.style.colorCalltip=id-IDM_VIEW_SHOWCALLTIP_OFF;if(!d.style.colorCalltip)d.Send(SCI_CALLTIPCANCEL);break;
    case IDM_VIEW_HIGHLIGHTCURRENTLINE_NONE:case IDM_VIEW_HIGHLIGHTCURRENTLINE_BACK:case IDM_VIEW_HIGHLIGHTCURRENTLINE_FRAME:d.style.lineHighlight=id-IDM_VIEW_HIGHLIGHTCURRENTLINE_NONE;HighlightCurrentLine(d);break;
    case IDM_VIEW_HIGHLIGHTCURRENTLINE_SUBLINE:d.style.highlightSubline=!d.style.highlightSubline;HighlightCurrentLine(d);break;
    case IDM_VIEW_CHANGE_HISTORY_MARKER:
        if(!StyleCommandState(&d,id).enabled)return false;
        d.Send(SCI_SETCHANGEHISTORY,d.Send(SCI_GETCHANGEHISTORY)&SC_CHANGE_HISTORY_MARKERS?SC_CHANGE_HISTORY_ENABLED:SC_CHANGE_HISTORY_ENABLED|SC_CHANGE_HISTORY_MARKERS);
        d.Send(SCI_SETMARGINWIDTHN,3,(d.Send(SCI_GETCHANGEHISTORY)&SC_CHANGE_HISTORY_MARKERS)?MulDiv(8,GetDpiForWindow(d.hwnd),96):0);
        break;
    case IDM_VIEW_HIGHLIGHTCURRENT_BLOCK:d.style.highlightBlock=!d.style.highlightBlock;if(!d.style.highlightBlock){d.Send(SCI_SETINDICATORCURRENT,11);d.Send(SCI_INDICATORCLEARRANGE,0,d.Send(SCI_GETLENGTH));}break;
    case IDM_VIEW_LONGLINEMARKER:d.style.longLineEnabled=!d.Send(SCI_GETEDGEMODE);d.Send(SCI_SETEDGEMODE,d.style.longLineEnabled?d.edgeMode:EDGE_NONE);break;
    case IDM_VIEW_WORDWRAP:d.Send(SCI_SETWRAPMODE,d.Send(SCI_GETWRAPMODE)?SC_WRAP_NONE:d.style.wrapMode);break;
    case IDM_VIEW_WORDWRAPSYMBOLS:d.style.wrapSymbolsShown=!d.Send(SCI_GETWRAPVISUALFLAGS);ApplyDocumentWrapSettings(d);break;
    case IDM_VIEW_SHOW_FOLDING:d.style.folding=!d.style.folding;d.Send(SCI_SETMARGINWIDTHN,2,(d.style.folding && !d.LargeFile())?MulDiv(16,GetDpiForWindow(d.hwnd),96):0);break;
    case IDM_VIEW_FOLD_ALL:d.Send(SCI_FOLDALL,SC_FOLDACTION_TOGGLE|SC_FOLDACTION_CONTRACT_EVERY_LEVEL);break;
    case IDM_VIEW_FOLD_DEFAULT:ToggleFoldLevels(d,0,true);break;
    case IDM_VIEW_FOLD_CURRENT_LEVEL:case IDM_VIEW_FOLD_CURRENT_BLOCK:{
        auto line=d.Send(SCI_LINEFROMPOSITION,d.Send(SCI_GETCURRENTPOS));if(!(d.Send(SCI_GETFOLDLEVEL,line)&SC_FOLDLEVELHEADERFLAG))line=d.Send(SCI_GETFOLDPARENT,line);
        if(line<0)break;
        if(id==IDM_VIEW_FOLD_CURRENT_BLOCK)d.Send(SCI_TOGGLEFOLD,line);else ToggleFoldLevels(d,FoldDepth(d,line),false);
        break;
    }
    case IDM_VIEW_MATCHBRACES:d.style.matchBraces=!d.style.matchBraces;if(!d.style.matchBraces)d.Send(SCI_BRACEHIGHLIGHT,static_cast<WPARAM>(-1),-1);break;
    case IDM_VIEW_AUTOCOMPLETION_IGNORECASE:d.style.completionIgnoreCase=!d.style.completionIgnoreCase;d.Send(SCI_AUTOCSETIGNORECASE,d.style.completionIgnoreCase);break;
    // Only accepted settings may be promoted to the frame's editor defaults.
    case IDM_VIEW_TABSETTINGS:return ShowEditorDialog(owner,d,IDD_TABSETTINGS)==IDOK;
    case IDM_VIEW_WORDWRAPSETTINGS:return ShowEditorDialog(owner,d,IDD_WORDWRAP)==IDOK;
    case IDM_VIEW_AUTOCOMPLETION_SETTINGS:return ShowEditorDialog(owner,d,IDD_AUTOCOMPLETION)==IDOK;
    case IDM_SET_LATEX_INPUT_METHOD:d.style.latex=!d.style.latex;break;
    case CMD_TAB_COMPLETION:
        if(d.Send(SCI_AUTOCACTIVE))d.Send(SCI_AUTOCCOMPLETE);
        else d.Send(SCI_TAB,TAB_COMPLETION_DEFAULT|(d.style.latex?TAB_COMPLETION_LATEX:0));
        break;
    case IDM_EDIT_COMPLETEWORD:Complete(d,true);break;
    default:
        if(id==IDM_VIEW_MARKOCCURRENCES_OFF)d.style.markOccurrences=d.style.markOccurrences?0:1;
        else if(id>=IDM_VIEW_MARKOCCURRENCES_CASE && id<=IDM_VIEW_MARKOCCURRENCES_BOOKMARK){d.style.completionOptions[id]=!CompletionOption(d,id,0);}
        break;
    }
    return true;
}
void HandleStyleNotification(EditorDocument& d,const SCNotification& n) {
    try {
    if(n.nmhdr.code==SCN_FOCUSOUT){d.style.completionPending=false;++d.completionEpoch;return;}
    if(n.nmhdr.code==SCN_ZOOM){HighlightCurrentLine(d);return;}
    if(n.nmhdr.code==SCN_MODIFIED && (n.modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))){
        ++d.style.revision;
        return;
    }
    if (d.LargeFile()) return;
    if(n.nmhdr.code==SCN_AUTOCCHARDELETED){d.Send(SCI_AUTOCCANCEL);if(d.style.completion)Complete(d);return;}
    if(n.nmhdr.code==SCN_AUTOCSELECTION || n.nmhdr.code==SCN_USERLISTSELECTION){
        if((n.listCompletionMethod==SC_AC_NEWLINE&&!CompletionOption(d,IDC_AUTOC_FILLUP_ENTER)) || (n.listCompletionMethod==SC_AC_TAB&&!CompletionOption(d,IDC_AUTOC_FILLUP_TAB))){
            d.Send(SCI_AUTOCCANCEL);d.Send(n.listCompletionMethod==SC_AC_NEWLINE?SCI_NEWLINE:SCI_TAB);return;
        }
        if(!n.text)return;std::string text=n.text;auto current=d.Send(SCI_GETCURRENTPOS);auto open=text.find_first_of("([{<");LRESULT offset=static_cast<LRESULT>(text.size());
        if(open!=std::string::npos){auto next=current;while(d.Send(SCI_GETCHARAT,next)==' ' || d.Send(SCI_GETCHARAT,next)=='\t')++next;
            const auto brace=text[open];offset=static_cast<LRESULT>(open+1);
            if(d.Send(SCI_GETCHARAT,next)==brace){text.resize(open);offset+=next-current;}
            else {char closing=brace=='('?')':static_cast<char>(brace+2);if(d.Send(SCI_GETCHARAT,next)==closing && text.size()>open+1 && text[open+1]==closing)text.resize(open+1);}
        }
        d.Send(SCI_BEGINUNDOACTION);d.Send(SCI_SETSEL,n.position,current);d.Send(SCI_REPLACESEL,0,reinterpret_cast<LPARAM>(text.c_str()));d.Send(SCI_SETSEL,n.position+offset,n.position+offset);d.Send(SCI_ENDUNDOACTION);d.Send(SCI_AUTOCCANCEL);
        if(n.listCompletionMethod==SC_AC_TAB && d.style.latex)d.Send(SCI_TAB,TAB_COMPLETION_LATEX);
        return;
    }
    if(n.nmhdr.code==SCN_MARGINCLICK){if(n.margin==2)d.Send(SCI_TOGGLEFOLD,d.Send(SCI_LINEFROMPOSITION,n.position));if(n.margin==1){auto line=d.Send(SCI_LINEFROMPOSITION,n.position);d.Send(d.Send(SCI_MARKERGET,line)&1?SCI_MARKERDELETE:SCI_MARKERADD,line,0);}return;}
    if(n.nmhdr.code==SCN_CALLTIPCLICK && d.style.colorStart>=0){
        d.Send(SCI_CALLTIPCANCEL);if(d.Send(SCI_GETREADONLY))return;
        if(Range(d,d.style.colorStart,d.style.colorEnd)!=d.style.calltipHex)return;
        CHOOSECOLORW choose{sizeof(choose)};choose.hwndOwner=GetAncestor(d.hwnd,GA_ROOT);choose.rgbResult=d.style.calltipColor&0xFFFFFF;choose.lpCustColors=customColors;choose.Flags=CC_FULLOPEN|CC_RGBINIT;
        if(ChooseColorW(&choose)){
            unsigned r=GetRValue(choose.rgbResult),g=GetGValue(choose.rgbResult),b=GetBValue(choose.rgbResult),alpha=d.style.calltipColor>>24;
            bool hasAlpha=d.style.calltipHex.size()==4 || d.style.calltipHex.size()==8;unsigned value=0;
            if(!hasAlpha)value=d.style.colorCalltip<=2?(r<<16)|(g<<8)|b:(b<<16)|(g<<8)|r;
            else switch(d.style.colorCalltip){case 1:value=(r<<24)|(g<<16)|(b<<8)|alpha;break;case 2:value=(alpha<<24)|(r<<16)|(g<<8)|b;break;case 3:value=(b<<24)|(g<<16)|(r<<8)|alpha;break;default:value=(alpha<<24)|(b<<16)|(g<<8)|r;break;}
            char output[16]{};sprintf_s(output,hasAlpha?"%08X":"%06X",value);d.Send(SCI_SETTARGETRANGE,d.style.colorStart,d.style.colorEnd);d.Send(SCI_REPLACETARGET,strlen(output),reinterpret_cast<LPARAM>(output));d.style.colorStart=d.style.colorEnd=-1;
        }return;
    }
    if(n.nmhdr.code==SCN_DWELLSTART && d.style.colorCalltip && n.position>=0){
        auto base=std::max<LRESULT>(0,n.position-10);auto text=Range(d,base,std::min<LRESULT>(d.Send(SCI_GETLENGTH),n.position+10));auto position=static_cast<size_t>(n.position-base);if(position>=text.size())return;
        size_t start=position;while(start>0 && isxdigit(static_cast<unsigned char>(text[start-1])))--start;
        size_t prefix=start;
        if(start>0 && text[start-1]=='#')prefix=start-1;
        else if(start>1 && (text[start-1]=='x'||text[start-1]=='X') && text[start-2]=='0')prefix=start-2;
        else if(text[start]=='#'){prefix=start;++start;}
        else return;
        auto end=start;while(end<text.size() && isxdigit(static_cast<unsigned char>(text[end])))++end;
        auto digits=end-start;if(digits!=3&&digits!=4&&digits!=6&&digits!=8)return;
        if((digits==3||digits==4) && text[prefix]!='#')return;
        unsigned value=static_cast<unsigned>(std::stoul(text.substr(start,digits),nullptr,16)),r=0,g=0,b=0,alpha=255;
        if(digits<=4){if(digits==4){if(d.style.colorCalltip==2){alpha=((value>>12)&15)*17;value&=0xFFF;}else{alpha=(value&15)*17;value>>=4;}}r=((value>>8)&15)*17;g=((value>>4)&15)*17;b=(value&15)*17;}
        else if(digits==6){r=(value>>16)&255;g=(value>>8)&255;b=value&255;if(d.style.colorCalltip>=3)std::swap(r,b);}
        else {unsigned c1=(value>>24)&255,c2=(value>>16)&255,c3=(value>>8)&255,c4=value&255;switch(d.style.colorCalltip){case 1:r=c1;g=c2;b=c3;alpha=c4;break;case 2:alpha=c1;r=c2;g=c3;b=c4;break;case 3:b=c1;g=c2;r=c3;alpha=c4;break;default:alpha=c1;b=c2;g=c3;r=c4;break;}}
        d.style.colorStart=base+start;d.style.colorEnd=base+end;d.style.calltipColor=RGB(r,g,b)|(alpha<<24);d.style.calltipHex=text.substr(start,digits);
        auto label=text.substr(prefix,end-prefix)+"\nR: "+std::to_string(r)+"  G: "+std::to_string(g)+"  B: "+std::to_string(b)+"  A: "+std::to_string(alpha)+"\nClick to edit color";
        d.Send(SCI_CALLTIPSETBACK,RGB(r,g,b));d.Send(SCI_CALLTIPSETFORE,RGB(255-r,255-g,255-b));d.Send(SCI_CALLTIPSHOW,base+prefix,reinterpret_cast<LPARAM>(label.c_str()));return;
    }
    if(n.nmhdr.code==SCN_CHARADDED && !d.Send(SCI_GETREADONLY)) {
        static thread_local bool adding=false;if(adding)return;adding=true;struct ResetAdding{bool& v;~ResetAdding(){v=false;}}resetAdding{adding};
        // Scintilla sends one notification for each caret; automatic edits only
        // apply when there is a single target selection to preserve isolation.
        if(d.Send(SCI_GETSELECTIONS)!=1)return;
        if(n.listType)return;
        if(n.ch=='\n' || n.ch=='\r'){if(d.style.autoIndent && (n.ch=='\n' || d.Send(SCI_GETEOLMODE)!=SC_EOL_CRLF))ApplyDocumentAutoIndent(d);return;}
        const struct{int character,control;} pairs[]={{'(',IDC_AUTO_INSERT_PARENTHESIS},{'{',IDC_AUTO_INSERT_BRACE},{'[',IDC_AUTO_INSERT_SQUARE_BRACKET},{'<',IDC_AUTO_INSERT_ANGLE_BRACKET},{'"',IDC_AUTO_INSERT_DOUBLE_QUOTE},{'\'',IDC_AUTO_INSERT_SINGLE_QUOTE},{'`',IDC_AUTO_INSERT_BACKTICK},{',',IDC_AUTO_INSERT_SPACE_COMMA}};
        for(unsigned index=0;index<8;++index)if(n.ch==pairs[index].character){if(CompletionOption(d,pairs[index].control))ApplyDocumentAutoPair(d,n.ch,static_cast<int>(index));return;}
        auto position=d.Send(SCI_GETCURRENTPOS);const auto* activeLexer=GetDocumentLexer(d);
        if(n.ch=='>'){
            bool closed=ApplyDocumentAutoCloseTag(d,CompletionOption(d,IDC_AUTO_CLOSE_TAGS)!=0);
            if(!closed && d.style.completion && d.Send(SCI_GETCHARAT,position-2)=='-')Complete(d);
            return;
        }
        bool directInput=n.characterSource==SC_CHARACTERSOURCE_DIRECT_INPUT;
        bool allowedSource=directInput || (n.ch<128 && !CompletionOption(d,IDC_AUTOC_ENGLISH_IME_ONLY,0));
        bool word=n.ch>=0 && (n.ch<128?GetCompletionCache(*activeLexer).Word(static_cast<unsigned>(n.ch)):d.Send(SCI_GETCHARACTERCLASS,n.ch)==3);
        if(d.style.completion && allowedSource && word)Complete(d);
    }
    if(n.nmhdr.code==SCN_UPDATEUI) {
        auto originalIndicator=d.Send(SCI_GETINDICATORCURRENT);
        struct RestoreIndicator{EditorDocument& d;LRESULT indicator;~RestoreIndicator(){d.Send(SCI_SETINDICATORCURRENT,indicator);}}restoreIndicator{d,originalIndicator};
        static thread_local bool updating=false;if(updating)return;updating=true;
        struct Reset {bool& value;~Reset(){value=false;}}reset{updating};
        auto pos=d.Send(SCI_GETCURRENTPOS);
        if(d.style.completionPending && (pos!=d.style.completionPosition || GetFocus()!=d.hwnd))d.style.completionPending=false;
        if(d.style.matchBraces){auto brace=pos,match=d.Send(SCI_BRACEMATCH,brace,0);if(match<0&&pos){brace=pos-1;match=d.Send(SCI_BRACEMATCH,brace,0);}d.Send(SCI_BRACEHIGHLIGHT,static_cast<WPARAM>(match<0?-1:brace),match);}
        if(d.style.highlightBlock){
            d.Send(SCI_SETINDICATORCURRENT,11);d.Send(SCI_INDICATORCLEARRANGE,0,d.Send(SCI_GETLENGTH));d.Send(SCI_INDICSETSTYLE,11,INDIC_STRAIGHTBOX);d.Send(SCI_INDICSETFORE,11,RGB(80,120,200));d.Send(SCI_INDICSETALPHA,11,12);
            auto line=d.Send(SCI_LINEFROMPOSITION,pos);if(!(d.Send(SCI_GETFOLDLEVEL,line)&SC_FOLDLEVELHEADERFLAG))line=d.Send(SCI_GETFOLDPARENT,line);
            if(line>=0){auto endLine=d.Send(SCI_GETLASTCHILD,line,-1),start=d.Send(SCI_POSITIONFROMLINE,line),end=d.Send(SCI_GETLINEENDPOSITION,endLine);d.Send(SCI_INDICATORFILLRANGE,start,end-start);}
        }
        auto start=d.Send(SCI_GETSELECTIONSTART),end=d.Send(SCI_GETSELECTIONEND);
        if(start==end){start=d.Send(SCI_WORDSTARTPOSITION,pos,TRUE);end=d.Send(SCI_WORDENDPOSITION,pos,TRUE);}
        const auto needle=d.style.markOccurrences && end>start && end-start<=256?Range(d,start,end):std::string{};
        const int flags=(CompletionOption(d,IDM_VIEW_MARKOCCURRENCES_CASE,0)?SCFIND_MATCHCASE:0)|(CompletionOption(d,IDM_VIEW_MARKOCCURRENCES_WORD,0)?SCFIND_WHOLEWORD:0);
        if(d.style.occurrenceNeedle!=needle || d.style.occurrenceRevision!=d.style.revision || d.style.occurrenceFlags!=flags){
            d.style.occurrenceNeedle=needle;d.style.occurrenceCursor=0;d.style.occurrenceRevision=d.style.revision;d.style.occurrenceFlags=flags;d.style.occurrences=0;d.style.occurrencePending=!needle.empty();
            d.Send(SCI_SETINDICATORCURRENT,8);d.Send(SCI_INDICATORCLEARRANGE,0,d.Send(SCI_GETLENGTH));
        }
        // The frame schedules all occurrence scans under one shared time budget.

    }
    } catch(const std::exception& error) {
        d.Send(SCI_AUTOCCANCEL);d.Send(SCI_CALLTIPCANCEL);
        OutputDebugStringA("MultiPad4 style notification failed: ");OutputDebugStringA(error.what());OutputDebugStringA("\n");
        MessageBeep(MB_ICONERROR);
    }
}
void LoadStyleDialogPreferences(const std::filesystem::path&);
void LoadStylePreferences(const std::filesystem::path& path){
    SetConfigurationUserDirectory(path.parent_path());ReloadLanguageConfiguration();LoadThemePreferences(path);
    for(unsigned i=0;i<16;++i){wchar_t key[4]{};swprintf_s(key,L"%02u",i+1);wchar_t color[32]{};GetPrivateProfileStringW(L"Custom Colors",key,L"#FFFFFF",color,32,path.c_str());unsigned value=0xFFFFFF;swscanf_s(color,L"#%x",&value);customColors[i]=RGB((value>>16)&255,(value>>8)&255,value&255);}
    defaultScheme=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Style Preferences",L"DefaultScheme",1,path.c_str())),1,static_cast<int>(Count)-1);
    autoSelectScheme=GetPrivateProfileIntW(L"Style Preferences",L"AutoSelect",1,path.c_str())!=0;
    favoriteSchemes.clear();wchar_t favorites[1024]{};GetPrivateProfileStringW(L"Style Preferences",L"Favorites",L"",favorites,1024,path.c_str());std::wistringstream list(favorites);int favorite=0;while(list>>favorite)if(favorite>0&&favorite<static_cast<int>(Count)&&!FavoriteContains(favorite)&&favoriteSchemes.size()<31)favoriteSchemes.push_back(favorite);
    auto readStable=[&](const std::wstring& section,const std::wstring& key)->std::optional<std::wstring>{wchar_t value[32768]{};constexpr wchar_t missing[]=L"{MultiPad4-missing-configuration-field}";GetPrivateProfileStringW(section.c_str(),key.c_str(),missing,value,32768,path.c_str());if(value==std::wstring(missing))return {};return std::wstring(value);};
    if(auto stable=readStable(L"Language Preferences",L"Default")){int index=LanguageIndex(ConfigurationUtf8(*stable));if(index>0)defaultScheme=index;}
    if(auto stable=readStable(L"Language Preferences",L"Favorites")){favoriteSchemes.clear();std::wistringstream stableList(*stable);std::wstring id;while(stableList>>id){int index=LanguageIndex(ConfigurationUtf8(id));if(index>0&&!FavoriteContains(index)&&favoriteSchemes.size()<31)favoriteSchemes.push_back(index);}}
    LoadStyleDialogPreferences(path);
}
void WriteStyleDialogPreferencesChecked(const std::filesystem::path&);
namespace {
void EnsureUnicodeProfile(const std::filesystem::path& path){
    std::error_code error;if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path(),error);
    if(error)throw std::runtime_error("Cannot create style settings directory: "+error.message());
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){if(GetLastError()!=ERROR_FILE_EXISTS)throw std::runtime_error("Cannot create style settings file.");return;}
    WORD bom=0xFEFF;DWORD written=0;bool ok=WriteFile(file,&bom,sizeof(bom),&written,nullptr)&&written==sizeof(bom);CloseHandle(file);
    if(!ok)throw std::runtime_error("Cannot initialize UTF-16 style settings file.");
}
void WriteStyleProfile(const wchar_t* section,const wchar_t* key,const wchar_t* value,const wchar_t* path){
    if(!WritePrivateProfileStringW(section,key,value,path))throw std::runtime_error("Cannot write style settings.");
}
}
void SaveStylePreferences(const std::filesystem::path& path){
    EnsureUnicodeProfile(path);SaveThemePreferences(path);
    for(unsigned i=0;i<16;++i){wchar_t key[4]{};swprintf_s(key,L"%02u",i+1);wchar_t color[16]{};swprintf_s(color,L"#%02X%02X%02X",GetRValue(customColors[i]),GetGValue(customColors[i]),GetBValue(customColors[i]));WriteStyleProfile(L"Custom Colors",key,color,path.c_str());}
    auto scheme=std::to_wstring(defaultScheme);std::wstring favorites;for(int favorite:favoriteSchemes){favorites+=std::to_wstring(favorite)+L" ";}WriteStyleProfile(L"Style Preferences",L"Favorites",favorites.c_str(),path.c_str());
    WriteStyleProfile(L"Style Preferences",L"DefaultScheme",scheme.c_str(),path.c_str());
    WriteStyleProfile(L"Style Preferences",L"AutoSelect",autoSelectScheme?L"1":L"0",path.c_str());
    WriteStyleProfile(L"Language Preferences",L"Default",ConfigurationWide(lexerCatalog[defaultScheme]->id).c_str(),path.c_str());
    std::wstring stableFavorites;for(int index:favoriteSchemes)stableFavorites+=ConfigurationWide(lexerCatalog[index]->id)+L" ";
    WriteStyleProfile(L"Language Preferences",L"Favorites",stableFavorites.c_str(),path.c_str());
    WriteStyleDialogPreferencesChecked(path);
}

void DetectDocumentIndentation(EditorDocument& d){
    if(!d.style.detectIndentation)return;
    auto length = (std::min)(static_cast<size_t>(d.Send(SCI_GETLENGTH)), size_t(1024 * 1024));
    auto sample = d.Range(0, length);
    const int width = simd::DetectIndentation(sample);
    if (!width) return;
    const bool spaces = width <= 8;
    d.Send(SCI_SETUSETABS, !spaces); d.style.tabsAsSpaces = spaces;
    if (spaces) {
        d.Send(SCI_SETTABWIDTH, width); d.Send(SCI_SETINDENT, width);
        d.style.tabWidth = width; d.style.indentWidth = width;
    }
}

bool CompletionInputPending(){
    MSG input{};
    return (GetQueueStatus(QS_INPUT)&0xffff0000) || PeekMessageW(&input,nullptr,WM_KEYFIRST,WM_KEYLAST,PM_NOREMOVE);
}
void ProcessStyleBackground(EditorDocument& d, unsigned budgetMilliseconds, bool allowLongLine){
    if (d.LargeFile() || !budgetMilliseconds) return;
    static thread_local bool processing=false;if(processing || (!d.style.occurrencePending && !d.style.completionPending))return;processing=true;struct ResetProcessing{bool& value;~ResetProcessing(){value=false;}}resetProcessing{processing};
    auto validCompletion=[&]{return d.style.completionPending && d.style.completion && !d.completionComposing && !d.Send(SCI_GETREADONLY) && d.Send(SCI_GETSELECTIONS)==1 && GetFocus()==d.hwnd &&
        d.completionEpoch==d.style.completionEpoch && d.style.revision==d.style.completionRevision && !d.style.completionConfiguration.owner_before(d.style.configuration) && !d.style.configuration.owner_before(d.style.completionConfiguration) && GetDocumentLexer(d)==d.style.completionLexer && d.style.resolvedDialect==d.style.completionDialect && !d.style.completionTheme.owner_before(d.style.theme) && !d.style.theme.owner_before(d.style.completionTheme) &&
        d.Send(SCI_GETCURRENTPOS)==d.style.completionPosition && d.Send(SCI_WORDSTARTPOSITION,d.style.completionPosition,TRUE)>=d.style.completionStart;};
    if(d.style.completionPending){
        if(!validCompletion())d.style.completionPending=false;
        else{
            const auto position=d.style.completionPosition,styled=d.Send(SCI_GETENDSTYLED);
            const auto slice=static_cast<LRESULT>((std::min)(budgetMilliseconds,16u))*4096;
            if(styled<position){
                // shortcut: long-line lexing is indivisible; use an interruptible lexer for a hard callback deadline.
                if(LongCompletionLine(d,styled) && !allowLongLine){QueueCompletion(d);return;}
                auto end=(std::min)(position,styled+slice);
                if(LongCompletionLine(d,styled))end=d.Send(SCI_GETLINEENDPOSITION,d.Send(SCI_LINEFROMPOSITION,styled));
                else if(LongCompletionLine(d,end)){
                    const auto line=d.Send(SCI_LINEFROMPOSITION,end);
                    end=d.Send(SCI_POSITIONFROMLINE,line)-1;
                    // Include an empty preceding line's LF so the next slice can reach the long line.
                    end=(std::max)(end,styled+1);
                }
                d.Send(SCI_COLOURISE,styled,end);
            }
            if(validCompletion() && d.Send(SCI_GETENDSTYLED)>=position){
                d.style.completionPending=false;
                try{Complete(d);}catch(const std::exception& error){
                    d.Send(SCI_AUTOCCANCEL);d.Send(SCI_CALLTIPCANCEL);
                    OutputDebugStringA("MultiPad4 completion failed: ");OutputDebugStringA(error.what());OutputDebugStringA("\n");MessageBeep(MB_ICONERROR);
                }
            }
            if(d.style.completionPending)QueueCompletion(d);
            return;
        }
    }
    if(d.style.revision!=d.style.occurrenceRevision)d.style.occurrencePending=false;
    if(!d.style.occurrencePending)return;
    const auto originalIndicator=d.Send(SCI_GETINDICATORCURRENT),originalStart=d.Send(SCI_GETTARGETSTART),originalEnd=d.Send(SCI_GETTARGETEND),originalFlags=d.Send(SCI_GETSEARCHFLAGS);
    struct Restore{EditorDocument& d;LRESULT indicator,start,end,flags;~Restore(){d.Send(SCI_SETINDICATORCURRENT,indicator);d.Send(SCI_SETTARGETRANGE,start,end);d.Send(SCI_SETSEARCHFLAGS,flags);}}restore{d,originalIndicator,originalStart,originalEnd,originalFlags};
    auto& state=d.style;const auto length=d.Send(SCI_GETLENGTH);const auto tick=std::chrono::steady_clock::now();
    d.Send(SCI_SETINDICATORCURRENT,8);d.Send(SCI_SETSEARCHFLAGS,state.occurrenceFlags);
    for(unsigned count=0;count<2048 && std::chrono::steady_clock::now()-tick<std::chrono::milliseconds(budgetMilliseconds);++count){
        if(state.occurrenceCursor>=length){state.occurrencePending=false;break;}
        const auto end=std::min<LRESULT>(length,state.occurrenceCursor+64*1024+static_cast<LRESULT>(state.occurrenceNeedle.size()));
        d.Send(SCI_SETTARGETRANGE,state.occurrenceCursor,end);
        auto found=d.Send(SCI_SEARCHINTARGET,state.occurrenceNeedle.size(),reinterpret_cast<LPARAM>(state.occurrenceNeedle.c_str()));
        if(found<0){if(end==length){state.occurrencePending=false;break;}state.occurrenceCursor=end-static_cast<LRESULT>(state.occurrenceNeedle.size());continue;}
        const auto matchEnd=d.Send(SCI_GETTARGETEND);d.Send(SCI_INDICATORFILLRANGE,found,matchEnd-found);++state.occurrences;
        if(CompletionOption(d,IDM_VIEW_MARKOCCURRENCES_BOOKMARK,0))d.Send(SCI_MARKERADD,d.Send(SCI_LINEFROMPOSITION,found),0);
        state.occurrenceCursor=matchEnd>found?matchEnd:d.Send(SCI_POSITIONAFTER,found);
    }
}
