#include "Session.h"
#include "LanguageConfiguration.h"
#include "ConfigurationXml.h"
#include "MainFrame.h"
#include "FrameCommands.h"
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <cerrno>
#include <climits>
namespace {
constexpr wchar_t SessionOwner[] = L"MultiPad4.MDI";
long long Number(const AppSettings& profile, const std::wstring& section, const wchar_t* key, long long fallback) {
    auto text = profile.ReadString(section.c_str(), key, std::to_wstring(fallback).c_str());
    errno = 0;
    wchar_t* end = nullptr; auto value = wcstoll(text.c_str(), &end, 10);
    return errno != ERANGE && end != text.c_str() && !*end ? value : fallback;
}
// Persist user configuration, never transient lexer caches or HWND-bound state.
#define SESSION_STYLE_NUMBERS(X) \
    X(scheme) X(dialect) X(dark) X(folding) X(matchBraces) X(highlightBlock) \
    X(completion) X(completionIgnoreCase) X(autoIndent) X(latex) X(colorCalltip) X(fontKind) \
    X(csvConfigured) X(csvOptions) X(tabWidth) X(indentWidth) X(tabsAsSpaces) X(useSchemeTabs) \
    X(tabsOverride) X(tabIndents) X(backspaceUnindents) X(detectIndentation) X(backspaceMask) \
    X(wrapMode) X(wrapIndent) X(wrapSymbols) X(wrapSymbolsShown) X(lineHighlight) X(highlightSubline) \
    X(longLineColumn) X(longLineMode) X(longLineEnabled) X(markOccurrences) X(fontSize) X(codeFontSize) X(textFontSize)
#define SESSION_STYLE_STRINGS(X) X(font) X(codeFont) X(textFont) X(completionFillups) X(themePath)
#define SESSION_WIDEN_IMPL(x) L##x
#define SESSION_WIDEN(x) SESSION_WIDEN_IMPL(x)
void SaveStyle(AppSettings& profile, const std::wstring& section, const DocumentStyleState& style) {
#define WRITE_NUMBER(field) profile.WriteString(section.c_str(), L"Style." SESSION_WIDEN(#field), std::to_wstring(style.field));
    SESSION_STYLE_NUMBERS(WRITE_NUMBER)
#undef WRITE_NUMBER
#define WRITE_STRING(field) profile.WriteString(section.c_str(), L"Style." SESSION_WIDEN(#field), style.field);
    SESSION_STYLE_STRINGS(WRITE_STRING)
#undef WRITE_STRING
    if(!style.themePath.empty()){auto relative=std::filesystem::path(style.themePath).lexically_relative(profile.path.parent_path());if(!relative.empty()&&*relative.begin()!=L"..")profile.WriteString(section.c_str(),L"Style.themePath",relative.generic_wstring());}
    std::wstring keys;
    profile.WriteString(section.c_str(),L"LanguageId",style.scheme>0&&style.scheme<90?ConfigurationWide(LanguageCatalog()[style.scheme]->id):L"auto");
    std::wstring variantId;
    for(auto language:LanguageCatalog())for(const auto& variant:language->variants)if(variant.command==style.dialect)variantId=ConfigurationWide(variant.id);
    profile.WriteString(section.c_str(),L"VariantId",variantId);

    for(const auto& [id,value]:style.semanticOverrides){keys+=ConfigurationWide(id)+L" ";profile.WriteString(section.c_str(),(L"Style.Semantic."+ConfigurationWide(id)).c_str(),value);}
    profile.WriteString(section.c_str(),L"Style.SemanticKeys",keys);keys.clear();
    for (const auto& [key, value] : style.overrides) {
        keys += std::to_wstring(key) + L" ";
        profile.WriteString(section.c_str(), (L"Style.Override." + std::to_wstring(key)).c_str(), value);
    }
    profile.WriteString(section.c_str(), L"Style.OverrideKeys", keys);
    keys.clear();
    for (const auto& [key, value] : style.completionOptions) keys += std::to_wstring(key) + L" " + std::to_wstring(value) + L" ";
    profile.WriteString(section.c_str(), L"Style.CompletionOptions", keys);
}
void RestoreStyle(const AppSettings& profile, const std::wstring& section, DocumentStyleState& style) {
#define READ_NUMBER(field) style.field = static_cast<decltype(style.field)>(std::clamp(Number(profile, section, L"Style." SESSION_WIDEN(#field), style.field), static_cast<long long>(INT_MIN), static_cast<long long>(INT_MAX)));
    SESSION_STYLE_NUMBERS(READ_NUMBER)
#undef READ_NUMBER
#define READ_STRING(field) style.field = profile.ReadString(section.c_str(), L"Style." SESSION_WIDEN(#field), style.field.c_str());
    SESSION_STYLE_STRINGS(READ_STRING)
#undef READ_STRING
    style.overrides.clear(); style.completionOptions.clear();
    std::wistringstream overrides(profile.ReadString(section.c_str(), L"Style.OverrideKeys")); unsigned key;
    while (overrides >> key){auto value=profile.ReadString(section.c_str(), (L"Style.Override." + std::to_wstring(key)).c_str());try{ValidateLegacyStyle(value);style.overrides[key]=std::move(value);}catch(const std::exception& error){RecordConfigurationDiagnostic(profile.path,error);}}
    std::wistringstream options(profile.ReadString(section.c_str(), L"Style.CompletionOptions")); int option, value;
    while (options >> option >> value) style.completionOptions[option] = value;
    auto stable=profile.ReadString(section.c_str(),L"LanguageId");
    if(!stable.empty())style.scheme=stable==L"auto"?0:LanguageIndex(ConfigurationUtf8(stable));
    auto variantId=profile.ReadString(section.c_str(),L"VariantId");
    if(!variantId.empty()){style.dialect=0;for(auto language:LanguageCatalog())for(const auto& variant:language->variants)if(ConfigurationWide(variant.id)==variantId)style.dialect=variant.command;}
    style.semanticOverrides.clear();std::wistringstream semantic(profile.ReadString(section.c_str(),L"Style.SemanticKeys"));std::wstring id;
    while(semantic>>id){auto value=profile.ReadString(section.c_str(),(L"Style.Semantic."+id).c_str());try{ValidateLegacyStyle(value);style.semanticOverrides[ConfigurationUtf8(id)]=std::move(value);}catch(const std::exception& error){RecordConfigurationDiagnostic(profile.path,error);}}
    for(const auto& [key,value]:style.overrides)for(auto language:LanguageCatalog())if(language->rid==(key>>8)){
        unsigned index=key&255;if(index<language->iStyleCount)for(const auto& category:language->categories)if(category.group==ConfigurationUtf8(language->Styles[index].id))style.semanticOverrides.try_emplace(category.id,value);
    }
    if(!style.themePath.empty())try{if(std::filesystem::path(style.themePath).is_relative())style.themePath=(profile.path.parent_path()/style.themePath).lexically_normal().wstring();style.theme=LoadTheme(style.themePath);}catch(const std::exception&){style.theme.reset();}
    style.appliedTabScheme = -1;
}
#undef SESSION_STYLE_NUMBERS
#undef SESSION_STYLE_STRINGS
#undef SESSION_WIDEN
#undef SESSION_WIDEN_IMPL
bool ValidEncoding(long long value) {
    return (value >= 0 && value <= static_cast<int>(files::Encoding::Oem)) ||
        (value >= static_cast<int>(files::Encoding::CodePageBase) && value <= static_cast<int>(files::Encoding::CodePageBase) + 65535 &&
            IsValidCodePage(static_cast<UINT>(value - static_cast<int>(files::Encoding::CodePageBase))));
}

}
void SaveRelaunchSession(MainFrame& frame, const std::filesystem::path& path, bool allDocuments, EditorDocument* active) {
    if (!active) active = frame.Active();
    if (frame.searchDialog.hwnd) frame.searchDialog.ReadFields();
    AppSettings profile; profile.Initialize(true); profile.path = std::filesystem::absolute(path); profile.persistent = true;
    profile.WriteString(L"Session", L"Owner", SessionOwner);
    profile.WriteString(L"Session", L"UntitledCount", std::to_wstring(frame.untitledCount));
    size_t count = 0, activeIndex = 0;
    for (int i = 0; i < frame.tabs.GetPageCount(); ++i) {
        auto* d = static_cast<EditorDocument*>(frame.tabs.GetPageData(i));
        if (!allDocuments && d != active) continue;
        if (d == active) activeIndex = count;
        auto section = L"Document" + std::to_wstring(count++);
        auto write = [&](const wchar_t* key, long long value) { profile.WriteString(section.c_str(), key, std::to_wstring(value)); };
        profile.WriteString(section.c_str(), L"Path", d->path.wstring()); profile.WriteString(section.c_str(), L"Untitled", d->untitled);
        // A user who declined save also declined any pending encoding/EOL edit.
        write(L"Encoding", d->encodingDirty && !d->path.empty() ? -1 : static_cast<int>(d->encoding));
        SaveStyle(profile, section, d->style);
        write(L"Eol", d->Modified() ? -1 : d->Send(SCI_GETEOLMODE));
        write(L"FastViewOffset", d->FastView() ? static_cast<long long>(d->fastView->Begin()) : 0);
        write(L"Caret", d->Send(SCI_GETCURRENTPOS)); write(L"Anchor", d->Send(SCI_GETANCHOR));
        write(L"XOffset", d->Send(SCI_GETXOFFSET));
        write(L"ViewWhitespace", d->Send(SCI_GETVIEWWS)); write(L"ViewEol", d->Send(SCI_GETVIEWEOL));
        write(L"IndentGuides", d->Send(SCI_GETINDENTATIONGUIDES)); write(L"VirtualSpace", d->Send(SCI_GETVIRTUALSPACEOPTIONS));
        write(L"MoveExtendsSelection", d->Send(SCI_GETMOVEEXTENDSSELECTION));
        write(L"SelectionMode", d->Send(SCI_GETSELECTIONMODE)); write(L"MainSelection", d->Send(SCI_GETMAINSELECTION));
        auto selections = d->Send(SCI_GETSELECTIONS); write(L"Selections", selections);
        for (LRESULT selection = 0; selection < selections; ++selection) {
            auto prefix = L"Selection" + std::to_wstring(selection);
            write((prefix + L".Caret").c_str(), d->Send(SCI_GETSELECTIONNCARET, selection));
            write((prefix + L".Anchor").c_str(), d->Send(SCI_GETSELECTIONNANCHOR, selection));
            write((prefix + L".CaretVirtual").c_str(), d->Send(SCI_GETSELECTIONNCARETVIRTUALSPACE, selection));
            write((prefix + L".AnchorVirtual").c_str(), d->Send(SCI_GETSELECTIONNANCHORVIRTUALSPACE, selection));
        }
        write(L"RectCaret", d->Send(SCI_GETRECTANGULARSELECTIONCARET)); write(L"RectAnchor", d->Send(SCI_GETRECTANGULARSELECTIONANCHOR));
        write(L"RectCaretVirtual", d->Send(SCI_GETRECTANGULARSELECTIONCARETVIRTUALSPACE)); write(L"RectAnchorVirtual", d->Send(SCI_GETRECTANGULARSELECTIONANCHORVIRTUALSPACE));
        std::wstring collapsed;
        for (LRESULT line = 0; line < d->Send(SCI_GETLINECOUNT); ++line)
            if ((d->Send(SCI_GETFOLDLEVEL, line) & SC_FOLDLEVELHEADERFLAG) && !d->Send(SCI_GETFOLDEXPANDED, line)) collapsed += std::to_wstring(line) + L" ";
        profile.WriteString(section.c_str(), L"CollapsedFolds", collapsed);
        write(L"FirstVisibleLine", d->Send(SCI_GETFIRSTVISIBLELINE)); write(L"Zoom", d->Send(SCI_GETZOOM));
        write(L"ReadOnly", d->Send(SCI_GETREADONLY)); write(L"Wrap", d->Send(SCI_GETWRAPMODE));
        write(L"WrapStartIndent", d->Send(SCI_GETWRAPSTARTINDENT)); write(L"WrapFlagLocation", d->Send(SCI_GETWRAPVISUALFLAGSLOCATION));
        write(L"MultipleSelection", d->Send(SCI_GETMULTIPLESELECTION)); write(L"AdditionalSelectionTyping", d->Send(SCI_GETADDITIONALSELECTIONTYPING));
        write(L"Overtype", d->Send(SCI_GETOVERTYPE));
        write(L"WrapIndent", d->Send(SCI_GETWRAPINDENTMODE)); write(L"WrapFlags", d->Send(SCI_GETWRAPVISUALFLAGS));
        write(L"LineNumbers", d->Send(SCI_GETMARGINWIDTHN, 0) != 0); write(L"BookmarkMargin", d->Send(SCI_GETMARGINWIDTHN, 1) != 0);
        write(L"EdgeVisible", d->Send(SCI_GETEDGEMODE) != EDGE_NONE); write(L"EdgeColumn", d->Send(SCI_GETEDGECOLUMN)); write(L"EdgeMode", d->edgeMode);
        write(L"Scheme", d->style.scheme); write(L"Dialect", d->style.dialect); write(L"Dark", d->style.dark); write(L"Folding", d->style.folding);
        write(L"TabWidth", d->Send(SCI_GETTABWIDTH)); write(L"IndentWidth", d->Send(SCI_GETINDENT)); write(L"UseTabs", d->Send(SCI_GETUSETABS));
        write(L"TabIndents", d->Send(SCI_GETTABINDENTS)); write(L"Backspace", d->Send(SCI_GETBACKSPACEUNINDENTS));
        write(L"FontKind", d->style.fontKind); write(L"MonitorMode", d->runtime.monitorMode); write(L"MonitorLog", d->runtime.monitorLog);
        write(L"KeepAtEnd", d->runtime.keepAtEnd); write(L"ResetWatch", d->runtime.resetWatch);
        write(L"PasteBoard", frame.runtime.pasteBoard == d->hwnd);
        profile.WriteString(section.c_str(), L"Find", d->search.findText); profile.WriteString(section.c_str(), L"Replace", d->search.replaceText);
        write(L"SearchFlags", d->search.flags);
        unsigned searchOptions = static_cast<unsigned>(d->search.wrap) | (static_cast<unsigned>(d->search.regex) << 1) | (static_cast<unsigned>(d->search.wildcard) << 2)
            | (static_cast<unsigned>(d->search.transformBackslashes) << 3) | (static_cast<unsigned>(d->search.dotMatchesNewline) << 4) | (static_cast<unsigned>(d->search.useBoostRegex) << 5)
            | (static_cast<unsigned>(d->search.closeAfterAction) << 6) | (static_cast<unsigned>(d->search.bookmarkMatches) << 7)
            | (static_cast<unsigned>(d->search.useMonospaced) << 8) | (static_cast<unsigned>(d->search.transparent) << 9)
            | (static_cast<unsigned>(d->search.closeAfterFind) << 10) | (static_cast<unsigned>(d->search.closeAfterReplace) << 11);
        write(L"SearchOptions", searchOptions);
        for (size_t history = 0; history < 32; ++history) {
            profile.WriteString(section.c_str(), (L"FindHistory" + std::to_wstring(history)).c_str(), history < d->search.findHistory.size() ? d->search.findHistory[history] : L"");
            profile.WriteString(section.c_str(), (L"ReplaceHistory" + std::to_wstring(history)).c_str(), history < d->search.replaceHistory.size() ? d->search.replaceHistory[history] : L"");
        }
        write(L"LineSelectionMode", d->runtime.lineSelectionMode);
        profile.WriteString(section.c_str(), L"TimestampTemplate", d->runtime.timestampTemplate);
        profile.WriteString(section.c_str(), L"DateTimeShort", d->runtime.dateTimeShort);
        profile.WriteString(section.c_str(), L"DateTimeLong", d->runtime.dateTimeLong);
        std::wstring bookmarks; LRESULT line = 0;
        while ((line = d->Send(SCI_MARKERNEXT, line, 1)) >= 0) { bookmarks += std::to_wstring(line++) + L" "; }
        profile.WriteString(section.c_str(), L"Bookmarks", bookmarks);
    }
    profile.WriteString(L"Session", L"Count", std::to_wstring(count)); profile.WriteString(L"Session", L"Active", std::to_wstring(activeIndex));
    if (!profile.Save()) throw std::runtime_error("Cannot save the MDI relaunch session.");
}
void RestoreRelaunchSession(MainFrame& frame, const std::filesystem::path& path) {
    AppSettings profile; profile.Initialize(true); profile.path = std::filesystem::absolute(path); profile.persistent = true;
    if (profile.ReadString(L"Session", L"Owner") != SessionOwner) throw std::runtime_error("This is not a MultiPad4 MDI session.");
    auto count = std::clamp(Number(profile, L"Session", L"Count", 0), 0LL, 10000LL);
    auto activeIndex = Number(profile, L"Session", L"Active", 0);
    frame.untitledCount = max(frame.untitledCount, static_cast<unsigned>(std::clamp(Number(profile, L"Session", L"UntitledCount", 0), 0LL, 1000000LL)));
    EditorDocument* active = nullptr;
    for (long long i = 0; i < count; ++i) {
        auto section = L"Document" + std::to_wstring(i);
        auto read = [&](const wchar_t* key, long long fallback) { return Number(profile, section, key, fallback); };
        auto file = profile.ReadString(section.c_str(), L"Path"); auto encoding = read(L"Encoding", -1);
        if (!ValidEncoding(encoding)) encoding = -1;
        EditorDocument* d = nullptr;
        bool exists = false;
        if (!file.empty()) {
            HANDLE probe = CreateFileW(file.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (probe != INVALID_HANDLE_VALUE) { BY_HANDLE_FILE_INFORMATION info{}; exists = GetFileInformationByHandle(probe, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY); CloseHandle(probe); }
            else if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) { frame.Error(std::runtime_error("Cannot access a file from the relaunch session.")); continue; }
        }
        if (!file.empty() && !exists) {
            // Never attach a missing/unreadable path to a newly blank document:
            // the original might reappear before a subsequent ordinary Save.
            frame.Error(std::runtime_error("A file from the relaunch session is missing or is not a regular file."));
            continue;
        }
        if (exists) {
            if (!frame.Open(file, encoding >= 0 ? std::optional{static_cast<files::Encoding>(encoding)} : std::nullopt)) continue;
            d = frame.Active();
        } else {
            d = frame.NewDocument(); if (!d) continue;
            if (encoding >= 0) d->encoding = static_cast<files::Encoding>(encoding);
            d->untitled = profile.ReadString(section.c_str(), L"Untitled", d->untitled.c_str());
        }
        if (!d) continue;
        if (i == activeIndex) active = d;
        d->style.scheme = static_cast<int>(read(L"Scheme", 0)); d->style.dialect = static_cast<int>(read(L"Dialect", 0));
        d->style.dark = read(L"Dark", 0) != 0; d->style.folding = read(L"Folding", 0) != 0;
        d->style.fontKind = static_cast<int>(read(L"FontKind", -1)); RestoreStyle(profile, section, d->style); d->ConfigureLexer();
        d->Send(SCI_SETWRAPMODE, std::clamp(read(L"Wrap", 0), 0LL, static_cast<long long>(SC_WRAP_AUTO)));
        d->Send(SCI_SETWRAPSTARTINDENT, std::clamp(read(L"WrapStartIndent", 0), 0LL, 1000000LL));
        d->Send(SCI_SETWRAPVISUALFLAGSLOCATION, read(L"WrapFlagLocation", 0) & 3);
        d->Send(SCI_SETMULTIPLESELECTION, read(L"MultipleSelection", 1) != 0);
        d->Send(SCI_SETADDITIONALSELECTIONTYPING, read(L"AdditionalSelectionTyping", 1) != 0);
        d->Send(SCI_SETOVERTYPE, read(L"Overtype", 0) != 0);
        d->Send(SCI_SETWRAPINDENTMODE, std::clamp(read(L"WrapIndent", 0), 0LL, 3LL)); d->Send(SCI_SETWRAPVISUALFLAGS, read(L"WrapFlags", 0));
        d->Send(SCI_SETZOOM, std::clamp(read(L"Zoom", 100), 10LL, 500LL));
        d->Send(SCI_SETMARGINWIDTHN, 0, read(L"LineNumbers", 1) ? MulDiv(54, GetDpiForWindow(d->hwnd), 96) : 0); d->Send(SCI_SETMARGINWIDTHN, 1, read(L"BookmarkMargin", 0) ? MulDiv(16, GetDpiForWindow(d->hwnd), 96) : 0);
        d->Send(SCI_SETTABWIDTH, std::clamp(read(L"TabWidth", 4), 1LL, 256LL)); d->Send(SCI_SETINDENT, std::clamp(read(L"IndentWidth", 4), 0LL, 256LL));
        d->Send(SCI_SETUSETABS, read(L"UseTabs", 0)); d->Send(SCI_SETTABINDENTS, read(L"TabIndents", 1)); d->Send(SCI_SETBACKSPACEUNINDENTS, read(L"Backspace", 2));
        auto eol = read(L"Eol", -1); if (eol >= SC_EOL_CRLF && eol <= SC_EOL_LF) d->Send(SCI_SETEOLMODE, eol);
        d->edgeMode = read(L"EdgeMode", EDGE_LINE) == EDGE_BACKGROUND ? EDGE_BACKGROUND : EDGE_LINE;
        d->Send(SCI_SETEDGECOLUMN, std::clamp(read(L"EdgeColumn", 80), 0LL, 1000000LL)); d->Send(SCI_SETEDGEMODE, read(L"EdgeVisible", 0) ? d->edgeMode : EDGE_NONE);
        d->runtime.monitorMode = static_cast<int>(std::clamp(read(L"MonitorMode", 0), 0LL, 2LL)); d->runtime.monitorLog = read(L"MonitorLog", 0) != 0;
        d->runtime.lineSelectionMode = static_cast<int>(std::clamp(read(L"LineSelectionMode", 1), 0LL, 2LL));
        d->runtime.timestampTemplate = profile.ReadString(section.c_str(), L"TimestampTemplate", d->runtime.timestampTemplate.c_str());
        d->runtime.dateTimeShort = profile.ReadString(section.c_str(), L"DateTimeShort", d->runtime.dateTimeShort.c_str());
        d->runtime.dateTimeLong = profile.ReadString(section.c_str(), L"DateTimeLong", d->runtime.dateTimeLong.c_str());
        d->runtime.keepAtEnd = read(L"KeepAtEnd", 0) != 0; d->runtime.resetWatch = read(L"ResetWatch", 0) != 0; SnapshotDocument(*d);
        if (read(L"PasteBoard", 0)) { frame.runtime.pasteBoard = d->hwnd; frame.runtime.clipboardSequence = GetClipboardSequenceNumber(); }
        d->search.findText = profile.ReadString(section.c_str(), L"Find"); d->search.replaceText = profile.ReadString(section.c_str(), L"Replace");
        d->search.flags = static_cast<int>(read(L"SearchFlags", 0)); auto searchOptions = read(L"SearchOptions", 1);
        d->search.wrap = (searchOptions & 1) != 0; d->search.regex = (searchOptions & 2) != 0; d->search.wildcard = (searchOptions & 4) != 0;
        d->search.transformBackslashes = (searchOptions & 8) != 0; d->search.dotMatchesNewline = (searchOptions & 16) != 0; d->search.useBoostRegex = (searchOptions & 32) != 0;
        d->search.closeAfterAction = (searchOptions & 64) != 0; d->search.bookmarkMatches = (searchOptions & 128) != 0;
        d->search.useMonospaced = (searchOptions & 256) != 0; d->search.transparent = (searchOptions & 512) != 0;
        d->search.closeAfterFind = (searchOptions & 1024) != 0; d->search.closeAfterReplace = (searchOptions & 2048) != 0;
        d->search.findHistory.clear(); d->search.replaceHistory.clear();
        for (size_t history = 0; history < 32; ++history) {
            auto find = profile.ReadString(section.c_str(), (L"FindHistory" + std::to_wstring(history)).c_str());
            auto replace = profile.ReadString(section.c_str(), (L"ReplaceHistory" + std::to_wstring(history)).c_str());
            if (!find.empty()) d->search.findHistory.push_back(find);
            if (!replace.empty()) d->search.replaceHistory.push_back(replace);
        }
        std::wistringstream bookmarks(profile.ReadString(section.c_str(), L"Bookmarks")); LRESULT line = 0;
        while (bookmarks >> line) if (line >= 0 && line < d->Send(SCI_GETLINECOUNT)) d->Send(SCI_MARKERADD, line, 0);
        if (d->FastView()) d->fastView->Seek(static_cast<unsigned long long>((std::max)(0LL, read(L"FastViewOffset", 0))));
        auto length = d->Send(SCI_GETLENGTH);
        d->Send(SCI_SETSEL, std::clamp<LRESULT>(read(L"Anchor", 0), 0, length), std::clamp<LRESULT>(read(L"Caret", 0), 0, length));
        d->Send(SCI_SETVIEWWS, std::clamp(read(L"ViewWhitespace", 0), 0LL, 3LL)); d->Send(SCI_SETVIEWEOL, read(L"ViewEol", 0) != 0);
        d->Send(SCI_SETINDENTATIONGUIDES, std::clamp(read(L"IndentGuides", 0), 0LL, 3LL));
        d->Send(SCI_SETVIRTUALSPACEOPTIONS, read(L"VirtualSpace", 0) & 7);
        auto selections = std::clamp(read(L"Selections", 1), 1LL, 10000LL);
        d->Send(SCI_CLEARSELECTIONS);
        for (long long selection = 0; selection < selections; ++selection) {
            auto prefix = L"Selection" + std::to_wstring(selection);
            auto caret = std::clamp<LRESULT>(read((prefix + L".Caret").c_str(), read(L"Caret", 0)), 0, length);
            auto anchor = std::clamp<LRESULT>(read((prefix + L".Anchor").c_str(), read(L"Anchor", 0)), 0, length);
            if (!selection) d->Send(SCI_SETSELECTION, caret, anchor); else d->Send(SCI_ADDSELECTION, caret, anchor);
            d->Send(SCI_SETSELECTIONNCARETVIRTUALSPACE, selection, std::clamp(read((prefix + L".CaretVirtual").c_str(), 0), 0LL, 1000000LL));
            d->Send(SCI_SETSELECTIONNANCHORVIRTUALSPACE, selection, std::clamp(read((prefix + L".AnchorVirtual").c_str(), 0), 0LL, 1000000LL));
        }
        d->Send(SCI_SETMAINSELECTION, std::clamp(read(L"MainSelection", 0), 0LL, selections - 1));
        auto mode = std::clamp(read(L"SelectionMode", SC_SEL_STREAM), 0LL, 3LL);
        d->Send(SCI_CHANGESELECTIONMODE, mode);
        d->Send(SCI_SETMOVEEXTENDSSELECTION, read(L"MoveExtendsSelection", 0) != 0);
        if (mode == SC_SEL_RECTANGLE || mode == SC_SEL_THIN) {
            d->Send(SCI_SETRECTANGULARSELECTIONCARET, std::clamp<LRESULT>(read(L"RectCaret", 0), 0, length));
            d->Send(SCI_SETRECTANGULARSELECTIONANCHOR, std::clamp<LRESULT>(read(L"RectAnchor", 0), 0, length));
            d->Send(SCI_SETRECTANGULARSELECTIONCARETVIRTUALSPACE, std::clamp(read(L"RectCaretVirtual", 0), 0LL, 1000000LL));
            d->Send(SCI_SETRECTANGULARSELECTIONANCHORVIRTUALSPACE, std::clamp(read(L"RectAnchorVirtual", 0), 0LL, 1000000LL));
        }
        d->Send(SCI_COLOURISE, 0, -1);
        std::wistringstream collapsed(profile.ReadString(section.c_str(), L"CollapsedFolds"));
        while (collapsed >> line) if (line >= 0 && line < d->Send(SCI_GETLINECOUNT) && (d->Send(SCI_GETFOLDLEVEL, line) & SC_FOLDLEVELHEADERFLAG)) d->Send(SCI_FOLDLINE, line, SC_FOLDACTION_CONTRACT);
        d->Send(SCI_SETXOFFSET, std::clamp(read(L"XOffset", 0), 0LL, 10000000LL));
        d->Send(SCI_SETFIRSTVISIBLELINE, max(0LL, read(L"FirstVisibleLine", 0)));
        d->Send(SCI_SETREADONLY, read(L"ReadOnly", 0) != 0);
    }
    if (active) for (int i = 0; i < frame.tabs.GetPageCount(); ++i) if (frame.tabs.GetPageData(i) == active) { frame.tabs.SetActivePage(i); break; }
    frame.Refresh();
    // Only delete internal one-shot relaunch files, never an arbitrary session
    // file supplied by a caller or a file outside the process temp directory.
    if (profile.path.filename().wstring().starts_with(L"MultiPad4-relaunch-") &&
        profile.path.parent_path() == std::filesystem::temp_directory_path()) {
        std::error_code error; std::filesystem::remove(profile.path, error);
    }
}
