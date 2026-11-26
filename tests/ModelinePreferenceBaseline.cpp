// Frozen e22d140 FileCommands::ApplyFileVariables and FrameCommands::RememberDocumentPreferences.
// Test-only oracle; parsing and document-scoped types remain the existing shared implementation.
#include "MainFrame.h"
#include "FileCommands.h"
#include "FrameCommands.h"
#include "StyleManager.h"
#include "FileVariables.h"
#include "resource.h"
void BaselineApplyFileVariables(EditorDocument& document){
    if (document.FastView()) return;
    RestoreFileVariables(document);
    auto sample = document.Range(0, 4096);
    auto length = static_cast<size_t>(document.Send(SCI_GETLENGTH));
    if (length > 4096) sample += document.Range((std::max)(size_t(4096), length - (std::min)(length, size_t(4096))), 4096);
    auto region=filevars::Region(sample);int value=0;
    if(filevars::Integer(region,"enable-local-variables",value)&&!value)return;
    auto& baseline=document.fileVariables;baseline.baseline=document.style;baseline.baselineWrap=static_cast<int>(document.Send(SCI_GETWRAPMODE));baseline.baselineEdge=static_cast<int>(document.Send(SCI_GETEDGECOLUMN));
    auto mode=filevars::String(region,"mode");if(!mode.empty())SelectDocumentScheme(document,files::FromUtf8(mode));
    int tab=0,indent=0;
    const bool hasTab=filevars::Integer(region,"tab-width",tab);
    const bool hasIndent=filevars::Integer(region,"*basic-indent",indent)||filevars::Integer(region,"*basic-offset",indent);
    if(hasTab||hasIndent){if(!hasTab)tab=indent;if(!hasIndent)indent=tab;tab=(std::clamp)(tab,1,256);indent=(std::clamp)(indent,0,256);
        document.style.tabWidth=tab;document.style.indentWidth=indent;document.style.useSchemeTabs=false;document.Send(SCI_SETTABWIDTH,tab);document.Send(SCI_SETINDENT,indent);}
    if(filevars::Integer(region,"indent-tabs-mode",value)){document.style.tabsAsSpaces=!value;document.style.useSchemeTabs=false;document.Send(SCI_SETUSETABS,value!=0);}
    if(filevars::Integer(region,"*tab-always-indent",value)){document.style.tabIndents=value!=0;document.Send(SCI_SETTABINDENTS,value!=0);}
    if(filevars::Integer(region,"truncate-lines",value))document.Send(SCI_SETWRAPMODE,value?SC_WRAP_NONE:document.style.wrapMode);
    if(filevars::Integer(region,"fill-column",value))document.Send(SCI_SETEDGECOLUMN,(std::clamp)(value,0,1000000));
    ApplyDocumentWrapSettings(document);
    if (document.LargeFile()) document.Send(SCI_SETWRAPMODE, SC_WRAP_NONE);
    baseline.applied=document.style;baseline.appliedWrap=static_cast<int>(document.Send(SCI_GETWRAPMODE));baseline.appliedEdge=static_cast<int>(document.Send(SCI_GETEDGECOLUMN));baseline.active=filevars::Recognized(region);
}

void BaselineRememberDocumentPreferences(MainFrame& frame, EditorDocument& d, UINT command) {
    if (command == IDM_VIEW_TABSASSPACES) {
        d.style.tabsAsSpaces = !d.Send(SCI_GETUSETABS);
        d.style.tabsOverride = true;
    }
    const auto previousTabs = frame.defaultStyle;
    frame.defaultStyle = d.style;
    // A file's effective column (including fill-column) is not a new-file default.
    frame.defaultStyle.longLineColumn = previousTabs.longLineColumn;
    frame.defaultStyle.longLineMode = previousTabs.longLineMode;
    frame.defaultStyle.longLineEnabled = previousTabs.longLineEnabled;
    if (command == IDM_VIEW_LONGLINESETTINGS) {
        frame.defaultStyle.longLineColumn = static_cast<int>(d.Send(SCI_GETEDGECOLUMN));
        frame.defaultStyle.longLineMode = d.edgeMode;
    }
    if (command == IDM_VIEW_LONGLINEMARKER || command == IDM_VIEW_LONGLINESETTINGS)
        frame.defaultStyle.longLineEnabled = d.Send(SCI_GETEDGEMODE) != EDGE_NONE;
    // Widths and spaces are resolved from the shared global/scheme settings
    // when a document is created. Never promote a file's effective values.
    frame.defaultStyle.tabWidth = 4; frame.defaultStyle.indentWidth = 4;
    frame.defaultStyle.tabsAsSpaces = false;
    frame.defaultStyle.useSchemeTabs = true; frame.defaultStyle.tabsOverride = false;
    if (command != IDM_VIEW_TABSETTINGS) {
        frame.defaultStyle.tabIndents = previousTabs.tabIndents;
        frame.defaultStyle.backspaceMask = previousTabs.backspaceMask;
        frame.defaultStyle.detectIndentation = previousTabs.detectIndentation;
    }
    frame.defaultStyle.backspaceUnindents = (frame.defaultStyle.backspaceMask & 1) != 0;
    frame.defaultStyle.scheme = 0; frame.defaultStyle.dialect = 0; frame.defaultStyle.appliedTabScheme = -1;
    frame.defaultStyle.overrides.clear();
    frame.defaultStyle.autoClosers.clear();
    frame.defaultStyle.colorStart = frame.defaultStyle.colorEnd = -1;
    frame.defaultStyle.calltipHex.clear(); frame.defaultStyle.calltipColor = 0;
    frame.defaultStyle.resolvedDialect = 0;
    frame.defaultStyle.resolvedScheme = 0;
    frame.defaultStyle.revision = frame.defaultStyle.occurrenceRevision = 0;
    frame.defaultStyle.occurrenceNeedle.clear(); frame.defaultStyle.occurrenceCursor = 0;
    frame.defaultStyle.occurrences = 0; frame.defaultStyle.occurrenceFlags = 0; frame.defaultStyle.occurrencePending = false;
    auto writeStyle = [&](const wchar_t* key, int value) { frame.settings.WriteString(L"Editor Defaults", key, std::to_wstring(value)); };
    const auto& style = frame.defaultStyle;
    frame.settings.WriteString(L"Editor Defaults", L"Font", style.font);
    frame.settings.WriteString(L"Editor Defaults", L"CodeFont", style.codeFont);
    frame.settings.WriteString(L"Editor Defaults", L"TextFont", style.textFont);
    writeStyle(L"CodeFontSize", style.codeFontSize); writeStyle(L"TextFontSize", style.textFontSize);
    writeStyle(L"Dark", style.dark); writeStyle(L"FontSize", style.fontSize); writeStyle(L"FontKind", style.fontKind);
    writeStyle(L"Completion", style.completion); writeStyle(L"IgnoreCase", style.completionIgnoreCase);
    writeStyle(L"AutoIndent", style.autoIndent); writeStyle(L"LaTeX", style.latex);
    writeStyle(L"Folding", style.folding); writeStyle(L"MatchBraces", style.matchBraces);
    writeStyle(L"MarkOccurrences", style.markOccurrences); writeStyle(L"ColorCalltip", style.colorCalltip);
    writeStyle(L"HighlightBlock", style.highlightBlock);
    writeStyle(L"LineHighlight", style.lineHighlight); writeStyle(L"HighlightSubline", style.highlightSubline);
    writeStyle(L"BackspaceMask", style.backspaceMask); writeStyle(L"TabIndents", style.tabIndents); writeStyle(L"DetectIndentation", style.detectIndentation);
    if (command == IDM_VIEW_TABSETTINGS) {
        for (auto& other : frame.documents) {
            // Upstream has a per-file TabIndents value (including modelines),
            // while BackspaceMask and DetectIndentation are always global.
            int value = 0;
            const bool fileTabIndents = other.get() != &d && other->fileVariables.active
                && filevars::Integer(filevars::Region(other->Text()), "*tab-always-indent", value)
                && other->style.tabIndents == other->fileVariables.applied.tabIndents;
            if (!fileTabIndents) other->style.tabIndents = style.tabIndents;
            if (other->fileVariables.active) other->fileVariables.baseline.tabIndents = style.tabIndents;
            other->style.backspaceMask = style.backspaceMask;
            other->style.backspaceUnindents = style.backspaceUnindents;
            other->style.detectIndentation = style.detectIndentation;
            other->Send(SCI_SETTABINDENTS, other->style.tabIndents);
            other->Send(SCI_SETBACKSPACEUNINDENTS, style.backspaceMask & 3);
        }
    }
    writeStyle(L"WrapMode", style.wrapMode); writeStyle(L"WrapIndent", style.wrapIndent); writeStyle(L"WrapSymbols", style.wrapSymbols); writeStyle(L"WrapSymbolsShown", style.wrapSymbolsShown);
    writeStyle(L"CsvOptions", style.csvOptions); writeStyle(L"CsvConfigured", style.csvConfigured);
    frame.settings.WriteString(L"Editor Defaults", L"CompletionFillups", style.completionFillups);
    for (const auto& [id, value] : style.completionOptions) writeStyle((L"Completion" + std::to_wstring(id)).c_str(), value);
    if (command == IDM_VIEW_LINENUMBERS) writeStyle(L"LineNumbers", d.Send(SCI_GETMARGINWIDTHN, 0) != 0);
    if (command == IDM_VIEW_MARGIN) writeStyle(L"BookmarkMargin", d.Send(SCI_GETMARGINWIDTHN, 1) != 0);
    if (command == IDM_VIEW_LONGLINEMARKER || command == IDM_VIEW_LONGLINESETTINGS) {
        writeStyle(L"EdgeVisible", style.longLineEnabled); writeStyle(L"EdgeColumn", style.longLineColumn); writeStyle(L"EdgeMode", style.longLineMode);
        for (auto& other : frame.documents) {
            int value = 0;
            const bool fileColumn = other->fileVariables.active
                && filevars::Integer(filevars::Region(other->Text()), "fill-column", value)
                && other->Send(SCI_GETEDGECOLUMN) == other->fileVariables.appliedEdge;
            if (command == IDM_VIEW_LONGLINESETTINGS && other.get() != &d) {
                // Preserve an explicit file value; ordinary peers follow the global limit.
                if (!fileColumn && other->Send(SCI_GETEDGECOLUMN) == previousTabs.longLineColumn)
                    other->Send(SCI_SETEDGECOLUMN, style.longLineColumn);
                if (other->style.longLineColumn == previousTabs.longLineColumn)
                    other->style.longLineColumn = style.longLineColumn;
            }
            if (other->fileVariables.active) {
                if (other->fileVariables.baselineEdge == previousTabs.longLineColumn)
                    other->fileVariables.baselineEdge = style.longLineColumn;
                if (other->fileVariables.baseline.longLineColumn == previousTabs.longLineColumn)
                    other->fileVariables.baseline.longLineColumn = style.longLineColumn;
            }
            other->edgeMode = style.longLineMode;
            other->style.longLineMode = style.longLineMode;
            other->style.longLineEnabled = style.longLineEnabled;
            other->Send(SCI_SETEDGEMODE, style.longLineEnabled ? style.longLineMode : EDGE_NONE);
        }
    }
    if (command == IDM_VIEW_ZOOMIN || command == IDM_VIEW_ZOOMOUT || command == IDM_VIEW_ZOOM_LEVEL || command == IDM_VIEW_RESETZOOM) writeStyle(L"Zoom", static_cast<int>(d.Send(SCI_GETZOOM)));
    if (command == IDM_VIEW_WORDWRAPSETTINGS) frame.settings.Set(IDM_VIEW_WORDWRAP, static_cast<int>(d.Send(SCI_GETWRAPMODE)));
    if (command == IDM_VIEW_STYLE_THEME_DEFAULT || command == IDM_VIEW_STYLE_THEME_DARK) {
        for (auto& other : frame.documents) { other->style.dark = style.dark; ConfigureDocumentStyle(*other); }
    }
    if (command == IDM_VIEW_AUTOCOMPLETION_SETTINGS || command == IDM_VIEW_AUTOCOMPLETION_IGNORECASE || command == IDM_SET_LATEX_INPUT_METHOD) {
        for (auto& other : frame.documents) {
            other->style.completion = style.completion; other->style.completionIgnoreCase = style.completionIgnoreCase;
            other->style.autoIndent = style.autoIndent; other->style.latex = style.latex;
            other->style.completionFillups = style.completionFillups; other->style.completionOptions = style.completionOptions;
            ConfigureDocumentStyle(*other);
        }
    }
    if (command == IDM_VIEW_DEFAULT_CODE_FONT || command == IDM_VIEW_DEFAULT_TEXT_FONT) {
        for (auto& other : frame.documents) {
            other->style.font = style.font; other->style.fontSize = style.fontSize;
            other->style.codeFont = style.codeFont; other->style.textFont = style.textFont;
            other->style.codeFontSize = style.codeFontSize; other->style.textFontSize = style.textFontSize;
            ConfigureDocumentStyle(*other);
        }
    }
    UINT getter = 0;
    switch (command) {
    case IDM_VIEW_WORDWRAP: getter = SCI_GETWRAPMODE; break;
    case IDM_VIEW_SHOWWHITESPACE: getter = SCI_GETVIEWWS; break;
    case IDM_VIEW_SHOWEOLS: getter = SCI_GETVIEWEOL; break;
    case IDM_VIEW_SHOWINDENTGUIDES: getter = SCI_GETINDENTATIONGUIDES; break;
    case IDM_VIEW_WORDWRAPSYMBOLS: getter = SCI_GETWRAPVISUALFLAGS; break;
    case IDM_SET_MULTIPLE_SELECTION: getter = SCI_GETMULTIPLESELECTION; break;
    }
    if (getter) frame.settings.Set(command, static_cast<int>(d.Send(getter)));
}
