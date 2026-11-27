            if (const auto gate = searchDetail::gate.load()) { ++searchDetail::entered; WaitForSingleObject(gate, INFINITE); }
