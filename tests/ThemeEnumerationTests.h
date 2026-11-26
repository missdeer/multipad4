#pragma once
#include "ThemeEnumerationHooks.h"
#include "StyleDialogTests.h"
#include "ThemeDialog.h"
#include "ConfigurationXml.h"
#include "EditTransformTests.h"
#include <source_location>
namespace themeenumerationtest {
inline void Require(bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) throw std::runtime_error("Theme enumeration regression at line " + std::to_string(at.line()));
}
inline std::vector<std::wstring> Labels(HWND dialog) {
    std::vector<std::wstring> result;
    for (LRESULT i = 0; i < SendDlgItemMessageW(dialog, IDC_THEME_LIST, CB_GETCOUNT, 0, 0); ++i) {
        auto length = SendDlgItemMessageW(dialog, IDC_THEME_LIST, CB_GETLBTEXTLEN, i, 0); Require(length >= 0);
        std::wstring label(static_cast<size_t>(length) + 1, L'\0'); SendDlgItemMessageW(dialog, IDC_THEME_LIST, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(label.data())); label.resize(length); result.push_back(std::move(label));
    }
    return result;
}
}
inline void RunThemeEnumerationTests(MainFrame& frame, bool benchmark) {
    using namespace themeenumerationtest;
    const auto folder = std::filesystem::temp_directory_path() / (L"MultiPad4-theme-enum-" + std::to_wstring(GetCurrentProcessId()));
    const auto themes = folder / L"config/themes"; std::filesystem::create_directories(themes);
    auto write = [&](const wchar_t* name, const std::string& text) { const auto path = themes / name; files::WriteAtomic(path, {text}); return path; };
    const auto changed = write(L"changed.xml", "<theme version='1' name='First label'><style id='cpp.number' fg='010203'/></theme>");
    write(L"duplicate-a.xml", "<theme version='1' name='Duplicate label'/>"); write(L"duplicate-b.xml", "<theme version='1' name='Duplicate label'/>");
    const auto malformed = write(L"malformed.xml", "<theme version='1' name='Malformed body'><style");
    const auto dtd = write(L"dtd.xml", "<!DOCTYPE theme [<!ENTITY name 'unsafe'>]><theme version='1' name='&name;'/>");
    bool rejected = false; try { ReadConfigurationXmlRoot(dtd); } catch (const std::exception&) { rejected = true; } Require(rejected);
    rejected = false; try { LoadTheme(malformed); } catch (const std::exception&) { rejected = true; } Require(rejected);
    Require(ReadConfigurationXmlRoot(changed).Attribute(L"name") == L"First label");
    auto* d = frame.NewDocument(); auto* peer = frame.NewDocument(); Require(d && peer);
    d->Load({"int value = 1;\n"}); peer->Load({"isolated peer"}); frame.tabs.SetActivePage(frame.tabs.GetPageCount() - 2);
    const auto saved = d->style.theme, peerTheme = peer->style.theme;
    LoadThemePreferences(folder / L"settings.ini");
    auto open = [&](bool old) {
        baseline = old; std::vector<std::wstring> labels;
        StyleDialogTest::RunCommand(frame, IDM_VIEW_SCHEME_CONFIG, [&](HWND dialog) { labels = Labels(dialog); }, IDCANCEL);
        baseline = false; Require(d->style.theme == saved && peer->style.theme == peerTheme); return labels;
    };
    auto labels = open(false); Require(std::count(labels.begin(), labels.end(), L"Duplicate label") == 2);
    Require(std::find(labels.begin(), labels.end(), L"First label") != labels.end());
    Require(std::find(labels.begin(), labels.end(), L"Malformed body") != labels.end());
    Require(std::find(labels.begin(), labels.end(), L"dtd") != labels.end());
    write(L"changed.xml", "<theme version='1' name='Updated label'><style id='cpp.number' fg='040506'/></theme>");
    labels = open(false); Require(std::find(labels.begin(), labels.end(), L"Updated label") != labels.end());
    // Fully decode only the selected theme, retaining cancellation isolation.
    StyleDialogTest::RunCommand(frame, IDM_VIEW_SCHEME_CONFIG, [&](HWND dialog) {
        const auto index = SendDlgItemMessageW(dialog, IDC_THEME_LIST, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(L"Updated label")); Require(index >= 0);
        SendDlgItemMessageW(dialog, IDC_THEME_LIST, CB_SETCURSEL, index, 0); SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDC_THEME_LIST, CBN_SELCHANGE), 0);
        Require(d->style.theme && d->style.theme->name == L"Updated label" && peer->style.theme == peerTheme);
    }, IDCANCEL);
    Require(d->style.theme == saved && peer->style.theme == peerTheme);
    ThemeDefinition theme;
    for (const auto* language : LanguageCatalog()) for (unsigned slot : {0u, 2u, 7u})
        theme.keywords[{language->id, slot}] = (slot == 2 ? "replace:" : "append:") + language->id + "_" + std::to_string(slot);
    const auto base = CurrentConfiguration(); baseline = true; const auto before = ConfigurationWithTheme(base, theme); baseline = false;
    const auto after = ConfigurationWithTheme(base, theme); Require(before->rulesVersion == after->rulesVersion);
    for (size_t i = 0; i < 90; ++i) Require(before->languages[i]->rulesVersion == after->languages[i]->rulesVersion && before->languages[i]->keywords == after->languages[i]->keywords && before->languages[i]->keywordAttr == after->languages[i]->keywordAttr);
    if (benchmark) {
        std::filesystem::remove(malformed); std::filesystem::remove(dtd);
        std::string body = "<theme version='1' name='Measured theme'>"; for (int i = 0; i < 2000; ++i) body += "<x-metadata value='unchanged'/>"; body += "</theme>";
        for (int i = 0; i < 128; ++i) write((L"bulk-" + std::to_wstring(i) + L".xml").c_str(), body);
        std::ofstream csv("theme-enumeration-results.csv"); csv << "iteration,path,files,dialog_ms,enumeration_ms,cpu_ms,private_before,peak_private,keyword_ms,keyword_visits\n";
        for (int iteration = 0; iteration < 7; ++iteration) {
            std::vector<std::wstring> expected;
            for (bool old : {true, false}) {
                const auto memory = transformtest::PrivateBytes(); std::atomic<SIZE_T> peak{memory};
                std::jthread sampler([&](std::stop_token stop) { while (!stop.stop_requested()) { peak.store((std::max)(peak.load(), transformtest::PrivateBytes())); Sleep(1); } });
                const auto cpu = transformtest::CpuMilliseconds(); const auto started = std::chrono::steady_clock::now(); labels = open(old);
                const auto dialogMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(), cpuMs = transformtest::CpuMilliseconds() - cpu;
                const auto enumMs = enumerationMs; sampler.request_stop(); sampler.join();
                if (old) expected = labels; else Require(labels == expected);
                baseline = old; keywordVisits = 0; const auto keywordStarted = std::chrono::steady_clock::now();
                const auto result = ConfigurationWithTheme(base, theme);
                const auto keywordMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - keywordStarted).count(); baseline = false;
                Require(result->rulesVersion == before->rulesVersion);
                csv << iteration << ',' << (old ? "before" : "after") << ',' << labels.size() << ',' << dialogMs << ',' << enumMs << ',' << cpuMs << ',' << memory << ',' << peak.load() << ',' << keywordMs << ',' << keywordVisits << '\n'; csv.flush();
            }
        }
    }
    LoadThemePreferences(frame.settings.path);
    for (auto* document : {d, peer}) { document->Send(SCI_SETSAVEPOINT); document->encodingDirty = false;
        for (int i = 0; i < frame.tabs.GetPageCount(); ++i) if (frame.tabs.GetPageData(i) == document) { Require(frame.Close(i)); break; }
    }
    std::filesystem::remove_all(folder);
}
