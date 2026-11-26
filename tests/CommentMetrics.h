#pragma once
struct EditorDocument;
namespace commenttest {
using Baseline = void (*)(EditorDocument&, bool);
inline Baseline baseline = nullptr;
}
void BaselineToggleDocumentComment(EditorDocument& document, bool block);
