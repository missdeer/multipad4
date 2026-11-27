        if (const auto gate = testing::nextIndexGate.exchange(nullptr)) { testing::indexEntered = true; WaitForSingleObject(gate, INFINITE); }
