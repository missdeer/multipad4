#pragma once
#include "CompletionIndentBaseline.h"
#include <chrono>
#include <cstdio>
#include <source_location>

inline void RunIndentReadTests(MainFrame& frame, bool benchmark) {
    auto require = [](bool value, std::source_location where = std::source_location::current()) {
        if (!value) throw std::runtime_error("Indent read regression at " + std::to_string(where.line()));
    };
    auto* document = frame.NewDocument();
    document->style.completion = document->style.autoIndent = false;
    struct Result { std::string text, undone; LRESULT caret; bool bookmark; };
    auto run = [&](bool baseline, const std::string& text, const wchar_t* language, int eol, int width) {
        require(SelectDocumentScheme(*document, language)); document->Load({text});
        document->Send(SCI_SETEOLMODE, eol); document->Send(SCI_SETTABWIDTH, width); document->Send(SCI_SETINDENT, width);
        document->Send(SCI_SETUSETABS, FALSE); document->Send(SCI_SETTABINDENTS, TRUE);
        document->Send(SCI_COLOURISE, 0, -1);
        if (wcscmp(language, L"sql") == 0) {
            document->Send(SCI_STARTSTYLING, 0);
            document->Send(SCI_SETSTYLING, 5, GetDocumentLexer(*document)->autoIdentWordStyle);
            document->Send(SCI_SETFOLDLEVEL, 0, SC_FOLDLEVELBASE | SC_FOLDLEVELHEADERFLAG);
            document->Send(SCI_SETFOLDLEVEL, 1, SC_FOLDLEVELBASE + 1);
        }
        document->Send(SCI_GOTOPOS, document->Send(SCI_POSITIONFROMLINE, document->Send(SCI_GETLINECOUNT) - 1));
        document->Send(SCI_MARKERADD, 0, 0);
        document->Send(SCI_BEGINUNDOACTION);
        if (baseline) indentbaseline::ApplyDocumentAutoIndent(*document); else ApplyDocumentAutoIndent(*document);
        document->Send(SCI_ENDUNDOACTION);
        Result result{document->Text(), {}, document->Send(SCI_GETCURRENTPOS), document->Send(SCI_MARKERGET, 0) != 0};
        if (document->Send(SCI_CANUNDO)) document->Send(SCI_UNDO);
        result.undone = document->Text();
        return result;
    };
    for (int eol : {SC_EOL_LF, SC_EOL_CRLF, SC_EOL_CR}) {
        const std::string newline = eol == SC_EOL_CRLF ? "\r\n" : eol == SC_EOL_CR ? "\r" : "\n";
        for (const auto& line : {std::string("    value"), std::string("\t\ttext"), std::string("    {"), std::string("    /**"), std::string("    /*!"), std::string("    :"), std::string("   ")}) {
            const auto text = line + newline + "}";
            const auto before = run(true, text, L"cpp", eol, 4), after = run(false, text, L"cpp", eol, 4);
            require(before.text == after.text && before.caret == after.caret && before.bookmark == after.bookmark && before.undone == after.undone);
        }
    }
    for (const auto* word : {"BEGIN", "START"}) {
        const std::string text = std::string(word) + std::string(65530, ' ') + "TrAnSaCtIoN\n";
        const auto before = run(true, text, L"sql", SC_EOL_LF, 4), after = run(false, text, L"sql", SC_EOL_LF, 4);
        require(before.text == after.text && before.caret == after.caret && before.undone == after.undone);
        require(after.text.find("COMMIT;") != std::string::npos);
    }
    if (benchmark) {
        frame.ShowWindow(SW_SHOWNORMAL);
        for (int workload : {0, 1}) for (bool baseline : {true, false}) {
            const auto text = workload == 0 ? "    " + std::string(8 * 1024 * 1024, 'x') + "\n" : std::string(2 * 1024 * 1024, ' ') + "x\n";
            std::vector<double> callbacks, painted;
            for (int sample = 0; sample < 11; ++sample) {
                require(SelectDocumentScheme(*document, L"txt")); document->Load({text});
                document->Send(SCI_GOTOPOS, document->Send(SCI_GETLENGTH));
                const auto start = std::chrono::steady_clock::now();
                if (baseline) indentbaseline::ApplyDocumentAutoIndent(*document); else ApplyDocumentAutoIndent(*document);
                const auto done = std::chrono::steady_clock::now();
                RedrawWindow(frame.m_hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                callbacks.push_back(std::chrono::duration<double, std::milli>(done - start).count());
                painted.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
                require(document->Text() == text + (workload ? std::string(2 * 1024 * 1024, ' ') : "    "));
            }
            std::sort(callbacks.begin(), callbacks.end()); std::sort(painted.begin(), painted.end());
            std::printf("indent workload=%d variant=%s median_ms=%.6f p95_p99_max_ms=%.6f paint_median_ms=%.6f bytes=%zu\n", workload, baseline ? "before" : "after", callbacks[5], callbacks.back(), painted[5], text.size());
        }
    }
    document->Send(SCI_SETSAVEPOINT); require(frame.Close(frame.tabs.GetActivePage()));
}
