#include "PluginMaintenance.h"
#include "PluginDownload.h"
#include "FileIO.h"
#include "../3rdparty/miniz/miniz.h"
#include <windows.h>
#include <bcrypt.h>
#include <objbase.h>
#include <array>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>

namespace {
namespace fs = std::filesystem;

bool SameName(std::wstring_view a, std::wstring_view b) {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

bool ValidFilename(std::wstring_view name) {
    if (name.empty() || name.back() == L'.' || name.back() == L' ' ||
        name.find_first_of(L"\\/:*?\"<>|") != name.npos) return false;
    for (wchar_t ch : name) if (ch < 32) return false;
    auto stem = name.substr(0, name.find(L'.'));
    while (!stem.empty() && stem.back() == L' ') stem.remove_suffix(1);
    for (auto device : {L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$"})
        if (SameName(stem, device)) return false;
    return !(stem.size() == 4 && (SameName(stem.substr(0, 3), L"COM") || SameName(stem.substr(0, 3), L"LPT")) &&
        std::wstring_view(L"123456789\u00b9\u00b2\u00b3").find(stem.back()) != stem.npos);
}

fs::path NewDirectory(const fs::path& parent, const wchar_t* prefix) {
    GUID guid;
    if (FAILED(CoCreateGuid(&guid))) throw std::runtime_error("Cannot generate a temporary directory name.");
    wchar_t suffix[39]{};
    StringFromGUID2(guid, suffix, static_cast<int>(std::size(suffix)));
    auto path = parent / (prefix + std::wstring(suffix));
    if (!fs::create_directory(path)) throw std::runtime_error("Temporary directory already exists.");
    return path;
}

// Check each component used by the transaction before following it.
bool DirectoryExists(const fs::path& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return false;
        throw fs::filesystem_error("Cannot inspect plugin directory", path,
            std::error_code(static_cast<int>(error), std::system_category()));
    }
    if ((attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != FILE_ATTRIBUTE_DIRECTORY)
        throw std::runtime_error("Expected a regular plugin directory: " + files::ToUtf8(path.wstring()));
    return true;
}

void RejectReparseEntries(const fs::path& path) {
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        const auto attributes = GetFileAttributesW(entry.path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Cannot copy plugin entry: " + files::ToUtf8(entry.path().wstring()));
    }
}

void VerifyHash(const fs::path& zip, const std::wstring& expected) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Cannot initialize SHA-256.");
    const auto closeAlgorithm = [](void* value) { BCryptCloseAlgorithmProvider(value, 0); };
    std::unique_ptr<void, decltype(closeAlgorithm)> provider(algorithm, closeAlgorithm);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
        throw std::runtime_error("Cannot create SHA-256 hash.");
    std::unique_ptr<void, decltype(&BCryptDestroyHash)> digest(hash, BCryptDestroyHash);
    std::ifstream input(zip, std::ios::binary);
    std::array<unsigned char, 64 * 1024> buffer;
    do {
        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        if (BCryptHashData(hash, buffer.data(), static_cast<ULONG>(input.gcount()), 0) < 0)
            throw std::runtime_error("Cannot calculate SHA-256.");
    } while (input);
    if (!input.eof() || input.bad()) throw std::runtime_error("Cannot read downloaded package.");
    std::array<unsigned char, 32> value;
    if (BCryptFinishHash(hash, value.data(), static_cast<ULONG>(value.size()), 0) < 0)
        throw std::runtime_error("Cannot finish SHA-256.");
    std::wstring actual;
    for (auto byte : value) {
        actual += L"0123456789abcdef"[byte >> 4];
        actual += L"0123456789abcdef"[byte & 15];
    }
    if (!SameName(actual, expected)) throw std::runtime_error("SHA-256 mismatch.");
}

fs::path ZipPath(std::string_view name, mz_uint16 flags) {
    const bool utf8 = (flags & (1 << 11)) != 0;
    const UINT codepage = utf8 ? CP_UTF8 : 437;
    const DWORD conversion = utf8 ? MB_ERR_INVALID_CHARS : 0;
    const int length = static_cast<int>(name.size());
    int count = MultiByteToWideChar(codepage, conversion, name.data(), length, nullptr, 0);
    if (!count) throw std::runtime_error("Invalid ZIP filename encoding.");
    std::wstring wide(count, L'\0');
    if (!MultiByteToWideChar(codepage, conversion, name.data(), length, wide.data(), count))
        throw std::runtime_error("Invalid ZIP filename encoding.");
    fs::path relative;
    std::wstring_view rest(wide);
    while (!rest.empty()) {
        const auto end = rest.find_first_of(L"/\\");
        const auto component = rest.substr(0, end);
        if (!ValidFilename(component)) throw std::runtime_error("Unsafe ZIP entry.");
        relative /= component;
        if (end == rest.npos) break;
        rest.remove_prefix(end + 1);
    }
    return relative;
}

size_t WriteZipEntry(void* opaque, mz_uint64, const void* data, size_t size) {
    DWORD written = 0;
    return WriteFile(opaque, data, static_cast<DWORD>(size), &written, nullptr) ? written : 0;
}

void ExtractZip(const fs::path& zip, const fs::path& target) {
    std::unique_ptr<FILE, decltype(&fclose)> input(_wfopen(zip.c_str(), L"rb"), fclose);
    if (!input) throw std::runtime_error("Cannot open ZIP package.");
    mz_zip_archive archive{};
    if (!mz_zip_reader_init_cfile(&archive, input.get(), 0, 0)) throw std::runtime_error("Invalid ZIP package.");
    std::unique_ptr<mz_zip_archive, decltype(&mz_zip_reader_end)> reader(&archive, mz_zip_reader_end);
    for (mz_uint index = 0; index < mz_zip_reader_get_num_files(&archive); ++index) {
        mz_zip_archive_file_stat entry{};
        if (!mz_zip_reader_file_stat(&archive, index, &entry)) throw std::runtime_error("Invalid ZIP entry.");
        if (((entry.m_external_attr >> 16) & 0170000) == 0120000 ||
            (entry.m_external_attr & FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("Unsafe ZIP entry.");
        const auto length = mz_zip_reader_get_filename(&archive, index, nullptr, 0);
        if (length <= 1) throw std::runtime_error("Empty ZIP entry name.");
        std::string name(length, '\0');
        if (mz_zip_reader_get_filename(&archive, index, name.data(), length) != length)
            throw std::runtime_error("Invalid ZIP entry name.");
        name.pop_back();
        const auto destination = target / ZipPath(name, entry.m_bit_flag);
        if (entry.m_is_directory) {
            fs::create_directories(destination);
            continue;
        }
        fs::create_directories(destination.parent_path());
        const HANDLE file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            throw fs::filesystem_error("Cannot create ZIP entry", destination,
                std::error_code(static_cast<int>(GetLastError()), std::system_category()));
        std::unique_ptr<void, decltype(&CloseHandle)> close(file, CloseHandle);
        if (!mz_zip_reader_extract_to_callback(&archive, index, WriteZipEntry, file, 0))
            throw std::runtime_error(mz_zip_get_error_string(mz_zip_get_last_error(&archive)));
    }
}

void VerifyDll(const fs::path& dll) {
    std::ifstream file(dll, std::ios::binary);
    IMAGE_DOS_HEADER dos{};
    DWORD signature = 0;
    IMAGE_FILE_HEADER header{};
    if (!file.read(reinterpret_cast<char*>(&dos), sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < static_cast<LONG>(sizeof(dos)) || !file.seekg(dos.e_lfanew) ||
        !file.read(reinterpret_cast<char*>(&signature), sizeof(signature)) || signature != IMAGE_NT_SIGNATURE ||
        !file.read(reinterpret_cast<char*>(&header), sizeof(header)) ||
        header.Machine != IMAGE_FILE_MACHINE_AMD64 || !(header.Characteristics & IMAGE_FILE_DLL))
        throw std::runtime_error("Package does not contain an x64 plugin DLL.");
}

fs::path StagePackage(const PluginPackage& package, const fs::path& directory) {
    const auto zip = directory / L"package.zip";
    if (package.hash.size() != 64 || package.hash.find_first_not_of(L"0123456789abcdefABCDEF") != package.hash.npos ||
        package.url.size() < 8 || !SameName(std::wstring_view(package.url).substr(0, 8), L"https://") ||
        package.url.find(L'\0') != package.url.npos)
        throw std::runtime_error("Invalid package metadata.");
    DownloadPluginPackage(package.url, zip);
    VerifyHash(zip, package.hash);
    auto extracted = directory / L"contents";
    fs::create_directory(extracted);
    ExtractZip(zip, extracted);
    const auto dll = package.folder + L".dll";
    if (!fs::exists(extracted / dll)) extracted /= package.folder;
    VerifyDll(extracted / dll);
    return extracted;
}

struct PluginMove {
    fs::path destination, source, backup;
    bool installed = false, backedUp = false;
};

void CommitMoves(std::vector<PluginMove>& moves, const fs::path& staging, bool& keepStaging) {
    try {
        for (auto& move : moves) {
            fs::create_directories(move.destination.parent_path());
            if (!move.backup.empty()) {
                fs::rename(move.destination, move.backup);
                move.backedUp = true;
            }
            if (!move.source.empty()) {
                fs::rename(move.source, move.destination);
                move.installed = true;
            }
        }
    } catch (const std::exception& error) {
        for (auto it = moves.rbegin(); it != moves.rend(); ++it) {
            std::error_code failure;
            if (it->installed) fs::rename(it->destination, it->source, failure);
            if (!failure && it->backedUp) fs::rename(it->backup, it->destination, failure);
            if (failure) keepStaging = true;
        }
        if (keepStaging)
            throw std::runtime_error(std::string(error.what()) + "\nRollback incomplete. Backups: " + files::ToUtf8(staging.wstring()));
        throw;
    }
}
}

bool ValidPluginFolder(const std::wstring& name) {
    return ValidFilename(name) && !SameName(name, L"Config") && !SameName(name, L"disabled") && !SameName(name, L"APIs");
}

std::filesystem::path CreatePluginTaskDirectory() {
    return NewDirectory(fs::temp_directory_path(), L"MultiPad4-plugins-");
}

void ExecutePluginMaintenance(const PluginMaintenance& task) {
    const auto& root = task.root;
    if (!DirectoryExists(root)) fs::create_directories(root);
    const auto staging = NewDirectory(root, L".multipad4-stage-");
    bool keepStaging = false;
    try {
        std::vector<PluginMove> moves;
        auto compare = [](const std::wstring& a, const std::wstring& b) {
            return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        };
        std::set<std::wstring, decltype(compare)> names(compare);
        const auto disabled = root / L"disabled";
        for (const auto& package : task.packages) {
            const auto& name = package.folder;
            if (!ValidPluginFolder(name) || !names.insert(name).second)
                throw std::runtime_error("Invalid or duplicate plugin folder: " + files::ToUtf8(name));
            try {
                const auto work = staging / std::to_wstring(moves.size());
                fs::create_directory(work);
                const auto& parent = package.disabled ? disabled : root;
                const auto current = parent / name;
                const bool exists = DirectoryExists(parent) && DirectoryExists(current);
                PluginMove move{current, {}, {}};
                switch (task.operation) {
                case PluginOperation::Install:
                    if (exists) throw std::runtime_error("Plugin already exists.");
                    move.source = StagePackage(package, work);
                    break;
                case PluginOperation::Update:
                    if (!exists) throw std::runtime_error("Plugin not found.");
                    RejectReparseEntries(current);
                    {
                        const auto contents = StagePackage(package, work);
                        const auto merged = work / L"merged";
                        fs::copy(current, merged, fs::copy_options::recursive);
                        fs::copy(contents, merged, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
                        move.source = merged;
                        move.backup = work / L"backup";
                    }
                    break;
                case PluginOperation::Disable:
                case PluginOperation::Enable:
                    move.source = current;
                    {
                        const auto& destination = task.operation == PluginOperation::Disable ? disabled : root;
                        move.destination = destination / name;
                        if (!exists || (DirectoryExists(destination) && DirectoryExists(move.destination)))
                            throw std::runtime_error("Missing source or destination already exists.");
                    }
                    break;
                case PluginOperation::Remove:
                    if (!exists) throw std::runtime_error("Plugin not found.");
                    move.backup = work / L"backup";
                    break;
                }
                moves.push_back(std::move(move));
            } catch (const std::exception& error) {
                throw std::runtime_error(files::ToUtf8(name) + ": " + error.what());
            }
        }
        CommitMoves(moves, staging, keepStaging);
    } catch (...) {
        std::error_code ignored;
        if (!keepStaging) fs::remove_all(staging, ignored);
        throw;
    }
    fs::remove_all(staging);
}
