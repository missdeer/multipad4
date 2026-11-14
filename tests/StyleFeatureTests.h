#pragma once
#include "../src/StyleManager.h"
#include "../src/CompletionEngine.h"
#include "../src/resource.h"
#include "../3rdparty/notepad4/src/EditLexer.h"
#include "../src/LexerCatalog.h"
#include <source_location>
#include <stdexcept>
#include <fstream>
// Supply a scratch document; these tests intentionally replace its buffer.
inline void RunStyleFeatureTests(EditorDocument& d) {
    auto require=[](bool ok,std::source_location where=std::source_location::current()){
        if(!ok)throw std::runtime_error("Style feature assertion at line "+std::to_string(where.line()));
    };
    const size_t count=sizeof(lexerCatalog)/sizeof(*lexerCatalog);
    require(DocumentSchemeCount()==count-1 && count>=90);
    for(size_t i=1;i<count;++i){d.style.scheme=static_cast<int>(i);ConfigureDocumentStyle(d);require(d.Send(SCI_GETLEXER)==lexerCatalog[i]->iLexer);}
    struct Sample {const wchar_t* path;const char* text;int lexer,style;};
    const Sample samples[]={
        {L"sample.cpp","int value = 42;\n",SCLEX_CPP,SCE_C_WORD2},
        {L"sample.py","def function():\n    return True\n",SCLEX_PYTHON,SCE_PY_WORD},
        {L"sample.json","true\n",SCLEX_JSON,SCE_JSON_KEYWORD},
        {L"sample.rs","fn main() {}\n",SCLEX_RUST,SCE_RUST_WORD},
        {L"sample.go","package main\n",SCLEX_GO,SCE_GO_WORD},
        {L"sample.js","function name() {}\n",SCLEX_JAVASCRIPT,SCE_JS_WORD},
    };
    for(const auto& sample:samples){d.style.scheme=0;d.path=sample.path;d.Load({sample.text});ConfigureDocumentStyle(d);if(d.Send(SCI_GETLEXER)!=sample.lexer || d.Send(SCI_GETSTYLEINDEXAT,0)!=sample.style)throw std::runtime_error("Style sample "+files::ToUtf8(sample.path)+" lexer="+std::to_string(d.Send(SCI_GETLEXER))+" expected="+std::to_string(sample.lexer)+" style="+std::to_string(d.Send(SCI_GETSTYLEINDEXAT,0))+" expected="+std::to_string(sample.style));}
    // The pinned fork's SCI_GETTEXT length excludes its trailing NUL.
    // Exercise both fixed prefix sniff buffers with longer source documents.
    d.style.scheme=0;d.path=L"unknown.extension";d.Load({std::string(4096,'a')});ConfigureDocumentStyle(d);require(d.Send(SCI_GETLENGTH)==4096);
    d.path=L"page.aspx";d.Load({std::string(4096,' ') + "<%@ Page Language=\"VB\" %>"});ConfigureDocumentStyle(d);require(d.Send(SCI_GETLEXER)==SCLEX_HTML);
    d.style.scheme=0;d.path=L"script";d.Load({"#!/usr/bin/env python3\nprint(1)\n"});ConfigureDocumentStyle(d);require(d.Send(SCI_GETLEXER)==SCLEX_PYTHON);
    d.path=L"document.conf";d.Load({"<?xml version=\"1.0\"?><root/>"});ConfigureDocumentStyle(d);require(GetDocumentLexer(d)->rid==NP2LEX_XML);
    d.path=L"CMakeCache.txt";d.Load({"NAME:STRING=value\n"});ConfigureDocumentStyle(d);require(d.Send(SCI_GETLEXER)==SCLEX_CMAKE);
    d.path=L"theme.scss";d.Load({"$color: red;"});ConfigureDocumentStyle(d);require(StyleCommandState(&d,IDM_LEXER_SCSS).checked && !StyleCommandState(&d,IDM_LEXER_CSS).checked);
    d.path=L"table.csv";d.style.csvConfigured=false;d.Load({"name;value;unit\nlength;2;cm\n"});ConfigureDocumentStyle(d);require(GetDocumentLexer(d)->rid==NP2LEX_CSV && (d.style.csvOptions&255)==';');
    d.path=L"config.h.in";d.Load({"#define VALUE 1\n"});ConfigureDocumentStyle(d);require(d.Send(SCI_GETLEXER)==SCLEX_CPP);
    d.path=L"script.r";d.Load({"REBOL []\n"});ConfigureDocumentStyle(d);require(GetDocumentLexer(d)->rid==NP2LEX_REBOL);
    d.path=L"script.m";d.Load({"# octave comment\n"});ConfigureDocumentStyle(d);require(GetDocumentLexer(d)->rid==NP2LEX_MATLAB && d.style.resolvedDialect==IDM_LEXER_OCTAVE);
    d.path=L"unknown.extension";d.Load({"{\n\"key\": 1\n}\n"});ConfigureDocumentStyle(d);require(GetDocumentLexer(d)->rid==NP2LEX_JSON);
    auto sniffed=GetDocumentLexer(d);d.Send(SCI_SETTEXT,0,reinterpret_cast<LPARAM>("#!/bin/sh\n"));require(GetDocumentLexer(d)==sniffed);ConfigureDocumentStyle(d);require(GetDocumentLexer(d)->rid==NP2LEX_BASH);
    require(SelectDocumentScheme(d,L"Python"));require(GetDocumentLexer(d)->rid==NP2LEX_PYTHON);
    d.style.scheme=0;
    d.style.scheme=0;d.path=L"sample.cpp";d.Load({"int main() {\n    return 0;\n}\n"});ConfigureDocumentStyle(d);
    require(d.language==L"C/C++");
    require(d.Send(SCI_STYLEGETFORE,SCE_C_NUMBER)==RGB(255,0,0));
    auto wrap=d.Send(SCI_GETWRAPMODE),margin=d.Send(SCI_GETMARGINWIDTHN,0);
    d.Send(SCI_SETWRAPMODE,SC_WRAP_WORD);d.Send(SCI_SETMARGINWIDTHN,0,0);ConfigureDocumentStyle(d);
    require(d.Send(SCI_GETWRAPMODE)==SC_WRAP_WORD && d.Send(SCI_GETMARGINWIDTHN,0)==0);
    d.Send(SCI_SETWRAPMODE,wrap);d.Send(SCI_SETMARGINWIDTHN,0,margin);
    bool priorFolding=d.style.folding;require(ExecuteStyleCommand(d.hwnd,d,IDM_VIEW_SHOW_FOLDING));
    require((d.Send(SCI_GETMARGINWIDTHN,2)>0)==!priorFolding);
    ExecuteStyleCommand(d.hwnd,d,IDM_VIEW_SHOW_FOLDING);require((d.Send(SCI_GETMARGINWIDTHN,2)>0)==priorFolding);
    d.Load({"one\n two\nthree\n four\n"});
    d.Send(SCI_SETLEXER,SCLEX_NULL);d.Send(SCI_COLOURISE,0,-1);
    d.Send(SCI_SETFOLDLEVEL,0,SC_FOLDLEVELBASE|SC_FOLDLEVELHEADERFLAG);
    d.Send(SCI_SETFOLDLEVEL,1,SC_FOLDLEVELBASE+1);
    d.Send(SCI_SETFOLDLEVEL,2,SC_FOLDLEVELBASE|SC_FOLDLEVELHEADERFLAG);
    d.Send(SCI_SETFOLDLEVEL,3,SC_FOLDLEVELBASE+1);
    d.Send(SCI_GOTOPOS,d.Send(SCI_POSITIONFROMLINE,1));
    ExecuteStyleCommand(d.hwnd,d,IDM_EDIT_GOTO_BLOCK_START);
    if(d.Send(SCI_LINEFROMPOSITION,d.Send(SCI_GETCURRENTPOS))!=0)throw std::runtime_error("Fold start/previous actual line="+std::to_string(d.Send(SCI_LINEFROMPOSITION,d.Send(SCI_GETCURRENTPOS)))+" parent1="+std::to_string(d.Send(SCI_GETFOLDPARENT,1))+" level0="+std::to_string(d.Send(SCI_GETFOLDLEVEL,0)));
    ExecuteStyleCommand(d.hwnd,d,IDM_EDIT_GOTO_NEXT_SIBLING_BLOCK);
    if(d.Send(SCI_LINEFROMPOSITION,d.Send(SCI_GETCURRENTPOS))!=2)throw std::runtime_error("Fold next actual line="+std::to_string(d.Send(SCI_LINEFROMPOSITION,d.Send(SCI_GETCURRENTPOS))));
    ExecuteStyleCommand(d.hwnd,d,IDM_EDIT_GOTO_PREV_SIBLING_BLOCK);
    if(d.Send(SCI_LINEFROMPOSITION,d.Send(SCI_GETCURRENTPOS))!=0)throw std::runtime_error("Fold start/previous actual line="+std::to_string(d.Send(SCI_LINEFROMPOSITION,d.Send(SCI_GETCURRENTPOS)))+" parent1="+std::to_string(d.Send(SCI_GETFOLDPARENT,1))+" level0="+std::to_string(d.Send(SCI_GETFOLDLEVEL,0)));
    d.style.latex=true;d.Load({"\\alpha"});d.Send(SCI_GOTOPOS,d.Send(SCI_GETLENGTH));
    require(ExecuteStyleCommand(d.hwnd,d,CMD_TAB_COMPLETION));require(d.Text()=="α");
    d.style.latex=false;d.Load({"ret"});d.path=L"sample.cpp";ConfigureDocumentStyle(d);d.Send(SCI_GOTOPOS,3);
    ExecuteStyleCommand(d.hwnd,d,IDM_EDIT_COMPLETEWORD);require(d.Send(SCI_AUTOCACTIVE)!=0 || d.Text()=="return");d.Send(SCI_AUTOCCANCEL);
    d.Load({"root\n  child\n    nested\n  child2\n"});d.style.detectIndentation=true;DetectDocumentIndentation(d);
    require(!d.Send(SCI_GETUSETABS) && d.Send(SCI_GETINDENT)==2);
    d.Load({"root\n\tchild\n\t\tnested\n"});DetectDocumentIndentation(d);require(d.Send(SCI_GETUSETABS));
    d.style.completion=false;d.Load({"("});d.Send(SCI_GOTOPOS,1);
    SCNotification typed{};typed.nmhdr.code=SCN_CHARADDED;typed.ch='(';HandleStyleNotification(d,typed);
    require(d.Text()=="()" && d.Send(SCI_GETCURRENTPOS)==1);
    d.Send(SCI_ADDTEXT,1,reinterpret_cast<LPARAM>(")"));typed.ch=')';HandleStyleNotification(d,typed);
    require(d.Text()=="())" && d.Send(SCI_GETCURRENTPOS)==2);
    SelectDocumentScheme(d,L"html");d.Load({"<"});d.Send(SCI_GOTOPOS,1);typed.ch='<';HandleStyleNotification(d,typed);require(d.Text()=="<");
    d.Load({"<div>"});d.Send(SCI_GOTOPOS,5);typed.ch='>';HandleStyleNotification(d,typed);require(d.Text()=="<div></div>" && d.Send(SCI_GETCURRENTPOS)==5);
    SelectDocumentScheme(d,L"cpp");
    d.Load({"// ret"});d.Send(SCI_GOTOPOS,6);d.style.completionOptions[IDC_AUTO_COMPLETE_INSIDE_COMMONT]=0;
    ExecuteStyleCommand(d.hwnd,d,IDM_EDIT_COMPLETEWORD);require(!d.Send(SCI_AUTOCACTIVE) && d.Text()=="// ret");
    d.style.completionOptions[IDC_AUTO_COMPLETE_INSIDE_COMMONT]=1;
    d.style.completion=true;d.Load({"résultat\nré"});d.Send(SCI_GOTOPOS,d.Send(SCI_GETLENGTH));typed.ch=0xE9;HandleStyleNotification(d,typed);require(d.Send(SCI_AUTOCACTIVE));d.Send(SCI_AUTOCCANCEL);d.style.completion=false;
    d.Load({"1234 12"});d.Send(SCI_GOTOPOS,7);d.style.completionOptions[IDC_AUTOC_MIN_NUMBER_LENGTH]=0;ExecuteStyleCommand(d.hwnd,d,IDM_EDIT_COMPLETEWORD);require(!d.Send(SCI_AUTOCACTIVE) && d.Text()=="1234 12");d.style.completionOptions[IDC_AUTOC_MIN_NUMBER_LENGTH]=3;
    SelectDocumentScheme(d,L"cpp");d.Load({"#inc"});d.Send(SCI_GOTOPOS,4);ExecuteStyleCommand(d.hwnd,d,IDM_EDIT_COMPLETEWORD);require(d.Send(SCI_AUTOCACTIVE));d.Send(SCI_AUTOCSELECT,0,reinterpret_cast<LPARAM>("include"));d.Send(SCI_AUTOCCOMPLETE);require(d.Text()=="#include");
    d.Load({"object.method()\nmet"});ConfigureDocumentStyle(d);d.Send(SCI_GOTOPOS,d.Send(SCI_GETLENGTH));std::vector<std::string> completions;uint32_t ignored[8]{};
    AddDocumentCompletionWords(d,"met",false,0,500,ignored,[&](std::string word){completions.push_back(std::move(word));});require(std::find(completions.begin(),completions.end(),"method()")!=completions.end());
    d.Load({"fo"});d.Send(SCI_GOTOPOS,2);
    SCNotification selected{};selected.nmhdr.code=SCN_AUTOCSELECTION;selected.text="for ()";selected.position=0;selected.listCompletionMethod=SC_AC_TAB;
    HandleStyleNotification(d,selected);require(d.Text()=="for ()" && d.Send(SCI_GETCURRENTPOS)==5);
    d.style.codeFont=L"Consolas";d.style.codeFontSize=15;d.style.textFont=L"Segoe UI";d.style.textFontSize=12;
    SelectDocumentScheme(d,L"cpp");require(d.Send(SCI_STYLEGETSIZE,STYLE_DEFAULT)==15);
    SelectDocumentScheme(d,L"txt");require(d.Send(SCI_STYLEGETSIZE,STYLE_DEFAULT)==12);
    d.style.codeFont.clear();d.style.textFont.clear();d.style.scheme=0;d.path=L"scratch.cpp";ConfigureDocumentStyle(d);
    d.Load({"#abc"});d.style.colorCalltip=1;SCNotification hover{};hover.nmhdr.code=SCN_DWELLSTART;hover.position=2;HandleStyleNotification(d,hover);
    require((d.style.calltipColor&0xFFFFFF)==RGB(170,187,204));require(d.style.colorStart==1 && d.style.colorEnd==4);
    d.Send(SCI_CALLTIPCANCEL);
    d.Load({"0x11223344"});hover.position=5;d.style.colorCalltip=2;HandleStyleNotification(d,hover);
    require(d.style.calltipColor==(RGB(0x22,0x33,0x44)|(0x11u<<24)));d.Send(SCI_CALLTIPCANCEL);d.style.colorCalltip=0;
    d.style.completion=false;d.style.autoIndent=true;d.Load({"    test\n"});d.Send(SCI_GOTOPOS,d.Send(SCI_GETLENGTH));
    SCNotification newline{};newline.nmhdr.code=SCN_CHARADDED;newline.ch='\n';HandleStyleNotification(d,newline);
    require(d.Send(SCI_GETLINEINDENTATION,1)==4);
    require(d.Text().starts_with("    test\n"));
    // Independent occurrence options must not behave like mutually exclusive modes.
    d.style.markOccurrences=1;d.style.completionOptions[IDM_VIEW_MARKOCCURRENCES_CASE]=1;d.style.completionOptions[IDM_VIEW_MARKOCCURRENCES_WORD]=1;
    d.Load({"alpha ALPHA alphabet alpha"});d.Send(SCI_SETSEL,0,5);
    SCNotification update{};update.nmhdr.code=SCN_UPDATEUI;HandleStyleNotification(d,update);
    require(d.style.occurrencePending);
    ProcessStyleBackground(d);
    require(d.Send(SCI_INDICATORVALUEAT,8,0)!=0);
    require(d.Send(SCI_INDICATORVALUEAT,8,6)==0);
    require(d.Send(SCI_INDICATORVALUEAT,8,12)==0);
    require(d.Send(SCI_INDICATORVALUEAT,8,21)!=0);
    std::string large(2*1024*1024,' ');large.replace(large.size()-5,5,"alpha");large.replace(0,5,"alpha");d.Load({large});d.Send(SCI_SETSEL,0,5);HandleStyleNotification(d,update);
    for(unsigned step=0;step<1000&&d.style.occurrencePending;++step)ProcessStyleBackground(d);
    require(!d.style.occurrencePending && d.style.occurrences==2);require(d.Send(SCI_INDICATORVALUEAT,8,large.size()-5)!=0);
    ExecuteStyleCommand(d.hwnd,d,IDM_VIEW_MARKOCCURRENCES_OFF);HandleStyleNotification(d,update);
    require(d.Send(SCI_INDICATORVALUEAT,8,0)==0);
    d.Load({"ab    "});d.Send(SCI_SETINDENT,4);d.Send(SCI_SETBACKSPACEUNINDENTS,2);d.Send(SCI_GOTOPOS,6);d.Send(SCI_DELETEBACK);require(d.Text()=="ab  ");
    d.Load({"ab    "});d.Send(SCI_SETBACKSPACEUNINDENTS,1);d.Send(SCI_GOTOPOS,6);d.Send(SCI_DELETEBACK);require(d.Text()=="ab   ");
    ExecuteStyleCommand(d.hwnd,d,IDM_VIEW_HIGHLIGHTCURRENTLINE_NONE);require(StyleCommandState(&d,IDM_VIEW_HIGHLIGHTCURRENTLINE_NONE).checked && !d.Send(SCI_GETELEMENTISSET,SC_ELEMENT_CARET_LINE_BACK));
    ExecuteStyleCommand(d.hwnd,d,IDM_VIEW_HIGHLIGHTCURRENTLINE_FRAME);d.Send(SCI_SETZOOM,100);auto frame100=d.Send(SCI_GETCARETLINEFRAME);d.Send(SCI_SETZOOM,200);require(frame100>0 && d.Send(SCI_GETCARETLINEFRAME)>frame100);
    ExecuteStyleCommand(d.hwnd,d,IDM_VIEW_HIGHLIGHTCURRENTLINE_BACK);require(d.Send(SCI_GETCARETLINEFRAME)==0 && StyleCommandState(&d,IDM_VIEW_HIGHLIGHTCURRENTLINE_BACK).checked);d.Send(SCI_SETZOOM,100);
    auto preferences=std::filesystem::temp_directory_path()/(L"MultiPad4-style-profile-"+std::to_wstring(GetCurrentProcessId()))/L"Nested"/L"Settings.ini";
    std::error_code cleanup;std::filesystem::remove_all(preferences.parent_path().parent_path(),cleanup);
    SaveStylePreferences(preferences);
    {std::ifstream input(preferences,std::ios::binary);char bom[2]{};input.read(bom,2);require(static_cast<unsigned char>(bom[0])==255&&static_cast<unsigned char>(bom[1])==254);}
    WritePrivateProfileStringW(L"Test",L"Font",L"中文字体",preferences.c_str());wchar_t fontName[32]{};GetPrivateProfileStringW(L"Test",L"Font",L"",fontName,32,preferences.c_str());require(wcscmp(fontName,L"中文字体")==0);
    std::filesystem::remove_all(preferences.parent_path().parent_path(),cleanup);
    d.Load({""});d.path.clear();d.style={};ConfigureDocumentStyle(d);d.Send(SCI_SETSAVEPOINT);
}

