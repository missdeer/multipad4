#pragma once
#include <atomic>
#include <filesystem>
namespace autosavetest {
inline bool baselineCapture = false;
inline size_t rangeCopies = 0, captureCopies = 0, captureSlices = 0;
using WriteGate = void(*)(const std::filesystem::path&, bool);
inline std::atomic<WriteGate> beforeWrite = nullptr;
}
