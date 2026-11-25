// Benchmark-only implementation from baseline f19a330.
namespace files {
Text BaselineDetectSample(const std::string& bytes, bool complete, const ReadOptions& options) {
    const auto decode = [&](Encoding encoding) {
        return Decode(std::string_view(bytes).substr(0, ChunkEnd(bytes, encoding, complete)), encoding);
    };
    if (bytes.starts_with("\xEF\xBB\xBF")) {
        try { return decode(Encoding::Utf8Bom); }
        catch (const InvalidByteSequence&) { return decode(Latin1); }
    }
    if (bytes.starts_with("\xFF\xFE")) return decode(Encoding::Utf16LE);
    if (bytes.starts_with("\xFE\xFF")) return decode(Encoding::Utf16BE);
    if (options.fileVariables) {
        auto region=filevars::Region(bytes);std::string name;
        int enabled=1;
        const bool parse= !filevars::Integer(region,"enable-local-variables",enabled)||enabled!=0;
        if(parse)for(auto key:{"encoding","charset","coding","fileencoding","/*!40101 SET NAMES "})if(!(name=filevars::String(region,key)).empty())break;
        Encoding declared = Encoding::Utf8;
        if(!name.empty()&&ParseEncodingName(FromUtf8(name),declared)
            && declared!=Encoding::Utf16LE && declared!=Encoding::Utf16BE && declared!=Encoding::Utf16LEPlain && declared!=Encoding::Utf16BEPlain) {
            try { return decode(declared); }
            catch (const InvalidByteSequence&) { return decode(Latin1); }
        }
    }

    if (bytes.size() >= 2 && bytes.size() % 2 == 0) {
        const int unicode = simd::DetectUtf16(bytes);
        if (unicode) {
            try { return decode(unicode == 1 ? Encoding::Utf16LEPlain : Encoding::Utf16BEPlain); }
            catch (const std::runtime_error&) {}
        }
        try { return decode(Encoding::Utf8); } catch(const std::runtime_error&) {}
        int unicodeFlags = IS_TEXT_UNICODE_STATISTICS | IS_TEXT_UNICODE_REVERSE_STATISTICS |
            IS_TEXT_UNICODE_CONTROLS | IS_TEXT_UNICODE_REVERSE_CONTROLS | IS_TEXT_UNICODE_ILLEGAL_CHARS;
        if(options.unicodeDetection)IsTextUnicode(bytes.data(), static_cast<int>((std::min)(bytes.size(), size_t(4096))), &unicodeFlags);
        else unicodeFlags=0;
        if(!(unicodeFlags & IS_TEXT_UNICODE_ILLEGAL_CHARS)) {
            const bool little=(unicodeFlags & (IS_TEXT_UNICODE_STATISTICS | IS_TEXT_UNICODE_CONTROLS))!=0;
            const bool big=(unicodeFlags & (IS_TEXT_UNICODE_REVERSE_STATISTICS | IS_TEXT_UNICODE_REVERSE_CONTROLS))!=0;
            if(bytes.size() >= 16 && little != big) {
                try { return decode(little ? Encoding::Utf16LEPlain : Encoding::Utf16BEPlain); }
                catch(const std::runtime_error&) {} // Invalid UTF-16 falls through to byte-oriented detection.
            }
        }
    }
    try { return decode(Encoding::Utf8); } catch(const std::runtime_error&) {}
    try { return decode(options.fallback); }
    catch (const InvalidByteSequence&) { return decode(Latin1); }
}
Encoding BaselineReadChunks(const std::filesystem::path& path, const ChunkConsumer& consume, const ReadOptions& options, std::optional<Encoding> forced, ReadStats* stats) {
    if (stats) *stats = {};
    ReadFaults faults(stats);
    ReadTimer total(stats ? &stats->totalMs : nullptr);
    const auto probeStart = stats ? ReadClock() : 0;
    MappedInput input(path, stats, options.backend);
    const auto sample = input.Read(0, 1024 * 1024 + SampleLookahead);
    Encoding encoding = forced.value_or(Encoding::Utf8);
    bool declared = false;
    if (!forced) {
        if (sample.starts_with("\xFF\xFE")) encoding = Encoding::Utf16LE;
        else if (sample.starts_with("\xFE\xFF")) encoding = Encoding::Utf16BE;
        else if (sample.starts_with("\xEF\xBB\xBF")) encoding = Encoding::Utf8Bom;
        else {
            if (options.fileVariables) {
                auto firstAndLast = input.Read(0, 512);
                if (input.size > 512) firstAndLast += input.Read((std::max)(512ULL, input.size - (std::min)(input.size, 512ULL)), 512);
                const auto region = filevars::Region(firstAndLast);
                int enabled = 1;
                if (!filevars::Integer(region, "enable-local-variables", enabled) || enabled) {
                    for (auto key : {"encoding", "charset", "coding", "fileencoding", "/*!40101 SET NAMES "}) {
                        const auto name = filevars::String(region, key);
                        if (!name.empty() && ParseEncodingName(FromUtf8(name), encoding)) {
                            declared = encoding != Encoding::Utf16LE && encoding != Encoding::Utf16BE && encoding != Encoding::Utf16LEPlain && encoding != Encoding::Utf16BEPlain;
                            if (declared) break;
                        }
                    }
                }
            }
            if (!declared) { auto detection = options; detection.fileVariables = false; encoding = BaselineDetectSample(sample, sample.size() == input.size, detection).encoding; }
        }
    }
    unsigned workers = options.validationWorkers;
    if (!workers) workers = input.size >= LargeFileThreshold ? (std::min)(4u, work::Concurrency()) : 1;
    if (encoding != Encoding::Utf8 && encoding != Encoding::Utf8Bom) workers = 1;
    Utf8Validator validator(workers);
    if (stats) stats->probeMs = ReadElapsed(probeStart) - stats->mappingMs - stats->copyOrReadMs;
    auto validUtf8 = [&](std::string_view text) {
        ReadTimer timer(stats ? &stats->validationMs : nullptr);
        if (stats) stats->validationBytes += text.size();
        return validator.Validate(text);
    };
    auto deliver = [&](std::string_view text) {
        ReadTimer timer(stats ? &stats->callbackMs : nullptr);
        if (stats) stats->deliveredBytes += text.size();
        consume(text);
    };
    // A sample cannot prove UTF-8 validity. Validate the entire byte stream
    // before delivering data so late legacy bytes cannot leave a mixed buffer.
    bool validatedUtf8 = false;
    if (!forced && !declared && encoding == Encoding::Utf8) {
        validatedUtf8 = true;
        for (unsigned long long offset = 0; offset < input.size;) {
            if (options.cancel && options.cancel->load(std::memory_order_relaxed)) Fail("File read cancelled.");
            auto bytes = input.View(offset, ConversionChunk + SampleLookahead);
            auto count = ChunkEnd(bytes, encoding, offset + bytes.size() == input.size);
            if (!validUtf8(std::string_view(bytes.data(), count))) { validatedUtf8 = false; encoding = options.fallback; break; }
            offset += count;
        }
    }
    for (unsigned long long offset = 0; offset < input.size;) {
        if (options.cancel && options.cancel->load(std::memory_order_relaxed)) Fail("File read cancelled.");
        auto bytes = input.View(offset, ConversionChunk + SampleLookahead);
        auto count = ChunkEnd(bytes, encoding, offset + bytes.size() == input.size);
        bytes = bytes.substr(0, count);
        if (encoding == Encoding::Utf8 || encoding == Encoding::Utf8Bom) {
            std::string_view text(bytes);
            if (!offset && text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
            if (!validatedUtf8 && !validUtf8(text)) Fail("Invalid UTF-8.");
            deliver(text);
        } else {
            Text decoded;
            { ReadTimer timer(stats ? &stats->decodeMs : nullptr);
                if (stats) stats->decodedBytes += bytes.size();
                decoded = Decode(bytes, encoding, offset == 0);
            }
            deliver(decoded.utf8);
        }
        offset += count;
    }
    return encoding;
}
}
