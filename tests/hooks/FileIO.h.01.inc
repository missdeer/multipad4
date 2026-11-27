namespace testing { inline std::atomic<bool> bufferedSessions{false}, fallbackEntered{false}; inline std::atomic<void*> nextFallbackGate{nullptr}; }
