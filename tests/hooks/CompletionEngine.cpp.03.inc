    static std::map<int,CompletionCache> caches;
    auto found=caches.find(lexer.rid);if(found!=caches.end())return found->second;
    CompletionCache cache{};cache.Initialize(&lexer);return caches.emplace(lexer.rid,cache).first->second;
