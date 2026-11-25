#include "PluginDownload.h"
#include "FileIO.h"
#include <windows.h>
#include <winhttp.h>
#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace {
constexpr wchar_t UserAgent[] = L"MultiPad4";
constexpr DWORD TimeoutMs = 30000;
constexpr size_t ChunkSize = 64 * 1024;

struct HttpHandle {
    HINTERNET value;
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
};

void CheckHttp(BOOL result, const char* action) {
    if (!result) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), action);
}

void Download(const std::wstring& url, std::ostream& output, std::wstring& host, size_t maximumBytes) {
    URL_COMPONENTSW parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    CheckHttp(WinHttpCrackUrl(url.c_str(), 0, 0, &parts), "Invalid package URL");
    if (parts.nScheme != INTERNET_SCHEME_HTTPS || !parts.dwHostNameLength || parts.dwUserNameLength || parts.dwPasswordLength)
        throw std::runtime_error("Invalid HTTPS package URL.");
    host.assign(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring resource = parts.dwUrlPathLength ? std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) : L"/";
    if (parts.dwExtraInfoLength) resource.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (auto fragment = resource.find(L'#'); fragment != std::wstring::npos) resource.resize(fragment);

    HttpHandle session{WinHttpOpen(UserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0)};
    CheckHttp(session.value != nullptr, "WinHttpOpen");
    CheckHttp(WinHttpSetTimeouts(session.value, TimeoutMs, TimeoutMs, TimeoutMs, TimeoutMs), "WinHttpSetTimeouts");
    HttpHandle connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};
    CheckHttp(connection.value != nullptr, "WinHttpConnect");
    HttpHandle request{WinHttpOpenRequest(connection.value, L"GET", resource.c_str(), nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE)};
    CheckHttp(request.value != nullptr, "WinHttpOpenRequest");
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    CheckHttp(WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)), "WinHttpSetOption");
    CheckHttp(WinHttpSendRequest(request.value, nullptr, 0, nullptr, 0, 0, 0), "WinHttpSendRequest");
    CheckHttp(WinHttpReceiveResponse(request.value, nullptr), "WinHttpReceiveResponse");
    DWORD status = 0, size = sizeof(status);
    CheckHttp(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &status, &size, nullptr), "WinHttpQueryHeaders");
    if (status != 200) throw std::runtime_error("HTTP status " + std::to_string(status));
    std::array<char, ChunkSize> buffer;
    for (;;) {
        DWORD read = 0;
        CheckHttp(WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read), "WinHttpReadData");
        if (!read) return;
        if (read > maximumBytes) throw std::runtime_error("Download exceeds the size limit.");
        maximumBytes -= read;
        output.write(buffer.data(), read);
    }
}
}

void DownloadPackage(const std::wstring& url, const std::filesystem::path& destination, size_t maximumBytes) {
    std::wstring host;
    try {
        std::ofstream output;
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output.open(destination, std::ios::binary);
        Download(url, output, host, maximumBytes);
        output.close();
    } catch (const std::exception& error) {
        throw std::runtime_error("Host " + files::ToUtf8(host) + ": " + error.what());
    }
}

std::string DownloadReleaseMetadata(const std::wstring& url) {
    std::ostringstream output;
    output.exceptions(std::ios::failbit | std::ios::badbit);
    std::wstring host;
    Download(url, output, host, 1024 * 1024);
    return std::move(output).str();
}
