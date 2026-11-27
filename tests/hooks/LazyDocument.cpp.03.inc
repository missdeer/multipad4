                if (failJournalAfter == 0) throw std::runtime_error("Injected partial journal write failure.");
                count = static_cast<DWORD>((std::min)(Position(count), failJournalAfter));
                failJournalAfter -= count; ++journalWrites;
