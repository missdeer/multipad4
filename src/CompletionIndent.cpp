#include "LanguageConfiguration.h"
// Adapted from pinned notepad4 EditAutoC.cpp, with explicit document state.
// See 3rdparty/notepad4/License.txt for original copyright and license.
#include "StyleManager.h"
#include "../3rdparty/notepad4/src/EditLexer.h"
#include "resource.h"
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <shlwapi.h>
namespace {
bool Same(const char* a,const char* b){return strcmp(a,b)==0;}
bool IsASpaceOrTab(char ch){return ch==' '||ch=='\t';}
bool IsAlpha(char ch){return (ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z');}
void* Allocate(size_t size){return calloc(1,size);}
}
enum AutoIndentType {
	AutoIndentType_None,
	AutoIndentType_IndentOnly,
	AutoIndentType_IndentAndClose,
};

static const char* EditKeywordIndent(const LanguageDefinition* pLex,int dialect,const char* head,AutoIndentType* indent) {
	char word[16] = "";
	char word_low[16] = "";
	int length = 0;
	const char *endPart = nullptr;
	*indent = AutoIndentType_None;

	while (*head && length < 15) {
		const char lower = static_cast<char>(tolower(static_cast<unsigned char>(*head)));
		if (lower < 'a' || lower > 'z') {
			break;
		}
		word[length] = *head;
		word_low[length] = lower;
		++length;
		++head;
	}

	switch (pLex->rid) {
	//case NP2LEX_AUTOIT3:
	case NP2LEX_BASH:
		if (dialect == IDM_LEXER_CSHELL) {
			if (Same(word, "if")) {
				*indent = AutoIndentType_IndentAndClose;
				endPart = "endif";
			} else if (Same(word, "switch")) {
				*indent = AutoIndentType_IndentAndClose;
				endPart = "endsw";
			} else if (Same(word, "foreach") || Same(word, "while")) {
				*indent = AutoIndentType_IndentAndClose;
				endPart = "end";
			}
		} else {
			if (Same(word, "if")) {
				*indent = AutoIndentType_IndentAndClose;
				endPart = "fi";
			} else if (Same(word, "case")) {
				*indent = AutoIndentType_IndentAndClose;
				endPart = "esac";
			} else if (Same(word, "do")) {
				*indent = AutoIndentType_IndentAndClose;
				endPart = "done";
			}
		}
		break;

	case NP2LEX_CMAKE:
		if (Same(word, "function")) {
			*indent = AutoIndentType_IndentAndClose;
			endPart = "endfunction()";
		} else if (Same(word, "macro")) {
			*indent = AutoIndentType_IndentAndClose;
			endPart = "endmacro()";
		} else if (Same(word, "if")) {
			*indent = AutoIndentType_IndentAndClose;
			endPart = "endif()";
		} else if (Same(word, "foreach")) {
			*indent = AutoIndentType_IndentAndClose;
			endPart = "endforeach()";
		} else if (Same(word, "while")) {
			*indent = AutoIndentType_IndentAndClose;
			endPart = "endwhile()";
		}
		break;
	//case NP2LEX_CPP:
	//case NP2LEX_RESOURCESCRIPT:

	//case NP2LEX_INNOSETUP:

	case NP2LEX_JULIA: {
		LPCSTR pKeywords = pLex->pszKeyWords[1];
		LPCSTR p = strstr(pKeywords, word);
		if (p == pKeywords || (p != nullptr && p[-1] == ' ')) {
			*indent = AutoIndentType_IndentAndClose;
			endPart = "end";
		}
	} break;

	case NP2LEX_LUA:
		if (Same(word, "function") || Same(word, "if") || Same(word, "do")) {
			*indent = AutoIndentType_IndentAndClose;
			endPart = "end";
		}
		break;

	case NP2LEX_MAKEFILE:
		if (Same(word, "if")) {
			*indent = AutoIndentType_IndentAndClose;
			endPart = "endif";
		} else if (Same(word, "define")) {
			*indent = AutoIndentType_IndentAndClose;
			endPart = "endef";
		} else if (Same(word, "for")) {
			*indent = AutoIndentType_IndentAndClose;
			endPart = "endfor";
		}
		break;
	case NP2LEX_MATLAB:
		if (Same(word, "function")) {
			*indent = AutoIndentType_IndentOnly;
			// 'end' is optional
		} else if (Same(word, "if") || Same(word, "for") || Same(word, "while") || Same(word, "switch") || Same(word, "try")) {
			*indent = AutoIndentType_IndentAndClose;
			if (dialect == IDM_LEXER_OCTAVE) {
				if (Same(word, "if")) {
					endPart = "endif";
				} else if (Same(word, "for")) {
					endPart = "endfor";
				} else if (Same(word, "while")) {
					endPart = "endwhile";
				} else if (Same(word, "switch")) {
					endPart = "endswitch";
				} else if (Same(word, "try")) {
					endPart = "end_try_catch";
				}
			}
			if (endPart == nullptr) {
				endPart = "end";
			}
		}
		break;

	//case NP2LEX_NSIS:
	//case NP2LEX_PASCAL:
	case NP2LEX_RUBY:
		if (Same(word, "if") || Same(word, "do") || Same(word, "while") || Same(word, "for")) {
			*indent = AutoIndentType_IndentAndClose;
			endPart = "end";
		}
		break;

	case NP2LEX_SQL:
		if (Same(word_low, "if")) {
			*indent = AutoIndentType_IndentAndClose;
			endPart = "END IF;";
		} else if (Same(word_low, "while")) {
			*indent = AutoIndentType_IndentAndClose;
			endPart = "END WHILE;";
		} else if (Same(word_low, "repeat")) {
			*indent = AutoIndentType_IndentAndClose;
			endPart = "END REPEAT;";
		} else if (Same(word_low, "loop") || Same(word_low, "for")) {
			*indent = AutoIndentType_IndentAndClose;
			endPart = "END LOOP;";
		} else if (Same(word_low, "case")) {
			*indent = AutoIndentType_IndentAndClose;
			endPart = "END CASE;";
		} else if (Same(word_low, "begin")) {
			*indent = AutoIndentType_IndentAndClose;
			if (StrStrIA(head, "transaction") != nullptr) {
				endPart = "COMMIT;";
			} else {
				endPart = "END";
			}
		} else if (Same(word_low, "start")) {
			if (StrStrIA(head, "transaction") != nullptr) {
				*indent = AutoIndentType_IndentAndClose;
				endPart = "COMMIT;";
			}
		}
		break;

	//case NP2LEX_VISUALBASIC:
	//case NP2LEX_VBSCRIPT:
	//case NP2LEX_VERILOG:
	//case NP2LEX_VHDL:
	}
	return endPart;
}

void ApplyDocumentAutoIndent(EditorDocument& d) {
    const auto* pLexCurrent=GetDocumentLexer(d);
	const LRESULT iCurPos = d.Send(SCI_GETCURRENTPOS);
	//const LRESULT iAnchorPos = SciCall_GetAnchor();
	const LRESULT iCurLine = d.Send(SCI_LINEFROMPOSITION,iCurPos);
	//const LRESULT iLineLength = SciCall_GetLineLength(iCurLine);
	//const LRESULT iIndentBefore = SciCall_GetLineIndentation(iCurLine - 1);

	// Move bookmark along with line if inserting lines (pressing return at beginning of line) because Scintilla does not do this for us
	if (iCurLine > 0) {
		const LRESULT iPrevLineLength = d.Send(SCI_GETLINEENDPOSITION,iCurLine-1) - d.Send(SCI_POSITIONFROMLINE,iCurLine-1);
		if (iPrevLineLength == 0) {
			const unsigned bitmask = static_cast<unsigned>(d.Send(SCI_MARKERGET,iCurLine-1));
			if (bitmask & 1) {
				d.Send(SCI_MARKERDELETE,iCurLine-1,0);
				d.Send(SCI_MARKERADD,iCurLine,0);
			}
		}
	}

	if (iCurLine > 0/* && iLineLength <= 2*/) {
		const LRESULT iPrevLineLength = d.Send(SCI_LINELENGTH,iCurLine-1);
		if (iPrevLineLength < 2) {
			return;
		}
		char *pLineBuf = static_cast<char *>(Allocate(2 * iPrevLineLength + 1 + static_cast<int>(d.Send(SCI_GETINDENT)) * 2 + 2 + 64));
		if (pLineBuf == nullptr) {
			return;
		}

		const int iEOLMode = static_cast<int>(d.Send(SCI_GETEOLMODE));
		AutoIndentType indent = AutoIndentType_None;
		LRESULT iIndentLen = 0;
		int commentStyle = 0;
		d.Send(SCI_GETLINE,iCurLine-1,reinterpret_cast<LPARAM>(pLineBuf));
		pLineBuf[iPrevLineLength] = '\0';

		int ch = static_cast<uint8_t>(pLineBuf[iPrevLineLength - 2]);
		if (ch == '\r') {
			ch = iPrevLineLength>=3 ? static_cast<uint8_t>(pLineBuf[iPrevLineLength - 3]) : 0;
			iIndentLen = 1;
		}
		if (ch == '{' || ch == '[' || ch == '(') {
			indent = AutoIndentType_IndentAndClose;
		} else if (ch == ':') { // case label/Python
			indent = AutoIndentType_IndentOnly;
		} else if (ch == '*' || ch == '!') { // indent block comment
			iIndentLen = iPrevLineLength - (2 + iIndentLen);
			if (iIndentLen >= 2 && pLineBuf[iIndentLen - 2] == '/' && pLineBuf[iIndentLen - 1] == '*') {
				indent = AutoIndentType_IndentOnly;
				commentStyle = 1;
			}
		}

		iIndentLen = 0;
		ch = static_cast<int>(d.Send(SCI_GETCHARAT,d.Send(SCI_POSITIONFROMLINE,iCurLine)));
		const bool closeBrace = (ch == '}' || ch == ']' || ch == ')');
		if (indent == AutoIndentType_IndentAndClose && !closeBrace) {
			indent = AutoIndentType_IndentOnly;
		}

		char *pPos;
		const char *endPart = nullptr;
		for (pPos = pLineBuf; *pPos; pPos++) {
			if (!IsASpaceOrTab(*pPos)) {
				if (indent == AutoIndentType_None && IsAlpha(*pPos)) { // indent on keywords
					const int style = static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT,d.Send(SCI_POSITIONFROMLINE,iCurLine-1)+iIndentLen));
					if (style != 0 && style == pLexCurrent->autoIdentWordStyle) {
						endPart = EditKeywordIndent(pLexCurrent,d.style.dialect,pPos,&indent);
					}
				}
				if (indent != AutoIndentType_None) {
					memset(pPos, 0, iPrevLineLength - iIndentLen);
				}
				*pPos = '\0';
				break;
			}
			iIndentLen += 1;
		}

		if (indent == AutoIndentType_IndentAndClose && endPart) {
			const int level = static_cast<int>(d.Send(SCI_GETFOLDLEVEL,iCurLine));
			if (!(level & SC_FOLDLEVELHEADERFLAG)) {
				const LRESULT parent = d.Send(SCI_GETFOLDPARENT,iCurLine);
				if (parent >= 0 && parent + 1 == iCurLine) {
					const LRESULT child = d.Send(SCI_GETLASTCHILD,parent,-1);
					// TODO: check endPart is on this line
					if (d.Send(SCI_LINELENGTH,child)) {
						indent = AutoIndentType_IndentOnly;
					}
				} else {
					indent = AutoIndentType_None;
				}
			}
		}

		LRESULT iIndentPos = iCurPos;
		if (indent != AutoIndentType_None) {
			int pad = static_cast<int>(d.Send(SCI_GETINDENT));
			iIndentPos += iIndentLen;
			ch = ' ';
			if (d.Send(SCI_GETTABINDENTS)) {
				if (!d.Send(SCI_GETUSETABS)) {
					pad = static_cast<int>(d.Send(SCI_GETTABWIDTH));
					ch = ' ';
				} else {
					pad = 1;
					ch = '\t';
				}
			}
			if (commentStyle) {
				iIndentPos += 2;
				*pPos++ = ' ';
				*pPos++ = '*';
			} else {
				iIndentPos += pad;
				while (pad-- > 0) {
					*pPos++ = static_cast<char>(ch);
				}
			}
			if (indent == AutoIndentType_IndentAndClose) {
				switch (iEOLMode) {
				default: // SC_EOL_CRLF
					*pPos++ = '\r';
					*pPos++ = '\n';
					break;
				case SC_EOL_LF:
					*pPos++ = '\n';
					break;
				case SC_EOL_CR:
					*pPos++ = '\r';
					break;
				}
				strncpy(pPos, pLineBuf, iIndentLen + 1);
				pPos += iIndentLen;
				if (endPart) {
					iIndentLen = strlen(endPart);
					memcpy(pPos, endPart, iIndentLen);
					pPos += iIndentLen;
				}
			}
			*pPos = '\0';
		}

		if (*pLineBuf) {
			d.Send(SCI_ADDTEXT,strlen(pLineBuf),reinterpret_cast<LPARAM>(pLineBuf));
			if (indent != AutoIndentType_None) {
				d.Send(SCI_SETSEL,iIndentPos,iIndentPos);
			}

			//const LRESULT iPrevLineStartPos = d.Send(SCI_POSITIONFROMLINE,iCurLine-1);
			//const LRESULT iPrevLineEndPos = d.Send(SCI_GETLINEENDPOSITION,iCurLine-1);
			//const LRESULT iPrevLineIndentPos = SciCall_GetLineIndentPosition(iCurLine - 1);

			//if (iPrevLineEndPos == iPrevLineIndentPos) {
			//	SciCall_DeleteRange(iPrevLineStartPos, iPrevLineEndPos - iPrevLineStartPos);
			//}
		}

		free(pLineBuf);
		//const LRESULT iIndent = SciCall_GetLineIndentation(iCurLine);
		//SciCall_SetLineIndentation(iCurLine, iIndentBefore);
		//iIndentLen = SciCall_GetLineIndentation(iCurLine);
		//if (iIndentLen > 0)
		//	SciCall_SetSel(iAnchorPos + iIndentLen, iCurPos + iIndentLen);
	}
}
