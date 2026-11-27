    if (autosavetest::baselineCapture && task->offset < task->length) {
        auto bytes = d.Range(task->offset, (std::min)(size_t(256 * 1024), task->length - task->offset));
        task->offset += bytes.size(); task->text.append(bytes);
        autosavetest::rangeCopies += bytes.size(); autosavetest::captureCopies += bytes.size(); ++autosavetest::captureSlices;
        return;
    }
