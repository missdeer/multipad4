#pragma once
#include "../../3rdparty/nlohmann-json/json.hpp"
#include <windows.h>
#include <string>
#include <string_view>
#include <stdexcept>
#include <charconv>

namespace lsp {
using Json = nlohmann::json;
constexpr size_t MaxMessage = 32 * 1024 * 1024;
// LSP uses UTF-16 code units even though Scintilla stores UTF-8 bytes.
inline size_t Units(std::string_view text) {
    size_t result = 0;
    for (unsigned char c : text) if ((c & 0xc0) != 0x80) result += c >= 0xf0 ? 2 : 1;
    return result;
}
inline Json Position(std::string_view text, size_t byte) {
    byte = (std::min)(byte, text.size());
    size_t line = 0, start = 0;
    for (size_t i = 0; i < byte; ++i) {
        if (text[i] == '\r') { ++line; if (i + 1 < byte && text[i + 1] == '\n') ++i; start = i + 1; }
        else if (text[i] == '\n') { ++line; start = i + 1; }
    }
    return {{"line", line}, {"character", Units(text.substr(start, byte - start))}};
}
inline size_t Offset(std::string_view text, const Json& position) {
    if (!position.at("line").is_number_integer() || !position.at("character").is_number_integer())
        throw std::runtime_error("Non-integer LSP position");
    const auto line = position.at("line").get<int64_t>(), column = position.at("character").get<int64_t>();
    if (line < 0 || column < 0 || line > INT_MAX || column > INT_MAX) throw std::runtime_error("Negative LSP position");
    size_t byte = 0;
    for (int i = 0; i < line; ++i) {
        while (byte < text.size() && text[byte] != '\n' && text[byte] != '\r') ++byte;
        if (byte == text.size()) throw std::runtime_error("LSP line out of range");
        if (text[byte++] == '\r' && byte < text.size() && text[byte] == '\n') ++byte;
    }
    int units = 0;
    while (units < column && byte < text.size() && text[byte] != '\r' && text[byte] != '\n') {
        const auto c = static_cast<unsigned char>(text[byte]);
        units += c >= 0xf0 ? 2 : 1;
        ++byte;
        while (byte < text.size() && (static_cast<unsigned char>(text[byte]) & 0xc0) == 0x80) ++byte;
    }
    if (units != column) throw std::runtime_error("LSP column out of range");
    return byte;
}
inline Json Change(std::string_view before, std::string_view after) {
    size_t start = 0, oldEnd = before.size(), newEnd = after.size();
    while (start < oldEnd && start < newEnd && before[start] == after[start]) ++start;
    // Keep UTF-8 characters and CRLF pairs whole at both ends of the edit.
    auto inside = [](std::string_view text, size_t byte) {
        return byte < text.size() && ((static_cast<unsigned char>(text[byte]) & 0xc0) == 0x80 ||
            (byte && text[byte] == '\n' && text[byte - 1] == '\r'));
    };
    while (start && (inside(before, start) || inside(after, start))) --start;
    while (oldEnd > start && newEnd > start && before[oldEnd - 1] == after[newEnd - 1]) { --oldEnd; --newEnd; }
    while (inside(before, oldEnd) || inside(after, newEnd)) { ++oldEnd; ++newEnd; }
    return {{"range", {{"start", Position(before, start)}, {"end", Position(before, oldEnd)}}},
        {"text", after.substr(start, newEnd - start)}};
}
inline std::wstring Quote(std::wstring_view arg) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        if (c == L'"') result += L'\\';
        slashes = 0; result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
inline std::string Frame(const Json& value) {
    auto body = value.dump();
    if (body.size() > MaxMessage) throw std::runtime_error("LSP message too large");
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}
class Decoder {
    std::string buffer;
public:
    template<class Receive> void Feed(std::string_view bytes, Receive receive) {
        buffer.append(bytes);
        for (;;) {
            auto end = buffer.find("\r\n\r\n");
            if (end == std::string::npos) {
                if (buffer.size() > 8192) throw std::runtime_error("LSP header too large");
                return;
            }
            if (end > 8192) throw std::runtime_error("LSP header too large");
            size_t length = 0; bool found = false;
            for (size_t start = 0; start < end;) {
                const auto next = buffer.find("\r\n", start);
                auto header = std::string_view(buffer).substr(start, next - start);
                auto colon = header.find(':');
                if (colon == std::string_view::npos) throw std::runtime_error("Malformed LSP header");
                if (_strnicmp(header.data(), "Content-Length", colon) == 0 && colon == 14) {
                    if (found) throw std::runtime_error("Duplicate LSP length");
                    auto value = header.substr(colon + 1);
                    while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
                    auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), length);
                    if (error != std::errc{} || ptr != value.data() + value.size() || length > MaxMessage || !length)
                        throw std::runtime_error("Invalid LSP length");
                    found = true;
                }
                start = next + 2;
            }
            if (!found) throw std::runtime_error("Missing LSP length");
            if (buffer.size() - end - 4 < length) return;
            auto value = Json::parse(buffer.begin() + end + 4, buffer.begin() + end + 4 + length);
            if (!value.is_object() || value.value("jsonrpc", "") != "2.0") throw std::runtime_error("Invalid JSON-RPC message");
            receive(std::move(value));
            buffer.erase(0, end + 4 + length);
        }
    }
};
}
