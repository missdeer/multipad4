    static void Append(const Root& node, Position offset, Position length, std::string& output, ReadSessions* sessions = nullptr) {
        if (!node || !length) return;
        const auto left = Length(node->left), end = left + node->piece.length;
        if (offset < left) { auto take = (std::min)(length, left - offset); Append(node->left, offset, take, output, sessions); offset += take; length -= take; }
        if (length && offset < end) { auto start = offset - left, take = (std::min)(length, end - offset);
#ifdef MULTIPAD4_PIECE_READ_TEST
            ++backingReads; copiedBytes += 2 * take;
#endif
            output += node->piece.source->Read(node->piece.offset + start, static_cast<size_t>(take), sessions); offset += take; length -= take; }
        if (length) Append(node->right, offset - end, length, output, sessions);
    }
