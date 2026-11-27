        if (sessioncapturetest::baseline) {
        for (size_t history = 0; history < 32; ++history) {
            profile.WriteString(section.c_str(), (L"FindHistory" + std::to_wstring(history)).c_str(), history < d->search.findHistory.size() ? d->search.findHistory[history] : L"");
            profile.WriteString(section.c_str(), (L"ReplaceHistory" + std::to_wstring(history)).c_str(), history < d->search.replaceHistory.size() ? d->search.replaceHistory[history] : L"");
        }
            sessioncapturetest::historyWrites += 64;
        } else
