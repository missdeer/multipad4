    extern bool lineLookupBaseline;
    if (lineLookupBaseline) {
        const auto size = Size(snapshot); Position high = (std::min)(size, Position(IndexBlockBytes)), low = 0;
        while (high < size && Impl::Prefix(snapshot.root, high, sessions).breaks < line) { low = high; high = (std::min)(size, high * 2); }
        if (Impl::Prefix(snapshot.root, high, sessions).breaks < line) return size;
        const auto width = snapshot.root ? snapshot.root->piece.source->Width() : 1;
        const bool little = !snapshot.root || snapshot.root->piece.source->Little();
        low /= width; high /= width;
        while (low < high) { auto middle = low + (high - low) / 2; if (Impl::Prefix(snapshot.root, middle * width, sessions).breaks >= line) high = middle; else low = middle + 1; }
        low *= width;
        if (low && low < size) { std::string pair; Impl::Append(snapshot.root, low - width, 2 * width, pair, &sessions); if (Unit(pair, 0, width, little) == '\r' && Unit(pair, width, width, little) == '\n') low += width; }
        return low;
    }
