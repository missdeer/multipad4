#include "SimdGraphics.h"
#include "SimdText.h"
#include "StyleManager.h"
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
void EnsureUnicodeProfile(const std::filesystem::path&);
void WriteStyleProfile(const wchar_t*,const wchar_t*,const wchar_t*,const wchar_t*);
COLORREF customColors[16]{};
constexpr size_t Count = sizeof(lexerCatalog) / sizeof(*lexerCatalog);
std::map<std::pair<int,unsigned>, std::wstring> sharedOverrides;
int defaultScheme = 1;
bool autoSelectScheme = true;
std::vector<int> favoriteSchemes;
bool FavoriteContains(int id){return std::find(favoriteSchemes.begin(),favoriteSchemes.end(),id)!=favoriteSchemes.end();}
std::map<int,std::wstring> sharedExtensions;
std::map<std::pair<int,unsigned>,std::wstring> darkOverrides;

std::wstring LocalizedLabel(UINT id,const wchar_t* fallback){
    auto instance=_Module.GetModuleInstance();auto resource=FindResourceExW(instance,RT_STRING,MAKEINTRESOURCEW((id>>4)+1),GetThreadUILanguage());
    if(!resource)return fallback;auto loaded=LoadResource(instance,resource);auto* cursor=static_cast<const wchar_t*>(LockResource(loaded));if(!cursor)return fallback;
    for(UINT index=0;index<(id&15);++index)cursor+=*cursor+1;
    return *cursor?std::wstring(cursor+1,*cursor):std::wstring(fallback);
}
bool Starts(const char* value,const char* prefix){return strncmp(value,prefix,strlen(prefix))==0;}
EDITLEXER* SniffShebang(char* pchText,int& dialect) {
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
					return &lexPython;
				}
				if (Starts(name, "groovy")) {
					return &lexGroovy;
				}
				if (Starts(name, "scala")) {
					return &lexScala;
				}
				if (Starts(name, "rscript")) {
					return &lexRLang;
				}
				if (Starts(name, "escript")) {
					return &lexErlang;
				}
			}

			if (Starts(name, "bash") || Starts(name, "dash")) {
				return &lexBash;
			}
			if (Starts(name, "tcsh")) {
				dialect = IDM_LEXER_CSHELL;
				return &lexBash;
			}
			if (Starts(name, "perl")) {
				return &lexPerl;
			}
			if (Starts(name, "ruby")) {
				return &lexRuby;
			}
			//if (Starts(name, "rust")) {
			//	return &lexRust;
			//}
			if (Starts(name, "gawk") || Starts(name, "nawk")) {
				return &lexAwk;
			}
			if (Starts(name, "node")) {
				return &lexJavaScript;
			}
			if (Starts(name, "wish")) {
				return &lexTcl;
			}
			if (Starts(name, "wlua")) {
				return &lexLua;
			}
		}
		if (len >= 3) {
			if (Starts(name, "awk")) {
				return &lexAwk;
			}
			if (Starts(name, "lua")) {
				return &lexLua;
			}
			if (Starts(name, "php")) {
				return &lexPHP;
			}
			if (Starts(name, "tcl")) {
				return &lexTcl;
			}
			if (Starts(name, "ash") || Starts(name, "zsh") || Starts(name, "ksh")) {
				return &lexBash;
			}
			if (Starts(name, "csh")) {
				dialect = IDM_LEXER_CSHELL;
				return &lexBash;
			}
			if (Starts(name, "ipy")) {
				return &lexPython;
			}
		}
		if (len >= 2) {
			if (Starts(name, "sh")) {
				return &lexBash;
			}
			if (Starts(name, "py")) {
				return &lexPython;
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
    if(extension==L"m" && d.hwnd){int dialect=0;auto sniffed=DetectObjCAndMatlab(d,dialect);if(sniffed)for(size_t i=1;i<Count;++i)if(lexerCatalog[i]==sniffed)return static_cast<int>(i);}
    if (name.starts_with(L".bash") || name == L".profile") extension = L"sh";
    int matched=0;
    if(extension==L"r" && d.hwnd){char prefix[10]{};d.Send(SCI_GETTEXT,sizeof(prefix)-1,reinterpret_cast<LPARAM>(prefix));if(_strnicmp(prefix,"rebol",5)==0 && (prefix[5]==' ' || prefix[5]=='\t' || prefix[5]=='['))for(size_t i=1;i<Count;++i)if(lexerCatalog[i]->rid==NP2LEX_REBOL)return static_cast<int>(i);}
    if(name.starts_with(L"my") && (extension==L"ini" || extension==L"cnf"))for(size_t i=1;i<Count;++i)if(lexerCatalog[i]->rid==NP2LEX_CONFIG){matched=static_cast<int>(i);break;}
    for (size_t i = 1; i < Count; ++i) {
        if(matched)break;
        if (!lexerCatalog[i]->pszDefExt) continue;
        std::wistringstream fields(sharedExtensions.contains(lexerCatalog[i]->rid)?sharedExtensions[lexerCatalog[i]->rid]:lexerCatalog[i]->pszDefExt); std::wstring entry;
        while (std::getline(fields, entry, L';')) {
            entry.erase(0, entry.find_first_not_of(L" \t"));
            auto end = entry.find_last_not_of(L" \t"); if (end != std::wstring::npos) entry.resize(end+1);
            lower(entry); if (!extension.empty() && entry == extension){matched=static_cast<int>(i);break;}
        }
        if(matched)break;
    }
    if(matched && lexerCatalog[matched]->iLexer!=SCLEX_CONFIG)return matched;
    if(d.hwnd){
        char prefix[512]{};d.Send(SCI_GETTEXT,sizeof(prefix)-1,reinterpret_cast<LPARAM>(prefix));int dialect=0;
        auto sniffed=SniffShebang(prefix,dialect);
        if(!sniffed){const char* first=prefix;while(*first&&isspace(static_cast<unsigned char>(*first)))++first;
            if(Starts(first,"<?xml"))sniffed=&lexXML;
            else if(Starts(first,"<?php"))sniffed=&lexPHP;
            else if(_strnicmp(first,"<html",5)==0 || (_strnicmp(first,"<!doctype",9)==0 && StrStrIA(first,"html")))sniffed=&lexHTML;
            else if(*first=='<' && (isalpha(static_cast<unsigned char>(first[1])) || first[1]=='!' || first[1]=='?' || first[1]=='/'))sniffed=&lexXML;
        }
        if(sniffed)for(size_t i=1;i<Count;++i)if(lexerCatalog[i]==sniffed)return static_cast<int>(i);
    }
    if(!matched && d.hwnd){auto detected=AutoDetectContent(d,name.starts_with(L"."));if(detected)for(size_t i=1;i<Count;++i)if(lexerCatalog[i]==detected)return static_cast<int>(i);}
    return matched?matched:defaultScheme;
}
int ActualScheme(const EditorDocument& d){return d.style.resolvedScheme>0 && d.style.resolvedScheme<static_cast<int>(Count)?d.style.resolvedScheme:ResolveScheme(d);}
std::wstring Value(const EditorDocument& d, const EDITLEXER& lexer, unsigned index) {
    if(d.style.dark){auto found=darkOverrides.find({lexer.rid,index});if(found!=darkOverrides.end())return found->second;}
    auto found = sharedOverrides.find({lexer.rid,index});
    if (found != sharedOverrides.end()) return found->second;
    auto local = d.style.overrides.find((static_cast<unsigned>(lexer.rid) << 8) | index);
    return local == d.style.overrides.end() ? lexer.Styles[index].pszDefault : local->second;
}
unsigned StyleProperty(const std::wstring& value,const wchar_t* name,unsigned fallback){
    auto at=value.find(name);if(at==std::wstring::npos)return fallback;auto* first=value.c_str()+at+wcslen(name);
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
            unsigned color = 0; if (swscanf_s(val.c_str(), L"#%x", &color) == 1) {
                auto rgb = RGB((color>>16)&255,(color>>8)&255,color&255);
                d.Send(key == L"fore" ? SCI_STYLESETFORE : SCI_STYLESETBACK,style,rgb);
            }
        } else if (key == L"font") {
            while(!val.empty() && iswspace(val.front()))val.erase(val.begin());while(!val.empty()&&iswspace(val.back()))val.pop_back();
            if(val==L"$(Code)")val=d.style.codeFont.empty()?d.style.font:d.style.codeFont;
            else if(val==L"$(Text)")val=d.style.textFont.empty()?d.style.font:d.style.textFont;
            if (val.find(L"$(") == std::wstring::npos) { auto face = files::ToUtf8(val); d.Send(SCI_STYLESETFONT,style,reinterpret_cast<LPARAM>(face.c_str())); }
        } else if (key == L"size") {
            double size = wcstod(val.c_str(), nullptr);
            if (val.starts_with(L"+") || val.starts_with(L"-")) size += d.Send(SCI_STYLEGETSIZE,STYLE_DEFAULT);
            d.Send(SCI_STYLESETSIZEFRACTIONAL,style,static_cast<int>(std::clamp(size,2.0,512.0)*100));
        } else if (key == L"bold") d.Send(SCI_STYLESETBOLD,style,TRUE);
        else if (key == L"italic") d.Send(SCI_STYLESETITALIC,style,TRUE);
        else if (key == L"underline") d.Send(SCI_STYLESETUNDERLINE,style,TRUE);
        else if (key == L"strikeout") d.Send(SCI_STYLESETSTRIKE,style,TRUE);
        else if (key == L"overline") d.Send(SCI_STYLESETOVERLINE,style,TRUE);
        else if (key == L"eolfilled") d.Send(SCI_STYLESETEOLFILLED,style,TRUE);
        else if (key == L"charset") d.Send(SCI_STYLESETCHARACTERSET,style,wcstol(val.c_str(),nullptr,10));
        else if (key == L"case") d.Send(SCI_STYLESETCASE,style,val==L"u" ? SC_CASE_UPPER : val==L"l" ? SC_CASE_LOWER : SC_CASE_MIXED);
        else if (key == L"weight") d.Send(SCI_STYLESETWEIGHT,style,wcstol(val.c_str(),nullptr,10));
    }
}
int SchemeForCommand(UINT id) {
    int rid = 0;
    if (id == IDM_LEXER_TEXTFILE) rid = NP2LEX_TEXTFILE;
    else if (id == IDM_LEXER_2NDTEXTFILE) rid = NP2LEX_2NDTEXTFILE;
    else if (id == IDM_LEXER_CSV) rid = NP2LEX_CSV;
    else if (id >= IDM_LEXER_CSS && id <= IDM_LEXER_HSS) rid = NP2LEX_CSS;
    else if (id >= IDM_LEXER_JAVASCRIPT && id <= IDM_LEXER_JAVASCRIPT_JSX) rid = NP2LEX_JAVASCRIPT;
    else if (id >= IDM_LEXER_TYPESCRIPT && id <= IDM_LEXER_TYPESCRIPT_TSX) rid = NP2LEX_TYPESCRIPT;
    else if (id >= IDM_LEXER_WEB && id <= IDM_LEXER_JSPX) rid = NP2LEX_HTML;
    else if (id == IDM_LEXER_PHP) rid = NP2LEX_PHP;
    else if (id >= IDM_LEXER_MARKDOWN_GITHUB && id <= IDM_LEXER_MARKDOWN_PANDOC) rid = NP2LEX_MARKDOWN;
    else if (id == IDM_LEXER_MATHEMATICA) rid = NP2LEX_MATHEMATICA;
    else if (id >= IDM_LEXER_MATLAB && id <= IDM_LEXER_SCILAB) rid = NP2LEX_MATLAB;
    else if (id >= IDM_LEXER_BASH && id <= IDM_LEXER_M4) rid = NP2LEX_BASH;
    else if (id >= IDM_LEXER_XML && id <= IDM_LEXER_SVG) rid = NP2LEX_XML;
    else if (id == IDM_LEXER_VBNET || id == IDM_LEXER_VBA) rid = NP2LEX_VISUALBASIC;
    else if (id == IDM_LEXER_VBS) rid = NP2LEX_VBSCRIPT;
    else if (id == IDM_LEXER_APACHE) rid = NP2LEX_CONFIG;
    for (size_t i=1;i<Count;++i) if (lexerCatalog[i]->rid == rid) return static_cast<int>(i);
    return 0;
}
int Dialect(const EditorDocument& d, const EDITLEXER& lexer,int* command=nullptr) {
    int id=d.style.dialect;
    if(!id){
        auto filename=SchemeFilename(d);auto extension=filename.extension().wstring();for(auto& ch:extension)ch=static_cast<wchar_t>(towlower(ch));
        const std::pair<const wchar_t*,int> dialects[]={{L".scss",IDM_LEXER_SCSS},{L".less",IDM_LEXER_LESS},{L".hss",IDM_LEXER_HSS},{L".jsx",IDM_LEXER_JAVASCRIPT_JSX},{L".tsx",IDM_LEXER_TYPESCRIPT_TSX},{L".csh",IDM_LEXER_CSHELL},{L".tcsh",IDM_LEXER_CSHELL},{L".m4",IDM_LEXER_M4},{L".ac",IDM_LEXER_M4},{L".xsd",IDM_LEXER_XSD},{L".xsl",IDM_LEXER_XSLT},{L".xslt",IDM_LEXER_XSLT},{L".dtd",IDM_LEXER_DTD},{L".plist",IDM_LEXER_PROPERTY_LIST},{L".sci",IDM_LEXER_SCILAB},{L".sce",IDM_LEXER_SCILAB},{L".aspx",IDM_LEXER_ASPX_CS},{L".jsp",IDM_LEXER_JSP},{L".jspx",IDM_LEXER_JSPX},{L".asp",IDM_LEXER_ASP_VBS},{L".svg",IDM_LEXER_SVG},{L".vba",IDM_LEXER_VBA},{L".vbs",IDM_LEXER_VBS}};
        for(const auto& entry:dialects)if(extension==entry.first){id=entry.second;break;}
        auto name=filename.wstring();for(auto& ch:name)ch=static_cast<wchar_t>(towlower(ch));
        if(lexer.rid==NP2LEX_MATLAB && extension==L".m"){int sniffedDialect=0;DetectObjCAndMatlab(d,sniffedDialect);if(sniffedDialect)id=sniffedDialect;}
        if(lexer.rid==NP2LEX_CONFIG && (name.starts_with(L"httpd") || extension==L".htaccess"))id=IDM_LEXER_APACHE;
        if((extension==L".aspx" || extension==L".asp") && d.hwnd){char text[1024]{};d.Send(SCI_GETTEXT,sizeof(text)-1,reinterpret_cast<LPARAM>(text));for(auto& ch:text)ch=static_cast<char>(tolower(static_cast<unsigned char>(ch)));if(strstr(text,"language=\"vb") || strstr(text,"language='vb"))id=extension==L".aspx"?IDM_LEXER_ASPX_VB:IDM_LEXER_ASP_VBS;else if(strstr(text,"language=\"jscript")||strstr(text,"language='jscript")||strstr(text,"language=\"javascript"))id=IDM_LEXER_ASP_JS;}
    }
    if(!d.style.dialect && (lexer.rid==NP2LEX_HTML || lexer.rid==NP2LEX_XML)){auto markup=DetectDocumentMarkupDialect(d);if(markup)id=markup;}
    if(command)*command=id;
    switch(lexer.rid) {
    case NP2LEX_ACTIONSCRIPT: case NP2LEX_APDL: case NP2LEX_ELIXIR: case NP2LEX_RESOURCESCRIPT: return 1;
    case NP2LEX_CSS: return id >= IDM_LEXER_CSS && id <= IDM_LEXER_HSS ? id-IDM_LEXER_CSS : 0;
    case NP2LEX_JAVASCRIPT: return id == IDM_LEXER_JAVASCRIPT_JSX ? 1 : 0;
    case NP2LEX_TYPESCRIPT: return id == IDM_LEXER_TYPESCRIPT_TSX ? 3 : 2;
    case NP2LEX_BASH: return id >= IDM_LEXER_BASH && id <= IDM_LEXER_M4 ? id-IDM_LEXER_BASH : 0;
    case NP2LEX_MARKDOWN: return id >= IDM_LEXER_MARKDOWN_GITHUB && id <= IDM_LEXER_MARKDOWN_PANDOC ? id-IDM_LEXER_MARKDOWN_GITHUB : 0;
    case NP2LEX_MATLAB: return id >= IDM_LEXER_MATLAB && id <= IDM_LEXER_SCILAB ? id-IDM_LEXER_MATLAB : 0;
    case NP2LEX_VISUALBASIC: return id == IDM_LEXER_VBA ? 1 : 0;
    case NP2LEX_VBSCRIPT: return 2;
    case NP2LEX_CSV: return d.style.csvOptions;
    case NP2LEX_HTML:
        if(id==IDM_LEXER_ASPX_VB || id==IDM_LEXER_ASP_VBS)return 2|4|8|(2<<12);
        if(id==IDM_LEXER_JSP)return 2|4|16|8|(1<<12);
        if(id==IDM_LEXER_JSPX)return 2|4|16|32|1|(1<<12);
        return 2|4|8|(1<<12);
    case NP2LEX_XML:return 1|(1<<12)|(id==IDM_LEXER_SVG?4:0);
    default:return 0;
    }
}
std::string Range(EditorDocument& d,LRESULT start,LRESULT end){
    if(end<start)return {};std::string text(static_cast<size_t>(end-start)+1,'\0');Sci_TextRangeFull range{{start,end},text.data()};d.Send(SCI_GETTEXTRANGEFULL,0,reinterpret_cast<LPARAM>(&range));text.resize(static_cast<size_t>(end-start));return text;
}
int CompletionOption(const EditorDocument& d,int id,int fallback=1) {
    auto found=d.style.completionOptions.find(id);return found==d.style.completionOptions.end()?fallback:found->second;
}
int StyleCategory(const EDITLEXER& lexer,int style){
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
    d.Send(SCI_COLOURISE,0,-1);const auto* lexer=GetDocumentLexer(d);
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
void Complete(EditorDocument& d,bool autoInsert=false) {
    if (d.Send(SCI_GETREADONLY) || d.Send(SCI_AUTOCACTIVE)) return;
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
    if(position-start<minimum) return;
    if(CompletionOption(d,IDC_AUTOC_ENGLISH_IME_ONLY,0)){
        auto context=ImmGetContext(d.hwnd);DWORD conversion=0,sentence=0;
        bool native=context && ImmGetOpenStatus(context) && ImmGetConversionStatus(context,&conversion,&sentence) && (conversion&IME_CMODE_NATIVE);
        if(context)ImmReleaseContext(d.hwnd,context);if(native)return;
    }
    auto prefix=Range(d,start,position);
    auto styled=d.Send(SCI_GETENDSTYLED);if(styled<position)d.Send(SCI_COLOURISE,styled,position);
    const bool rootNumber=!prefix.empty() && isdigit(static_cast<unsigned char>(prefix[0]));
    const bool ignoreLexer=CompletionOption(d,IDC_AUTOC_ONLY_DOCUMENT_WORDS,0)!=0 || rootNumber;
    const bool ignoreCase=ignoreLexer || d.style.completionIgnoreCase;
    std::set<std::string> words,foldedWords;
    int category=StyleCategory(lexer,static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT,max<LRESULT>(0,position-1))));
    if(category==1 && !CompletionOption(d,IDC_AUTO_COMPLETE_INSIDE_COMMONT))return;
    if(category==2 && !CompletionOption(d,IDC_AUTO_COMPLETE_INSIDE_STRING))return;
    if((lexer.iLexer==SCLEX_NULL || category==3) && !CompletionOption(d,IDC_AUTO_COMPLETE_INSIDE_PLAINTEXT))return;
    if(rootNumber){int minimumNumber=CompletionOption(d,IDC_AUTOC_MIN_NUMBER_LENGTH,3);if(minimumNumber<=0)return;if(prefix.size()>1 && prefix[0]=='0' && (tolower(static_cast<unsigned char>(prefix[1]))=='x' || tolower(static_cast<unsigned char>(prefix[1]))=='b' || tolower(static_cast<unsigned char>(prefix[1]))=='o'))minimumNumber+=2;if(prefix.size()<static_cast<size_t>(minimumNumber))return;}
    auto accept=[&](std::string word) {
        if(word.size()<prefix.size()) return;
        bool matches=true;
        for(size_t i=0;i<prefix.size();++i) if(ignoreCase ? tolower(static_cast<unsigned char>(prefix[i]))!=tolower(static_cast<unsigned char>(word[i])) : prefix[i]!=word[i]) {matches=false;break;}
        if(matches && word.find_first_of("\r\n\t")==std::string::npos){
            if(ignoreCase){auto folded=word;for(auto& ch:folded)if(static_cast<unsigned char>(ch)<128)ch=static_cast<char>(tolower(static_cast<unsigned char>(ch)));if(!foldedWords.insert(std::move(folded)).second)return;}
            words.insert(std::move(word));
        }
    };
    auto addKeywordList=[&](const char* list){
        if(!list)return;std::istringstream stream(list);std::string word;
        while(stream>>word){for(auto& ch:word)if(ch=='^')ch=' ';auto open=word.find('(');if(open!=std::string::npos){word.resize(open+1);word+=')';}accept(word);}
    };
    auto addLexer=[&](const EDITLEXER& source){for(unsigned i=0;i<min<unsigned>(source.keywordCount,16);++i)if(!((source.keywordAttr>>(i*4))&KeywordAttr_NoAutoComp))addKeywordList(source.pszKeyWords[i]);};
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
        const char* const* extra=GetCompletionExtraKeywords(d.style.resolvedDialect);if(!extra && category==1)extra=cache.extraKeywords;
        if(extra)for(unsigned i=0;i<4;++i)addKeywordList(extra[i]);
        int style=static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT,max<LRESULT>(0,position-1)));
        if(lexer.iLexer==SCLEX_HTML || lexer.iLexer==SCLEX_PHPSCRIPT){
            if(style>=SCE_STYLE_OFFSET_PHP)addLexer(lexPHP);
            else if(style>=SCE_STYLE_OFFSET_CSS)addLexer(lexCSS);
            else if(style>=SCE_STYLE_OFFSET_JS)addLexer(lexJavaScript);
            else if(style>=SCE_HB_DEFAULT)addLexer(lexVBScript);
        }
        if(lexer.rid==NP2LEX_TYPESCRIPT)addLexer(lexJavaScript);
        if(lexer.iLexer==SCLEX_PHPSCRIPT || lexer.iLexer==SCLEX_JAVASCRIPT || lexer.iLexer==SCLEX_MARKDOWN)for(unsigned i=0;i<3 && i<lexHTML.keywordCount;++i)addKeywordList(lexHTML.pszKeyWords[i]);
    };
    collectKeywords();
    bool latexRoot=d.style.latex && start>0 && d.Send(SCI_GETCHARAT,start-1)=='\\';
    bool emojiRoot=d.style.latex && start>1 && d.Send(SCI_GETCHARAT,start-1)==':' && d.Send(SCI_GETCHARAT,start-2)=='\\';
    if(latexRoot || emojiRoot){words.clear();foldedWords.clear();addKeywordList(emojiRoot?EmojiInputSequenceString:LaTeXInputSequenceString);}
    auto collectDocument=[&](){
        if(contextResult==1 || latexRoot || emojiRoot || !CompletionOption(d,IDC_AUTOC_SCAN_DOCUMENT_WORDS))return;
        uint32_t ignored[8]{};
        for(unsigned i=0;i<8;++i){ignored[i]=cache.IgnoreWordStyleMask[i];if(category!=1&&!CompletionOption(d,IDC_SCAN_WORD_INSIDE_COMMONT))ignored[i]|=cache.CommentStyleMask[i];if(category!=2&&!CompletionOption(d,IDC_SCAN_WORD_INSIDE_STRING))ignored[i]|=cache.AllStringStyleMask[i];if(category!=3&&!CompletionOption(d,IDC_SCAN_WORD_INSIDE_PLAINTEXT))ignored[i]|=cache.PlainTextStyleMask[i];}
        AddDocumentCompletionWords(d,prefix,ignoreCase,documentPrefix,static_cast<UINT>(CompletionOption(d,IDC_AUTOC_SCAN_WORDS_TIMEOUT,500)),ignored,accept);
    };
    collectDocument();
    if(words.empty() && !latexRoot && !emojiRoot){
        auto separator=prefix.find_last_of(":.#@<\\/->$%");
        if(separator!=std::string::npos && separator+1<prefix.size()){
            start+=separator+1;prefix.erase(0,separator+1);documentPrefix=0;collectKeywords();collectDocument();
        }
    }
    std::string list; for(const auto& word:words) {if(!list.empty())list+='\n';list+=word;}
    if(!list.empty() && !(words.size()==1 && words.begin()->size()==prefix.size())) {d.Send(SCI_AUTOCSETSEPARATOR,'\n');d.Send(SCI_AUTOCSETIGNORECASE,ignoreCase);d.Send(SCI_AUTOCSETORDER,SC_ORDER_PERFORMSORT);d.Send(SCI_AUTOCSETCHOOSESINGLE,autoInsert);d.Send(SCI_AUTOCSHOW,prefix.size(),reinterpret_cast<LPARAM>(list.c_str()));}
}
}
size_t DocumentSchemeCount() { return Count-1; }
const EDITLEXER* GetDocumentLexer(const EditorDocument& document) {return lexerCatalog[ActualScheme(document)];}
bool SelectDocumentScheme(EditorDocument& document,const std::wstring& name) {
    for(size_t i=1;i<Count;++i) {
        const auto& lexer=*lexerCatalog[i];
        bool matches=CompareStringOrdinal(lexer.pszName,-1,name.c_str(),-1,TRUE)==CSTR_EQUAL || (name.size()>=3 && _wcsnicmp(lexer.pszName,name.c_str(),name.size())==0);
        if(!matches && lexer.pszDefExt){std::wistringstream fields(lexer.pszDefExt);std::wstring extension;while(std::getline(fields,extension,L';')){extension.erase(0,extension.find_first_not_of(L" \t"));extension.erase(extension.find_last_not_of(L" \t")+1);if(CompareStringOrdinal(extension.c_str(),-1,name.c_str(),-1,TRUE)==CSTR_EQUAL){matches=true;break;}}}
        if(matches){document.style.scheme=static_cast<int>(i);document.style.dialect=0;ConfigureDocumentStyle(document);return true;}
    }
    return false;
}
void ForgetDocumentStyle(EditorDocument& d) {d.style.autoClosers.clear();d.style.colorStart=d.style.colorEnd=-1;d.style.calltipHex.clear();d.Send(SCI_AUTOCCANCEL);d.Send(SCI_CALLTIPCANCEL);}
void ApplyStyleTabSettings(EditorDocument&,int,int,int,bool,bool);
void ConfigureDocumentStyle(EditorDocument& d) {
    d.style.lineHighlight=std::clamp(d.style.lineHighlight,0,2);d.style.wrapMode=std::clamp(d.style.wrapMode,1,4);d.style.wrapIndent=std::clamp(d.style.wrapIndent,0,7);int symbols=std::clamp(d.style.wrapSymbols,0,32);d.style.wrapSymbols=std::clamp(symbols%10,0,2)+10*std::clamp(symbols/10,0,3);
    d.style.fontSize=std::clamp(d.style.fontSize,2,512);d.style.codeFontSize=std::clamp(d.style.codeFontSize,2,512);d.style.textFontSize=std::clamp(d.style.textFontSize,2,512);
    bool initializeAppearance=d.style.appliedTabScheme<0;
    const auto selected=ResolveScheme(d);d.style.resolvedScheme=selected;
    auto& lexer=*lexerCatalog[selected];
    ApplyStyleTabSettings(d,selected,lexer.defaultTabWidth,lexer.defaultIndentWidth,(lexer.lexerAttr&LexerAttr_TabAsSpaces)!=0,!(lexer.lexerAttr&LexerAttr_NoGlobalTabSettings));
    d.language=lexer.pszName;
    if(lexer.rid==NP2LEX_CPP)d.language=L"C/C++";
    if(lexer.rid==NP2LEX_PYTHON)d.language=L"Python";
    if(lexer.rid==NP2LEX_JSON)d.language=L"JSON";
    if(lexer.rid==NP2LEX_TEXTFILE)d.language=L"Text";
    d.language=LocalizedLabel(lexer.rid,d.language.c_str());
    if(initializeAppearance){d.edgeMode=std::clamp(d.style.longLineMode,EDGE_LINE,EDGE_BACKGROUND);d.Send(SCI_SETEDGECOLUMN,std::clamp(d.style.longLineColumn,0,1000000));d.Send(SCI_SETEDGEMODE,d.style.longLineEnabled?d.edgeMode:EDGE_NONE);}
    ApplyDocumentWrapSettings(d);
    d.Send(SCI_SETLEXER,lexer.iLexer);
    if(lexer.rid==NP2LEX_CSV && !d.style.csvConfigured){auto ext=SchemeFilename(d).extension().wstring();int delimiter=_wcsicmp(ext.c_str(),L".tsv")==0?'\t':DetectDocumentCSVDelimiter(d);d.style.csvOptions=(static_cast<int>('"')<<8)|(delimiter?delimiter:',');}
    int effective=0;unsigned dialect=static_cast<unsigned>(Dialect(d,lexer,&effective));d.style.resolvedDialect=effective;
    // This fork stores HTML/CSV options as a little-endian four-byte property;
    // ordinary small dialects are ASCII digits (same representation as upstream).
    if(dialect<10 && lexer.rid!=NP2LEX_HTML && lexer.rid!=NP2LEX_XML && lexer.rid!=NP2LEX_CSV)dialect+='0';
    char dialectBytes[5]{};memcpy(dialectBytes,&dialect,sizeof(dialect));
    d.Send(SCI_SETPROPERTY,reinterpret_cast<WPARAM>("lexer.lang"),reinterpret_cast<LPARAM>(dialectBytes));
    d.Send(SCI_SETPROPERTY,reinterpret_cast<WPARAM>("fold"),reinterpret_cast<LPARAM>("1"));
    for(unsigned i=0;i<16;++i) {
        auto attr=(lexer.keywordAttr>>(i*4))&15;
        const char* words=i<lexer.keywordCount && !(attr&KeywordAttr_NoLexer) ? lexer.pszKeyWords[i] : "";
        d.Send(SCI_SETKEYWORDS,i|((attr&3)<<8),reinterpret_cast<LPARAM>(words?words:""));
    }
    d.Send(SCI_STYLERESETDEFAULT);
    bool codeFont=d.style.fontKind<0?lexer.bUseDefaultCodeStyle:d.style.fontKind==0;
    const auto& chosenFont=codeFont?d.style.codeFont:d.style.textFont;
    auto font=files::ToUtf8(chosenFont.empty()?d.style.font:chosenFont);
    d.Send(SCI_STYLESETFONT,STYLE_DEFAULT,reinterpret_cast<LPARAM>(font.c_str()));
    d.Send(SCI_STYLESETSIZE,STYLE_DEFAULT,chosenFont.empty()?d.style.fontSize:(codeFont?d.style.codeFontSize:d.style.textFontSize));
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};GetUserDefaultLocaleName(locale,LOCALE_NAME_MAX_LENGTH);auto fontLocale=files::ToUtf8(locale);d.Send(SCI_SETFONTLOCALE,0,reinterpret_cast<LPARAM>(fontLocale.c_str()));
    d.Send(SCI_STYLESETFORE,STYLE_DEFAULT,d.style.dark?RGB(220,220,220):RGB(32,36,42));
    d.Send(SCI_STYLESETBACK,STYLE_DEFAULT,d.style.dark?RGB(30,30,30):RGB(255,255,255));
    ApplyStyle(d,STYLE_DEFAULT,Value(d,*lexerCatalog[0],d.style.fontKind<0?(lexer.bUseDefaultCodeStyle?0:1):d.style.fontKind));
    ApplyStyle(d,STYLE_DEFAULT,Value(d,lexer,0));d.Send(SCI_STYLECLEARALL);
    for(unsigned index : {2u,5u,6u,16u,19u})ApplyStyle(d,lexerCatalog[0]->Styles[index].iStyle,Value(d,*lexerCatalog[0],index));
    auto applyAll=[&](const EDITLEXER& styles,unsigned offset,unsigned limit=0){
        for(unsigned i=1;i<styles.iStyleCount;++i){auto packed=styles.Styles[i].iStyle;const auto value=Value(d,styles,i);const unsigned first=(packed&255)+offset;
            ApplyStyle(d,first,value);
            if(packed>255)d.Send(SCI_COPYSTYLES,first|(offset<<8),packed>>8);
            if(limit && styles.Styles[i].iStyle==limit)break;
        }
    };
    applyAll(lexer,0);
    if(lexer.rid==NP2LEX_HTML || lexer.rid==NP2LEX_XML || lexer.rid==NP2LEX_MARKDOWN || lexer.rid==NP2LEX_PHP){
        if(lexer.rid!=NP2LEX_MARKDOWN){applyAll(lexJavaScript,SCE_STYLE_OFFSET_JS);applyAll(lexCSS,SCE_STYLE_OFFSET_CSS);}
        if(lexer.rid==NP2LEX_MARKDOWN || lexer.rid==NP2LEX_PHP)applyAll(lexHTML,0,SCE_H_QUESTION);
    }
    auto property=[&](unsigned index,const wchar_t* name,unsigned fallback){
        const auto value=Value(d,*lexerCatalog[0],index);const auto at=value.find(name);
        if(at==std::wstring::npos)return fallback;
        const auto first=value.c_str()+at+wcslen(name);
        if(*first=='#'){unsigned color=0;if(swscanf_s(first,L"#%x",&color)==1)return static_cast<unsigned>(RGB((color>>16)&255,(color>>8)&255,color&255));}
        return static_cast<unsigned>(wcstoul(first,nullptr,10));
    };
    auto element=[&](int elementId,unsigned index,const wchar_t* key,unsigned fallback){unsigned color=property(index,key,fallback);unsigned alpha=std::min(property(index,L"alpha:",255),255u);d.Send(SCI_SETELEMENTCOLOUR,elementId,(color&0xFFFFFF)|(alpha<<24));};
    element(SC_ELEMENT_WHITE_SPACE,8,L"fore:",RGB(255,64,0));
    element(SC_ELEMENT_SELECTION_BACK,7,L"back:",GetSysColor(COLOR_HIGHLIGHT));
    element(SC_ELEMENT_SELECTION_ADDITIONAL_BACK,7,L"fore:",GetSysColor(COLOR_HIGHLIGHT));
    auto caret = property(10, L"fore:", d.style.dark ? RGB(220,220,220) : RGB(0,0,0));
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
    d.Send(SCI_SETMARGINWIDTHN,2,d.style.folding?MulDiv(16,GetDpiForWindow(d.hwnd),96):0);
    d.Send(SCI_SETMARGINTYPEN,2,SC_MARGIN_SYMBOL);d.Send(SCI_SETMARGINMASKN,2,SC_MASK_FOLDERS);
    d.Send(SCI_SETMARGINSENSITIVEN,2,TRUE);
    const int folderShapes[]={SC_MARK_BOXPLUSCONNECTED,SC_MARK_BOXMINUSCONNECTED,SC_MARK_TCORNER,SC_MARK_LCORNER,SC_MARK_VLINE,SC_MARK_BOXPLUS,SC_MARK_BOXMINUS};
    for(int marker=SC_MARKNUM_FOLDEREND;marker<=SC_MARKNUM_FOLDEROPEN;++marker){
        d.Send(SCI_MARKERDEFINE,marker,folderShapes[marker-SC_MARKNUM_FOLDEREND]);d.Send(SCI_MARKERSETFORETRANSLUCENT,marker,property(15,L"back:",d.style.dark?RGB(80,80,80):RGB(173,216,230))|0xFF000000u);d.Send(SCI_MARKERSETBACKTRANSLUCENT,marker,property(15,L"fore:",RGB(128,128,255))|0xFF000000u);
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
    d.Send(SCI_COLOURISE,0,-1);
}
namespace {
struct SchemeDialogContext {
    EditorDocument& document;
    bool customize;
    int scheme = 1;
    unsigned style = 0;
    std::map<std::pair<int,unsigned>,std::wstring> saved;
    bool changing = true;
    bool favorites = false;
    bool extension = false;
    std::map<int,std::wstring> savedExtensions;
    std::map<std::pair<int,unsigned>,std::wstring> savedDark;
    struct Layout {HWND window;RECT rectangle;};
    std::vector<Layout> controls;
    int clientWidth=0,clientHeight=0;
};
void SaveStyleField(HWND window, SchemeDialogContext& context) {
    if(!context.customize || context.changing) return;
    int length=GetWindowTextLengthW(GetDlgItem(window,IDC_STYLEEDIT));
    std::wstring value(static_cast<size_t>(length)+1,L'\0');
    GetDlgItemTextW(window,IDC_STYLEEDIT,value.data(),length+1);value.resize(length);
    if(context.extension)sharedExtensions[lexerCatalog[context.scheme]->rid]=std::move(value);
    else if(context.document.style.dark)darkOverrides[{lexerCatalog[context.scheme]->rid,context.style}]=std::move(value);
    else sharedOverrides[{lexerCatalog[context.scheme]->rid,context.style}]=std::move(value);
}
void ShowStyleField(HWND window, SchemeDialogContext& context) {
    context.changing=true;
    const auto& lexer=*lexerCatalog[context.scheme];
    for(int control : {IDC_STYLEFORE,IDC_STYLEBACK,IDC_STYLEFONT})EnableWindow(GetDlgItem(window,control),!context.extension);
    if(context.extension){
        SetDlgItemTextW(window,IDC_STYLELABEL,L"Associated filename extensions:");
        SetDlgItemTextW(window,IDC_STYLEVALUE_DEFAULT,lexer.pszDefExt?lexer.pszDefExt:L"");
        SetDlgItemTextW(window,IDC_STYLEEDIT,sharedExtensions.contains(lexer.rid)?sharedExtensions[lexer.rid].c_str():lexer.pszDefExt?lexer.pszDefExt:L"");context.changing=false;return;
    }
    auto styleLabel=LocalizedLabel(lexer.Styles[context.style].rid,lexer.Styles[context.style].pszName);SetDlgItemTextW(window,IDC_STYLELABEL,styleLabel.c_str());
    SetDlgItemTextW(window,IDC_STYLEVALUE_DEFAULT,lexer.Styles[context.style].pszDefault);
    auto& definitions=context.document.style.dark?darkOverrides:sharedOverrides;
    auto found=definitions.find({lexer.rid,context.style});
    auto value=found==definitions.end()?lexer.Styles[context.style].pszDefault:found->second.c_str();
    SetDlgItemTextW(window,IDC_STYLEEDIT,value);context.changing=false;
}
void MoveFavoriteItem(HWND window,int direction){
    CTreeViewCtrl tree(GetDlgItem(window,IDC_STYLELIST));auto selected=tree.GetSelectedItem();if(!selected)return;
    auto neighbor=direction<0?tree.GetPrevSiblingItem(selected):tree.GetNextSiblingItem(selected);if(!neighbor)return;
    wchar_t first[256]{},second[256]{};tree.GetItemText(selected,first,256);tree.GetItemText(neighbor,second,256);
    auto data=tree.GetItemData(selected),other=tree.GetItemData(neighbor);bool checked=tree.GetCheckState(selected)!=0,otherCheck=tree.GetCheckState(neighbor)!=0;
    tree.SetItemText(selected,second);tree.SetItemData(selected,other);tree.SetCheckState(selected,otherCheck);
    tree.SetItemText(neighbor,first);tree.SetItemData(neighbor,data);tree.SetCheckState(neighbor,checked);tree.SelectItem(neighbor);tree.EnsureVisible(neighbor);
}
INT_PTR CALLBACK SchemeProcedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    try {
    auto* c=reinterpret_cast<SchemeDialogContext*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG) {
        c=reinterpret_cast<SchemeDialogContext*>(l);SetWindowLongPtrW(window,DWLP_USER,l);
        CTreeViewCtrl tree(GetDlgItem(window,IDC_STYLELIST));HTREEITEM selected=nullptr;
        if(c->favorites){tree.ModifyStyle(0,TVS_CHECKBOXES);SetWindowTextW(window,L"Favorite Schemes (Ctrl+Up/Down reorders)");
            for(int control:{IDC_DEFAULTSCHEME,IDC_AUTOSELECT}){auto button=GetDlgItem(window,control);auto style=GetWindowLongPtrW(button,GWL_STYLE);SetWindowLongPtrW(button,GWL_STYLE,(style&~BS_TYPEMASK)|BS_PUSHBUTTON);SetWindowTextW(button,control==IDC_DEFAULTSCHEME?L"Move Up":L"Move Down");RECT placement{control==IDC_DEFAULTSCHEME?7:86,171,control==IDC_DEFAULTSCHEME?79:158,191};MapDialogRect(window,&placement);SetWindowPos(button,nullptr,placement.left,placement.top,placement.right-placement.left,placement.bottom-placement.top,SWP_NOZORDER);ShowWindow(button,SW_SHOW);}}
        std::vector<size_t> order;
        if(!c->customize)for(int i:favoriteSchemes)if(i>0 && i<static_cast<int>(Count))order.push_back(static_cast<size_t>(i));
        for(size_t i=c->customize?0:1;i<Count;++i)if(c->customize || !FavoriteContains(static_cast<int>(i)))order.push_back(i);
        for(size_t i:order) {
            auto name=LocalizedLabel(lexerCatalog[i]->rid,lexerCatalog[i]->pszName);auto root=tree.InsertItem(name.c_str(),TVI_ROOT,TVI_LAST);tree.SetItemData(root,(i<<16));
            if(static_cast<int>(i)==c->scheme)selected=root;
            if(c->favorites)tree.SetCheckState(root,FavoriteContains(static_cast<int>(i)));
            if(c->customize)for(unsigned j=0;j<lexerCatalog[i]->iStyleCount;++j) {
                auto label=LocalizedLabel(lexerCatalog[i]->Styles[j].rid,lexerCatalog[i]->Styles[j].pszName);auto item=tree.InsertItem(label.c_str(),root,TVI_LAST);tree.SetItemData(item,(i<<16)|(j+1));
            }
        }
        tree.SelectItem(selected);
        if(c->customize) {
            SetDlgItemTextW(window,IDC_PREVSTYLE,L"<");SetDlgItemTextW(window,IDC_NEXTSTYLE,L">");
            SetDlgItemTextW(window,IDC_STYLEEDIT_HELP,L"Use upstream style definitions: font:Consolas; size:11; fore:#RRGGBB; back:#RRGGBB; bold; italic; underline; eolfilled. Select a scheme and style at left. Preview applies to the target document; Cancel restores prior definitions.");
            ShowStyleField(window,*c);
        } else {
            CheckDlgButton(window,IDC_AUTOSELECT,autoSelectScheme?BST_CHECKED:BST_UNCHECKED);
            CheckDlgButton(window,IDC_DEFAULTSCHEME,c->scheme==defaultScheme?BST_CHECKED:BST_UNCHECKED);
        }
        PositionDialogResizeGrip(window,IDC_RESIZEGRIP3);
        RECT client{};GetClientRect(window,&client);c->clientWidth=client.right;c->clientHeight=client.bottom;
        for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){RECT rectangle{};GetWindowRect(child,&rectangle);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rectangle),2);c->controls.push_back({child,rectangle});}
        CWindow(window).CenterWindow(GetParent(window));return TRUE;
    }
    if(!c)return FALSE;
    if(message==WM_GETMINMAXINFO && c->clientWidth){
        auto* bounds=reinterpret_cast<MINMAXINFO*>(l);RECT client{},outer{};GetClientRect(window,&client);GetWindowRect(window,&outer);bounds->ptMinTrackSize.x=c->clientWidth+outer.right-outer.left-client.right;bounds->ptMinTrackSize.y=c->clientHeight+outer.bottom-outer.top-client.bottom;return TRUE;
    }
    if(message==WM_SIZE && c->clientWidth){
        int dx=LOWORD(l)-c->clientWidth,dy=HIWORD(l)-c->clientHeight;
        for(const auto& item:c->controls){RECT r=item.rectangle;int id=GetDlgCtrlID(item.window);
            if(id==IDC_STYLELIST){r.bottom+=dy;if(!c->customize)r.right+=dx;}
            else if(!c->customize){r.top+=dy;r.bottom+=dy;if(id==IDOK||id==IDCANCEL){r.left+=dx;r.right+=dx;}else if(c->favorites && (id==IDC_DEFAULTSCHEME||id==IDC_AUTOSELECT)){if(id==IDC_AUTOSELECT){r.left+=dx/2;r.right+=dx;}else r.right+=dx/2;}else if(id!=IDC_RESIZEGRIP3)r.right+=dx;}
            else if(id==IDC_INFO_GROUPBOX || id==IDC_STYLEEDIT_HELP){r.right+=dx;r.bottom+=dy;}
            else if(id==IDC_TITLE){r.right+=dx;}
            else if(id==IDC_RESIZEGRIP3){r.top+=dy;r.bottom+=dy;}
            else if(id!=-1 && id!=65535){r.top+=dy;r.bottom+=dy;if(id==IDC_STYLEEDIT || id==IDC_STYLEVALUE_DEFAULT || id==IDC_STYLELABEL || id==IDC_STYLELABEL_DEFAULT)r.right+=dx;else if(id==IDOK || id==IDCANCEL || id==IDC_PREVIEW || id==IDC_STYLEDEFAULT || id==IDC_PREVSTYLE || id==IDC_NEXTSTYLE){r.left+=dx;r.right+=dx;}}
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
            SaveStyleField(window,*c);c->scheme=static_cast<int>(change->itemNew.lParam>>16);c->extension=(change->itemNew.lParam&65535)==0;c->style=c->extension?0:static_cast<unsigned>((change->itemNew.lParam&65535)-1);
            if(c->customize)ShowStyleField(window,*c);return TRUE;
        }
        if(n->idFrom==IDC_STYLELIST && n->code==NM_DBLCLK && !c->customize)SendMessageW(window,WM_COMMAND,IDOK,0);
    }
    if(message==WM_CLOSE || (message==WM_COMMAND && LOWORD(w)==IDCANCEL)) {
        if(c->customize){sharedOverrides=c->saved;sharedExtensions=c->savedExtensions;darkOverrides=c->savedDark;ConfigureDocumentStyle(c->document);}EndDialog(window,IDCANCEL);return TRUE;
    }
    if(message!=WM_COMMAND)return FALSE;
    UINT id=LOWORD(w);
    if(c->favorites && (id==IDC_DEFAULTSCHEME || id==IDC_AUTOSELECT)){MoveFavoriteItem(window,id==IDC_DEFAULTSCHEME?-1:1);return TRUE;}
    if(c->customize && id==IDC_STYLEEDIT && HIWORD(w)==EN_CHANGE){SaveStyleField(window,*c);return TRUE;}
    if(id==IDOK) {
        if(c->favorites){favoriteSchemes.clear();CTreeViewCtrl tree(GetDlgItem(window,IDC_STYLELIST));for(auto item=tree.GetRootItem();item;item=tree.GetNextSiblingItem(item))if(tree.GetCheckState(item) && favoriteSchemes.size()<31)favoriteSchemes.push_back(static_cast<int>(tree.GetItemData(item)>>16));EndDialog(window,IDOK);return TRUE;}
        if(c->customize)SaveStyleField(window,*c);
        else {c->document.style.scheme=c->scheme;autoSelectScheme=IsDlgButtonChecked(window,IDC_AUTOSELECT)!=0;if(IsDlgButtonChecked(window,IDC_DEFAULTSCHEME))defaultScheme=c->scheme;}
        ConfigureDocumentStyle(c->document);EndDialog(window,IDOK);return TRUE;
    }
    if(!c->customize)return FALSE;
    if(id==IDC_PREVIEW){SaveStyleField(window,*c);ConfigureDocumentStyle(c->document);return TRUE;}
    if(id==IDC_STYLEDEFAULT){
        if(c->extension)sharedExtensions.erase(lexerCatalog[c->scheme]->rid);
        else if(c->document.style.dark){auto key=std::make_pair(lexerCatalog[c->scheme]->rid,c->style);auto saved=c->savedDark.find(key);if(saved!=c->savedDark.end())darkOverrides[key]=saved->second;else darkOverrides.erase(key);}
        else sharedOverrides.erase({lexerCatalog[c->scheme]->rid,c->style});
        ShowStyleField(window,*c);return TRUE;
    }
    if(id==IDC_RESETALL){if(c->document.style.dark)darkOverrides=c->savedDark;else sharedOverrides.clear();sharedExtensions.clear();ShowStyleField(window,*c);return TRUE;}
    if(id==IDC_PREVSTYLE || id==IDC_NEXTSTYLE){SaveStyleField(window,*c);c->extension=false;auto count=lexerCatalog[c->scheme]->iStyleCount;c->style=(c->style+count+(id==IDC_NEXTSTYLE?1:-1))%count;ShowStyleField(window,*c);return TRUE;}
    if(id==IDC_STYLEFORE || id==IDC_STYLEBACK) {
        CHOOSECOLORW choose{sizeof(choose)};choose.hwndOwner=window;choose.Flags=CC_FULLOPEN|CC_RGBINIT;choose.lpCustColors=customColors;
        if(ChooseColorW(&choose)){
            wchar_t color[48]{};swprintf_s(color,L"; %s:#%02X%02X%02X",id==IDC_STYLEFORE?L"fore":L"back",GetRValue(choose.rgbResult),GetGValue(choose.rgbResult),GetBValue(choose.rgbResult));
            auto& value=(c->document.style.dark?darkOverrides:sharedOverrides)[{lexerCatalog[c->scheme]->rid,c->style}];value+=color;ShowStyleField(window,*c);
        }return TRUE;
    }
    if(id==IDC_STYLEFONT) {
        LOGFONTW font{};wcsncpy_s(font.lfFaceName,c->document.style.font.c_str(),_TRUNCATE);font.lfHeight=-MulDiv(c->document.style.fontSize,GetDpiForWindow(window),72);
        CHOOSEFONTW choose{sizeof(choose)};choose.hwndOwner=window;choose.lpLogFont=&font;choose.Flags=CF_SCREENFONTS|CF_INITTOLOGFONTSTRUCT;
        if(ChooseFontW(&choose)){auto& value=(c->document.style.dark?darkOverrides:sharedOverrides)[{lexerCatalog[c->scheme]->rid,c->style}];value+=L"; font:"+std::wstring(font.lfFaceName)+L"; size:"+std::to_wstring(choose.iPointSize/10);ShowStyleField(window,*c);}return TRUE;
    }
    if(id==IDC_IMPORT || id==IDC_EXPORT) {
        CFileDialog file(id==IDC_IMPORT,L"ini",nullptr,OFN_NOCHANGEDIR|(id==IDC_IMPORT?OFN_FILEMUSTEXIST:OFN_OVERWRITEPROMPT),L"Scheme settings (*.ini)\0*.ini\0\0",window);
        if(file.DoModal()!=IDOK)return TRUE;
        if(id==IDC_EXPORT)EnsureUnicodeProfile(file.m_szFileName);
        for(size_t i=0;i<Count;++i)for(unsigned j=0;j<lexerCatalog[i]->iStyleCount;++j){
            const auto& lexer=*lexerCatalog[i];std::wstring key=lexer.Styles[j].pszName;
            if(id==IDC_EXPORT){auto& definitions=c->document.style.dark?darkOverrides:sharedOverrides;auto found=definitions.find({lexer.rid,j});WriteStyleProfile(lexer.pszName,key.c_str(),found==definitions.end()?lexer.Styles[j].pszDefault:found->second.c_str(),file.m_szFileName);}
            else{wchar_t value[2048]{};if(GetPrivateProfileStringW(lexer.pszName,key.c_str(),nullptr,value,2048,file.m_szFileName))(c->document.style.dark?darkOverrides:sharedOverrides)[{lexer.rid,j}]=value;}
        }
        for(auto lexer:lexerCatalog)if(lexer->pszDefExt){
            if(id==IDC_EXPORT)WriteStyleProfile(L"File Extensions",lexer->pszName,sharedExtensions.contains(lexer->rid)?sharedExtensions[lexer->rid].c_str():lexer->pszDefExt,file.m_szFileName);
            else {wchar_t extension[2048]{};if(GetPrivateProfileStringW(L"File Extensions",lexer->pszName,nullptr,extension,2048,file.m_szFileName))sharedExtensions[lexer->rid]=extension;}
        }
        ShowStyleField(window,*c);return TRUE;
    }
    return FALSE;
    }catch(const std::exception& error){OutputDebugStringA(error.what());MessageBeep(MB_ICONERROR);return TRUE;}
}
void SelectScheme(HWND owner,EditorDocument& document,bool customize,bool favorites=false) {
    SchemeDialogContext c{document,customize,ActualScheme(document),0,sharedOverrides};c.favorites=favorites;c.savedExtensions=sharedExtensions;c.savedDark=darkOverrides;
    DialogBoxParamW(_Module.GetModuleInstance(),MAKEINTRESOURCEW(customize?IDD_STYLECONFIG:IDD_STYLESELECT),owner,SchemeProcedure,reinterpret_cast<LPARAM>(&c));
}
}
namespace {
UINT DefaultSchemeCommand(const EDITLEXER& lexer){
    switch(lexer.rid){case NP2LEX_TEXTFILE:return IDM_LEXER_TEXTFILE;case NP2LEX_2NDTEXTFILE:return IDM_LEXER_2NDTEXTFILE;case NP2LEX_CSV:return IDM_LEXER_CSV;case NP2LEX_CSS:return IDM_LEXER_CSS;case NP2LEX_JAVASCRIPT:return IDM_LEXER_JAVASCRIPT;case NP2LEX_TYPESCRIPT:return IDM_LEXER_TYPESCRIPT;case NP2LEX_HTML:return IDM_LEXER_WEB;case NP2LEX_PHP:return IDM_LEXER_PHP;case NP2LEX_MARKDOWN:return IDM_LEXER_MARKDOWN_GITHUB;case NP2LEX_MATHEMATICA:return IDM_LEXER_MATHEMATICA;case NP2LEX_MATLAB:return IDM_LEXER_MATLAB;case NP2LEX_BASH:return IDM_LEXER_BASH;case NP2LEX_VISUALBASIC:return IDM_LEXER_VBNET;case NP2LEX_VBSCRIPT:return IDM_LEXER_VBS;case NP2LEX_XML:return IDM_LEXER_XML;default:return 0;}
}
}
CommandState StyleCommandState(const EditorDocument* d,UINT id) {
    int scheme=SchemeForCommand(id);
    if(scheme){UINT selected=d?static_cast<UINT>(d->style.dialect?d->style.dialect:d->style.resolvedDialect):0;if(d && !selected)selected=DefaultSchemeCommand(*GetDocumentLexer(*d));return {true,d!=nullptr,d && ActualScheme(*d)==scheme && selected==id};}
    if(id>=IDM_EDIT_GOTO_BLOCK_START && id<=IDM_EDIT_GOTO_NEXT_SIBLING_BLOCK)return {true,d!=nullptr,false};
    if(id>=IDM_VIEW_FOLD_LEVEL1 && id<=IDM_VIEW_FOLD_LEVEL10)return {true,d!=nullptr,false};
    switch(id){
    case IDM_VIEW_SCHEME:case IDM_VIEW_SCHEME_CONFIG:case IDM_VIEW_SCHEME_FAVORITE:case IDM_VIEW_DEFAULT_CODE_FONT:case IDM_VIEW_DEFAULT_TEXT_FONT:
    case IDM_VIEW_FOLD_CURRENT_LEVEL:case IDM_VIEW_FOLD_CURRENT_BLOCK:case IDM_VIEW_FOLD_DEFAULT:case IDM_VIEW_FOLD_ALL:
    case IDM_VIEW_TABSETTINGS:case IDM_VIEW_WORDWRAPSETTINGS:case IDM_VIEW_AUTOCOMPLETION_SETTINGS:
    case IDM_EDIT_COMPLETEWORD:return {true,d!=nullptr,false};
    case CMD_TAB_COMPLETION:return {true,d && !d->Send(SCI_GETREADONLY),false};
    case IDM_SET_LATEX_INPUT_METHOD:return {true,d!=nullptr,d&&d->style.latex};
    case IDM_VIEW_SHOWCALLTIP_OFF:case IDM_VIEW_SHOWCALLTIP_RGBA:case IDM_VIEW_SHOWCALLTIP_ARGB:case IDM_VIEW_SHOWCALLTIP_BGRA:case IDM_VIEW_SHOWCALLTIP_ABGR:return {true,d!=nullptr,d&&d->style.colorCalltip==static_cast<int>(id-IDM_VIEW_SHOWCALLTIP_OFF)};
    case IDM_VIEW_USEDEFAULT_CODESTYLE:return {true,d!=nullptr,d&&(d->style.fontKind<0?lexerCatalog[ActualScheme(*d)]->bUseDefaultCodeStyle:d->style.fontKind==0)};
    case IDM_VIEW_HIGHLIGHTCURRENTLINE_NONE:case IDM_VIEW_HIGHLIGHTCURRENTLINE_BACK:case IDM_VIEW_HIGHLIGHTCURRENTLINE_FRAME:return {true,d!=nullptr,d&&d->style.lineHighlight==static_cast<int>(id-IDM_VIEW_HIGHLIGHTCURRENTLINE_NONE)};
    case IDM_VIEW_HIGHLIGHTCURRENTLINE_SUBLINE:return {true,d!=nullptr,d&&d->style.highlightSubline};
    case IDM_VIEW_CHANGE_HISTORY_MARKER:return {true,d!=nullptr,d&&(d->Send(SCI_GETCHANGEHISTORY)&SC_CHANGE_HISTORY_MARKERS)!=0};
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
    case IDM_VIEW_SCHEME:SelectScheme(owner,d,false);break;
    case IDM_VIEW_SCHEME_CONFIG:SelectScheme(owner,d,true);break;
    case IDM_VIEW_SCHEME_FAVORITE:SelectScheme(owner,d,false,true);break;
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
    case IDM_VIEW_CHANGE_HISTORY_MARKER:d.Send(SCI_SETCHANGEHISTORY,d.Send(SCI_GETCHANGEHISTORY)&SC_CHANGE_HISTORY_MARKERS?SC_CHANGE_HISTORY_ENABLED:SC_CHANGE_HISTORY_ENABLED|SC_CHANGE_HISTORY_MARKERS);break;
    case IDM_VIEW_HIGHLIGHTCURRENT_BLOCK:d.style.highlightBlock=!d.style.highlightBlock;if(!d.style.highlightBlock){d.Send(SCI_SETINDICATORCURRENT,11);d.Send(SCI_INDICATORCLEARRANGE,0,d.Send(SCI_GETLENGTH));}break;
    case IDM_VIEW_LONGLINEMARKER:d.style.longLineEnabled=!d.Send(SCI_GETEDGEMODE);d.Send(SCI_SETEDGEMODE,d.style.longLineEnabled?d.edgeMode:EDGE_NONE);break;
    case IDM_VIEW_WORDWRAP:d.Send(SCI_SETWRAPMODE,d.Send(SCI_GETWRAPMODE)?SC_WRAP_NONE:d.style.wrapMode);break;
    case IDM_VIEW_WORDWRAPSYMBOLS:d.style.wrapSymbolsShown=!d.Send(SCI_GETWRAPVISUALFLAGS);ApplyDocumentWrapSettings(d);break;
    case IDM_VIEW_SHOW_FOLDING:d.style.folding=!d.style.folding;d.Send(SCI_SETMARGINWIDTHN,2,d.style.folding?MulDiv(16,GetDpiForWindow(d.hwnd),96):0);break;
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
    if(n.nmhdr.code==SCN_ZOOM){HighlightCurrentLine(d);return;}
    if(n.nmhdr.code==SCN_MODIFIED && (n.modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))){
        ++d.style.revision;
        return;
    }
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
        if(d.style.matchBraces){auto brace=pos,match=d.Send(SCI_BRACEMATCH,brace,0);if(match<0&&pos){brace=pos-1;match=d.Send(SCI_BRACEMATCH,brace,0);}d.Send(SCI_BRACEHIGHLIGHT,static_cast<WPARAM>(match<0?-1:brace),match);}
        if(d.style.highlightBlock){
            d.Send(SCI_SETINDICATORCURRENT,11);d.Send(SCI_INDICATORCLEARRANGE,0,d.Send(SCI_GETLENGTH));d.Send(SCI_INDICSETSTYLE,11,INDIC_STRAIGHTBOX);d.Send(SCI_INDICSETFORE,11,RGB(80,120,200));d.Send(SCI_INDICSETALPHA,11,12);
            auto line=d.Send(SCI_LINEFROMPOSITION,pos);if(!(d.Send(SCI_GETFOLDLEVEL,line)&SC_FOLDLEVELHEADERFLAG))line=d.Send(SCI_GETFOLDPARENT,line);
            if(line>=0){auto endLine=d.Send(SCI_GETLASTCHILD,line,-1),start=d.Send(SCI_POSITIONFROMLINE,line),end=d.Send(SCI_GETLINEENDPOSITION,endLine);d.Send(SCI_INDICATORFILLRANGE,start,end-start);}
        }
        auto start=d.Send(SCI_GETSELECTIONSTART),end=d.Send(SCI_GETSELECTIONEND);
        if(start==end){start=d.Send(SCI_WORDSTARTPOSITION,pos,TRUE);end=d.Send(SCI_WORDENDPOSITION,pos,TRUE);}
        const auto needle=d.style.markOccurrences && end>start?Range(d,start,end):std::string{};
        const int flags=(CompletionOption(d,IDM_VIEW_MARKOCCURRENCES_CASE,0)?SCFIND_MATCHCASE:0)|(CompletionOption(d,IDM_VIEW_MARKOCCURRENCES_WORD,0)?SCFIND_WHOLEWORD:0);
        if(d.style.occurrenceNeedle!=needle || d.style.occurrenceRevision!=d.style.revision || d.style.occurrenceFlags!=flags){
            d.style.occurrenceNeedle=needle;d.style.occurrenceCursor=0;d.style.occurrenceRevision=d.style.revision;d.style.occurrenceFlags=flags;d.style.occurrences=0;d.style.occurrencePending=!needle.empty();
            d.Send(SCI_SETINDICATORCURRENT,8);d.Send(SCI_INDICATORCLEARRANGE,0,d.Send(SCI_GETLENGTH));
        }
        if(d.style.occurrencePending)ProcessStyleBackground(d);

    }
    } catch(const std::exception& error) {
        d.Send(SCI_AUTOCCANCEL);d.Send(SCI_CALLTIPCANCEL);
        OutputDebugStringA("MultiPad4 style notification failed: ");OutputDebugStringA(error.what());OutputDebugStringA("\n");
        MessageBeep(MB_ICONERROR);
    }
}
void LoadStyleDialogPreferences(const std::filesystem::path&);
void LoadStylePreferences(const std::filesystem::path& path){
    darkOverrides.clear();
    for(unsigned i=0;i<16;++i){wchar_t key[4]{};swprintf_s(key,L"%02u",i+1);wchar_t color[32]{};GetPrivateProfileStringW(L"Custom Colors",key,L"#FFFFFF",color,32,path.c_str());unsigned value=0xFFFFFF;swscanf_s(color,L"#%x",&value);customColors[i]=RGB((value>>16)&255,(value>>8)&255,value&255);}
    auto theme=path.parent_path()/L"Notepad4 DarkTheme.ini";
    if(!std::filesystem::exists(theme)){wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);theme=std::filesystem::path(executable).parent_path()/L"Notepad4 DarkTheme.ini";}
    if(std::filesystem::exists(theme))for(auto lexer:lexerCatalog)for(unsigned index=0;index<lexer->iStyleCount;++index){wchar_t value[2048]{};if(GetPrivateProfileStringW(lexer->pszName,lexer->Styles[index].pszName,nullptr,value,2048,theme.c_str()))darkOverrides[{lexer->rid,index}]=value;}
    defaultScheme=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Style Preferences",L"DefaultScheme",1,path.c_str())),1,static_cast<int>(Count)-1);
    autoSelectScheme=GetPrivateProfileIntW(L"Style Preferences",L"AutoSelect",1,path.c_str())!=0;
    favoriteSchemes.clear();wchar_t favorites[1024]{};GetPrivateProfileStringW(L"Style Preferences",L"Favorites",L"",favorites,1024,path.c_str());std::wistringstream list(favorites);int favorite=0;while(list>>favorite)if(favorite>0&&favorite<static_cast<int>(Count)&&!FavoriteContains(favorite)&&favoriteSchemes.size()<31)favoriteSchemes.push_back(favorite);
    sharedExtensions.clear();for(auto lexer:lexerCatalog){auto key=std::to_wstring(lexer->rid);wchar_t extensions[2048]{};if(GetPrivateProfileStringW(L"File Extensions",key.c_str(),nullptr,extensions,2048,path.c_str()))sharedExtensions[lexer->rid]=extensions;}
    sharedOverrides.clear();
    for(auto lexer:lexerCatalog)for(unsigned index=0;index<lexer->iStyleCount;++index){
        auto section=L"Styles."+std::to_wstring(lexer->rid),key=std::to_wstring(index);wchar_t value[2048]{};
        if(GetPrivateProfileStringW(section.c_str(),key.c_str(),nullptr,value,2048,path.c_str()))sharedOverrides[{lexer->rid,index}]=value;
    }
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
    EnsureUnicodeProfile(path);
    for(unsigned i=0;i<16;++i){wchar_t key[4]{};swprintf_s(key,L"%02u",i+1);wchar_t color[16]{};swprintf_s(color,L"#%02X%02X%02X",GetRValue(customColors[i]),GetGValue(customColors[i]),GetBValue(customColors[i]));WriteStyleProfile(L"Custom Colors",key,color,path.c_str());}
    auto scheme=std::to_wstring(defaultScheme);std::wstring favorites;for(int favorite:favoriteSchemes){favorites+=std::to_wstring(favorite)+L" ";}WriteStyleProfile(L"Style Preferences",L"Favorites",favorites.c_str(),path.c_str());
    WriteStyleProfile(L"Style Preferences",L"DefaultScheme",scheme.c_str(),path.c_str());
    WriteStyleProfile(L"Style Preferences",L"AutoSelect",autoSelectScheme?L"1":L"0",path.c_str());
    WriteStyleProfile(L"File Extensions",nullptr,nullptr,path.c_str());for(auto& [rid,extensions]:sharedExtensions){auto key=std::to_wstring(rid);WriteStyleProfile(L"File Extensions",key.c_str(),extensions.c_str(),path.c_str());}
    for(auto lexer:lexerCatalog){auto section=L"Styles."+std::to_wstring(lexer->rid);WriteStyleProfile(section.c_str(),nullptr,nullptr,path.c_str());}
    for(auto& [entry,value]:sharedOverrides){auto section=L"Styles."+std::to_wstring(entry.first),key=std::to_wstring(entry.second);WriteStyleProfile(section.c_str(),key.c_str(),value.c_str(),path.c_str());}
    auto theme=path.parent_path()/L"Notepad4 DarkTheme.ini";
    if(!darkOverrides.empty() || std::filesystem::exists(theme)){
        EnsureUnicodeProfile(theme);
        for(auto lexer:lexerCatalog){
            std::wstring section;
            for(unsigned index=0;index<lexer->iStyleCount;++index){auto found=darkOverrides.find({lexer->rid,index});if(found!=darkOverrides.end()){section+=lexer->Styles[index].pszName;section+=L'=';section+=found->second;section+=L'\0';}}
            section+=L'\0';if(section.size()==1)section+=L'\0';
            if(!WritePrivateProfileSectionW(lexer->pszName,section.c_str(),theme.c_str()))throw std::runtime_error("Cannot write dark style theme.");
        }
    }
    WriteStyleDialogPreferencesChecked(path);
}

void DetectDocumentIndentation(EditorDocument& d){
    if(!d.style.detectIndentation)return;
    auto length = (std::min)(static_cast<size_t>(d.Send(SCI_GETLENGTH)), size_t(1024 * 1024));
    auto data = reinterpret_cast<const char*>(d.Send(SCI_GETCHARACTERPOINTER));
    const int width = simd::DetectIndentation(std::string_view(data, length));
    if (!width) return;
    const bool spaces = width <= 8;
    d.Send(SCI_SETUSETABS, !spaces); d.style.tabsAsSpaces = spaces;
    if (spaces) {
        d.Send(SCI_SETTABWIDTH, width); d.Send(SCI_SETINDENT, width);
        d.style.tabWidth = width; d.style.indentWidth = width;
    }
}

void ProcessStyleBackground(EditorDocument& d){
    static thread_local bool processing=false;if(processing || !d.style.occurrencePending)return;processing=true;struct ResetProcessing{bool& value;~ResetProcessing(){value=false;}}resetProcessing{processing};
    if(d.style.revision!=d.style.occurrenceRevision){d.style.occurrencePending=false;return;}
    const auto originalIndicator=d.Send(SCI_GETINDICATORCURRENT),originalStart=d.Send(SCI_GETTARGETSTART),originalEnd=d.Send(SCI_GETTARGETEND),originalFlags=d.Send(SCI_GETSEARCHFLAGS);
    struct Restore{EditorDocument& d;LRESULT indicator,start,end,flags;~Restore(){d.Send(SCI_SETINDICATORCURRENT,indicator);d.Send(SCI_SETTARGETRANGE,start,end);d.Send(SCI_SETSEARCHFLAGS,flags);}}restore{d,originalIndicator,originalStart,originalEnd,originalFlags};
    auto& state=d.style;const auto length=d.Send(SCI_GETLENGTH);const auto tick=GetTickCount64();
    d.Send(SCI_SETINDICATORCURRENT,8);d.Send(SCI_SETSEARCHFLAGS,state.occurrenceFlags);
    for(unsigned count=0;count<2048 && GetTickCount64()-tick<8;++count){
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
