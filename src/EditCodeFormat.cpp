// Original notepad4 Bridge.cpp code pretty/compress algorithms, adapted to an
// explicit MDI document. Authors and license are recorded in THIRD_PARTY.md.
#include "EditCodeFormat.h"
#include "../3rdparty/notepad4/src/EditLexer.h"
#include "StyleManager.h"
#include <string_view>
namespace {
constexpr uint32_t DefaultWordCharSet[8] = {0x00000000U, 0x03ff0000U, 0x87fffffeU, 0x07fffffeU,
                                            0xffffffffU, 0xffffffffU, 0xffffffffU, 0xffffffffU};
bool BitTestEx(const uint32_t *values, unsigned value) {
    return value < 256 && (values[value >> 5] & (1U << (value & 31)));
}
} // namespace
namespace { // code pretty

enum {
    SpaceOption_None = 0,
    SpaceOption_IndentAfter = 1,
    SpaceOption_SpaceBefore = 2,
    SpaceOption_SpaceAfter = 4,
    SpaceOption_NewLineBefore = 8,
    SpaceOption_NewLineAfter = 16,
    SpaceOption_DanglingStmt = 32,
    SpaceOption_PushBrace = 64,
    SpaceOption_PopBrace = 128,
};

int AddStyleSeparator(LPCEDITLEXER pLex, int ch, int chPrev, int style) noexcept {
    if ((style >= pLex->stringStyleFirst && style <= pLex->stringStyleLast) ||
        (pLex->iLexer == SCLEX_JSON && (style == SCE_JSON_PROPERTYNAME)) ||
        (pLex->iLexer == SCLEX_JAVASCRIPT && (style == SCE_JS_KEY || style == SCE_JS_OPERATOR_PF))) {
        return SpaceOption_None;
    }
    // a++ + ++b, a + +1, a-- - --b, a - -1
    if (ch == '+' || ch == '-') {
        if (style == SCE_CSS_OPERATOR2 && pLex->iLexer == SCLEX_CSS) {
            // '+' and '-' inside math function requires space on both side
            return SpaceOption_SpaceBefore | SpaceOption_SpaceAfter;
        }
        if (ch == chPrev) {
            return SpaceOption_SpaceBefore;
        }
    }
    // var name; return .5; CSS property: 1 #1 .5 --name;
    if (BitTestEx(DefaultWordCharSet, chPrev)) {
        if (BitTestEx(DefaultWordCharSet, ch) || (ch == '-' && pLex->iLexer == SCLEX_CSS)) {
            // TODO: improve CSS An+B when B is negative, https://www.w3.org/TR/css-syntax-3/#anb-microsyntax
            return SpaceOption_SpaceBefore;
        }
        if ((ch == '.' || ch == '$' || ch == '#') && style != pLex->operatorStyle && style != pLex->operatorStyle2) {
            return SpaceOption_SpaceBefore;
        }
    }
    return SpaceOption_None;
}

void CodePretty(EditorDocument &document, std::string &output, LPCEDITLEXER pLex, const char *styledText,
                size_t textLength) {
    char fmtbuf[128];
    std::string braceStack(1, '\0'); // sentinel
    memset(fmtbuf, 0, 4);

    unsigned fmtlen = 0;
    uint32_t blockLevel = 0;
    uint32_t indentPrev = 0;
    uint8_t chPrev = 0;
    uint8_t chPrevNonWhite = 0;
    bool commentEndEOL = false;
    bool defaultCase = false;

    unsigned eol = '\r' | ('\n' << 8);
    unsigned eolWidth = static_cast<unsigned>(document.Send(SCI_GETEOLMODE));
    eol >>= 8 * (eolWidth >> 1);
    eolWidth = (eolWidth == SC_EOL_CRLF) ? 2 : 1;

    constexpr unsigned maxFmtLen = sizeof(fmtbuf) - 4 - 4; // \r\n + 4 + \r\n
    constexpr uint8_t braceObject = '{' + 1;
    constexpr uint8_t braceTemplate = '{' + 2;
    constexpr uint8_t bracketArray = '[' + 1;

    uint8_t braceTop = '\0';
    uint8_t stylePrev = styledText[0];
    uint8_t styleBefore = stylePrev;
    const char *const textBuffer = styledText + textLength + 1;
    for (size_t offset = 0; offset < textLength; offset++) {
        if (fmtlen >= maxFmtLen) {
            // keep last character and eol in the buffer
            output += std::string_view{fmtbuf, fmtlen - 4};
            memcpy(fmtbuf, &fmtbuf[fmtlen - 4], 4);
            fmtlen = 4;
        }

        const uint8_t style = styledText[offset];
        if (style == 0) {
            styleBefore = 0;
            continue;
        }

        int spaceOption = SpaceOption_None;
        unsigned operatorLen = 0;
        const uint8_t ch = textBuffer[offset];
        if (style <= pLex->commentStyleMarker) {
            if (ch == '/' && (chPrev == '\n' || styleBefore == 0 || styleBefore > pLex->commentStyleMarker) &&
                textBuffer[offset + 1] == '/') {
                commentEndEOL = true; // fix indentation after comment line
            } else if (styledText[offset + 1] == 0 &&
                       (textBuffer[offset + 1] == '\n' || textBuffer[offset + 1] == '\r')) {
                commentEndEOL = true; // keep new line after block comment
                spaceOption = SpaceOption_NewLineAfter;
            }
        } else if (style != styleBefore) {
            spaceOption = AddStyleSeparator(pLex, ch, chPrev, style);
            if (style == SCE_JS_WORD && pLex->iLexer == SCLEX_JAVASCRIPT && (ch == 'c' || ch == 'd')) {
                if ((textBuffer[offset + 1] == 'a' || textBuffer[offset + 1] == 'e') &&
                    (textBuffer[offset + 2] == 's' || textBuffer[offset + 2] == 'f')) {
                    defaultCase = true;
                    spaceOption |= SpaceOption_NewLineBefore;
                }
            }
            if (chPrev == ')') {
                if (pLex->iLexer != SCLEX_JSON &&
                    (stylePrev == pLex->operatorStyle || stylePrev == pLex->operatorStyle2)) {
                    if (pLex->iLexer == SCLEX_CSS) {
                        if ((ch == '+' || ch == '-') ||
                            (ch != ':' && style != SCE_CSS_OPERATOR)) { // :not([class]):hover
                            spaceOption |= SpaceOption_SpaceBefore;     // CSS property: function() value
                        }
                    } else if (style != SCE_JS_OPERATOR && style != SCE_JS_OPERATOR2) {
                        spaceOption |= SpaceOption_NewLineBefore | SpaceOption_DanglingStmt;
                        indentPrev++; // if (), for (), while () statement
                    }
                }
            } else if ((stylePrev == SCE_CSS_AT_RULE && pLex->iLexer == SCLEX_CSS) ||
                       (stylePrev == SCE_JS_WORD && pLex->iLexer == SCLEX_JAVASCRIPT &&
                        (style <= SCE_JS_OPERATOR_PF || ch == '{' || ch == '[' || ch == '!' || ch == '~' ||
                         (ch == '(' && chPrev != 't')))) {
                // not set(), get();
                spaceOption |= SpaceOption_SpaceBefore;
            }
        }
        if (style == pLex->operatorStyle || style == pLex->operatorStyle2) {
            if (ch == ':') {
                spaceOption |= SpaceOption_SpaceAfter; // property: value
                if (pLex->iLexer == SCLEX_JAVASCRIPT) {
                    if (defaultCase && braceTop == '{') {
                        defaultCase = false;
                        spaceOption |= SpaceOption_NewLineAfter;
                    } else if (stylePrev != SCE_JS_KEY && stylePrev != SCE_JS_LABEL &&
                               (stylePrev != SCE_JS_WORD || chPrev != 't')) {
                        spaceOption |= SpaceOption_SpaceBefore; // ternary operator, not set: / get:
                    }
                }
            } else if (ch == ',') {
                if (pLex->iLexer == SCLEX_JSON || braceTop == braceObject || braceTop == bracketArray) {
                    spaceOption |= SpaceOption_NewLineAfter;
                } else {
                    spaceOption |= SpaceOption_SpaceAfter;
                }
            } else if (ch == ';') {
                if (braceTop == '(') {
                    spaceOption |= SpaceOption_SpaceAfter; // for (;;)
                } else {
                    spaceOption |= SpaceOption_NewLineAfter;
                }
            } else if (ch == '{' || ch == '[') {
                if (ch == '{' && (chPrev == ')' || pLex->iLexer == SCLEX_CSS)) {
                    spaceOption |= SpaceOption_SpaceBefore; // if (...){}, CSS: selector {rule}
                }
                if (pLex->iLexer == SCLEX_JAVASCRIPT) {
                    spaceOption |= SpaceOption_PushBrace;
                    if (chPrev == '$' && style == styleBefore) {
                        braceTop = braceTemplate; // ${}
                    } else if (chPrevNonWhite == '=' || chPrevNonWhite == '(' || chPrevNonWhite == ',' ||
                               chPrevNonWhite == '[' ||
                               (chPrevNonWhite == ':' && (braceTop == braceObject || braceTop == bracketArray))) {
                        braceTop = ch + 1;
                        spaceOption |= SpaceOption_NewLineAfter | SpaceOption_IndentAfter;
                    } else {
                        braceTop = ch;
                        if (ch == '{') {
                            spaceOption |= SpaceOption_NewLineAfter | SpaceOption_IndentAfter;
                            if (stylePrev >= SCE_JS_IDENTIFIER) {
                                spaceOption |= SpaceOption_SpaceBefore;
                            }
                        }
                    }
                } else if (ch == '{' || pLex->iLexer == SCLEX_JSON) {
                    spaceOption |= SpaceOption_NewLineAfter | SpaceOption_IndentAfter;
                }
            } else if (ch == '}' || ch == ']') {
                if ((ch == '}' && braceTop != braceTemplate) ||
                    (ch == ']' && (braceTop == bracketArray || pLex->iLexer == SCLEX_JSON))) {
                    spaceOption |= SpaceOption_NewLineBefore | SpaceOption_NewLineAfter;
                    if (blockLevel > 0) {
                        --blockLevel;
                    }
                }
                if (pLex->iLexer == SCLEX_JAVASCRIPT && static_cast<uint8_t>(ch - braceTop) < 3) {
                    spaceOption |= SpaceOption_PopBrace;
                }
            } else if (pLex->iLexer != SCLEX_JSON) {
                if (pLex->iLexer == SCLEX_CSS) {
                    const uint8_t chNext = textBuffer[offset + 1];
                    if (style == SCE_CSS_OPERATOR2 || ch == '>' // child combinator
                        || (ch == '&' && chNext != ':' && chNext != '.' &&
                            chNext != ')')              // nesting selector, &:hover, &.class, :not(&)
                        || (ch == '~' && chNext != '=') // subsequent-sibling combinator
                        || (ch == '|' && chNext == '|') // column combinator
                        // next-sibling combinator
                        || (ch == '+' && styledText[offset + 1] != SCE_CSS_NUMBER &&
                            styledText[offset + 1] != SCE_CSS_DIMENSION)) {
                        spaceOption |= SpaceOption_SpaceAfter;
                        if (ch == '|') {
                            operatorLen = 2;
                        }
                        if (chPrev != '(') { // :has(> img)
                            spaceOption |= SpaceOption_SpaceBefore;
                        }
                    } else if (ch == '!') {
                        spaceOption |= SpaceOption_SpaceBefore; // !important
                    }
                } else {
                    if (ch == '(') {
                        braceTop = '(';
                        spaceOption |= SpaceOption_PushBrace;
                    } else if (ch == ')') {
                        if (braceTop == '(') {
                            spaceOption |= SpaceOption_PopBrace;
                        }
                    } else if (ch != '.' && ch != '~' && ch != '$') {
                        // https://tc39.es/ecma262/#sec-punctuators
                        // https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators
                        // !		!=		!==
                        // %		%=
                        // &	&&	&=		&&=
                        // *	**	*=		**=
                        // +	++	+=
                        // -	--	-=
                        // /		/=
                        // <	<<	<=		<<=
                        // =	=>	==		===
                        // >	>>	>=		>>=	>>>		>>>=
                        // ?	?.	??		??=
                        // ^		^=
                        // |	||	|=		||=
                        uint8_t chNext = textBuffer[offset + 1];
                        if (chNext == '=' || (ch != '!' && ch == chNext)) {
                            operatorLen = 2;
                            spaceOption |= SpaceOption_SpaceBefore | SpaceOption_SpaceAfter;
                            chNext = textBuffer[offset + 2];
                            if (chNext == '=' || chNext == '>') {
                                operatorLen = 3;
                                if (chNext == '>' && textBuffer[offset + 3] == '=') {
                                    operatorLen = 4;
                                }
                            }
                        } else if (ch == '+' || ch == '-') {
                            // detect unary / binary operator, similar to FollowExpression()
                            if (stylePrev == SCE_JS_WORD) {
                                spaceOption |= SpaceOption_SpaceBefore;
                            } else if (chPrevNonWhite == ')' || chPrevNonWhite == ']' ||
                                       (stylePrev >= SCE_JS_NUMBER && stylePrev <= SCE_JS_OPERATOR_PF) ||
                                       (stylePrev >= SCE_JS_IDENTIFIER && stylePrev <= SCE_JS_CONSTANT)) {
                                spaceOption |= SpaceOption_SpaceBefore | SpaceOption_SpaceAfter;
                            }
                        } else if (ch == '*' && stylePrev == SCE_JS_WORD) {
                            // yield*, function*
                            spaceOption |= SpaceOption_SpaceAfter;
                        } else if (ch != '!' && (ch != '?' || chNext != '.')) {
                            spaceOption |= SpaceOption_SpaceBefore | SpaceOption_SpaceAfter;
                            if (ch == '=' && chNext == '>') {
                                operatorLen = 2;
                            }
                        }
                    }
                }
            }
            if (chPrev == '\n' &&
                (ch == ',' || ch == ')'                      // JavaScript: function({})
                 || ch == ';'                                // JavaScript: {};
                 || ch == '='                                // JavaScript: let {} = value, const {} = value
                 || ch == ':'                                // JavaScript: ternary operator
                 || (ch == '.' && pLex->iLexer != SCLEX_CSS) // JavaScript: {}.property, [].property; CSS: .class {rule}
                 || ((ch == ']' || ch == '}') && chPrevNonWhite == ch - 2))) { // empty [], {}
                chPrev = '\0';
                fmtlen -= eolWidth;
                if ((spaceOption & SpaceOption_NewLineBefore) == 0) {
                    chPrev = fmtbuf[fmtlen - 1];
                }
            }
            if (spaceOption & SpaceOption_PushBrace) {
                braceStack.push_back(static_cast<char>(braceTop));
            } else if (spaceOption & SpaceOption_PopBrace) {
                braceStack.pop_back();
                braceTop = braceStack.back();
            }
        }
        if (chPrev > ' ') {
            if (spaceOption & SpaceOption_NewLineBefore) {
                chPrev = '\n';
                memcpy(&fmtbuf[fmtlen], &eol, 2);
                fmtlen += eolWidth;
            } else if (spaceOption & SpaceOption_SpaceBefore) {
                fmtbuf[fmtlen++] = ' ';
            }
        }
        if (chPrev == '\n') {
            uint32_t count = (spaceOption & SpaceOption_DanglingStmt) ? indentPrev : blockLevel;
            indentPrev = count;
            char indent = '\t';
            if (!document.Send(SCI_GETUSETABS)) {
                indent = ' ';
                count *= static_cast<uint32_t>(document.Send(SCI_GETTABWIDTH));
            }
            if (count != 0) {
                output += std::string_view{fmtbuf, fmtlen};
                fmtlen = 0;
                output.append(count, indent);
            }
        }
        if (ch == '\r' || ch == '\n') {
            spaceOption |= SpaceOption_NewLineAfter;
            if (ch == '\r' && textBuffer[offset + 1] == '\n') {
                offset += 1;
            }
        } else {
            if (ch > ' ' && style > pLex->commentStyleMarker) {
                chPrevNonWhite = ch;
            }
            if (operatorLen > 1) {
                memcpy(&fmtbuf[fmtlen], textBuffer + offset, operatorLen);
                fmtlen += operatorLen;
                offset += operatorLen - 1;
                chPrevNonWhite = chPrev = fmtbuf[fmtlen - 1];
            } else {
                chPrev = ch;
                fmtbuf[fmtlen++] = static_cast<char>(ch);
            }
        }
        if (spaceOption & SpaceOption_NewLineAfter) {
            blockLevel += spaceOption & SpaceOption_IndentAfter;
            chPrev = '\n';
            // don't add indentation inside comment and string
            if ((!commentEndEOL && style <= pLex->commentStyleMarker) ||
                (style >= pLex->stringStyleFirst && style <= pLex->stringStyleLast)) {
                chPrev = '\r';
            }
            commentEndEOL = false;
            defaultCase = false;
            memcpy(&fmtbuf[fmtlen], &eol, 2);
            fmtlen += eolWidth;
        } else if (spaceOption & SpaceOption_SpaceAfter) {
            chPrev = ' ';
            fmtbuf[fmtlen++] = ' ';
        }
        stylePrev = style;
        styleBefore = style;
    }
    if (fmtlen != 0) {
        output += std::string_view{fmtbuf, fmtlen};
    }
}

} // namespace

bool CanFormatDocumentCode(const EditorDocument &document) {
    const auto *lexer = GetDocumentLexer(document);
    return lexer && (lexer->lexerAttr & LexerAttr_CodePretty) != 0;
}
void FormatDocumentCode(EditorDocument &document, bool pretty) {
    const auto *lexer = GetDocumentLexer(document);
    if (!lexer || !CanFormatDocumentCode(document) || document.Send(SCI_GETREADONLY))
        return;
    auto start = document.Send(SCI_GETSELECTIONSTART), end = document.Send(SCI_GETSELECTIONEND);
    if (start == end) {
        start = 0;
        end = document.Send(SCI_GETLENGTH);
    }
    if (start == end)
        return;
    document.Send(SCI_COLOURISE, 0, end);
    auto text = document.Range(start, end - start);
    std::vector<char> styled(text.size() * 2 + 2, '\0');
    for (size_t i = 0; i < text.size(); ++i)
        styled[i] = static_cast<char>(document.Send(SCI_GETSTYLEINDEXAT, start + i));
    memcpy(styled.data() + text.size() + 1, text.data(), text.size());
    std::string output;
    if (pretty)
        CodePretty(document, output, lexer, styled.data(), text.size());
    else {
        int previous = 0, previousStyle = static_cast<uint8_t>(styled[0]);
        for (size_t i = 0; i < text.size(); ++i) {
            const auto style = static_cast<uint8_t>(styled[i]);
            if (style > lexer->commentStyleMarker) {
                unsigned char character = text[i];
                if (character == '\\' && style != lexer->escapeCharacterStyle && i + 1 < text.size() &&
                    (text[i + 1] == '\n' || text[i + 1] == '\r')) {
                    i += (text[i + 1] == '\r' && i + 2 < text.size() && text[i + 2] == '\n') ? 2 : 1;
                    continue;
                }
                int spacing = style != previousStyle ? AddStyleSeparator(lexer, character, previous, style) : 0;
                if (spacing & SpaceOption_SpaceBefore)
                    output += ' ';
                output += character;
                previous = character;
                if (spacing & SpaceOption_SpaceAfter) {
                    output += ' ';
                    previous = ' ';
                }
            }
            previousStyle = style;
        }
    }
    if (output == text)
        return;
    document.Send(SCI_BEGINUNDOACTION);
    document.Send(SCI_SETTARGETRANGE, start, end);
    document.Send(SCI_REPLACETARGET, output.size(), reinterpret_cast<LPARAM>(output.data()));
    document.Send(SCI_SETSEL, start, start + output.size());
    document.Send(SCI_ENDUNDOACTION);
}
