#include "../plugins/LSPClient/Transport.h"
#include <iostream>
#include <chrono>
#include <source_location>
#include <io.h>
#include <fcntl.h>
#include <fstream>
using namespace lsp;
void Require(bool value, std::source_location where=std::source_location::current()) {if(!value)throw std::runtime_error("LSP test failed at line "+std::to_string(where.line()));}
int main(int argc,char** argv) {
    try {
        if(argc>1) {
            if(std::string_view(argv[1])=="--idle-server") { Sleep(INFINITE); return 0; }
            if(std::string_view(argv[1])=="--exit-test") { std::cerr<<"mock server fatal detail"<<std::endl; return 37; }
            _setmode(_fileno(stdout),_O_BINARY);
            Decoder decoder;char bytes[8192]; DWORD count;
            const bool mock=std::string_view(argv[1])=="--mock-lsp";
            std::string uri;
            unsigned completions=0;
            std::map<std::string,std::string> documents;
            while(ReadFile(GetStdHandle(STD_INPUT_HANDLE),bytes,sizeof(bytes),&count,nullptr) && count) {
                decoder.Feed({bytes,count},[&](Json value){
                    auto send=[](Json response){std::cout<<Frame(response)<<std::flush;};
                    if(!mock){send({{"jsonrpc","2.0"},{"id",value.at("id")},{"result",value.at("params")}});return;}
                    const auto method=value.value("method","");
                    Json result=nullptr;
                    if(method=="initialize")result={{"capabilities",{{"textDocumentSync",{{"openClose",true},{"change",2},{"save",true}}},{"completionProvider",Json::object()},{"hoverProvider",true},{"definitionProvider",true}}}};
                    else if(method=="textDocument/didOpen"){
                        uri=value["params"]["textDocument"]["uri"].get<std::string>();
                        documents[uri]=value["params"]["textDocument"]["text"].get<std::string>();
                        auto notification=Json::parse(R"({"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{"uri":"","version":1,"diagnostics":[{"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":1}},"message":"mock diagnostic","severity":1}]}})");
                        notification["params"]["uri"]=uri;
                        notification["params"]["diagnostics"][0]["range"]["end"]["character"]=999999;
                        auto malformed=notification["params"]["diagnostics"][0];
                        malformed["range"]["end"]["character"]="invalid";
                        notification["params"]["diagnostics"].insert(notification["params"]["diagnostics"].begin(),malformed);
                        send(notification);
                    }
                    else if(method=="textDocument/didChange") {
                        auto& text=documents.at(value["params"]["textDocument"]["uri"].get<std::string>());
                        for(const auto& change:value["params"]["contentChanges"]) {
                            auto start=Offset(text,change["range"]["start"]),end=Offset(text,change["range"]["end"]);
                            text.replace(start,end-start,change["text"].get<std::string>());
                        }
                        if(text=="exit-server")ExitProcess(37);
                    }
                    else if(method=="textDocument/completion"){
                        Sleep(100);
                        if(++completions==1) {
                            send({{"jsonrpc","2.0"},{"id",value["id"]},{"result",{{"isIncomplete",true},{"items",Json::array()}}}});
                            return;
                        }
                        if(completions==2)Require(value["params"]["context"]["triggerKind"]==3);
                        result=Json::parse(R"([{"label":"hello","detail":"completion documentation","textEdit":{"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":2}},"newText":"hello"},"additionalTextEdits":[{"range":{"start":{"line":0,"character":2},"end":{"line":0,"character":2}},"newText":";"}]}])");
                        const auto& current=documents.at(value["params"]["textDocument"]["uri"].get<std::string>());
                        if(current=="fb")result=Json::array({{{"label","fooBar"}}});
                        else if(current=="alias")result=Json::array({{{"label","Pretty Display"},{"filterText","alias"},{"insertText","resolvedText"}}});
                        if(current=="he2") {
                            result[0]["label"]="he2Result";
                            result[0]["textEdit"]["range"]["end"]["character"]=3;
                            result[0]["additionalTextEdits"][0]["range"]["start"]["character"]=3;
                            result[0]["additionalTextEdits"][0]["range"]["end"]["character"]=3;
                        }
                    }else if(method=="textDocument/hover") {
                        send({{"jsonrpc","2.0"},{"method","window/logMessage"},{"params",{{"message",7}}}});
                        result={{"contents",{{"kind","plaintext"},{"value","Mock hover documentation"}}}};
                    }
                    else if(method=="textDocument/signatureHelp")result=Json::parse(R"json({"signatures":[{"label":"hello(int value)","parameters":[{"label":[6,15]}]},{"label":"hello(double value)","parameters":[{"label":[6,18]}]}],"activeSignature":null,"activeParameter":null})json");
                    else if(method=="textDocument/definition") {
                        result=Json::parse(R"([{"uri":"","range":{"start":{"line":0,"character":0},"end":{"line":0,"character":1}}}])");
                        result[0]["uri"]=uri;
                    }
                    if(value.contains("id"))send({{"jsonrpc","2.0"},{"id",value["id"]},{"result",result}});
                });
            }
            return 0;
        }
        const std::string text="a\xf0\x9f\x98\x80\xe4\xb8\xad\r\nnext\rlast\n";
        Require(Position(text,5)==Json({{"line",0},{"character",3}}));
        Require(Offset(text,{{"line",0},{"character",4}})==8);
        Require(Offset(text,{{"line",1},{"character",2}})==12);
        Require(Offset(text,{{"line",2},{"character",0}})==15);
        bool rejected=false;try{Offset(text,{{"line",0},{"character",2}});}catch(...){rejected=true;}Require(rejected);
        for(const auto& pair : std::vector<std::pair<std::string,std::string>>{
            {"abc","abXc"},{text,text+"!"},{"a\r\nb","a\nb"},{"a\xf0\x9f\x98\x80","a\xf0\x9f\x98\x81"},{"abc",""},{"","abc"}}) {
            auto change=Change(pair.first,pair.second);
            auto begin=Offset(pair.first,change["range"]["start"]),end=Offset(pair.first,change["range"]["end"]);
            auto applied=pair.first;applied.replace(begin,end-begin,change["text"].get<std::string>());Require(applied==pair.second);
        }
        Json value{{"jsonrpc","2.0"},{"id",1},{"params",{{"text",text}}}};
        auto frame=Frame(value);Decoder decoder;int count=0;
        for(char c:frame)decoder.Feed({&c,1},[&](Json message){Require(message==value);++count;});
        decoder.Feed(frame+frame,[&](Json message){Require(message==value);++count;});Require(count==3);
        for(auto invalid:{"Content-Length: -1\r\n\r\n","Content-Length: 999999999\r\n\r\n","Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}","Other: 2\r\n\r\n{}"}) {
            bool threw=false;try{Decoder bad;bad.Feed(invalid,[](Json){});}catch(...){threw=true;}Require(threw);
        }
        Require(Quote(L"a b\\")==L"\"a b\\\\\"");
        wchar_t executable[32768];GetModuleFileNameW(nullptr,executable,_countof(executable));
        {
            Transport transport(Quote(executable)+L" --server",std::filesystem::path(executable).parent_path());
            transport.Send(value);bool returned=false;
            for(int i=0;i<200 && !returned;++i) {
                for(const auto& response:transport.Poll()){Require(response.at("result")==value.at("params"));returned=true;}
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            Require(returned);
        }
        // Destruction must cancel a reader blocked on an idle server.
        {Transport transport(Quote(executable)+L" --server",std::filesystem::path(executable).parent_path());}
        {
            Transport transport(Quote(executable)+L" --idle-server",std::filesystem::path(executable).parent_path());
            transport.Send({{"jsonrpc","2.0"},{"method","test"},{"params",std::string(1024*1024,'x')}});
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        const auto log=std::filesystem::temp_directory_path()/(L"MultiPad4-LSP-exit-"+std::to_wstring(GetCurrentProcessId())+L".log");
        {
            Transport transport(Quote(executable)+L" --exit-test",std::filesystem::path(executable).parent_path(),log);
            bool exited=false;
            for(int i=0;i<200 && !exited;++i) {
                exited=transport.Error().find("0x00000025")!=std::string::npos;
                if(!exited)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            Require(exited);
        }
        {std::ifstream input(log);std::string detail((std::istreambuf_iterator<char>(input)),{});Require(detail.find("mock server fatal detail")!=std::string::npos);}
        std::filesystem::remove(log);
        std::cout<<"LSP framing, UTF-16, quoting, process I/O and cancellation passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
