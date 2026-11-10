// Original pairing/quote context algorithms from pinned EditAutoC.cpp.
// Copyright and license: 3rdparty/notepad4/License.txt.
#include "StyleManager.h"
#include "CompletionEngine.h"
#include "../3rdparty/notepad4/src/EditLexer.h"
#include <cctype>
#include <shlwapi.h>
namespace {
enum AutoInsertCharacter {AutoInsertCharacter_Parenthesis,AutoInsertCharacter_Brace,AutoInsertCharacter_SquareBracket,AutoInsertCharacter_AngleBracket,AutoInsertCharacter_DoubleQuote,AutoInsertCharacter_SingleQuote,AutoInsertCharacter_Backtick,AutoInsertCharacter_Comma};
constexpr bool IsAlpha(int ch){return (ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z');}
bool IsDefaultWordChar(int ch){return ch>=128 || IsAlpha(ch)||(ch>='0'&&ch<='9')||ch=='_';}
bool Raw(EditorDocument& d,int style){const auto& c=GetCompletionCache(*GetDocumentLexer(d));return c.Test(c.RawStringStyleMask,style);}
bool Generic(EditorDocument& d,int style){const auto& c=GetCompletionCache(*GetDocumentLexer(d));return c.Test(c.GenericTypeStyleMask,style);}
bool Prefix(EditorDocument& d,int ch){const auto& c=GetCompletionCache(*GetDocumentLexer(d));return c.Test(c.CharacterPrefixMask,ch);}
}
static constexpr bool IsEscapeCharacter(int ch) noexcept {
	return ch == '0'	// '\0'
		|| ch == 'a'	// '\a'
		|| ch == 'b'	// '\b'
		|| ch == 'e'	// '\e', GNU extension
		|| ch == 'f'	// '\f'
		|| ch == 'n'	// '\n'
		|| ch == 'r'	// '\r'
		|| ch == 't'	// '\t'
		|| ch == 'v'	// '\v'
		// other
		|| ch == '$';	// PHP variable
	// x u U ignored as they need to be followed with multiple hex digits.
}

static constexpr bool IsPrintfFormatSpecifier(int ch) noexcept {
	return IsAlpha(ch);
}

static bool IsEscapeCharOrFormatSpecifier(EditorDocument& d,LRESULT before,int ch,int chPrev,int style,bool punctuation) {
    const auto* pLexCurrent=GetDocumentLexer(d);
	// style for chPrev, style for ch is zero on typing
	const int stylePrev = static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT,before));
	if (stylePrev == 0) {
		return false;
	}
	if (chPrev == '%') {
		if (!IsPrintfFormatSpecifier(ch)) {
			return false;
		}
		if (style != 0 && pLexCurrent->formatSpecifierStyle) {
			return stylePrev == pLexCurrent->formatSpecifierStyle;
		}
		// legacy lexer without format specifier highlighting
		if (pLexCurrent->lexerAttr & LexerAttr_PrintfFormatSpecifier) {
			return !(stylePrev == pLexCurrent->operatorStyle || stylePrev == pLexCurrent->operatorStyle2);
		}
		return false;
	}

	if (style != 0 && pLexCurrent->escapeCharacterStyle) {
		if (stylePrev != pLexCurrent->escapeCharacterStyle) {
			if (pLexCurrent->iLexer != SCLEX_PHPSCRIPT || pLexCurrent->iLexer != SCLEX_HTML
				|| !(stylePrev == js_style(SCE_JS_ESCAPECHAR) || stylePrev == css_style(SCE_CSS_ESCAPECHAR))) {
				return false;
			}
		}
	} else if (!punctuation) {
		// legacy lexer without escape character highlighting
		if (!IsEscapeCharacter(ch)) {
			return false;
		}
	}

	if (!Raw(d,stylePrev)) {
		int chPrev2 = 0;
		const LRESULT before2 = d.Send(SCI_POSITIONBEFORE,before);
		if (before2 + 1 == before) {
			chPrev2 = static_cast<int>(d.Send(SCI_GETCHARAT,before2));
		}
		// simply treat chPrev == chPrev2 as escape escapeCharacterStart self
		return chPrev != chPrev2;
	}

	return false;
}

static bool CanAutoCloseSingleQuote(EditorDocument& d,int chPrev,int iCurrentStyle) {
    const auto* pLexCurrent=GetDocumentLexer(d);
	const int iLexer = pLexCurrent->iLexer;
	if (iCurrentStyle == 0) {
		if (iLexer == SCLEX_VISUALBASIC) {
			return false; // comment
		}
	} else {
		if (iCurrentStyle == pLexCurrent->noneSingleQuotedStyle) {
			return false;
		}
		if (iLexer == SCLEX_HTML) {
			if (iCurrentStyle == SCE_HB_DEFAULT) {
				return false; // comment
			}
		}
	}

	// someone's, don't
	if (IsDefaultWordChar(chPrev)) {
		// character or string prefix
		if (pLexCurrent->lexerAttr & LexerAttr_CharacterPrefix) {
			const int chPrev2 = static_cast<int>(d.Send(SCI_GETCHARAT,d.Send(SCI_GETCURRENTPOS)-3));
			if (IsDefaultWordChar(chPrev2)) {
				switch (iLexer) {
				case SCLEX_CPP:
					return chPrev2 == 'u' && chPrev == '8';

				case SCLEX_FORTRAN:
					return chPrev == '_';

				case SCLEX_PYTHON:
					return tolower(static_cast<unsigned char>(chPrev)) != tolower(static_cast<unsigned char>(chPrev2))
						&& Prefix(d,chPrev) && Prefix(d,chPrev2);
				}
			} else {
				return Prefix(d,chPrev);
			}
		}
		return false;
	}

	if (iLexer == SCLEX_RUST || iLexer == SCLEX_REBOL || iLexer == SCLEX_VERILOG) {
		// TODO: Rust lifetime, REBOL symbol, Verilog literal
		return false;
	}

	return true;
}

static bool EditIsOpenBraceMatched(EditorDocument& d,LRESULT pos,LRESULT startPos) {
	// SciCall_GetEndStyled() is d.Send(SCI_GETCURRENTPOS) - 1
	// only find close brace with same style in next 4KiB text
	d.Send(SCI_COLOURISE,0,pos+1024*4);
	// find next close brace
	const LRESULT iPos = d.Send(SCI_BRACEMATCH,pos,startPos);
	if (iPos >= 0) {
		// style may not matched when iPos > SciCall_GetEndStyled() (e.g. iPos on next line), see Document::BraceMatch()
#if 0
		SciCall_EnsureStyledTo(iPos + 1);
#endif
		// TODO: retry when style not matched
		if (d.Send(SCI_GETSTYLEINDEXAT,pos) == d.Send(SCI_GETSTYLEINDEXAT,iPos)) {
			// check whether next close brace already matched
			return pos == 0 || d.Send(SCI_BRACEMATCH,iPos,d.Send(SCI_POSITIONBEFORE,pos)) < 0;
		}
	}
	return false;
}

void ApplyDocumentAutoPair(EditorDocument& d,int ch,int option) {
    const auto what=static_cast<AutoInsertCharacter>(option);const auto* pLexCurrent=GetDocumentLexer(d);
	const LRESULT iCurPos = d.Send(SCI_GETCURRENTPOS);
	const int chPrev = static_cast<int>(d.Send(SCI_GETCHARAT,iCurPos-2));
	const int chNext = static_cast<int>(d.Send(SCI_GETCHARAT,iCurPos));
	const int iPrevStyle = static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT,iCurPos-2));
	const int iNextStyle = static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT,iCurPos));

	if (iPrevStyle != 0) {
		const int charStyle = pLexCurrent->characterLiteralStyle;
		if (charStyle != 0) {
			// within character literal
			if (iPrevStyle == charStyle && iNextStyle == charStyle) {
				return;
			}
			if (pLexCurrent->iLexer == SCLEX_RUST && (iPrevStyle == SCE_RUST_BYTE_CHARACTER && iNextStyle == SCE_RUST_BYTE_CHARACTER)) {
				return;
			}
		}

		// escape sequence
		if (ch != ',' && (chPrev != '\0' && chPrev == pLexCurrent->escapeCharacterStart)) {
			const int style = static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT,iCurPos-1));
			if (IsEscapeCharOrFormatSpecifier(d,iCurPos-2,ch,chPrev,style,true)) {
				return;
			}
		}
	}

	// see GenerateAutoInsertMask() in tools/GenerateTable.py
	ch += (169U >> (2*what)) & 3; // 0b10101001
	switch (what) {
	case AutoInsertCharacter_SquareBracket:
		if (pLexCurrent->iLexer == SCLEX_SMALI) { // JVM array type
			ch = 0;
		}
		break;
	case AutoInsertCharacter_AngleBracket:
		if (!Generic(d,iPrevStyle)) {
			// geriatric type, template
			ch = 0;
		}
		break;
	case AutoInsertCharacter_SingleQuote:
		if (!CanAutoCloseSingleQuote(d,chPrev,iPrevStyle)) {
			ch = 0;
		}
		break;
	case AutoInsertCharacter_Backtick:
		if (pLexCurrent->iLexer == SCLEX_VERILOG || pLexCurrent->iLexer == SCLEX_VHDL) {
			ch = 0; // directive and macro
		}
		break;
	case AutoInsertCharacter_Comma:
		ch = ' ';
		if ((chNext == ' ' || chNext == '\t' || (chPrev == '\'' && chNext == '\'') || (chPrev == '\"' && chNext == '\"'))) {
			ch = 0;
		}
		break;
	default:
		break;
	}

	if (ch) {
		if (what < AutoInsertCharacter_AngleBracket && EditIsOpenBraceMatched(d,iCurPos-1,iCurPos)) {
			return;
		}
		// TODO: auto escape quotes inside string

		const char tchIns[4] = { static_cast<char>(ch) };
		d.Send(SCI_REPLACESEL,0,reinterpret_cast<LPARAM>(tchIns));
		const LRESULT iCurrentPos = (what == AutoInsertCharacter_Comma) ? iCurPos + 1 : iCurPos;
		d.Send(SCI_SETSEL,iCurrentPos,iCurrentPos);
		if (what < AutoInsertCharacter_AngleBracket) {
			// fix brace matching
			d.Send(SCI_COLOURISE,0,iCurPos+1);
		}
	}
}


namespace {
bool IsASpaceOrTab(int ch){return ch==' '||ch=='\t';}
bool IsHtmlTagChar(int ch){return IsAlpha(ch)||(ch>='0'&&ch<='9')||ch==':'||ch=='_'||ch=='-'||ch=='.';}
bool IsHtmlVoidTag(const char* tag){return StrStrIA(" area base basefont br col command embed frame hr img input isindex keygen link meta p param source track wbr ",tag)!=nullptr;}
}
bool ApplyDocumentAutoCloseTag(EditorDocument& d,bool closeTags) {
    const auto* pLexCurrent=GetDocumentLexer(d);
	char tchBuf[512];
	const LRESULT iCurPos = d.Send(SCI_GETCURRENTPOS);
	const LRESULT iStartPos = max<LRESULT>(0, iCurPos - (COUNTOF(tchBuf) - 1));
	const LRESULT iSize = iCurPos - iStartPos;
	bool shouldAutoClose = false;
	bool autoClosed = false;

	if (iSize >= 3 && closeTags) {
		shouldAutoClose = true;
		int iCurrentStyle = static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT,iCurPos));
		const int iLexer = pLexCurrent->iLexer;
		if ((pLexCurrent->lexerAttr & LexerAttr_AngleBracketGeneric)
			&& (iCurrentStyle == 0 || iCurrentStyle == pLexCurrent->operatorStyle || iCurrentStyle == pLexCurrent->operatorStyle2)) {
			shouldAutoClose = false;
		} else if (iLexer == SCLEX_CPP || iLexer == SCLEX_INNOSETUP || iLexer == SCLEX_VERILOG) {
			// C++ like #include <path>
			const int preprocessor = (iLexer == SCLEX_CPP) ? SCE_C_PREPROCESSOR : ((iLexer == SCLEX_VERILOG) ? SCE_V_DIRECTIVE : SCE_INNO_PREPROCESSOR);
			const LRESULT iLine = d.Send(SCI_LINEFROMPOSITION,iCurPos);
			LRESULT iCurrentLinePos = d.Send(SCI_POSITIONFROMLINE,iLine);
			int ch = 0;
			while (iCurrentLinePos < iCurPos) {
				ch = static_cast<int>(d.Send(SCI_GETCHARAT,iCurrentLinePos));
				if (!IsASpaceOrTab(ch)) {
					break;
				}
				iCurrentLinePos++;
			}
			if (ch == ((iLexer == SCLEX_VERILOG) ? '`' : '#')) {
				iCurrentStyle = static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT,iCurrentLinePos));
				if (iCurrentStyle == preprocessor) {
					shouldAutoClose = false;
				}
			}
		}
	}

	if (shouldAutoClose) {
		const Sci_TextRangeFull tr = { { iStartPos, iCurPos }, tchBuf };
		d.Send(SCI_GETTEXTRANGEFULL,0,reinterpret_cast<LPARAM>(&tr));

		if (tchBuf[iSize - 2] != '/') {
			char tchIns[COUNTOF(tchBuf) + 4]{};
			tchIns[0] = '<';
			tchIns[1] = ' ';
			int cchIns = 2;
			const char *pCur = tchBuf + iSize - 2;
			while (pCur > tchBuf && *pCur != '<' && *pCur != '>') {
				--pCur;
			}

			if (*pCur == '<') {
				const LRESULT iPos = iStartPos + (pCur - tchBuf);
				const int style = static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT,iPos));
				if (style) {
					if (style == pLexCurrent->operatorStyle || style == pLexCurrent->operatorStyle2) {
						return false;
					}
					if ((pLexCurrent->iLexer == SCLEX_PHPSCRIPT || pLexCurrent->iLexer == SCLEX_HTML)
						&& (style == js_style(SCE_JS_OPERATOR) || style == js_style(SCE_JS_OPERATOR2))) {
						return false;
					}
				}

				pCur++;
				while (IsHtmlTagChar(*pCur)) {
					tchIns[cchIns++] = *pCur;
					pCur++;
				}
			}

			tchIns[cchIns] = ' ';
			tchIns[cchIns + 1] = '\0';

			if (cchIns > 3 && (pLexCurrent->rid != NP2LEX_XML)) {
				// HTML void tag except <p>
				if (IsHtmlVoidTag(tchIns + 1)) {
					autoClosed = true;
					cchIns = 0;
				}
			}
			if (cchIns > 2) {
				tchIns[1] = '/';
				tchIns[cchIns] = '>';
				autoClosed = true;
				d.Send(SCI_REPLACESEL,0,reinterpret_cast<LPARAM>(tchIns));
				d.Send(SCI_SETSEL,iCurPos,iCurPos);
			}
		}
	}

	return autoClosed;
}

bool IsDocumentEscapeOrFormat(EditorDocument& d,LRESULT before,int ch,int previous,int style,bool punctuation){return IsEscapeCharOrFormatSpecifier(d,before,ch,previous,style,punctuation);}
