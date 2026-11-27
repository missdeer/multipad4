void DirectoryWatch::FailForTest() {
    if (!state) return;
    { std::lock_guard lock(state->mutex); CancelIoEx(state->directory, &state->overlapped); }
    WaitForThreadpoolIoCallbacks(state->io, FALSE);
    std::lock_guard lock(state->mutex); state->error = ERROR_ACCESS_DENIED;
}
