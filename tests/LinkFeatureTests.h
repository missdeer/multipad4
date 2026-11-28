#pragma once
#include "MainFrame.h"
#include "QAArtifacts.h"
#include <stdexcept>
#include <source_location>
inline void RunLinkFeatureTests(MainFrame& frame) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) { if (!ok) throw std::runtime_error("Link feature assertion failed at line " + std::to_string(where.line())); };
    frame.NewDocument(); auto* document = frame.Active();
    const std::string text = "www.baidu.com\nhttps://192.168.1.1\nhttp://192.168.1.2\nuser+tag@example.com.\nordinary text a@b a..b@example.com\n";
    document->Send(SCI_SETTEXT,0,reinterpret_cast<LPARAM>(text.c_str()));
    document->Send(SCI_COLOURISE,0,-1);
    auto target = [&](const char* word) { return document->LinkTargetAt(static_cast<LRESULT>(text.find(word))); };
    require(target("www.baidu.com") == L"https://www.baidu.com");
    require(target("https://192") == L"https://192.168.1.1");
    require(target("http://192") == L"http://192.168.1.2");
    require(target("user+") == L"mailto:user+tag@example.com");
    require(target("ordinary").empty() && target("a@b").empty() && target("a..b").empty());
    require(document->Send(SCI_STYLEGETUNDERLINE,STYLE_LINK));
    require(document->Send(SCI_STYLEGETHOTSPOT,STYLE_LINK));
    // Recognition must not alter the buffer.
    require(document->Text() == text);
    frame.NewDocument(); auto* second = frame.Active();
    second->Send(SCI_SETTEXT,0,reinterpret_cast<LPARAM>("ordinary text"));
    second->Send(SCI_COLOURISE,0,-1);
    require(second->LinkTargetAt(2).empty() && target("www.baidu.com") == L"https://www.baidu.com");
    second->Send(SCI_SETSAVEPOINT); frame.Close(frame.tabs.GetActivePage());
    auto previous = qaArtifactDirectory;
    qaArtifactDirectory = std::filesystem::current_path() / L"tmp/link-artifacts";
    frame.ShowWindow(SW_SHOWNORMAL); frame.Refresh();
    CaptureQAWindow(frame,L"underlined-web-and-email-links");
    qaArtifactDirectory = previous;
    document->Send(SCI_SETTEXT,0,reinterpret_cast<LPARAM>("plain text"));
    document->Send(SCI_COLOURISE,0,-1);
    require(document->LinkTargetAt(2).empty());
    document->Send(SCI_SETTEXT,0,reinterpret_cast<LPARAM>("www.user@example.com\n"));
    document->Send(SCI_COLOURISE,0,-1);
    require(document->LinkTargetAt(0) == L"mailto:www.user@example.com");
    document->path=L"links.md"; document->ConfigureLexer();
    document->Send(SCI_SETTEXT,0,reinterpret_cast<LPARAM>("[site](https://example.com)\n<https://example.org>\n"));
    document->Send(SCI_COLOURISE,0,-1);
    require(document->LinkTargetAt(8) == L"https://example.com");
    require(document->LinkTargetAt(29) == L"https://example.org");
    document->path=L"links.reb"; document->ConfigureLexer();
    document->Send(SCI_SETTEXT,0,reinterpret_cast<LPARAM>("https://example.com user@example.com\n"));
    document->Send(SCI_COLOURISE,0,-1);
    require(document->LinkTargetAt(0) == L"https://example.com");
    require(document->LinkTargetAt(20) == L"mailto:user@example.com");
    document->Send(SCI_SETSAVEPOINT); frame.Close(frame.tabs.GetActivePage());
}
