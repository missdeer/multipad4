// Win32 adaptation of Kate LSP client's lifecycle, revision guards and completion flow.
// Reference: Kate addons/lspclient, Mark Nauwelaerts, MIT (see Kate.License.txt).
#include "PluginInterface.h"
#include "../PluginApi.h"
#include "FileIO.h"
#include "Transport.h"
#include <shlwapi.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <optional>

namespace {
using namespace lsp;
namespace fs = std::filesystem;
NppData host{};
HINSTANCE instance;
HWND timerWindow;
bool enabled = false, ticking = false;
enum Option { Documentation, AutoSignature, IncludeDeclaration, Parentheses, AutoHover, OnTyping,
    OnSave, Incremental, GotoHighlight, DiagnosticNotifications, Messages, OptionCount };
bool options[OptionCount]{true,true,true,true,true,false,false,false,true,true,true};
bool suspended = false;
int highlightIndicator = -1;
std::map<UINT_PTR, std::vector<Json>> resultTabs;
std::set<HWND> highlightedEditors;
std::vector<UINT_PTR> suspendedBuffers;
int indicator = -1, nextRequest = 1;
std::string lastError;
constexpr size_t MaxDocument = 8 * 1024 * 1024;
LRESULT Host(UINT m, WPARAM w = 0, LPARAM l = 0) { return SendMessageW(host._nppHandle, m, w, l); }
LRESULT Sci(HWND h, UINT m, WPARAM w = 0, LPARAM l = 0) { return SendMessageW(h, m, w, l); }
UINT_PTR Active() { return Host(NPPM_GETCURRENTBUFFERID); }
HWND Editor(UINT_PTR id) { return reinterpret_cast<HWND>(Host(MPM_GETBUFFEREDITORWINDOW, id)); }
std::string Text(HWND h) {
    const auto length = Sci(h, SCI_GETLENGTH);
    if (length < 0 || length > MaxDocument) throw std::runtime_error("LSP is limited to documents up to 8 MiB");
    std::string text(static_cast<size_t>(length) + 1, '\0');
    Sci(h, SCI_GETTEXT, text.size(), reinterpret_cast<LPARAM>(text.data())); text.resize(length); return text;
}
size_t EditorOffset(HWND h, const Json& position, bool clamp = false) {
    if (!position.at("line").is_number_integer() || !position.at("character").is_number_integer())
        throw std::runtime_error("Non-integer LSP position");
    const auto line = position.at("line").get<int64_t>(), column = position.at("character").get<int64_t>();
    if (line < 0 || column < 0 || column > INT_MAX) throw std::runtime_error("LSP position out of range");
    if (line >= Sci(h, SCI_GETLINECOUNT)) {
        if (clamp) return static_cast<size_t>(Sci(h, SCI_GETLENGTH));
        throw std::runtime_error("LSP line out of range");
    }
    const auto start = Sci(h, SCI_POSITIONFROMLINE, line);
    const auto end = Sci(h, SCI_GETLINEENDPOSITION, line);
    if (clamp && column > Sci(h, SCI_COUNTCODEUNITS, start, end)) return static_cast<size_t>(end);
    const auto byte = Sci(h, SCI_POSITIONRELATIVECODEUNITS, start, column);
    if (byte < start || byte > end || Sci(h, SCI_COUNTCODEUNITS, start, byte) != column)
        throw std::runtime_error("LSP column out of range");
    return static_cast<size_t>(byte);
}
fs::path Path(UINT_PTR id) {
    auto length = Host(MPM_GETBUFFERFILEPATH, id);
    if (length <= 0) return {};
    std::wstring value(length + 1, L'\0');
    Host(MPM_GETBUFFERFILEPATH, id, reinterpret_cast<LPARAM>(value.data())); value.resize(length); return value;
}
std::string Uri(const fs::path& path) {
    wchar_t url[65536]; DWORD size = _countof(url);
    if (FAILED(UrlCreateFromPathW(path.c_str(), url, &size, 0))) throw std::runtime_error("Cannot create document URI");
    return files::ToUtf8(url);
}
fs::path FromUri(const std::string& uri) {
    auto wide = files::FromUtf8(uri);
    if (!wide.starts_with(L"file://")) throw std::runtime_error("Only file URIs are supported");
    wchar_t path[32768]; DWORD length = _countof(path);
    if (FAILED(PathCreateFromUrlW(wide.c_str(), path, &length, 0))) throw std::runtime_error("Invalid file URI");
    return path;
}
fs::path Config() {
    const auto length = Host(NPPM_GETPLUGINSCONFIGDIR);
    if (length <= 0) throw std::runtime_error("Plugin configuration directory unavailable");
    std::wstring value(length + 1, '\0');
    if (!Host(NPPM_GETPLUGINSCONFIGDIR, value.size(), reinterpret_cast<LPARAM>(value.data()))) throw std::runtime_error("Cannot read plugin configuration directory");
    value.resize(length); return fs::path(value) / L"LSPClient.json";
}
void Log(const std::string& text) noexcept {
    try {
        std::ofstream output(Config().parent_path() / L"LSPClient-client.log", std::ios::app);
        SYSTEMTIME now{}; GetLocalTime(&now);
        char stamp[32]; snprintf(stamp,sizeof(stamp),"%04u-%02u-%02u %02u:%02u:%02u ",
            now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond);
        output << stamp << text << '\n';
    } catch (...) {}
}
struct Session {
    std::unique_ptr<Transport> transport;
    fs::path stderrFile;
    Json capabilities, settings;
    fs::path root;
    bool ready = false, failed = false;
    ULONGLONG started = GetTickCount64();
    int initialize = 0;
    std::deque<Json> deferred;
    bool Supports(const char* name) const {
        auto it = capabilities.find(name);
        return ready && !failed && it != capabilities.end() && (it->is_object() || *it == true);
    }
    bool CanComplete() const {
        const auto provider = capabilities.find("completionProvider");
        return provider != capabilities.end() && provider->is_object();
    }
    void Send(std::string_view method, Json params) {
        if (!transport) throw std::runtime_error("Language server is not running");
        transport->Send({{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}});
    }
};
struct Document {
    UINT_PTR id = 0;
    HWND editor = nullptr;
    Session* session = nullptr;
    std::string uri, language, text;
    int version = 0;
    uint64_t revision = 0;
    bool opened = false, dirty = true, disabled = false;
    ULONGLONG changed = 0;
    std::vector<Json> diagnostics;
    std::vector<std::pair<LRESULT,LRESULT>> selections;
    std::pair<LRESULT,LRESULT> expanded;
};
std::map<std::wstring, std::unique_ptr<Session>> sessions;
std::map<UINT_PTR, Document> documents;
struct Pending {
    std::string method;
    UINT_PTR buffer;
    uint64_t revision, epoch, hoverEpoch;
    LRESULT caret, position;
    ULONGLONG started;
    Session* session;
};
std::map<int, Pending> pending;
std::optional<Pending> completionRetry;
ULONGLONG completionRetryAt = 0;
uint64_t epoch = 0, hoverEpoch = 0;
std::string scheduledMethod;
ULONGLONG scheduledAt = 0;
struct Completion {
    UINT_PTR buffer = 0;
    uint64_t revision = 0;
    LRESULT start = 0;
    std::map<std::string, Json> items;
    LRESULT separator = ' ', typeSeparator = '?', order = 0, chooseSingle = 0;
} completion;
std::vector<Json> signatures;
size_t signatureIndex = 0;
UINT_PTR signatureBuffer = 0;
LRESULT signatureAnchor = 0;

void ClearCompletion(bool cancel = true) {
    auto previous = std::move(completion); completion = {};
    if (auto h = Editor(previous.buffer)) {
        if (cancel) Sci(h, SCI_AUTOCCANCEL);
        Sci(h, SCI_AUTOCSETSEPARATOR, previous.separator);
        Sci(h, SCI_AUTOCSETTYPESEPARATOR, previous.typeSeparator);
        Sci(h, SCI_AUTOCSETORDER, previous.order);
        Sci(h, SCI_AUTOCSETCHOOSESINGLE, previous.chooseSingle);
    }
}
void ClearDiagnostics(Document& d) {
    if (d.diagnostics.empty()) return;
    auto h = Editor(d.id);
    if (h && indicator >= 0) {
        Sci(h, SCI_SETINDICATORCURRENT, indicator);
        Sci(h, SCI_INDICATORCLEARRANGE, 0, Sci(h, SCI_GETLENGTH));
    }
    d.diagnostics.clear();
}
void ClearHighlights(HWND);
void Stop() {
    enabled = false; completionRetry.reset(); scheduledMethod.clear(); ++epoch; ClearCompletion();
    for (auto& [id, d] : documents) {
        ClearHighlights(d.editor); ClearDiagnostics(d); Host(MPM_SETEXTERNALCOMPLETION, id, FALSE);
        if (auto h = Editor(id)) Sci(h, SCI_CALLTIPCANCEL);
    }
    // The process job also stops descendants when a server ignores shutdown.
    for (auto& [key, s] : sessions) if (!s->failed) {
        try { s->transport->Send({{"jsonrpc","2.0"},{"id",nextRequest++},{"method","shutdown"},{"params",nullptr}}); }
        catch (...) {}
    }
    documents.clear(); pending.clear(); sessions.clear(); signatures.clear(); signatureBuffer = 0;
    if (timerWindow) KillTimer(timerWindow, 1);
}
void Error(const std::exception& e, bool display = false) {
    lastError = e.what();
    Log(lastError);
    if (display) MessageBoxW(host._nppHandle, files::FromUtf8(lastError).c_str(), L"LSP Client", MB_OK | MB_ICONERROR);
}
Json ReadConfig() {
    std::ifstream input(Config());
    if (!input) throw std::runtime_error("Use LSP Client > More Options > Configure Servers first");
    auto config = Json::parse(input);
    if (!config.is_object() || !config.contains("servers") || !config["servers"].is_array()) throw std::runtime_error("Expected a servers array in LSPClient.json");
    return config;
}
void Attach(UINT_PTR id = Active()) {
    const auto path = Path(id); auto h = Editor(id);
    if (!enabled || !h || path.empty() || Sci(h, SCI_GETCODEPAGE) != SC_CP_UTF8 || Sci(h, SCI_GETLENGTH) > MaxDocument) return;
    if (documents.contains(id) && documents.at(id).uri == Uri(path)) return;
    if (documents.contains(id)) {
        auto& old = documents.at(id);
        if (old.opened && !old.session->failed) old.session->Send("textDocument/didClose", {{"textDocument", {{"uri", old.uri}}}});
        Host(MPM_SETEXTERNALCOMPLETION, id, FALSE); ClearDiagnostics(old);
        documents.erase(id);
    }
    const auto config = ReadConfig();
    for (const auto& entry : config.at("servers")) {
        if (!entry.value("enabled", true)) continue;
        const auto extensions = entry.at("extensions").get<std::vector<std::string>>();
        const auto extension = files::ToUtf8(path.extension().wstring());
        if (std::none_of(extensions.begin(), extensions.end(), [&](const auto& ext) { return _stricmp(ext.c_str(), extension.c_str()) == 0; })) continue;
        const auto language = entry.at("languageId").get<std::string>();
        auto root = path.parent_path();
        const auto markers = entry.value("rootMarkers", std::vector<std::string>{".git"});
        for (auto directory = root; !directory.empty();) {
            if (std::any_of(markers.begin(), markers.end(), [&](const auto& marker) { return fs::exists(directory / files::FromUtf8(marker)); })) { root = directory; break; }
            auto parent = directory.parent_path(); if (parent == directory) break; directory = parent;
        }
        auto args = entry.at("command").get<std::vector<std::string>>();
        if (args.empty() || args.front().empty()) throw std::runtime_error("Empty language-server command");
        std::wstring command;
        for (const auto& arg : args) { if (!command.empty()) command += L' '; command += Quote(files::FromUtf8(arg)); }
        auto key = root.wstring() + L"\n" + command;
        auto existing = sessions.find(key);
        Session* session = existing == sessions.end() ? nullptr : existing->second.get();
        if (!session) {
            auto created = std::make_unique<Session>(); created->root = root;
            created->settings = entry.value("settings", Json::object());
            created->initialize = nextRequest++;
            created->stderrFile = Config().parent_path() / (L"LSPClient-server-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(created->initialize) + L".log");
            created->transport = std::make_unique<Transport>(command, root, created->stderrFile);
            Log("Started server; stderr: " + files::ToUtf8(created->stderrFile.wstring()));
            created->transport->Send({{"jsonrpc","2.0"},{"id",created->initialize},{"method","initialize"},{"params",{
                {"processId",GetCurrentProcessId()}, {"rootUri",Uri(root)},
                {"workspaceFolders",Json::array({{{"uri",Uri(root)},{"name",files::ToUtf8(root.filename().wstring())}}})},
                {"initializationOptions",entry.value("initializationOptions",Json(nullptr))},
                {"capabilities",{{"general",{{"positionEncodings",Json::array({"utf-16"})}}},
                    {"workspace",{{"configuration",true},{"workspaceFolders",true},{"applyEdit",false}}},
                    {"textDocument",{{"completion",{{"completionItem",{{"snippetSupport",false},{"insertReplaceSupport",true}}}}},
                        {"hover",{{"contentFormat",Json::array({"plaintext"})}}},
                        {"signatureHelp",{{"signatureInformation",{{"documentationFormat",Json::array({"plaintext"})},{"parameterInformation",{{"labelOffsetSupport",true}}}}}}},
                        {"publishDiagnostics",{{"versionSupport",true}}}}}}}
            }}});
            session = created.get(); sessions.emplace(key, std::move(created));
        }
        Document doc; doc.id = id; doc.editor = h; doc.uri = Uri(path); doc.language = language; doc.session = session;
        documents.emplace(id, std::move(doc)); return;
    }
}
void Sync(Document& d) {
    auto& s = *d.session; auto h = Editor(d.id);
    if (!s.ready || s.failed || d.disabled || !h || (!d.dirty && d.opened)) return;
    if (Sci(h, SCI_GETLENGTH) > MaxDocument || Sci(h, SCI_GETCODEPAGE) != SC_CP_UTF8) {
        if (d.opened) s.Send("textDocument/didClose", {{"textDocument", {{"uri", d.uri}}}});
        d.disabled = true; d.opened = false; d.dirty = false;
        Host(MPM_SETEXTERNALCOMPLETION, d.id, FALSE); ClearDiagnostics(d);
        lastError = "Document left LSP limits; restart LSP after reducing its size.";
        return;
    }
    auto text = Text(h);
    if (!d.opened) {
        s.Send("textDocument/didOpen", {{"textDocument",{{"uri",d.uri},{"languageId",d.language},{"version",++d.version},{"text",text}}}});
        d.opened = true;
        if (s.CanComplete()) Host(MPM_SETEXTERNALCOMPLETION, d.id, TRUE);
    } else if (text != d.text) {
        const auto sync = s.capabilities.value("textDocumentSync", Json(0));
        const auto kind = sync.is_number() ? sync.get<int>() : sync.value("change", 0);
        if (kind) {
            Json change{{"text",text}};
            if (kind == 2) {
                if (options[Incremental]) change = Change(d.text, text);
                else change["range"] = {{"start",{{"line",0},{"character",0}}},{"end",Position(d.text,d.text.size())}};
            }
            s.Send("textDocument/didChange", {{"textDocument",{{"uri",d.uri},{"version",++d.version}}},{"contentChanges",Json::array({change})}});
        }
    }
    d.text = std::move(text); d.dirty = false;
}
Json FormatOptions(HWND h) {
    return {{"tabSize",Sci(h,SCI_GETTABWIDTH)},{"insertSpaces",!Sci(h,SCI_GETUSETABS)}};
}
void Request(const std::string& method, LRESULT position = -1, ULONGLONG retryStarted = 0, std::string argument = {}) {
    Attach(); auto it = documents.find(Active()); if (it == documents.end()) return;
    auto& d = it->second; if (!d.session->ready || d.session->failed) return;
    if (method=="textDocument/completion" && !d.session->CanComplete()) return;
    auto h = Editor(d.id); if (!h || Sci(h, SCI_GETSELECTIONS) != 1 || (Host(MPM_GETCOMPLETIONSTATE,d.id)&MPC_COMPOSING)) return;
    Sync(d);
    if (!d.opened || d.disabled) return;
    if (pending.size() >= 128) return;
    if (position < 0) position = Sci(h, SCI_GETCURRENTPOS);
    const int request = nextRequest++;
    if (method == "textDocument/completion" && !retryStarted) completionRetry.reset();
    pending.emplace(request, Pending{method,d.id,d.revision,epoch,hoverEpoch,Sci(h,SCI_GETCURRENTPOS),position,retryStarted?retryStarted:GetTickCount64(),d.session});
    Json params{{"textDocument",{{"uri",d.uri}}},{"position",Position(d.text,position)}};
    if (method == "textDocument/completion") params["context"] = {{"triggerKind",retryStarted?3:1}};
    if (method == "workspace/symbol") params = {{"query",argument}};
    if (method == "textDocument/selectionRange") {
        params["positions"] = Json::array({Position(d.text,Sci(h,SCI_GETSELECTIONSTART))});
        params.erase("position");
    }
    if (method == "textDocument/formatting" || method == "textDocument/onTypeFormatting") {
        params["options"] = FormatOptions(h);
        if (argument.empty()) params.erase("position");
        else params["ch"] = argument;
    }
    if (method == "textDocument/references") params["context"] = {{"includeDeclaration",options[IncludeDeclaration]}};
    d.session->transport->Send({{"jsonrpc","2.0"},{"id",request},{"method",method},{"params",std::move(params)}});
}
std::string Content(const Json& content) {
    if (content.is_null()) return {};
    if (content.is_string()) return content.get<std::string>();
    if (content.is_object()) return content.value("value", "");
    if (content.is_array()) { std::string text; for (const auto& c : content) { if (!text.empty()) text += "\n"; text += Content(c); } return text; }
    return {};
}
void Tip(HWND h, LRESULT anchor, std::string text) {
    // Scintilla's CallTipShow cancels its completion list.
    if (Sci(h, SCI_AUTOCACTIVE)) return;
    if (text.size() > 16000) text.resize(16000);
    std::replace(text.begin(),text.end(),'\r',' ');
    // Scintilla reserves these control bytes for clickable overload arrows.
    std::replace(text.begin(),text.end(),'\1',' '); std::replace(text.begin(),text.end(),'\2',' ');
    if (!text.empty()) Sci(h, SCI_CALLTIPSHOW, anchor, reinterpret_cast<LPARAM>(text.c_str()));
}
void Signature() {
    auto h = Editor(signatureBuffer); if (!h || signatures.empty()) return;
    auto& signature = signatures[signatureIndex];
    auto label = signature.at("label").get<std::string>();
    auto tip = label;
    if (signatures.size() > 1) tip = "\1\2 " + std::to_string(signatureIndex + 1) + "/" + std::to_string(signatures.size()) + " " + label;
    const auto prefix = tip.size() - label.size();
    tip += "\n" + Content(signature.value("documentation",Json()));
    Sci(h, SCI_CALLTIPSHOW, signatureAnchor, reinterpret_cast<LPARAM>(tip.c_str()));
    const auto parameters = signature.value("parameters",Json::array());
    auto active = signature.contains("activeParameter") && !signature["activeParameter"].is_null() ? signature["activeParameter"].get<unsigned>() : 0u;
    if (active < parameters.size()) {
        const auto& parameter = parameters[active].at("label");
        size_t start = 0, end = 0;
        if (parameter.is_array() && parameter.size() == 2) {
            start = Offset(label,{{"line",0},{"character",parameter[0]}});
            end = Offset(label,{{"line",0},{"character",parameter[1]}});
        } else if (parameter.is_string()) {
            auto value = parameter.get<std::string>(); start = label.find(value); end = start + value.size();
        }
        if (start != std::string::npos && end >= start && end <= label.size()) Sci(h,SCI_CALLTIPSETHLT,prefix+start,prefix+end);
    }
}
void ShowCompletion(Document& d, const Json& result) {
    const auto items = result.is_array() ? result : result.value("items",Json::array());
    if (!items.is_array()) return;
    auto h = Editor(d.id); ClearCompletion(); completion.buffer = d.id; completion.revision = d.revision;
    completion.separator = Sci(h,SCI_AUTOCGETSEPARATOR); completion.typeSeparator = Sci(h,SCI_AUTOCGETTYPESEPARATOR); completion.order = Sci(h,SCI_AUTOCGETORDER); completion.chooseSingle = Sci(h,SCI_AUTOCGETCHOOSESINGLE);
    std::vector<std::pair<std::string,Json>> sorted;
    for (const auto& item : items) {
        if (!item.is_object() || !item.contains("label") || item.value("insertTextFormat",1) == 2) continue;
        auto label = item.at("label").get<std::string>();
        if (label.empty()) continue;
        sorted.emplace_back(item.value("sortText",label),item);
        if (sorted.size() == 1000) break;
    }
    std::stable_sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    std::string list;
    for (const auto& [sort,item] : sorted) {
        auto label = item.at("label").get<std::string>();
        for (auto& c : label) if (static_cast<unsigned char>(c)<32 || c=='\x7f') c=' ';
        const auto first=label.find_first_not_of(' '),last=label.find_last_not_of(' ');
        if(first==std::string::npos)continue;
        label=label.substr(first,last-first+1);
        if (!completion.items.emplace(label,item).second) continue;
        if (!list.empty()) list += '\n'; list += label;
    }
    if (list.empty()) { ClearCompletion(); return; }
    // Single-choice insertion bypasses SCN_AUTOCSELECTION and loses LSP TextEdits.
    Sci(h,SCI_AUTOCSETCHOOSESINGLE,FALSE);
    Sci(h,SCI_AUTOCSETSEPARATOR,'\n'); Sci(h,SCI_AUTOCSETTYPESEPARATOR,0x1f); Sci(h,SCI_AUTOCSETORDER,SC_ORDER_CUSTOM);
    const auto caret = Sci(h,SCI_GETCURRENTPOS);
    completion.start = Sci(h,SCI_WORDSTARTPOSITION,caret,TRUE);
    // The server has already filtered candidates; labels may be fuzzy matches.
    Sci(h,SCI_AUTOCSHOW,0,reinterpret_cast<LPARAM>(list.c_str()));
}
struct Edit { size_t start,end; std::string text; };
Edit ReadEdit(HWND h, const Json& edit) {
    const auto& range = edit.contains("range") ? edit.at("range") : edit.at("replace");
    Edit parsed{EditorOffset(h,range.at("start")),EditorOffset(h,range.at("end")),edit.at("newText").get<std::string>()};
    if (parsed.end < parsed.start || parsed.text.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid text edit");
    return parsed;
}
void PrepareEdits(std::vector<Edit>& edits) {
    std::stable_sort(edits.begin(),edits.end(),[](const auto& a,const auto& b){return std::tie(a.start,a.end)<std::tie(b.start,b.end);});
    for(size_t i=1;i<edits.size();++i) if(edits[i].start<edits[i-1].end) throw std::runtime_error("Overlapping text edits");
}
void ApplyEdits(HWND h, const std::vector<Edit>& edits) {
    if (Sci(h,SCI_GETREADONLY) || edits.empty()) return;
    Sci(h,SCI_BEGINUNDOACTION);
    for(auto it=edits.rbegin();it!=edits.rend();++it){Sci(h,SCI_SETTARGETRANGE,it->start,it->end);Sci(h,SCI_REPLACETARGET,it->text.size(),reinterpret_cast<LPARAM>(it->text.data()));}
    Sci(h,SCI_ENDUNDOACTION);
}
// Follow unchanged text across reindentation, including a whole-document edit.
size_t FormattedOffset(std::string_view before, std::string_view after, size_t position) {
    if (position == before.size()) return after.size();
    const auto whitespace = [](char c) { return c==' ' || c=='\t' || c=='\r' || c=='\n'; };
    size_t oldByte=0, newByte=0;
    while (oldByte < before.size()) {
        const auto oldStart=oldByte, newStart=newByte;
        while (oldByte<before.size() && whitespace(before[oldByte])) ++oldByte;
        while (newByte<after.size() && whitespace(after[newByte])) ++newByte;
        if (position < oldByte) return newStart + std::min(position-oldStart,newByte-newStart);
        if (position == oldByte) return newByte;
        if (newByte==after.size() || before[oldByte]!=after[newByte]) break;
        ++oldByte; ++newByte;
    }
    // A formatter may rewrite tokens too. Keep positions in the common suffix exact.
    size_t oldEnd=before.size(), newEnd=after.size();
    while (oldEnd>oldByte && newEnd>newByte && before[oldEnd-1]==after[newEnd-1]) { --oldEnd; --newEnd; }
    size_t mapped=position>=oldEnd ? newEnd+(position-oldEnd) : newByte+std::min(position-oldByte,newEnd-newByte);
    while (mapped && mapped<after.size() && (static_cast<unsigned char>(after[mapped])&0xc0)==0x80) --mapped;
    return mapped;
}
void FormatResult(HWND h,const Json& result) {
    if (!result.is_array() || result.empty() || Sci(h,SCI_GETREADONLY)) return;
    std::vector<Edit> edits;
    for (const auto& edit : result) edits.push_back(ReadEdit(h,edit));
    std::vector<std::pair<size_t,size_t>> selections;
    const auto count=Sci(h,SCI_GETSELECTIONS);
    for (LRESULT i=0;i<count;++i)
        selections.emplace_back(Sci(h,SCI_GETSELECTIONNANCHOR,i),Sci(h,SCI_GETSELECTIONNCARET,i));
    PrepareEdits(edits);
    for (auto it=edits.rbegin();it!=edits.rend();++it) {
        auto length=it->end-it->start;
        const char* bytes=length ? reinterpret_cast<const char*>(Sci(h,SCI_GETRANGEPOINTER,it->start,length)) : "";
        if (!bytes) throw std::runtime_error("Cannot read formatting range");
        auto map=[&](size_t position) {
            if (position<it->start) return position;
            if (position>=it->end) return position-length+it->text.size();
            return it->start+FormattedOffset({bytes,length},it->text,position-it->start);
        };
        for (auto& [anchor,caret]:selections) { anchor=map(anchor); caret=map(caret); }
    }
    ApplyEdits(h,edits);
    for (size_t i=0;i<selections.size();++i) {
        Sci(h,SCI_SETSELECTIONNANCHOR,i,selections[i].first);
        Sci(h,SCI_SETSELECTIONNCARET,i,selections[i].second);
    }
}
void StatusText(const std::string& message) {
    const auto wide = files::FromUtf8(message);
    Host(NPPM_SETSTATUSBAR,0,reinterpret_cast<LPARAM>(wide.c_str()));
}
void MarkRange(HWND h,const Json& range) {
    if (highlightIndicator < 0) return;
    auto start = EditorOffset(h,range.at("start")), end = EditorOffset(h,range.at("end"));
    Sci(h,SCI_INDICSETSTYLE,highlightIndicator,INDIC_ROUNDBOX);
    Sci(h,SCI_INDICSETFORE,highlightIndicator,RGB(65,150,225));
    Sci(h,SCI_SETINDICATORCURRENT,highlightIndicator);
    if(end>start) {
        Sci(h,SCI_INDICATORFILLRANGE,start,end-start);
        highlightedEditors.insert(h);
    }
}
void ClearHighlights(HWND h) {
    if (highlightedEditors.erase(h)) {
        Sci(h,SCI_SETINDICATORCURRENT,highlightIndicator);
        Sci(h,SCI_INDICATORCLEARRANGE,0,Sci(h,SCI_GETLENGTH));
    }
}
void Jump(const Json& location) {
    auto path = FromUri(location.value("uri",location.value("targetUri",std::string())));
    auto range = location.contains("range") ? location.at("range") : location.at("targetSelectionRange");
    if (!Host(NPPM_DOOPEN,0,reinterpret_cast<LPARAM>(path.c_str()))) return;
    if (auto h = Editor(Active())) {
        Sci(h,SCI_GOTOPOS,EditorOffset(h,range.at("start")));
        Sci(h,SCI_SCROLLCARET);
        if (options[GotoHighlight]) { ClearHighlights(h); MarkRange(h,range); }
    }
}
void ShowLocations(const Json& result, const wchar_t* title) {
    std::string lines;
    std::vector<Json> locations;
    for (const auto& entry : result) {
        auto& location = entry.contains("location") ? entry.at("location") : entry;
        const auto range = location.contains("range") ? location.at("range") : location.at("targetSelectionRange");
        const auto path = FromUri(location.value("uri",location.value("targetUri",std::string())));
        auto name = entry.value("name","");
        std::replace_if(name.begin(),name.end(),[](char c){return c=='\r'||c=='\n';},' ');
        lines += name + (name.empty()?"":"  ") + files::ToUtf8(path.wstring()) + ":" +
            std::to_string(range.at("start").at("line").get<int64_t>()+1) + "\r\n";
        locations.push_back(location);
    }
    if (locations.empty()) { StatusText("No matching locations"); return; }
    auto previous = Active();
    if (!Host(NPPM_MENUCOMMAND,0,41001) || Active()==previous) return;
    auto id = Active(); auto h = Editor(id);
    if (!h || !Path(id).empty() || Sci(h,SCI_GETLENGTH)) return;
    Host(NPPM_SETUNTITLEDNAME,id,reinterpret_cast<LPARAM>(title));
    Sci(h,SCI_SETTEXT,0,reinterpret_cast<LPARAM>(lines.c_str()));
    Sci(h,SCI_EMPTYUNDOBUFFER); Sci(h,SCI_SETSAVEPOINT); Sci(h,SCI_SETREADONLY,TRUE);
    resultTabs.emplace(id,std::move(locations));
}
void InsertCompletion(const SCNotification& notification) {
    const auto id = completion.buffer; auto h = Editor(id); auto found = documents.find(id);
    auto item = completion.items.find(notification.text ? notification.text : "");
    if (!h || found == documents.end() || item == completion.items.end() || found->second.revision != completion.revision || Sci(h,SCI_GETREADONLY)) { ClearCompletion(); return; }
    const auto state=Host(MPM_GETCOMPLETIONSTATE,id);
    if((notification.listCompletionMethod==SC_AC_NEWLINE && !(state&MPC_ACCEPTENTER)) ||
        (notification.listCompletionMethod==SC_AC_TAB && !(state&MPC_ACCEPTTAB))) {
        ClearCompletion();
        Sci(h,notification.listCompletionMethod==SC_AC_NEWLINE?SCI_NEWLINE:SCI_TAB);
        return;
    }
    auto value = item->second;
    std::vector<Edit> edits;
    if (value.contains("textEdit")) edits.push_back(ReadEdit(h,value.at("textEdit")));
    else edits.push_back({static_cast<size_t>(completion.start),static_cast<size_t>(Sci(h,SCI_GETCURRENTPOS)),value.value("insertText",value.at("label").get<std::string>())});
    const int kind = value.value("kind",0);
    bool addParens = options[Parentheses] && (kind == 2 || kind == 3) &&
        edits.front().text.find('(') == std::string::npos && Sci(h,SCI_GETCHARAT,edits.front().end) != '(';
    if (addParens) edits.front().text += "()";
    const auto mainStart=edits[0].start; auto caret=edits[0].start+edits[0].text.size() - addParens;
    for (const auto& edit : value.value("additionalTextEdits",Json::array())) {
        edits.push_back(ReadEdit(h,edit)); const auto& added=edits.back();
        if (added.end<=mainStart) caret = caret + added.text.size() - (added.end-added.start);
    }
    PrepareEdits(edits);
    ClearCompletion();
    ApplyEdits(h,edits);
    Sci(h,SCI_SETSEL,caret,caret);
}
void Diagnostics(Session& session,const Json& params) {
    for (auto& [id,d] : documents) {
        if (d.session!=&session || d.uri!=params.at("uri").get<std::string>() || d.dirty || !d.opened ||
            (params.contains("version") && !params["version"].is_null() && params["version"]!=d.version)) continue;
        auto h=Editor(id); if(!h) continue;
        ClearDiagnostics(d);
        if(indicator<0) continue;
        Sci(h,SCI_INDICSETSTYLE,indicator,INDIC_SQUIGGLE); Sci(h,SCI_INDICSETFORE,indicator,RGB(220,55,55)); Sci(h,SCI_SETINDICATORCURRENT,indicator);
        if (options[DiagnosticNotifications] && id==Active() && !params.at("diagnostics").empty())
            StatusText(params.at("diagnostics").front().value("message",""));
        size_t count = 0;
        bool reported = false;
        for(const auto& diagnostic:params.at("diagnostics")) {
            if (++count > 2000) break;
            try {
                const auto& range=diagnostic.at("range");
                auto start=EditorOffset(h,range.at("start"),true),end=EditorOffset(h,range.at("end"),true);
                if(end<start) continue;
                Sci(h,SCI_INDICATORFILLRANGE,start,end-start); d.diagnostics.push_back(diagnostic);
            } catch (const std::exception& e) {
                if (!reported) Error(std::runtime_error(std::string("Ignored diagnostic: ") + e.what()));
                reported = true;
            }
        }
    }
}
void Receive(Session& session,const Json& message) {
    if(message.contains("method")) {
        const auto method=message.at("method").get<std::string>();
        if(message.contains("id")) {
            Json result=nullptr;
            if(method=="workspace/configuration") {
                result=Json::array();
                for(const auto& item:message.at("params").at("items")) {
                    Json setting=session.settings;
                    auto section=item.value("section","");
                    for(size_t start=0;start<section.size();) {auto end=section.find('.',start);auto key=section.substr(start,end-start);setting=setting.is_object()?setting.value(key,Json(nullptr)):Json(nullptr);if(end==std::string::npos)break;start=end+1;}
                    result.push_back(std::move(setting));
                }
            } else if(method=="workspace/workspaceFolders") result=Json::array({{{"uri",Uri(session.root)},{"name",files::ToUtf8(session.root.filename().wstring())}}});
            else {
                session.transport->Send({{"jsonrpc","2.0"},{"id",message["id"]},{"error",{{"code",-32601},{"message","Client method not supported"}}}}); return;
            }
            session.transport->Send({{"jsonrpc","2.0"},{"id",message["id"]},{"result",result}});
        } else if(method=="textDocument/publishDiagnostics") Diagnostics(session,message.at("params"));
        else if(method=="window/logMessage" || method=="window/showMessage") {
            lastError=message.at("params").value("message","");
            if (options[Messages]) StatusText(lastError);
        }
        return;
    }
    if(!message.contains("id") || !message["id"].is_number_integer()) return;
    const auto id=message["id"].get<int>();
    if(id==session.initialize) {
        if(message.contains("error")) throw std::runtime_error(message["error"].dump());
        session.capabilities=message.at("result").at("capabilities");
        if(session.capabilities.value("positionEncoding","utf-16")!="utf-16") throw std::runtime_error("Server selected unsupported position encoding");
        session.Send("initialized",Json::object());
        session.Send("workspace/didChangeConfiguration",{{"settings",session.settings}}); session.ready=true; return;
    }
    auto it=pending.find(id);if(it==pending.end() || it->second.session!=&session)return;
    auto request=it->second;pending.erase(it);
    if(message.contains("error")){lastError=message["error"].dump();return;}
    auto doc=documents.find(request.buffer);auto h=Editor(request.buffer);
    if(doc==documents.end() || Active()!=request.buffer || !h || doc->second.dirty || request.revision!=doc->second.revision || request.epoch!=epoch || request.caret!=Sci(h,SCI_GETCURRENTPOS) || GetFocus()!=h)return;
    if(Host(MPM_GETCOMPLETIONSTATE,request.buffer)&MPC_COMPOSING)return;
    if(request.method=="textDocument/hover" && request.hoverEpoch!=hoverEpoch)return;
    const auto& result=message.at("result");if(result.is_null())return;
    if(request.method=="textDocument/completion") {
        ShowCompletion(doc->second,result);
        completionRetry.reset();
        if (result.is_object() && result.value("isIncomplete",false)) {
            completionRetry = request; completionRetryAt = GetTickCount64() + 500;
        }
    }
    else if(request.method=="textDocument/hover")Tip(h,request.position,Content(result.at("contents")));
    else if(request.method=="textDocument/signatureHelp") {
        signatures=result.at("signatures").get<std::vector<Json>>();if(signatures.empty())return;
        signatureIndex=(std::min)(result.contains("activeSignature") && !result["activeSignature"].is_null() ? result["activeSignature"].get<size_t>() : size_t(0),signatures.size()-1);
        if(!signatures[signatureIndex].contains("activeParameter"))signatures[signatureIndex]["activeParameter"]=result.contains("activeParameter") ? result["activeParameter"] : Json(0u);
        signatureBuffer=request.buffer;signatureAnchor=request.caret;Signature();
    } else if (request.method=="textDocument/documentHighlight") {
        ClearHighlights(h);
        for (const auto& item : result) MarkRange(h,item.at("range"));
    } else if (request.method=="textDocument/selectionRange") {
        auto start = Sci(h,SCI_GETSELECTIONSTART), end = Sci(h,SCI_GETSELECTIONEND);
        if (result.empty()) return;
        for (const Json* node = &result.at(0); node;) {
            const auto& range = node->at("range");
            auto left = static_cast<LRESULT>(EditorOffset(h,range.at("start")));
            auto right = static_cast<LRESULT>(EditorOffset(h,range.at("end")));
            if (left<=start && end<=right && (left<start || end<right)) {
                if (doc->second.expanded!=std::pair{start,end}) doc->second.selections.clear();
                doc->second.expanded={left,right};
                doc->second.selections.emplace_back(Sci(h,SCI_GETANCHOR),request.caret);
                Sci(h,SCI_SETSEL,left,right);
                break;
            }
            auto parent = node->find("parent");
            node = parent==node->end() ? nullptr : &*parent;
        }
    } else if (request.method=="textDocument/formatting" || request.method=="textDocument/onTypeFormatting") {
        FormatResult(h,result);
    } else if (request.method=="workspace/symbol" || request.method=="textDocument/references" || (result.is_array() && result.size()>1)) {
        ShowLocations(result,request.method=="workspace/symbol" ? L"LSP Symbols" : L"LSP References");
    } else if (!result.empty()) {
        Jump(result.is_array()?result.front():result);
    }
}
void Tick() {
    if(!enabled || ticking)return;
    ticking=true;
    try {
        for(auto& [key,s]:sessions) {
            if(s->failed)continue;
            try {
                auto failure=s->transport->Error();if(!failure.empty())throw std::runtime_error(failure);
                if(!s->ready && GetTickCount64()-s->started>15000)throw std::runtime_error("Language server initialization timed out");
                auto incoming = s->transport->Poll();
                if (!s->deferred.empty()) {
                    s->deferred.insert(s->deferred.end(),std::make_move_iterator(incoming.begin()),std::make_move_iterator(incoming.end()));
                    incoming.clear(); incoming.swap(s->deferred);
                }
                for(const auto& message:incoming) {
                    try { Receive(*s,message); }
                    catch(const std::exception& e) {
                        if (!s->ready) throw;
                        const auto method = message.find("method");
                        const auto name = method != message.end() && method->is_string() ? method->get<std::string>() : "response";
                        Error(std::runtime_error("Ignored LSP message " + name + ": " + e.what()));
                    }
                }
            } catch(const std::exception& e) {
                Error(std::runtime_error(std::string(e.what()) + "; server log: " + files::ToUtf8(s->stderrFile.wstring())));
                s->failed=true;s->transport.reset();
                for(auto& [id,d]:documents)if(d.session==s.get()){
                    d.opened=false;
                    Host(MPM_SETEXTERNALCOMPLETION,id,FALSE);ClearDiagnostics(d);
                }
            }
        }
        for(auto& [id,d]:documents)if(d.dirty && GetTickCount64()-d.changed>=180)Sync(d);
        if (!scheduledMethod.empty() && GetTickCount64() >= scheduledAt) {
            auto method = std::exchange(scheduledMethod, {});
            if (GetFocus() == Editor(Active())) Request(method);
        }
        if (completionRetry && GetTickCount64() >= completionRetryAt) {
            const auto retry = *completionRetry; completionRetry.reset();
            const auto doc = documents.find(retry.buffer); auto h = Editor(retry.buffer);
            if (doc != documents.end() && Active() == retry.buffer && h && GetFocus() == h &&
                retry.epoch == epoch && retry.revision == doc->second.revision && retry.caret == Sci(h,SCI_GETCURRENTPOS) &&
                GetTickCount64() - retry.started < 10000)
                Request("textDocument/completion",-1,retry.started);
        }
        std::erase_if(pending,[](const auto& item){return GetTickCount64()-item.second.started>10000;});
    } catch(const std::exception& e){Error(e);}
    ticking=false;
}
constexpr UINT FormatAfterInput = WM_APP + 1;
UINT_PTR inputSequence = 0;
LRESULT CALLBACK WindowProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_TIMER){Tick();return 0;}
    if(m==FormatAfterInput) {
        if(w==inputSequence && enabled && options[OnTyping] && GetFocus()==Editor(Active())) {
            try { Request("textDocument/onTypeFormatting",-1,0,std::string(1,static_cast<char>(l))); }
            catch(const std::exception& e){Error(e);}
        }
        return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
void SetSuspended(bool value);
void Start() {
    auto buffers=std::move(suspendedBuffers);
    for(const auto& [id,doc]:documents) buffers.push_back(id);
    SetSuspended(false);
    try {
        Stop();ReadConfig();enabled=true;lastError.clear();
        for(auto id:buffers) Attach(id);
        Attach();SetTimer(timerWindow,1,40,nullptr);
    }catch(const std::exception& e){Stop();Error(e,true);}
}
void Configure() {
    try {
        auto path=Config();
        if(!fs::exists(path)) {
            wchar_t module[32768];GetModuleFileNameW(instance,module,_countof(module));
            fs::copy_file(fs::path(module).parent_path()/L"servers.example.json",path,fs::copy_options::none);
        }
        Host(NPPM_DOOPEN,0,reinterpret_cast<LPARAM>(path.c_str()));
    }catch(const std::exception& e){Error(e,true);}
}
template<const char* Method> void Command() {try{Request(Method);}catch(const std::exception& e){Error(e,true);}}
constexpr char CompleteMethod[]="textDocument/completion",HoverMethod[]="textDocument/hover",SignatureMethod[]="textDocument/signatureHelp",DefinitionMethod[]="textDocument/definition";
void Status(){MessageBoxW(host._nppHandle,files::FromUtf8(lastError.empty()?(enabled?"LSP enabled. Servers: "+std::to_string(sessions.size()):"LSP stopped. Configure Servers, then Restart All LSP Servers."):lastError).c_str(),L"LSP Client",MB_OK);}
constexpr char DeclarationMethod[]="textDocument/declaration", TypeMethod[]="textDocument/typeDefinition",
    ImplementationMethod[]="textDocument/implementation", HighlightMethod[]="textDocument/documentHighlight",
    FormatMethod[]="textDocument/formatting", SelectionMethod[]="textDocument/selectionRange",
    ReferencesMethod[]="textDocument/references";
INT_PTR CALLBACK QueryProc(HWND dialog,UINT message,WPARAM w,LPARAM l) {
    if (message==WM_INITDIALOG) { SetWindowLongPtrW(dialog,DWLP_USER,l); return TRUE; }
    if (message!=WM_COMMAND) return FALSE;
    if (LOWORD(w)==IDOK) GetDlgItemTextW(dialog,101,reinterpret_cast<wchar_t*>(GetWindowLongPtrW(dialog,DWLP_USER)),512);
    else if (LOWORD(w)!=IDCANCEL) return FALSE;
    EndDialog(dialog,LOWORD(w)); return TRUE;
}
void SearchSymbol() {
    wchar_t query[512]{};
    if (DialogBoxParamW(instance,MAKEINTRESOURCEW(100),host._nppHandle,QueryProc,reinterpret_cast<LPARAM>(query))==IDOK) {
        try { Request("workspace/symbol",-1,0,files::ToUtf8(query)); } catch(const std::exception& e){ Error(e,true); }
    }
}
void Shrink() {
    auto& d = documents.at(Active());
    auto [anchor,caret] = d.selections.back();
    d.selections.pop_back();
    d.expanded=std::minmax(anchor,caret);
    Sci(d.editor,SCI_SETSEL,anchor,caret);
}
void CloseResults() {
    std::vector<UINT_PTR> ids;
    for (auto& [id,locations] : resultTabs) ids.push_back(id);
    for (auto id : ids) Host(MPM_CLOSEBUFFER,id);
}
void RestartCurrent() {
    auto it=documents.find(Active());
    if(it==documents.end()) return;
    auto* session=it->second.session;
    ++epoch; completionRetry.reset(); scheduledMethod.clear(); ClearCompletion();
    std::erase_if(pending,[&](const auto& entry){return entry.second.session==session;});
    std::vector<UINT_PTR> buffers;
    for (auto doc=documents.begin();doc!=documents.end();) {
        auto& d=doc->second;
        if (d.session!=session) { ++doc; continue; }
        Host(MPM_SETEXTERNALCOMPLETION,d.id,FALSE); ClearDiagnostics(d); ClearHighlights(d.editor);
        Sci(d.editor,SCI_CALLTIPCANCEL);
        buffers.push_back(d.id);
        doc=documents.erase(doc);
    }
    std::erase_if(sessions,[&](const auto& entry){return entry.second.get()==session;});
    try { for(auto id:buffers) Attach(id); } catch(const std::exception& e){Error(e,true);}
}
void Suspend();
template<Option option> void Toggle();
ShortcutKey completeKey{true,true,false,VK_SPACE},signatureKey{true,true,true,VK_SPACE},definitionKey{false,true,false,VK_F12};
ShortcutKey symbolKey{true,true,false,'P'},expandKey{true,false,false,'2'};
FuncItem commands[]{
    {L"Go to Definition",Command<DefinitionMethod>,0,false,&definitionKey},
    {L"Go to Declaration",Command<DeclarationMethod>},
    {L"Go to Type Definition",Command<TypeMethod>},
    {L"Find Implementations",Command<ImplementationMethod>},
    {L"Highlight",Command<HighlightMethod>},{L"Symbol Info",Command<HoverMethod>},
    {L"Search and Go to Symbol\tCtrl+Alt+P",SearchSymbol,0,false,&symbolKey},
    {L"Format\tCtrl+T, F",Command<FormatMethod>},
    {L"Expand Selection\tCtrl+2",Command<SelectionMethod>,0,false,&expandKey},
    {L"Shrink Selection\tCtrl+Shift+2",Shrink},
    {},{L"Close All Dynamic Reference Tabs",CloseResults},{L"Suspend All",Suspend},{},
    {L"Restart LSP Server",RestartCurrent},{L"Restart All LSP Servers",Start},{},
    {L"Show selected completion documentation",Toggle<Documentation>},
    {L"Enable signature help with auto completion",Toggle<AutoSignature>},
    {L"Include declaration in references",Toggle<IncludeDeclaration>},
    {L"Add parentheses upon function completion",Toggle<Parentheses>},
    {L"Show hover information",Toggle<AutoHover>},{L"Format on typing",Toggle<OnTyping>},
    {L"Format on save",Toggle<OnSave>},{L"Incremental document synchronization",Toggle<Incremental>},
    {L"Highlight goto location",Toggle<GotoHighlight>},
    {L"Show Diagnostics Notifications",Toggle<DiagnosticNotifications>},{L"Show Messages",Toggle<Messages>},
    {},{L"Configure Servers...",Configure},{L"Complete",Command<CompleteMethod>,0,false,&completeKey},
    {L"Signature Help",Command<SignatureMethod>,0,false,&signatureKey},{L"Find References",Command<ReferencesMethod>},
    {L"Status / Last Server Message",Status},{L"Stop",Stop},
};
FuncItem& Item(PFUNCPLUGINCMD function) {
    return *std::find_if(std::begin(commands),std::end(commands),[&](auto& item){return item._pFunc==function;});
}
void SetSuspended(bool value) {
    suspended=value;
    Host(NPPM_SETMENUITEMCHECK,Item(Suspend)._cmdID,value);
}
void Suspend() {
    if (suspended) { Start(); }
    else {
        suspendedBuffers.clear();
        for(const auto& [id,doc]:documents) suspendedBuffers.push_back(id);
        Stop(); SetSuspended(true);
    }
}
template<Option option> void Toggle() {
    try {
        auto& item=(&Item(Toggle<Documentation>))[option];
        auto path=Config().parent_path()/L"LSPClient.ini";
        if (!WritePrivateProfileStringW(L"Options",item._itemName,options[option]?L"0":L"1",path.c_str()))
            throw std::runtime_error("Cannot save LSP menu options");
        options[option]=!options[option];
        Host(NPPM_SETMENUITEMCHECK,item._cmdID,options[option]);
        if constexpr(option==AutoHover) { ++hoverEpoch; Sci(Editor(Active()),SCI_CALLTIPCANCEL); }
    } catch(const std::exception& e){Error(e,true);}
}
void BuildOptionsMenu(HMENU root) {
    const auto first=&Item(Toggle<Documentation>)-commands;
    HMENU menu=CreatePopupMenu();
    if(!menu) return;
    const auto path=Config().parent_path()/L"LSPClient.ini";
    for (auto i=first;i<std::ssize(commands);++i) {
        auto& item=commands[i];
        DeleteMenu(root,static_cast<UINT>(first),MF_BYPOSITION);
        if (i-first==DiagnosticNotifications || i-first==Messages) AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        UINT flags=item._pFunc?MF_STRING:MF_SEPARATOR;
        if (i-first<OptionCount) {
            auto& flag=options[i-first];
            flag=GetPrivateProfileIntW(L"Options",item._itemName,flag,path.c_str())!=0;
            if (flag) flags|=MF_CHECKED;
        }
        AppendMenuW(menu,flags,item._cmdID,item._itemName);
    }
    AppendMenuW(root,MF_POPUP,reinterpret_cast<UINT_PTR>(menu),L"More Options");
}
bool Available(UINT id) {
    auto found=std::find_if(std::begin(commands),std::end(commands),[&](const auto& item){return static_cast<UINT>(item._cmdID)==id;});
    if(found==std::end(commands)) return false;
    auto f=found->_pFunc;
    if(f==CloseResults) return !resultTabs.empty();
    auto doc=documents.find(Active());
    if(f==RestartCurrent) return doc!=documents.end();
    if(f==Shrink) return doc!=documents.end() && !doc->second.selections.empty() &&
        doc->second.expanded==std::pair{Sci(doc->second.editor,SCI_GETSELECTIONSTART),Sci(doc->second.editor,SCI_GETSELECTIONEND)};
    const char* provider=nullptr;
    if(f==Command<DefinitionMethod>) provider="definitionProvider";
    if(f==Command<DeclarationMethod>) provider="declarationProvider";
    if(f==Command<TypeMethod>) provider="typeDefinitionProvider";
    if(f==Command<ImplementationMethod>) provider="implementationProvider";
    if(f==Command<HighlightMethod>) provider="documentHighlightProvider";
    if(f==Command<HoverMethod>) provider="hoverProvider";
    if(f==SearchSymbol) provider="workspaceSymbolProvider";
    if(f==Command<ReferencesMethod>) provider="referencesProvider";
    if(f==Command<FormatMethod>) provider="documentFormattingProvider";
    if(f==Command<SelectionMethod>) provider="selectionRangeProvider";
    if(!provider) return true;
    return doc!=documents.end() && !doc->second.disabled && doc->second.session->Supports(provider) &&
        (f!=Command<FormatMethod> || !Sci(doc->second.editor,SCI_GETREADONLY));
}
void FormatBeforeSave(Document& d) {
    if (!d.session->Supports("documentFormattingProvider") || Sci(d.editor,SCI_GETREADONLY)) return;
    Sync(d);
    if (!d.opened) return;
    auto& s=*d.session;
    const int id=nextRequest++;
    s.transport->Send({{"jsonrpc","2.0"},{"id",id},{"method",FormatMethod},{"params",{
        {"textDocument",{{"uri",d.uri}}},{"options",FormatOptions(d.editor)}}}});
    // Save is synchronous. Defer unrelated UI responses until after the write.
    auto deadline=GetTickCount64()+1000;
    do {
        Json response;
        for(auto& message:s.transport->Poll()) {
            if(!message.contains("method") && message.contains("id") && message["id"]==id) response=std::move(message);
            else if(message.contains("method") && message.contains("id")) Receive(s,message);
            else s.deferred.push_back(std::move(message));
        }
        if(!response.is_null()) { FormatResult(d.editor,response.at("result")); return; }
        if(!s.transport->Error().empty()) break;
        Sleep(5);
    } while(GetTickCount64()<deadline);
    Log("Format on save timed out; saving current text");
}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){instance=module;DisableThreadLibraryCalls(module);}return TRUE;}
extern "C" __declspec(dllexport) BOOL isUnicode(){return TRUE;}
extern "C" __declspec(dllexport) const wchar_t* getName(){return L"LSP Client";}
extern "C" __declspec(dllexport) const wchar_t* multipadGetMainMenuLabel(){return L"LSP Client";}
extern "C" __declspec(dllexport) BOOL multipadCommandEnabled(UINT id){return Available(id);}
extern "C" __declspec(dllexport) void multipadCustomizeMenu(HMENU menu){
    try { BuildOptionsMenu(menu); } catch(const std::exception& e){Error(e);}
}
extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count){*count=_countof(commands);return commands;}
extern "C" __declspec(dllexport) void setInfo(NppData data){
    host=data;WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=WindowProc;cls.lpszClassName=L"MultiPad4.LSPClient";
    RegisterClassW(&cls);timerWindow=CreateWindowW(cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,nullptr);
    if (!Host(NPPM_ALLOCATEINDICATOR,1,reinterpret_cast<LPARAM>(&highlightIndicator))) highlightIndicator=-1;
    if (!Host(NPPM_ALLOCATEINDICATOR,1,reinterpret_cast<LPARAM>(&indicator))) indicator=-1;
}
extern "C" __declspec(dllexport) LRESULT messageProc(UINT,WPARAM,LPARAM){return TRUE;}
extern "C" __declspec(dllexport) BOOL multipadPreTranslateMessage(MSG* m){
    static ULONGLONG chordDeadline=0;
    // Selection Actions also binds Ctrl+Shift+2. Claim it only while an LSP expansion can be undone.
    if(m->message==WM_KEYDOWN && m->wParam=='2' && GetKeyState(VK_CONTROL)<0 && GetKeyState(VK_SHIFT)<0 &&
        GetKeyState(VK_MENU)>=0 && m->hwnd==Editor(Active()) && Available(Item(Shrink)._cmdID)) {
        Shrink(); return TRUE;
    }
    if(m->message==WM_KEYDOWN && (chordDeadline || (m->wParam=='T' && GetKeyState(VK_CONTROL)<0) ||
        (m->wParam==VK_RETURN && !resultTabs.empty())) && m->hwnd==Editor(Active())) {
        if (m->wParam!=VK_CONTROL && m->wParam!=VK_SHIFT && m->wParam!=VK_MENU) {
            bool format=chordDeadline && GetTickCount64()<chordDeadline && m->wParam=='F';
            chordDeadline=0;
            if ((format || (m->wParam=='T' && GetKeyState(VK_CONTROL)<0)) && Available(Item(Command<FormatMethod>)._cmdID)) {
                if(format) { Command<FormatMethod>(); return TRUE; }
                if(m->wParam=='T' && GetKeyState(VK_CONTROL)<0 && GetKeyState(VK_SHIFT)>=0 && GetKeyState(VK_MENU)>=0) {
                    chordDeadline=GetTickCount64()+1500; return TRUE;
                }
            }
        }
        if(m->wParam==VK_RETURN && resultTabs.contains(Active())) {
            const auto& rows=resultTabs.at(Active());
            auto line=Sci(m->hwnd,SCI_LINEFROMPOSITION,Sci(m->hwnd,SCI_GETCURRENTPOS));
            try { if(line>=0 && static_cast<size_t>(line)<rows.size()) Jump(rows[line]); } catch(const std::exception& e){Error(e);}
            return TRUE;
        }
    }
    if(m->message==WM_KEYDOWN && m->wParam==VK_ESCAPE){++epoch;scheduledMethod.clear();ClearCompletion();signatureBuffer=0;}
    if(m->message==WM_IME_STARTCOMPOSITION){++epoch;ClearCompletion();}
    return FALSE;
}
extern "C" __declspec(dllexport) BOOL multipadHandleEditorNotification(const SCNotification* n){
    if(!n)return FALSE;
    if(n->nmhdr.code==SCN_DOUBLECLICK && resultTabs.contains(Active()) && n->nmhdr.hwndFrom==Editor(Active())) {
        const auto& rows=resultTabs.at(Active());
        try { if(n->line>=0 && static_cast<size_t>(n->line)<rows.size()) Jump(rows[n->line]); } catch(const std::exception& e){Error(e);}
        return TRUE;
    }
    if(!enabled)return FALSE;
    const bool selecting = n->nmhdr.code==SCN_AUTOCSELECTION && completion.buffer!=0;
    try {
        auto found=documents.find(Active());
        if(found==documents.end() || found->second.editor!=n->nmhdr.hwndFrom)
            found=std::find_if(documents.begin(),documents.end(),[&](const auto& item){return item.second.editor==n->nmhdr.hwndFrom;});if(found==documents.end())return FALSE;
        auto& d=found->second;
        switch(n->nmhdr.code){
        case SCN_MODIFIED:
            if(n->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT)){
                d.selections.clear(); ClearHighlights(d.editor); ++d.revision;d.dirty=true;d.changed=GetTickCount64();
                if(completion.buffer==d.id)ClearCompletion();
                ClearDiagnostics(d);
            }
            break;
        case SCN_CHARADDED:
            ++inputSequence;
            if(d.id != Active() || n->listType || n->characterSource==SC_CHARACTERSOURCE_TENTATIVE_INPUT ||
                (Host(MPM_GETCOMPLETIONSTATE,d.id)&MPC_COMPOSING)) break;
            if(d.session->ready && !d.session->failed && !Sci(Editor(d.id),SCI_GETREADONLY)) {
                if (options[OnTyping] && d.session->Supports("documentOnTypeFormattingProvider") && n->ch>0 && n->ch<128) {
                    const auto& provider=d.session->capabilities.at("documentOnTypeFormattingProvider");
                    std::string ch(1,static_cast<char>(n->ch));
                    auto more=provider.value("moreTriggerCharacter",std::vector<std::string>{});
                    if(provider.value("firstTriggerCharacter","")==ch || std::find(more.begin(),more.end(),ch)!=more.end())
                        PostMessageW(timerWindow,FormatAfterInput,inputSequence,n->ch);
                }
                if(options[AutoSignature] && (n->ch=='(' || n->ch==',')) scheduledMethod=SignatureMethod;
                else if(n->ch=='.' || n->ch=='_' || (n->ch>='a' && n->ch<='z') || (n->ch>='A' && n->ch<='Z') || (n->ch>='0' && n->ch<='9')) {
                    if(d.session->CanComplete())scheduledMethod=CompleteMethod;
                    else scheduledMethod.clear();
                }
                else scheduledMethod.clear();
                scheduledAt=GetTickCount64()+200;
            }
            break;
        case SCN_FOCUSOUT:++inputSequence;++epoch;scheduledMethod.clear();ClearCompletion();signatureBuffer=0;break;
        case SCN_AUTOCSELECTIONCHANGE:
            if(options[Documentation] && completion.buffer==d.id && n->text) {
                auto item=completion.items.find(n->text);
                if(item!=completion.items.end()) {
                    auto docs=Content(item->second.value("documentation",Json()));
                    StatusText(docs.empty()?item->second.value("detail",""):docs);
                }
            }
            break;
        case SCN_AUTOCSELECTION:
            if(completion.buffer==d.id){InsertCompletion(*n);return TRUE;}break;
        case SCN_AUTOCCHARDELETED:
            if(completion.buffer==d.id){ClearCompletion();return TRUE;}break;
        case SCN_AUTOCCANCELLED:if(completion.buffer==d.id)ClearCompletion(false);break;
        case SCN_CALLTIPCLICK:
            if(signatureBuffer==d.id && !signatures.empty()){signatureIndex=(signatureIndex+(n->position==1?signatures.size()-1:1))%signatures.size();Signature();return TRUE;}break;
        case SCN_DWELLSTART:if(options[AutoHover] && d.id==Active() && d.session->ready && n->position>=0){Request(HoverMethod,n->position);return TRUE;}break;
        case SCN_DWELLEND:if(d.id==Active()){++hoverEpoch;Sci(Editor(d.id),SCI_CALLTIPCANCEL);}break;
        }
    }catch(const std::exception& e){ClearCompletion();Error(e);return selecting;}
    return FALSE;
}
extern "C" __declspec(dllexport) void beNotified(SCNotification* n){
    if(!n)return;
    try {
        if(n->nmhdr.code==NPPN_SHUTDOWN){Stop();if(timerWindow)DestroyWindow(timerWindow);timerWindow=nullptr;UnregisterClassW(L"MultiPad4.LSPClient",instance);return;}
        if(n->nmhdr.code==NPPN_FILEBEFORECLOSE) {
            resultTabs.erase(n->nmhdr.idFrom);
            highlightedEditors.erase(Editor(n->nmhdr.idFrom));
        }
        if(!enabled)return;
        if(n->nmhdr.code==NPPN_FILEBEFORESAVE && options[OnSave]) {
            auto doc=documents.find(n->nmhdr.idFrom);
            if(doc!=documents.end()) FormatBeforeSave(doc->second);
        }
        if(n->nmhdr.code==NPPN_BUFFERACTIVATED || n->nmhdr.code==NPPN_FILEOPENED){++inputSequence;++epoch;scheduledMethod.clear();ClearCompletion();signatureBuffer=0;Attach();}
        if(n->nmhdr.code==NPPN_FILESAVED){
            Attach();auto it=documents.find(n->nmhdr.idFrom);if(it!=documents.end()){
                Sync(it->second);auto& s=*it->second.session;
                auto sync=s.capabilities.value("textDocumentSync",Json());
                if(it->second.opened && sync.is_object() && sync.contains("save") && sync["save"]!=false){
                    Json params{{"textDocument",{{"uri",it->second.uri}}}};
                    if(sync["save"].is_object() && sync["save"].value("includeText",false))params["text"]=it->second.text;
                    s.Send("textDocument/didSave",params);
                }
            }
        }
        if(n->nmhdr.code==NPPN_FILEBEFORECLOSE){auto it=documents.find(n->nmhdr.idFrom);if(it!=documents.end()){
            if(completion.buffer==it->first)ClearCompletion();ClearDiagnostics(it->second);
            Host(MPM_SETEXTERNALCOMPLETION,it->first,FALSE);
            if(it->second.opened && !it->second.session->failed)it->second.session->Send("textDocument/didClose",{{"textDocument",{{"uri",it->second.uri}}}});
            documents.erase(it);++epoch;
        }}
    }catch(const std::exception& e){Error(e);}
}
