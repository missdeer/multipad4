#include "LanguageConfiguration.h"
// Content detection algorithms from the pinned notepad4 Styles.cpp.
// Original copyright/license: 3rdparty/notepad4/License.txt.
#include "LexerDetection.h"
#include "StyleManager.h"
#include "../3rdparty/notepad4/src/EditLexer.h"
#include "LexerCatalog.h"
#include "resource.h"
#include <cstring>
#include <cctype>
#include <shlwapi.h>
namespace {
bool Space(char ch){return isspace(static_cast<unsigned char>(ch))!=0;}
bool Alnum(char ch){return isalnum(static_cast<unsigned char>(ch))!=0;}
bool Starts(const char* text,const char* prefix){return strncmp(text,prefix,strlen(prefix))==0;}
bool StartsCase(const char* text,const char* prefix){return _strnicmp(text,prefix,strlen(prefix))==0;}
}
bool MatchCPPKeyword(const char *p, int index) noexcept {
	if (*p < 'a' || *p > 'z') {
		return false;
	}

	char word[32];
	word[0] = ' ';
	word[1] = *p++;
	int len = 2;
	while (len < 30 && (*p == '_' || (*p >= 'a' && *p <= 'z'))) {
		word[len++] = *p++;
	}
	if (len == 30 || Alnum(*p)) {
		return false;
	}
	word[len++] = ' ';
	word[len++] = 0;
	const auto& cpp=Language("cpp");
	if(index<0||index>=static_cast<int>(cpp.keywordCount)||!cpp.pszKeyWords[index])return false;
	p = strstr(cpp.pszKeyWords[index], word);
	return p != nullptr;
}
const LanguageDefinition* DetectObjCAndMatlab(const EditorDocument& d,int& dialect) {
	char tchText[4000] = ""; // maybe contains header comments
	d.Send(SCI_GETTEXT,sizeof(tchText)-1,reinterpret_cast<LPARAM>(tchText));

	const char *p = tchText;
	dialect = 0;
	while (*p) {
		while (Space(*p)) {
			++p;
		}
		switch (*p) {
		case '#':	// ObjC preprocessor or octave comment
			if (!(p == tchText && p[1] == '!')) {
				++p;
				while (*p == ' ' || *p == '\t') {
					++p;
				}
				if (MatchCPPKeyword(p, 2)) {
					return &Language("cpp");
				}
				dialect = IDM_LEXER_OCTAVE;
				return &Language("matlab");
			}
			break;
		case '(':
			++p;
			if (*p == '*') { // Mathematica comment
				return &Language("mathematica");
			}
			break;
		case '@':	// ObjC keyword or Matlab command
			++p;
			if (MatchCPPKeyword(p, 3)) {
				return &Language("cpp");
			}
			return &Language("matlab");
		case '/':	// C/C++ style comment
			++p;
			if (*p == '/' || *p == '*') {
				return &Language("cpp");
			}
			break;
		case 'f':	// Matlab function
			if (Starts(p, "function") && (Space(p[CSTRLEN("function")]) || p[CSTRLEN("function")] == '[')) {
				return &Language("matlab");
			}
			break;
		case 'c':	// Matlab classdef
			if (Starts(p, "classdef") && Space(p[CSTRLEN("classdef")])) {
				return &Language("matlab");
			}
			break;
		case '%':	// ObjC modular or Matlab comment
			return &Language("matlab");
		}
		// skip to next line
		while (*p && !(*p == '\r' || *p == '\n')) {
			++p;
		}
	}

	return nullptr;
}
const LanguageDefinition* AutoDetectContent(const EditorDocument& d,bool bDotFile) {
	char tchText[4000] = ""; // maybe contains header comments
	d.Send(SCI_GETTEXT,sizeof(tchText)-1,reinterpret_cast<LPARAM>(tchText));

	const char *p = tchText;
	const bool shebang = *p == '#' && p[1] == '!';
	int cppCount = 0;
	int sharpCount = 0;
	bool maybeIni = false;
	bool maybeJson = false;
	uint8_t notJson = 0;
	uint8_t shellWord = 0;

	while (*p) {
		if (*p == '[') {
			// bracket at line beginning
			maybeIni = true;
		} else if (*p == '-' && p[1] == '-' && p[2] == '-' && Space(p[CSTRLEN("---")])) {
			// `---` at line beginning
			p += CSTRLEN("---");
			while (*p == ' ' || *p == '\t') {
				++p;
			}
			if (Space(*p) || *p == '#' || *p == '%') {
				return &Language("yaml");
			}
		} else {
			// skip leading space
			while (*p == ' ' || *p == '\t') {
				++p;
			}
			if (*p == '#') { // C/C++ preprocessor, comment
				if (!(p == tchText && shebang)) {
					++p;
					while (*p == ' ' || *p == '\t') {
						++p;
					}
					if (MatchCPPKeyword(p, 2)) {
						++cppCount;
					} else {
						++sharpCount;
					}
				}
			} else if (*p == '/') { // C/C++ style comment
				++p;
				if (*p == '/' || *p == '*') {
					++cppCount;
					if (!sharpCount) {
						return &Language("cpp");
					}
				}
			} else if (*p == '{' || *p == '}' || *p == '[') {
				maybeJson = true;
			} else if (*p == '\"') {
				maybeJson |= maybeIni;
			} else {
				// not a normal JSON file: line not starts with any of `{}[]"`, possible JSON with unquoted keys, which is rare.
				notJson |= *p;
				if (shellWord != 3) {
					if (Starts(p, "if") || Starts(p, "fi") || Starts(p, "do")) {
						if (static_cast<uint8_t>(p[2]) <= ' ') {
							shellWord |= (p[0] == 'f') ? 2 : 1;
						} else if (Starts(p, "done") && static_cast<uint8_t>(p[4]) <= ' ') {
							shellWord |= 2;
						}
					}
				}
			}
		}

		// skip to next line
		while (*p && !(*p == '\r' || *p == '\n')) {
			++p;
		}
		while (*p == '\r' || *p == '\n') {
			++p;
		}
	}

	if (cppCount > sharpCount && !(shebang || bDotFile)) {
		return &Language("cpp");
	}
	if (sharpCount) {
		return (shebang || shellWord == 3) ? &Language("bash") : &Language("config");
	}
	if (maybeJson && !notJson) {
		return &Language("json");
	}
	if (maybeIni) {
		return &Language("ini");
	}
	if (bDotFile) {
		return &Language("config");
	}
	if (maybeJson) {
		// for braces and brackets
		return &Language("cpp");
	}
	return nullptr;
}
int DetectDocumentMarkupDialect(const EditorDocument& d) {
	char tchText[4000] = ""; // maybe contains header comments
	d.Send(SCI_GETTEXT,sizeof(tchText)-1,reinterpret_cast<LPARAM>(tchText));

	// check DOCTYPE
	const char *p = StrStrIA(tchText, "<!DOCTYPE");
	if (p != nullptr) {
		p += CSTRLEN("<!DOCTYPE");
		while (Space(*p)) {
			++p;
		}
		if (StartsCase(p, "html")) {
			return IDM_LEXER_WEB;
		}
		//if (Starts(p, "plist")) {
		//	return IDM_LEXER_PROPERTY_LIST;
		//}
		if (Starts(p, "schema")) {
			return IDM_LEXER_XSD;
		}
	}

	//if (strstr(tchText, "<?php")) {
	//	return IDM_LEXER_PHP;
	//}
	// check Language
	p = strstr(tchText, "<%@");
	if (p != nullptr) {
		p = StrStrIA(p + CSTRLEN("<%@"), "Language");
		if (p != nullptr) {
			p += CSTRLEN("Language") + 1;
			while (*p == ' ' || *p == '=' || *p == '\"') {
				p++;
			}
			if (StartsCase(p, "C#")) {
				return IDM_LEXER_ASPX_CS;
			}
			if (StartsCase(p, "VBScript")) {
				return IDM_LEXER_ASP_VBS;
			}
			if (StartsCase(p, "VB")) {
				return IDM_LEXER_ASPX_VB;
			}
			if (StartsCase(p, "JScript")) {
				return IDM_LEXER_ASP_JS;
			}
			if (StartsCase(p, "Java")) {
				p += CSTRLEN("Java");
				return (static_cast<char>(tolower(static_cast<unsigned char>(*p))) == 's') ? IDM_LEXER_ASP_JS : IDM_LEXER_JSP;
			}
		}
	}

	// find root tag
	//p = tchText;
	//while (p - tchText < (ptrdiff_t)COUNTOF(tchText)) {
	//	p = strchr(p, '<');
	//	if (p == nullptr) {
	//		return 0;
	//	}
	//	if (Starts(p, "<!--")) {
	//		p += CSTRLEN("<!--");
	//		p = strstr(p, "-->");
	//		if (p != nullptr) {
	//			p += CSTRLEN("-->");
	//		} else {
	//			return 0;
	//		}
	//	} else if (Starts(p, "<?") || Starts(p, "<!")) {
	//		p += CSTRLEN("<?");
	//		p = strchr(p, '>');
	//		if (p != nullptr) {
	//			p++;
	//		} else {
	//			return 0;
	//		}
	//	} else {
	//		break;
	//	}
	//}
	//if (*p == '<') {
	//	p++;
	//	if (!IsAlpha(*p)) {
	//		return 0;
	//	}
	//} else {
	//	return 0;
	//}

	//if (StartsCase(p, "html"))
	//	return IDM_LEXER_WEB;
	//if (Starts(p, "schema")) {
	//	return IDM_LEXER_XSD;
	//}
	//if (Starts(p, "schema") || Starts(p, "xsd:schema") || Starts(p, "xs:schema"))
	//	return IDM_LEXER_XSD;
	//if (Starts(p, "xsl:stylesheet"))
	//	return IDM_LEXER_XSLT;

	//if (Starts(p, "plist"))
	//	return IDM_LEXER_PROPERTY_LIST;
	//if (Starts(p, "svg"))
	//	return IDM_LEXER_SVG;
	return 0;
}

int DetectDocumentCSVDelimiter(const EditorDocument& d){
    auto endPos=d.Send(SCI_GETLINECOUNT)>2?d.Send(SCI_POSITIONFROMLINE,2):d.Send(SCI_GETLENGTH);
    if(endPos<=0)return 0;
    int table[64]{};
    int offset=0;
    for(LRESULT start=0;start<endPos;){
        const auto end=(std::min)(endPos,start+64*1024);
        const auto* ptr=reinterpret_cast<const char*>(d.Send(SCI_GETRANGEPOINTER,start,end-start));
        const char* limit=ptr+(end-start);
        while(ptr<limit){
            const auto ch=static_cast<uint8_t>(*ptr++);
            constexpr uint64_t interesting=(UINT64_C(1)<<'\r')|(UINT64_C(1)<<'\n')|(UINT64_C(1)<<'\t')|(UINT64_C(1)<<',')|(UINT64_C(1)<<';');
            if(ch=='|' || (ch<=';' && (interesting&(UINT64_C(1)<<ch)))){
                // Both halves of CRLF select row two, including a split at the chunk boundary.
                if(ch=='\r'||ch=='\n')offset=32;
                else ++table[offset+(ch&31)];
            }
        }
        start=end;
    }
    uint32_t mask=0;
    for(unsigned ch:{'\t',',','|',';'})if(table[ch&31] && table[32+(ch&31)])mask|=1u<<(ch&31);

	constexpr uint32_t preferred = ';' | ('|' << 8) | ('\t' << 16) | (',' << 24);
	uint8_t delimiter = '\0';
	if (mask != 0) { // delimiter occurs in both row
		uint32_t value = preferred;
		int maxColumn = 0;
		while (value != 0) {
			const int index = value & 31;
			if (mask & (1U << index)) {
				const int first = table[index];
				const int second = table[32 + index];
				const int column = std::min(first, second);
				if (column > maxColumn && column >= 4*abs(first - second)) { // 75%
					maxColumn = column;
					delimiter = static_cast<uint8_t>(value & 0xff);
				}
			}
			value >>= 8;
		}
	}
	if (delimiter == 0) {
		uint32_t value = preferred;
		int maxColumn = 0;
		while (value != 0) {
			const int index = value & 31;
			const int column = table[index] + table[32 + index];
			if (column > maxColumn) {
				maxColumn = column;
				delimiter = static_cast<uint8_t>(value & 0xff);
			}
			value >>= 8;
		}
	}
#if 0
	watch.Stop();
	watch.ShowLog(__func__);
#endif
	if (delimiter != 0) {
		return delimiter;
	}

    return 0;
}
