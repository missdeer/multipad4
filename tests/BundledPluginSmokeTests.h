#pragma once
#include "Base64PluginTests.h"
#ifdef MULTIPAD4_TEST_WEB_TOOLS
#include "WebToolsPluginTests.h"
#endif
#ifdef MULTIPAD4_TEST_SELECTION_ACTIONS
#include "SelectionActionsPluginTests.h"
#endif
#ifdef MULTIPAD4_TEST_TEXT_TRANSLITERATION
#include "TextTransliterationPluginTests.h"
#endif

inline constexpr std::wstring_view bundledTestModules[] = {
    L"Matepath", L"OpenDocuments", L"Base64",
#ifdef MULTIPAD4_TEST_SELECTION_ACTIONS
    L"SelectionActions",
#endif
#ifdef MULTIPAD4_TEST_TEXT_TRANSLITERATION
    L"TextTransliteration",
#endif
#ifdef MULTIPAD4_TEST_WEB_TOOLS
    L"WebTools",
#endif
};

inline void RunBundledPluginSmokeTests(MainFrame& frame, EditorDocument& document,
                                       [[maybe_unused]] const std::filesystem::path& directory) {
    RunBase64PluginTests(frame, document);
#ifdef MULTIPAD4_TEST_WEB_TOOLS
    RunWebToolsPluginTests(frame, document);
#endif
#ifdef MULTIPAD4_TEST_SELECTION_ACTIONS
    RunSelectionActionsPluginTests(frame, document, directory);
#endif
#ifdef MULTIPAD4_TEST_TEXT_TRANSLITERATION
    RunTextTransliterationPluginTests(frame, document);
#endif
}
