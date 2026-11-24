#include "ProjectLibrary.h"
#include "FileIO.h"
#include "../../3rdparty/nlohmann-json/json.hpp"
#include <windows.h>
#include <algorithm>
#include <stdexcept>

namespace project {
namespace fs = std::filesystem;
using nlohmann::json;

bool SameText(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool AddMember(Project& project, Member member) {
    if (std::ranges::any_of(project.members, [&](const Member& existing) {
        return existing.folder == member.folder && SameText(existing.path.native(), member.path.native());
    })) return false;
    project.members.push_back(std::move(member));
    return true;
}

bool Contains(const Project& project, const fs::path& path) {
    if (path.empty()) return false;
    const auto parent = path.parent_path();
    return std::ranges::any_of(project.members, [&](const Member& member) {
        const auto& candidate = member.folder ? parent : path;
        if (SameText(member.path.native(), candidate.native())) return true;
        std::error_code error;
        return fs::equivalent(member.path, candidate, error);
    });
}

namespace {
std::optional<std::string> ReadContents(const fs::path& path) {
    if (!fs::exists(path)) return std::nullopt;
    return files::ReadWithEncoding(path, files::Encoding::Utf8).utf8;
}

std::wstring Text(const json& value) {
    auto text = files::FromUtf8(value.get<std::string>());
    if (text.find(L'\0') != std::wstring::npos) throw std::runtime_error("Invalid text in project library.");
    return text;
}

fs::path ReadPath(const json& value, const fs::path& directory) {
    fs::path path = Text(value);
    if (path.empty()) return {};
    if (!path.is_absolute()) {
        if (path.has_root_path()) throw std::runtime_error("Invalid path in project library.");
        path = directory / path;
    }
    path = path.lexically_normal().make_preferred();
    if (path != path.root_path() && !path.has_filename()) path = path.parent_path();
    return path;
}

std::string WritePath(const fs::path& path, const fs::path& directory) {
    if (path.empty()) return {};
    const auto relative = path.lexically_relative(directory);
    return files::ToUtf8(!relative.empty() && *relative.begin() != L".." ? relative.native() : path.native());
}
}

Library ReadLibrary(const fs::path& path, bool allowMissing) {
    Library library;
    library.original = ReadContents(path);
    if (!library.original) {
        if (!allowMissing) throw std::runtime_error("Project library could not be found.");
        return library;
    }

    const auto data = json::parse(*library.original);
    if (data.at("version") != 1 || !data.at("active").is_number_unsigned() || !data.at("projects").is_array())
        throw std::runtime_error("Invalid project library format.");
    library.active = data.at("active").get<size_t>();
    library.description = Text(data.at("description"));
    library.projects.clear();
    const auto directory = path.parent_path();
    for (const auto& value : data.at("projects")) {
        Project entry{Text(value.at("name")), {}, ReadPath(value.at("base"), directory), value.at("autoLoad").get<bool>()};
        if (entry.name.empty() || !value.at("members").is_array() ||
            std::ranges::any_of(library.projects, [&](const Project& other) { return SameText(other.name, entry.name); }))
            throw std::runtime_error("Invalid project name or member list.");
        for (const auto& item : value.at("members")) {
            Member member{ReadPath(item.at("path"), directory), item.at("folder").get<bool>()};
            if (member.path.empty()) throw std::runtime_error("Invalid member path in project library.");
            entry.members.push_back(std::move(member));
        }
        library.projects.push_back(std::move(entry));
    }
    if (library.active >= library.projects.size()) throw std::runtime_error("Invalid active project.");
    return library;
}

void SaveLibrary(const fs::path& path, const Library& library) {
    const auto directory = path.parent_path();
    auto projects = json::array();
    for (const auto& entry : library.projects) {
        auto members = json::array();
        for (const auto& item : entry.members)
            members.push_back({{"path", WritePath(item.path, directory)}, {"folder", item.folder}});
        projects.push_back({{"name", files::ToUtf8(entry.name)}, {"members", std::move(members)},
            {"base", WritePath(entry.base, directory)}, {"autoLoad", entry.autoLoad}});
    }
    const json data{{"version", 1}, {"active", library.active}, {"description", files::ToUtf8(library.description)},
        {"projects", std::move(projects)}};

    // Serialize the revision check and replacement across editor instances.
    const auto lockPath = path.native() + L".lock";
    const auto lock = CreateFileW(lockPath.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (lock == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot lock the project library for saving.");
    try {
        if (ReadContents(path) != library.original)
            throw std::runtime_error("The project library changed on disk. Reopen the command to load the latest version.");
        files::WriteAtomic(path, {data.dump(2) + '\n'});
    } catch (...) {
        CloseHandle(lock);
        throw;
    }
    CloseHandle(lock);
}
}
