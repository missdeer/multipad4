        if (const auto gate = testing::nextFallbackGate.exchange(nullptr)) { testing::fallbackEntered = true; WaitForSingleObject(gate, INFINITE); }
