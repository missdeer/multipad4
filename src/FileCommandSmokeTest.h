#pragma once
#include "CommandLine.h"
#include "FileCommands.h"
#include "FileDialogs.h"
#include "MainFrame.h"
#include <source_location>
inline void RunFileCommandSmokeTest(MainFrame& frame) {
    auto require=[](bool ok,std::source_location where=std::source_location::current()){if(!ok)throw std::runtime_error("File command/CLI regression failed at line "+std::to_string(where.line()));};
    auto parsed=ParseCommandLine(L"multipad4.exe /e windows-1252 /utf8bom /g 12,4 /ro /mr- \"alpha.*beta\" /rs /lf /s cpp + \"a b.txt\" c.txt");
    require(parsed.files.size()==2 && parsed.files[0]==L"a b.txt" && parsed.files[1]==L"c.txt");
    require(parsed.sourceEncoding==files::FromCodePage(1252) && parsed.targetEncoding==files::Encoding::Utf8Bom);
    require(parsed.line==12 && parsed.column==4 && parsed.readOnly && parsed.matchRegex && parsed.matchBackwards);
    require(parsed.reuseWindow==1 && parsed.singleFileInstance && parsed.eol==SC_EOL_LF && parsed.scheme==L"cpp");
    parsed=ParseCommandLine(L"multipad4.exe - file with spaces.txt");require(parsed.files.size()==1 && parsed.files[0]==L"file with spaces.txt");
    parsed=ParseCommandLine(L"multipad4.exe /p:10,20,640,480,1 /G 3,9,8 /n file.txt");
    require(parsed.position && parsed.position->right==650 && parsed.position->bottom==500 && parsed.maximize && parsed.visualColumn && parsed.tabWidth==8);
    parsed=ParseCommandLine(L"multipad4.exe /utf-16-be-signature + -leading.txt /ro");
    require(parsed.targetEncoding==files::Encoding::Utf16BE && parsed.files.size()==2 && parsed.files[0]==L"-leading.txt" && parsed.files[1]==L"/ro" && !parsed.readOnly);
    parsed=ParseCommandLine(L"multipad4.exe one file with spaces.txt");require(parsed.files.size()==1 && parsed.files[0]==L"one file with spaces.txt");
    parsed=ParseCommandLine(L"multipad4.exe /unknown option.txt");require(parsed.files.size()==1 && parsed.files[0]==L"/unknown option.txt");
    parsed=ParseCommandLine(L"multipad4.exe /z notepad.exe /pt Printer file.txt");require(parsed.notepadReplacement && parsed.print && parsed.printDialog && parsed.files.size()==1 && parsed.files[0]==L"file.txt");
    parsed=ParseCommandLine(L"multipad4.exe /pt file.txt");require(!parsed.print && parsed.positionFlags==L"t" && parsed.files[0]==L"file.txt");
    parsed=ParseCommandLine(L"multipad4.exe /p:1,2,-1,0 file.txt");require(parsed.position && parsed.position->right==CW_USEDEFAULT && parsed.position->bottom==CW_USEDEFAULT);
    parsed=ParseCommandLine(L"multipad4.exe /n /session \"C:\\session folder\\view.ini\" + file.txt");require(parsed.sessionFile==L"C:\\session folder\\view.ini"&&parsed.reuseWindow==0&&parsed.files.size()==1&&parsed.files[0]==L"file.txt");
    auto* original=frame.Active();auto originalOptions=original?original->Send(SCI_GETDOCUMENTOPTIONS):0;frame.NewDocument();auto* d=frame.Active();d->Load({"# tab-width: 8; c-basic-offset: 2; indent-tabs-mode: nil\nline   \r\n"});
    const auto initialTab=d->Send(SCI_GETTABWIDTH);
    ApplyFileVariables(*d);require(d->Send(SCI_GETTABWIDTH)==8 && d->Send(SCI_GETINDENT)==2 && !d->Send(SCI_GETUSETABS));
    d->Load({std::string(1024,' ') + "\nLocal Variables:\ntab-width: 7\nc-basic-offset: 3\ntruncate-lines: nil\nfill-column: 91\nEnd:\n"});
    ApplyFileVariables(*d);require(d->Send(SCI_GETTABWIDTH)==7 && d->Send(SCI_GETINDENT)==3 && d->Send(SCI_GETWRAPMODE)==d->style.wrapMode && d->Send(SCI_GETEDGECOLUMN)==91);
    d->Load({"enable-local-variables: nil; tab-width: 2"});ApplyFileVariables(*d);require(d->Send(SCI_GETTABWIDTH)==initialTab);
    d->Load({"line   \r\n"});
    frame.settings.WriteString(L"File Settings",L"StripTrailingBlanks",L"1");PrepareFileSave(frame,*d);require(d->Text().find("line\r\n")!=std::string::npos);
    frame.settings.WriteString(L"File Settings",L"StripTrailingBlanks",L"0");
    auto before=d->Text();ExecuteFileCommand(frame,IDM_FILE_LARGE_FILE_MODE);
    require((d->Send(SCI_GETDOCUMENTOPTIONS)&SC_DOCUMENTOPTION_TEXT_LARGE)!=0 && d->Text()==before);
    require(original==nullptr || original->Send(SCI_GETDOCUMENTOPTIONS)==originalOptions);
    d->Send(SCI_SETSAVEPOINT);d->encodingDirty=false;require(frame.Close(frame.tabs.GetActivePage()));
    auto* commandDocument=frame.NewDocument();require(commandDocument!=nullptr);
    auto documentOptions=ParseCommandLine(L"multipad4.exe /utf16be /lf /s json");
    ApplyCommandLine(frame,documentOptions);
    require(frame.Active()==commandDocument && commandDocument->encoding==files::Encoding::Utf16BE && commandDocument->Send(SCI_GETEOLMODE)==SC_EOL_LF);
    commandDocument->Load({"\t中文value\nlast"});
    documentOptions=ParseCommandLine(L"multipad4.exe /g 0,2");ApplyCommandLine(frame,documentOptions);require(commandDocument->Send(SCI_GETCURRENTPOS)==1);
    documentOptions=ParseCommandLine(L"multipad4.exe /G 1,-3,8");ApplyCommandLine(frame,documentOptions);require(commandDocument->Send(SCI_GETCURRENTPOS)==4);
    documentOptions=ParseCommandLine(L"multipad4.exe /g 9999,1");ApplyCommandLine(frame,documentOptions);require(commandDocument->Send(SCI_GETCURRENTPOS)==commandDocument->Send(SCI_GETLENGTH));
    commandDocument->Load({"alpha\nalpha\nalpha"});commandDocument->Send(SCI_GOTOPOS,8);
    documentOptions=ParseCommandLine(L"multipad4.exe /m alpha");ApplyCommandLine(frame,documentOptions);require(commandDocument->Send(SCI_GETSELECTIONSTART)==0);
    documentOptions=ParseCommandLine(L"multipad4.exe /g 2,1 /m- alpha");ApplyCommandLine(frame,documentOptions);require(commandDocument->Send(SCI_GETSELECTIONSTART)==0);
    int recentSetting=frame.settings.Get(IDM_VIEW_NOSAVERECENT,0), systemRecentSetting=frame.settings.Get(60050,1);documentOptions=ParseCommandLine(L"multipad4.exe /sysmru=0");ApplyCommandLine(frame,documentOptions);require(frame.runtime.useSystemMRU==0 && frame.settings.Get(60050,1)==systemRecentSetting && frame.settings.Get(IDM_VIEW_NOSAVERECENT,0)==recentSetting);
    commandDocument->encodingDirty=false;commandDocument->Send(SCI_SETSAVEPOINT);require(frame.Close(frame.tabs.GetActivePage()));
    auto directory=std::filesystem::temp_directory_path()/(L"MultiPad4-cli-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    auto path=directory/L"日本.txt";
    try {
        files::WriteAtomic(path,{"café\nsecond\nthird",files::FromCodePage(1252)});
        auto oldBrowser=frame.settings.ReadString(L"Tools",L"filebrowser.exe");
        auto oldFolderSetting=frame.settings.Get(IDM_SET_OPEN_FOLDER_MATEPATH,1);
        frame.settings.WriteString(L"Tools",L"filebrowser.exe",L"\""+(directory/L"missing file-browser.exe").wstring()+L"\" /test-argument");
        frame.settings.Set(IDM_SET_OPEN_FOLDER_MATEPATH,1);
        auto folderPlan=BuildFolderOpenPlan(frame,directory,FolderOpenRequest::OpenFolder);
        require(folderPlan.useMatepath&&!folderPlan.warnIfUnavailable&&!folderPlan.pluginAvailable&&folderPlan.folder==directory);
        require(folderPlan.command.find(L"/test-argument")!=std::wstring::npos && folderPlan.command.find(directory.wstring())!=std::wstring::npos);
        auto containingPlan=BuildFolderOpenPlan(frame,path,FolderOpenRequest::ContainingFolder);
        require(!containingPlan.useMatepath&&containingPlan.selectTarget&&containingPlan.folder==directory&&containingPlan.target==path);
        frame.settings.Set(IDM_SET_OPEN_FOLDER_MATEPATH,0);
        folderPlan=BuildFolderOpenPlan(frame,directory,FolderOpenRequest::OpenFolder);require(!folderPlan.useMatepath&&!folderPlan.selectTarget&&folderPlan.folder==directory);
        auto browsePlan=BuildFolderOpenPlan(frame,path,FolderOpenRequest::Browse);require(browsePlan.useMatepath&&browsePlan.warnIfUnavailable&&!browsePlan.pluginAvailable);
        auto rootPlan=BuildFolderOpenPlan(frame,path.root_path(),FolderOpenRequest::ContainingFolder);require(!rootPlan.selectTarget&&rootPlan.folder==path.root_path());
        frame.settings.WriteString(L"Tools",L"filebrowser.exe",oldBrowser);frame.settings.Set(IDM_SET_OPEN_FOLDER_MATEPATH,oldFolderSetting);
        const int oldDefaultEncoding=frame.settings.Get(IDM_ENCODING_SETDEFAULT,static_cast<int>(files::Encoding::Ansi));
        frame.settings.Set(IDM_ENCODING_SETDEFAULT,static_cast<int>(files::Encoding::Utf8));
        frame.settings.WriteString(L"File Settings",L"ANSIAsUTF8",L"1");
        if(files::CodePage(files::Encoding::Ansi)!=CP_UTF8){auto legacyPath=directory/L"legacy.txt";auto legacy=files::Decode(std::string("\xA1\xA1"),files::Encoding::Ansi);files::WriteAtomic(legacyPath,legacy);require(ReadFileForFrame(frame,legacyPath).encoding==files::Encoding::Utf8);}
        frame.settings.Set(IDM_ENCODING_SETDEFAULT,oldDefaultEncoding);frame.settings.WriteString(L"File Settings",L"ANSIAsUTF8",L"0");
        auto oldAsciiMode=frame.settings.ReadString(L"File Settings",L"ASCIIAsUTF8",L"1");
        frame.settings.Set(IDM_ENCODING_SETDEFAULT,static_cast<int>(files::Encoding::Utf16LE));frame.settings.WriteString(L"File Settings",L"ASCIIAsUTF8",L"0");
        auto plain=directory/L"plain.txt";files::WriteAtomic(plain,{"plain ASCII"});require(ReadFileForFrame(frame,plain).encoding==files::Encoding::Ansi);
        auto batch=directory/L"script.cmd";files::WriteAtomic(batch,{"echo ASCII"});require(ReadFileForFrame(frame,batch).encoding==files::Encoding::Ansi);
        auto vhdl=directory/L"logic.vhd";files::WriteAtomic(vhdl,{"entity ASCII"});require(ReadFileForFrame(frame,vhdl).encoding==files::FromCodePage(1252));
        auto shell=directory/L"script.sh";files::WriteAtomic(shell,{"echo ASCII"});require(ReadFileForFrame(frame,shell).encoding==files::Encoding::Utf8);
        files::WriteAtomic(plain,{"# coding: utf-8\nASCII"});require(ReadFileForFrame(frame,plain).encoding==files::Encoding::Utf8);
        frame.settings.Set(IDM_ENCODING_SETDEFAULT,oldDefaultEncoding);frame.settings.WriteString(L"File Settings",L"ASCIIAsUTF8",oldAsciiMode);
        auto nfo=directory/L"art.nfo";files::WriteAtomic(nfo,{"Unicode BOM 中文",files::Encoding::Utf16LE});
        frame.settings.WriteString(L"File Settings",L"NFOAsOEM",L"1");require(ReadFileForFrame(frame,nfo).encoding==files::Encoding::Utf16LE);frame.settings.WriteString(L"File Settings",L"NFOAsOEM",L"0");
        ParsedCommandLine command;command.files.push_back(path);command.sourceEncoding=files::FromCodePage(1252);command.line=2;command.column=2;command.readOnly=true;
        auto count=frame.tabs.GetPageCount();ApplyCommandLine(frame,command);auto* opened=frame.Active();
        require(opened && opened->Text()=="café\nsecond\nthird" && opened->Send(SCI_GETREADONLY));
        require(opened->Send(SCI_GETCURRENTPOS)==opened->Send(SCI_POSITIONFROMLINE,1)+1 && frame.tabs.GetPageCount()==count+1);
        auto backup=SaveDocumentBackup(frame,*opened);
        require(backup.parent_path()==directory && backup.filename().wstring().starts_with(L"日本.txt "));
        require(files::Read(backup).utf8==opened->Text() && opened->encoding==files::FromCodePage(1252) && !opened->Modified());
        ApplyCommandLine(frame,command);require(frame.Active()==opened && frame.tabs.GetPageCount()==count+1);
        auto* reference=frame.NewDocument();require(reference!=nullptr);reference->path=directory/L"reference.txt";
        auto referenceText=files::ToUtf8(path.wstring()+L":2:2");reference->Load({referenceText});reference->Send(SCI_GOTOPOS,referenceText.size()/2);
        auto referenceIndex=frame.tabs.GetActivePage();
        ExecuteFileCommand(frame,CMD_OPEN_PATH_OR_LINK);
        require(frame.Active()==opened && opened->Send(SCI_GETCURRENTPOS)==opened->Send(SCI_POSITIONFROMLINE,1)+1);
        require(frame.Close(referenceIndex));
        require(frame.Open(path));
        require(frame.Close(frame.tabs.GetActivePage()));
        auto variablesPath=directory/L"variables.cpp";
        files::WriteAtomic(variablesPath,{"// tab-width: 9; mode: python; truncate-lines: nil\nint value;\n"});
        require(frame.Open(variablesPath));auto* variableDocument=frame.Active();
        require(variableDocument->Send(SCI_GETTABWIDTH)==9 && variableDocument->fileVariables.active);
        auto baselineTab=variableDocument->fileVariables.baseline.tabWidth;
        ExecuteFileCommand(frame,CMD_RELOADNOFILEVARS);
        require(!variableDocument->fileVariables.active && variableDocument->Send(SCI_GETTABWIDTH)==baselineTab);
        variableDocument->Send(SCI_SETSEL,2,7);ExecuteFileCommand(frame,CMD_RELOADUTF8);
        require(variableDocument->Send(SCI_GETANCHOR)==2&&variableDocument->Send(SCI_GETCURRENTPOS)==7);
        require(frame.Close(frame.tabs.GetActivePage()));std::filesystem::remove_all(directory);
    }catch(...){std::error_code error;std::filesystem::remove_all(directory,error);throw;}
}
