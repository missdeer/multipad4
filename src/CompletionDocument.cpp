// Document word and compound completion algorithms from pinned EditAutoC.cpp.
// Copyright/license: 3rdparty/notepad4/License.txt.
#include "StyleManager.h"
#include "CompletionEngine.h"
#include "../3rdparty/notepad4/src/EditLexer.h"
#include <functional>
#include <cstring>
#include <chrono>
namespace {
constexpr LRESULT NP2_AUTOC_MAX_WORD_LENGTH=124;
constexpr size_t NP2_AUTOC_WORD_BUFFER_SIZE=128;
bool IsDefaultWordChar(unsigned ch){return ch>=128 || (ch>='0'&&ch<='9')||(ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||ch=='_';}
bool IsASpaceOrTab(int ch){return ch==' '||ch=='\t';}
bool IsWord(EditorDocument& d,int ch){return ch>=0 && (ch<128?GetCompletionCache(*GetDocumentLexer(d)).Word(ch):d.Send(SCI_GETCHARACTERCLASS,ch)==3);}
bool NeedSpaceAfterKeyword(const char* word,LRESULT length){auto p=strstr(" if for try using while elseif switch foreach synchronized ",word);return p&&p[-1]==' '&&p[length]==' ';}
struct DocumentWords {
    const std::string& root;int iStartLen;bool ignoreCase;const std::function<void(std::string)>& add;
    const char* pWordStart=root.c_str();
    bool StartsWith(const char* word){return (ignoreCase?_strnicmp(word,root.c_str(),root.size()):strncmp(word,root.c_str(),root.size()))==0;}
    void AddWord(const char* word,UINT length){add(std::string(word,length));}
    void AddSubWord(LPSTR,UINT,UINT);
};
LRESULT FindWord(EditorDocument& d,UINT flags,Sci_TextToFindFull* range,UINT timeout){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);auto start=range->chrg.cpMin;const auto end=range->chrg.cpMax;
    while(start<=end && std::chrono::steady_clock::now()<deadline){
        auto trial=*range;auto stop=std::min<LRESULT>(end,start+64*1024+static_cast<LRESULT>(range->textLength));trial.chrg={start,stop};
        auto found=d.Send(SCI_FINDTEXTFULL,flags,reinterpret_cast<LPARAM>(&trial));if(found>=0){range->chrgText=trial.chrgText;return found;}
        if(stop==end)break;start=stop-static_cast<LRESULT>(range->textLength);
    }
    return -1;
}
void DocumentWords::AddSubWord(LPSTR pWord,UINT wordLength,UINT iRootLen) {
	/*
	when pRoot is 'b', split 'bugprone-branch-clone' as following:
	1. first hyphen: 'bugprone-branch-clone' => 'bugprone', 'branch-clone'.
	2. second hyphen: 'bugprone-branch-clone' => 'bugprone-branch'; 'branch-clone' => 'branch'.
	*/

	LPCSTR words[8];
	UINT starts[8];
	UINT count = 0;

	for (UINT i = 0; i < wordLength - 1; i++) {
		const char ch = pWord[i];
		if (ch == '.' || ch == '-' || ch == ':') {
			if (i >= iRootLen) {
				pWord[i] = '\0';
				AddWord(pWord, i);
				for (UINT j = 0; j < count; j++) {
					const UINT subLen = i - starts[j];
					if (subLen >= iRootLen) {
						AddWord(words[j], subLen);
					}
				}
				pWord[i] = ch;
			}
			if (ch != '.' && (pWord[i + 1] == '>' || pWord[i + 1] == ':')) {
				++i;
			}

			const UINT subLen = wordLength - (i + 1);
			LPCSTR pSubRoot = pWord + i + 1;
			if (subLen >= iRootLen && StartsWith(pSubRoot)) {
				AddWord(pSubRoot, subLen);
				if (count < COUNTOF(words)) {
					words[count] = pSubRoot;
					starts[count] = i + 1;
					++count;
				}
			}
		}
	}
}
}
void AddDocumentCompletionWords(EditorDocument& d,const std::string& root,bool bIgnoreCase,char prefix,UINT timeout,const uint32_t (&ignoredStyleMask)[8],const std::function<void(std::string)>& add, size_t scanBytes) {
    DocumentWords pWList{root,static_cast<int>(root.size()),bIgnoreCase,add};const auto* pLexCurrent=GetDocumentLexer(d);
	LPCSTR const pRoot = pWList.pWordStart;
	const int iRootLen = pWList.iStartLen;

	std::vector<char> pFind(static_cast<size_t>(iRootLen)+2,'\0');
	pFind[0] = prefix;
	memcpy(pFind.data() + (prefix != '\0'), pRoot, iRootLen);
	UINT findFlag = (bIgnoreCase ? SCFIND_NONE : SCFIND_MATCHCASE) | SCFIND_MATCH_TO_WORD_END;
	if (IsDefaultWordChar(static_cast<uint8_t>(pRoot[0]))) {
		findFlag |= SCFIND_WORDSTART;
	}

	const LRESULT textLength = iRootLen + (prefix ? 1 : 0);
	const LRESULT iCurrentPos = d.Send(SCI_GETCURRENTPOS) - textLength;
	const LRESULT iDocLen = d.Send(SCI_GETLENGTH);
	Sci_TextToFindFull ft = { { 0, iDocLen }, pFind.data(), static_cast<size_t>(textLength), { 0, 0 } };
    if (scanBytes != SIZE_MAX) {
        const auto radius = static_cast<LRESULT>(scanBytes / 2);
        ft.chrg = {(std::max)(LRESULT(0), iCurrentPos - radius), (std::min)(iDocLen, iCurrentPos + radius)};
    }

    const auto timer=std::chrono::steady_clock::now();
    auto remaining=[&](){auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-timer).count();return elapsed>=timeout?0u:timeout-static_cast<UINT>(elapsed);};
	LRESULT iPosFind = FindWord(d,findFlag,&ft,remaining());

	while (iPosFind >= 0 && iPosFind < iDocLen && remaining()) {
		LRESULT wordEnd = iPosFind + iRootLen;
		const int style = static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT,wordEnd-1));
		wordEnd = ft.chrgText.cpMax;
		if (iPosFind != iCurrentPos && !GetCompletionCache(*pLexCurrent).Test(ignoredStyleMask,style)) {
			// find all word after '::', '->', '.' and '-'
			bool bSubWord = false;
			while (wordEnd < iDocLen) {
				const int ch = static_cast<int>(d.Send(SCI_GETCHARAT,wordEnd));
				if (!(ch == ':' || ch == '.' || ch == '-')) {
					if (ch == '!' && pLexCurrent->iLexer == SCLEX_RUST && style == SCE_RUST_MACRO) {
						// macro: println!()
						++wordEnd;
					}
					break;
				}

				const LRESULT before = wordEnd;
				LRESULT width = 0;
				int chNext = static_cast<int>(d.Send(SCI_GETCHARACTERANDWIDTH,wordEnd+1,reinterpret_cast<LPARAM>(&width)));
				if ((ch == '-' && chNext == '>') || (ch == ':' && chNext == ':')) {
					chNext = static_cast<int>(d.Send(SCI_GETCHARACTERANDWIDTH,wordEnd+2,reinterpret_cast<LPARAM>(&width)));
					if (IsWord(d,chNext)) {
						wordEnd += 2;
					}
				} else if (ch == '.' || (ch == '-' && style == d.Send(SCI_GETSTYLEINDEXAT,wordEnd))) {
					if (IsWord(d,chNext)) {
						++wordEnd;
					}
				}
				if (wordEnd == before) {
					break;
				}

				while (wordEnd < iDocLen && (chNext < 0x80 && !IsDefaultWordChar(chNext))) {
					wordEnd += width;
					chNext = static_cast<int>(d.Send(SCI_GETCHARACTERANDWIDTH,wordEnd,reinterpret_cast<LPARAM>(&width)));
					if (!IsWord(d,chNext)) {
						break;
					}
				}

				wordEnd = d.Send(SCI_WORDENDPOSITION,wordEnd,TRUE);
				if (wordEnd - iPosFind > NP2_AUTOC_MAX_WORD_LENGTH) {
					wordEnd = before;
					break;
				}
				bSubWord = true;
			}

			if (wordEnd - iPosFind >= iRootLen) {
				char wordBuf[NP2_AUTOC_WORD_BUFFER_SIZE];
				char *pWord = wordBuf;
				const Sci_TextRangeFull tr = { { iPosFind, min(iPosFind + NP2_AUTOC_MAX_WORD_LENGTH, wordEnd) }, pWord };
				int wordLength = static_cast<int>(d.Send(SCI_GETTEXTRANGEFULL,0,reinterpret_cast<LPARAM>(&tr)));

				const LRESULT before = d.Send(SCI_POSITIONBEFORE,iPosFind);
				if (before + 1 == iPosFind) {
					const int chPrev = static_cast<int>(d.Send(SCI_GETCHARAT,before));
					// word after escape character or format specifier
					if (chPrev == '%' || chPrev == pLexCurrent->escapeCharacterStart) {
						if (IsDocumentEscapeOrFormat(d,before,static_cast<uint8_t>(pWord[0]),chPrev,style,false)) {
							pWord++;
							--wordLength;
						}
					}
				}
				if (prefix && prefix == pWord[0]) {
					pWord++;
					--wordLength;
				}

				//if (pLexCurrent->iLexer == SCLEX_PHPSCRIPT && wordLength >= 2 && pWord[0] == '$' && pWord[1] == '$') {
				//	pWord++;
				//	--wordLength;
				//}
				while (wordLength > 0 && (pWord[wordLength - 1] == '-' || pWord[wordLength - 1] == ':' || pWord[wordLength - 1] == '.')) {
					--wordLength;
					pWord[wordLength] = '\0';
				}

				if (wordLength >= iRootLen && (pWord[0] != ':' || pWord[1] == ':') && pWList.StartsWith(pWord)) {
					bool space = false;
					if (!(pLexCurrent->iLexer == SCLEX_CPP && style == SCE_C_MACRO)) {
						while (remaining() && IsASpaceOrTab(static_cast<int>(d.Send(SCI_GETCHARAT,wordEnd)))) {
							space = true;
							wordEnd++;
						}
					}

					const int chWordEnd = static_cast<int>(d.Send(SCI_GETCHARAT,wordEnd));
					if ((pLexCurrent->iLexer == SCLEX_JULIA || pLexCurrent->iLexer == SCLEX_RUST) && chWordEnd == '!') {
						const int chNext = static_cast<int>(d.Send(SCI_GETCHARAT,wordEnd+1));
						if (chNext == '(') {
							wordEnd += 2;
							pWord[wordLength++] = '!';
							pWord[wordLength++] = '(';
							pWord[wordLength++] = ')';
						}
					}
					else if (chWordEnd == '(') {
						if (space && NeedSpaceAfterKeyword(pWord, wordLength)) {
							pWord[wordLength++] = ' ';
						}

						pWord[wordLength++] = '(';
						pWord[wordLength++] = ')';
						wordEnd++;
					}

					if (wordLength >= iRootLen) {
						pWord[wordLength] = '\0';
						pWList.AddWord(pWord, wordLength);
						if (bSubWord) {
							pWList.AddSubWord(pWord, wordLength, iRootLen);
						}
					}
				}
			}
		}

		ft.chrg.cpMin = wordEnd;
		iPosFind = FindWord(d,findFlag,&ft,remaining());
	}
}
