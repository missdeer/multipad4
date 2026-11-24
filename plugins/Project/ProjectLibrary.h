#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace project {
struct Member {
    std::filesystem::path path;
    bool folder = false;
};

struct Project {
    std::wstring name;
    std::vector<Member> members;
    std::filesystem::path base;
    bool autoLoad = false;
};

struct Library {
    std::vector<Project> projects{{L"Default", {}, {}, false}};
    size_t active = 0;
    std::wstring description;
    std::optional<std::string> original;
};

constexpr wchar_t DefaultLibrary[] = L"Project.mp4projects";
constexpr wchar_t LibrarySelection[] = L"Project-library.txt";
bool SameText(const std::wstring& left, const std::wstring& right);
bool AddMember(Project& project, Member member);
bool Contains(const Project& project, const std::filesystem::path& path);
Library ReadLibrary(const std::filesystem::path& path, bool allowMissing);
void SaveLibrary(const std::filesystem::path& path, const Library& library);
}
