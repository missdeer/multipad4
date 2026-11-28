// Extend the upstream incremental URL pass without copying the document.
const SplitView emailView = cb.AllView();
const Sci_PositionU emailLimit = startPos + length;
auto localChar = [](unsigned char ch) noexcept {
    return IsAlphaNumeric(ch) || ch == '.' || ch == '_' || ch == '%' || ch == '+' || ch == '-';
};
auto domainChar = [](unsigned char ch) noexcept { return IsAlphaNumeric(ch) || ch == '-'; };
auto emailBoundaryValid = [&](Sci_PositionU begin) noexcept {
    if (!begin) return true;
    const unsigned char previous = emailView[begin - 1];
    return previous < 128 && !localChar(previous) && !strchr("!#$&'*/=?^`{|}~\"\\", previous);
};
for (Sci_PositionU scan = startPos; scan < emailLimit;) {
    const bool first = scan < emailView.length1;
    const char* segment = first ? emailView.segment1 : emailView.segment2;
    const Sci_PositionU limit = std::min(emailLimit, first ? emailView.length1 : emailView.length);
    const char* at = static_cast<const char*>(memchr(segment + scan, '@', limit - scan));
    if (!at) { scan = limit; continue; }
    const Sci_PositionU middle = at - segment;
    scan = middle + 1;
    Sci_PositionU begin = middle, end = scan;
    while (begin > startPos && localChar(emailView[begin - 1])) --begin;
    if (begin == middle || emailView[begin] == '.' || emailView[middle - 1] == '.') continue;
    // Reject unsupported local parts in full instead of linking a different recipient.
    if (!emailBoundaryValid(begin)) continue;
    while (end < emailLimit && (domainChar(emailView[end]) || emailView[end] == '.')) ++end;
    // A sentence's final period is not part of the address.
    if (end > scan && emailView[end - 1] == '.') --end;
    bool dot = false, valid = end > scan;
    Sci_PositionU label = scan;
    for (Sci_PositionU pos = scan; pos <= end; ++pos) {
        if (pos == end || emailView[pos] == '.') {
            if (pos == label || emailView[label] == '-' || emailView[pos - 1] == '-') { valid = false; break; }
            dot |= pos != end; label = pos + 1;
        }
    }
    if (!valid || !dot) continue;
    bool consecutiveDot = false;
    for (Sci_PositionU pos = begin + 1; pos < middle; ++pos)
        consecutiveDot |= emailView[pos] == '.' && emailView[pos - 1] == '.';
    if (!consecutiveDot) DecorationFillRange(begin, 1, end - begin);
}
