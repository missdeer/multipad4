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
    void Send(std::string_view method, Json params) {
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
void Stop() {
    enabled = false; completionRetry.reset(); scheduledMethod.clear(); ++epoch; ClearCompletion();
    for (auto& [id, d] : documents) {
        ClearDiagnostics(d); Host(MPM_SETEXTERNALCOMPLETION, id, FALSE);
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
    if (!input) throw std::runtime_error("Use LSP Client > Configure Servers first");
    auto config = Json::parse(input);
    if (!config.is_object() || !config.contains("servers") || !config["servers"].is_array()) throw std::runtime_error("Expected a servers array in LSPClient.json");
    return config;
}
void Attach() {
    const auto id = Active(); const auto path = Path(id); auto h = Editor(id);
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
        Host(MPM_SETEXTERNALCOMPLETION, d.id, TRUE);
    } else if (text != d.text) {
        const auto sync = s.capabilities.value("textDocumentSync", Json(0));
        const auto kind = sync.is_number() ? sync.get<int>() : sync.value("change", 0);
        if (kind) {
            Json change{{"text",text}};
            if (kind == 2) change = Change(d.text, text);
            s.Send("textDocument/didChange", {{"textDocument",{{"uri",d.uri},{"version",++d.version}}},{"contentChanges",Json::array({change})}});
        }
    }
    d.text = std::move(text); d.dirty = false;
}
void Request(const std::string& method, LRESULT position = -1, ULONGLONG retryStarted = 0) {
    Attach(); auto it = documents.find(Active()); if (it == documents.end()) return;
    auto& d = it->second; if (!d.session->ready || d.session->failed) return;
    auto h = Editor(d.id); if (!h || Sci(h, SCI_GETSELECTIONS) != 1) return;
    Sync(d);
    if (!d.opened || d.disabled) return;
    if (pending.size() >= 128) return;
    if (position < 0) position = Sci(h, SCI_GETCURRENTPOS);
    const int request = nextRequest++;
    if (method == "textDocument/completion" && !retryStarted) completionRetry.reset();
    pending.emplace(request, Pending{method,d.id,d.revision,epoch,hoverEpoch,Sci(h,SCI_GETCURRENTPOS),position,retryStarted?retryStarted:GetTickCount64(),d.session});
    Json params{{"textDocument",{{"uri",d.uri}}},{"position",Position(d.text,position)}};
    if (method == "textDocument/completion") params["context"] = {{"triggerKind",retryStarted?3:1}};
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
void InsertCompletion(const SCNotification& notification) {
    const auto id = completion.buffer; auto h = Editor(id); auto found = documents.find(id);
    auto item = completion.items.find(notification.text ? notification.text : "");
    if (!h || found == documents.end() || item == completion.items.end() || found->second.revision != completion.revision || Sci(h,SCI_GETREADONLY)) { ClearCompletion(); return; }
    auto value = item->second;
    struct Edit { size_t start,end; std::string text; };
    std::vector<Edit> edits;
    auto add = [&](const Json& edit) {
        auto range = edit.contains("range") ? edit.at("range") : edit.at("replace");
        auto start=EditorOffset(h,range.at("start")),end=EditorOffset(h,range.at("end"));
        if (end<start) throw std::runtime_error("Invalid completion edit range");
        auto text=edit.at("newText").get<std::string>();
        if(text.find('\0')!=std::string::npos) throw std::runtime_error("NUL in completion edit");
        edits.push_back({start,end,std::move(text)});
    };
    if (value.contains("textEdit")) add(value.at("textEdit"));
    else edits.push_back({static_cast<size_t>(completion.start),static_cast<size_t>(Sci(h,SCI_GETCURRENTPOS)),value.value("insertText",value.at("label").get<std::string>())});
    const auto mainStart=edits[0].start; auto caret=edits[0].start+edits[0].text.size();
    for (const auto& edit : value.value("additionalTextEdits",Json::array())) {
        add(edit); const auto& added=edits.back();
        if (added.end<=mainStart) caret = caret + added.text.size() - (added.end-added.start);
    }
    std::sort(edits.begin(),edits.end(),[](const auto& a,const auto& b){return a.start<b.start;});
    for(size_t i=1;i<edits.size();++i) if(edits[i].start<edits[i-1].end || edits[i].start==edits[i-1].start) throw std::runtime_error("Overlapping completion edits");
    ClearCompletion();
    Sci(h,SCI_BEGINUNDOACTION);
    for(auto it=edits.rbegin();it!=edits.rend();++it){Sci(h,SCI_SETTARGETRANGE,it->start,it->end);Sci(h,SCI_REPLACETARGET,it->text.size(),reinterpret_cast<LPARAM>(it->text.data()));}
    Sci(h,SCI_SETSEL,caret,caret); Sci(h,SCI_ENDUNDOACTION);
}
void Diagnostics(Session& session,const Json& params) {
    for (auto& [id,d] : documents) {
        if (d.session!=&session || d.uri!=params.at("uri").get<std::string>() || d.dirty || !d.opened ||
            (params.contains("version") && !params["version"].is_null() && params["version"]!=d.version)) continue;
        auto h=Editor(id); if(!h) continue;
        ClearDiagnostics(d);
        if(indicator<0) continue;
        Sci(h,SCI_INDICSETSTYLE,indicator,INDIC_SQUIGGLE); Sci(h,SCI_INDICSETFORE,indicator,RGB(220,55,55)); Sci(h,SCI_SETINDICATORCURRENT,indicator);
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
        else if(method=="window/logMessage" || method=="window/showMessage") lastError=message.at("params").value("message","");
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
    } else if(request.method=="textDocument/definition") {
        Json location=result.is_array()?(result.empty()?Json():result.front()):result;if(location.is_null())return;
        auto path=FromUri(location.value("uri",location.value("targetUri",std::string())));
        auto range=location.contains("range")?location["range"]:location.at("targetSelectionRange");
        if(Host(NPPM_DOOPEN,0,reinterpret_cast<LPARAM>(path.c_str()))) {
            auto target=Editor(Active());if(!target)return;auto text=Text(target);auto position=Offset(text,range.at("start"));Sci(target,SCI_GOTOPOS,position);Sci(target,SCI_SCROLLCARET);
        }
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
                for(const auto& message:s->transport->Poll()) {
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
                for(auto& [id,d]:documents)if(d.session==s.get()){Host(MPM_SETEXTERNALCOMPLETION,id,FALSE);ClearDiagnostics(d);}
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
LRESULT CALLBACK WindowProc(HWND h,UINT m,WPARAM w,LPARAM l) {if(m==WM_TIMER){Tick();return 0;}return DefWindowProcW(h,m,w,l);}
void Start() {
    try {
        Stop();ReadConfig();enabled=true;lastError.clear();Attach();SetTimer(timerWindow,1,40,nullptr);
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
void Status(){MessageBoxW(host._nppHandle,files::FromUtf8(lastError.empty()?(enabled?"LSP enabled. Servers: "+std::to_string(sessions.size()):"LSP stopped. Configure Servers, then Start / Restart."):lastError).c_str(),L"LSP Client",MB_OK);}
ShortcutKey completeKey{true,true,false,VK_SPACE},signatureKey{true,true,true,VK_SPACE},definitionKey{false,true,false,VK_F12};
FuncItem commands[]{
    {L"Configure Servers...",Configure},{L"Start / Restart",Start},{L"Stop",Stop},
    {L"Complete",Command<CompleteMethod>,0,false,&completeKey},
    {L"Signature Help",Command<SignatureMethod>,0,false,&signatureKey},
    {L"Hover",Command<HoverMethod>},{L"Go to Definition",Command<DefinitionMethod>,0,false,&definitionKey},
    {L"Status / Last Server Message",Status},
};
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){instance=module;DisableThreadLibraryCalls(module);}return TRUE;}
extern "C" __declspec(dllexport) BOOL isUnicode(){return TRUE;}
extern "C" __declspec(dllexport) const wchar_t* getName(){return L"LSP Client";}
extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count){*count=_countof(commands);return commands;}
extern "C" __declspec(dllexport) void setInfo(NppData data){
    host=data;WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=WindowProc;cls.lpszClassName=L"MultiPad4.LSPClient";
    RegisterClassW(&cls);timerWindow=CreateWindowW(cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,nullptr);
    Host(NPPM_ALLOCATEINDICATOR,1,reinterpret_cast<LPARAM>(&indicator));
}
extern "C" __declspec(dllexport) LRESULT messageProc(UINT,WPARAM,LPARAM){return TRUE;}
extern "C" __declspec(dllexport) BOOL multipadPreTranslateMessage(MSG* m){
    if(m->message==WM_KEYDOWN && m->wParam==VK_ESCAPE){++epoch;scheduledMethod.clear();ClearCompletion();signatureBuffer=0;}
    if(m->message==WM_IME_STARTCOMPOSITION){++epoch;ClearCompletion();}
    return FALSE;
}
extern "C" __declspec(dllexport) BOOL multipadHandleEditorNotification(const SCNotification* n){
    if(!enabled || !n)return FALSE;
    const bool selecting = n->nmhdr.code==SCN_AUTOCSELECTION && completion.buffer!=0;
    try {
        auto found=documents.find(Active());
        if(found==documents.end() || found->second.editor!=n->nmhdr.hwndFrom)
            found=std::find_if(documents.begin(),documents.end(),[&](const auto& item){return item.second.editor==n->nmhdr.hwndFrom;});if(found==documents.end())return FALSE;
        auto& d=found->second;
        switch(n->nmhdr.code){
        case SCN_MODIFIED:
            if(n->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT)){
                ++d.revision;d.dirty=true;d.changed=GetTickCount64();
                if(completion.buffer==d.id)ClearCompletion();
                ClearDiagnostics(d);
            }
            break;
        case SCN_CHARADDED:
            if(d.id != Active()) break;
            if(d.session->ready && !d.session->failed && !Sci(Editor(d.id),SCI_GETREADONLY)) {
                if(n->ch=='(' || n->ch==',') scheduledMethod=SignatureMethod;
                else if(n->ch=='.' || n->ch=='_' || (n->ch>='a' && n->ch<='z') || (n->ch>='A' && n->ch<='Z') || (n->ch>='0' && n->ch<='9')) scheduledMethod=CompleteMethod;
                else scheduledMethod.clear();
                scheduledAt=GetTickCount64()+200;
            }
            break;
        case SCN_FOCUSOUT:++epoch;scheduledMethod.clear();ClearCompletion();signatureBuffer=0;break;
        case SCN_AUTOCSELECTION:
            if(completion.buffer==d.id){InsertCompletion(*n);return TRUE;}break;
        case SCN_AUTOCCHARDELETED:
            if(completion.buffer==d.id){ClearCompletion();return TRUE;}break;
        case SCN_AUTOCCANCELLED:if(completion.buffer==d.id)ClearCompletion(false);break;
        case SCN_CALLTIPCLICK:
            if(signatureBuffer==d.id && !signatures.empty()){signatureIndex=(signatureIndex+(n->position==1?signatures.size()-1:1))%signatures.size();Signature();return TRUE;}break;
        case SCN_DWELLSTART:if(d.id==Active() && d.session->ready && n->position>=0){Request(HoverMethod,n->position);return TRUE;}break;
        case SCN_DWELLEND:if(d.id==Active()){++hoverEpoch;Sci(Editor(d.id),SCI_CALLTIPCANCEL);}break;
        }
    }catch(const std::exception& e){ClearCompletion();Error(e);return selecting;}
    return FALSE;
}
extern "C" __declspec(dllexport) void beNotified(SCNotification* n){
    if(!n)return;
    try {
        if(n->nmhdr.code==NPPN_SHUTDOWN){Stop();if(timerWindow)DestroyWindow(timerWindow);timerWindow=nullptr;UnregisterClassW(L"MultiPad4.LSPClient",instance);return;}
        if(!enabled)return;
        if(n->nmhdr.code==NPPN_BUFFERACTIVATED || n->nmhdr.code==NPPN_FILEOPENED){++epoch;scheduledMethod.clear();ClearCompletion();signatureBuffer=0;Attach();}
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
