#pragma once
#include "App.h"
#include "FileIO.h"
struct EditorDocument;
// The Scintilla child is only a bounded rendering viewport. The backing file,
// global scrollbar and byte positions belong to this view, not its text buffer.
class FastFileView {
    EditorDocument& document;
    files::ReadOptions options;
    std::optional<files::Encoding> forced;
    std::unique_ptr<files::MappedFile> file;
    HWND window = nullptr, label = nullptr, button = nullptr;
    unsigned long long begin = 0, end = 0;
    files::Encoding encoding = files::Encoding::Utf8;
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditorProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void Layout();
    void UpdateBar();
public:
    explicit FastFileView(EditorDocument& document, const std::filesystem::path& path,
        const files::ReadOptions& options, std::optional<files::Encoding> forced);
    ~FastFileView();
    void Attach();
    void Seek(unsigned long long offset, bool bottom = false);
    void LoadForEditing();
    void SaveCopy(const std::filesystem::path& path);
    bool Active() const { return file != nullptr; }
    HWND Window() const { return window; }
    unsigned long long Size() const { return file ? file->Size() : 0; }
    unsigned long long Begin() const { return begin; }
    unsigned long long End() const { return end; }
    unsigned long long BytesRead() const { return file ? file->BytesRead() : 0; }
};
