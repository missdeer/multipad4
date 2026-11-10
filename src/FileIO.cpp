#include "FileIO.h"
#include <windows.h>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <regex>
#include <cctype>
#include <climits>
#include "FileVariables.h"
namespace files {
namespace {
constexpr size_t ConversionChunk = 16 * 1024 * 1024;
[[noreturn]] void Fail(const char* message) { throw std::runtime_error(message); }
size_t Utf8End(const std::string& text,size_t start) {
    size_t end=(std::min)(text.size(),start+ConversionChunk);
    if(end<text.size())while(end>start && (static_cast<unsigned char>(text[end])&0xC0)==0x80)--end;
    return end;
}
void Validate(const std::string& text) {
    for(size_t start=0;start<text.size();) {
        auto end=Utf8End(text,start);if(end==start)Fail("Invalid UTF-8.");
        if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data()+start,static_cast<int>(end-start),nullptr,0))Fail("Invalid UTF-8.");
        start=end;
    }
}
}
std::string ToUtf8(const std::wstring& text) {
    std::string output;
    for(size_t start=0;start<text.size();) {
        size_t end=(std::min)(text.size(),start+ConversionChunk);
        if(end<text.size() && text[end-1]>=0xD800 && text[end-1]<=0xDBFF)--end;
        int count=static_cast<int>(end-start);
        int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data()+start,count,nullptr,0,nullptr,nullptr);
        if(!length)Fail("Invalid UTF-16 text.");
        auto offset=output.size();output.resize(offset+length);
        if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data()+start,count,output.data()+offset,length,nullptr,nullptr))Fail("Invalid UTF-16 text.");
        start=end;
    }
    return output;
}
std::wstring FromUtf8(const std::string& text) {
    std::wstring output;
    for(size_t start=0;start<text.size();) {
        auto end=Utf8End(text,start);if(end==start)Fail("Invalid UTF-8.");
        int count=static_cast<int>(end-start);
        int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data()+start,count,nullptr,0);
        if(!length)Fail("Invalid UTF-8.");
        auto offset=output.size();output.resize(offset+length);
        if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data()+start,count,output.data()+offset,length))Fail("Invalid UTF-8.");
        start=end;
    }
    return output;
}
unsigned CodePage(Encoding encoding) {
    if (encoding == Encoding::Ansi || encoding == Encoding::Oem) {
        UINT codePage=encoding==Encoding::Ansi?GetACP():GetOEMCP();
        // Match upstream Encoding_InitDefaults/GetLegacyACP: choosing ANSI/OEM
        // still offers the legacy code page when Windows' UTF-8 locale is on.
        if(codePage==CP_UTF8){const LCTYPE setting=(encoding==Encoding::Ansi?0x00000666:0x00000999)|LOCALE_RETURN_NUMBER;
            UINT legacy=codePage;if(GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT,setting,reinterpret_cast<LPWSTR>(&legacy),sizeof(legacy)/sizeof(wchar_t))&&IsValidCodePage(legacy))codePage=legacy;
        }
        return codePage;
    }
    if (static_cast<unsigned>(encoding) >= static_cast<unsigned>(Encoding::CodePageBase))
        return static_cast<unsigned>(encoding) - static_cast<unsigned>(Encoding::CodePageBase);
    return CP_UTF8;
}
Encoding FromCodePage(unsigned page) {
    if (page == CP_UTF8) return Encoding::Utf8;
    if (page == 1200) return Encoding::Utf16LEPlain;
    if (page == 1201) return Encoding::Utf16BEPlain;
    if (!IsValidCodePage(page)) Fail("This encoding is not installed on Windows.");
    return static_cast<Encoding>(static_cast<unsigned>(Encoding::CodePageBase) + page);
}
bool ParseEncodingName(const std::wstring& value, Encoding& encoding) {
    auto normalize=[](const std::string& text){std::string result;for(unsigned char c:text)if(isalnum(c))result+=static_cast<char>(tolower(c));return result;};
    auto name=normalize(ToUtf8(value));
    if(name=="utf16"||name=="utf16le"||name=="unicode"||name=="ucs2"){encoding=Encoding::Utf16LEPlain;return true;}
    if(name=="utf16be"||name=="unicodebe"){encoding=Encoding::Utf16BEPlain;return true;}
    if(name=="utf8bom"||name=="utf8sig"||name=="utf8signature"){encoding=Encoding::Utf8Bom;return true;}
    struct Alias { Encoding value; const char* names; };
    static const Alias aliases[]={
#include "FileEncodingAliases.inc"
    };
    for(const auto& entry:aliases){std::string names=entry.names;size_t start=0;while(start<names.size()){
        auto end=names.find(',',start);auto item=names.substr(start,end==std::string::npos?end:end-start);
        if(!item.empty() && normalize(item)==name){encoding=entry.value;return true;}if(end==std::string::npos)break;start=end+1;
    }}
    if(name.starts_with("cp"))name.erase(0,2);else if(name.starts_with("windows"))name.erase(0,7);
    if(!name.empty() && std::all_of(name.begin(),name.end(),[](unsigned char c){return isdigit(c)!=0;})){
        unsigned long page=strtoul(name.c_str(),nullptr,10);if(page<=65535 && IsValidCodePage(page)){encoding=FromCodePage(page);return true;}
    }
    return false;
}
Text Decode(const std::string& input, Encoding encoding) {
    std::string bytes = input;
    Text result{{}, encoding};
    bool utf16 = encoding == Encoding::Utf16LE || encoding == Encoding::Utf16BE
        || encoding == Encoding::Utf16LEPlain || encoding == Encoding::Utf16BEPlain;
    if (utf16) {
        bool le = encoding == Encoding::Utf16LE || encoding == Encoding::Utf16LEPlain;
        if ((le && bytes.starts_with("\xFF\xFE")) || (!le && bytes.starts_with("\xFE\xFF"))) bytes.erase(0, 2);
        if (bytes.size() % 2) Fail("Truncated UTF-16 file.");
        std::wstring wide(bytes.size()/2, L'\0');
        for (size_t i = 0; i < wide.size(); ++i) {
            auto a = static_cast<unsigned char>(bytes[2*i]), b = static_cast<unsigned char>(bytes[2*i+1]);
            wide[i] = static_cast<wchar_t>(le ? a | (b << 8) : (a << 8) | b);
        }
        result.utf8 = ToUtf8(wide);
    } else if (encoding == Encoding::Utf8 || encoding == Encoding::Utf8Bom) {
        if (bytes.starts_with("\xEF\xBB\xBF")) bytes.erase(0, 3);
        Validate(bytes); result.utf8 = std::move(bytes);
    } else if (!bytes.empty()) {
        auto page=CodePage(encoding);CPINFOEXW info{};if(!GetCPInfoExW(page,0,&info))Fail("Encoding is not installed.");
        for(size_t start=0;start<bytes.size();) {
            size_t end=(std::min)(bytes.size(),start+ConversionChunk);
            if(end<bytes.size() && page==CP_UTF7){
                auto plus=bytes.rfind('+',end-1),minus=bytes.rfind('-',end-1);
                if(plus!=std::string::npos && plus>=start && (minus==std::string::npos||plus>minus)) {
                    if(plus>start)end=plus;else{auto close=bytes.find('-',end);if(close!=std::string::npos && close-start<INT_MAX)end=close+1;else Fail("Oversized UTF-7 shift sequence.");}
                }
            } else if(end<bytes.size() && info.MaxCharSize>1) {
                size_t position=start;
                while(position<end) {
                    const auto c=static_cast<BYTE>(bytes[position]);size_t count=1;
                    if(page==54936 && c>=0x81 && c<=0xFE && position+1<bytes.size()) {
                        auto next=static_cast<BYTE>(bytes[position+1]);count=next>=0x30&&next<=0x39?4:2;
                    } else if(IsDBCSLeadByteEx(page,c))count=2;
                    if(position+count>end){end=position;break;}position+=count;
                }
            }
            if(end<=start || end-start>INT_MAX)Fail("Invalid encoded byte sequence.");
            DWORD flags=(page==CP_UTF7||page==42||page==50220||page==50221||page==50222)?0:MB_ERR_INVALID_CHARS;
            auto count=static_cast<int>(end-start);
            int length=MultiByteToWideChar(page,flags,bytes.data()+start,count,nullptr,0);
            if(!length && GetLastError()==ERROR_INVALID_FLAGS){flags=0;length=MultiByteToWideChar(page,flags,bytes.data()+start,count,nullptr,0);}
            if(!length)Fail("Invalid byte sequence for selected encoding.");std::wstring wide(length,L'\0');
            if(!MultiByteToWideChar(page,flags,bytes.data()+start,count,wide.data(),length))Fail("Cannot decode file.");
            result.utf8+=ToUtf8(wide);start=end;
        }
    }
    return result;
}
namespace {
std::string ReadBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) Fail("Cannot open file.");
    auto length = in.tellg();
    if (length < 0 || static_cast<unsigned long long>(length) > std::string().max_size()) Fail("File exceeds available address space.");
    std::string bytes(static_cast<size_t>(length), '\0'); in.seekg(0);
    if (!bytes.empty() && !in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) Fail("Cannot read complete file.");
    return bytes;
}
}
Text ReadWithEncoding(const std::filesystem::path& path, Encoding encoding) { return Decode(ReadBytes(path), encoding); }
Text Read(const std::filesystem::path& path) { return Read(path, {}); }
Text Read(const std::filesystem::path& path, const ReadOptions& options) {
    auto bytes = ReadBytes(path);
    if (bytes.starts_with("\xEF\xBB\xBF")) return Decode(bytes, Encoding::Utf8Bom);
    if (bytes.starts_with("\xFF\xFE")) return Decode(bytes, Encoding::Utf16LE);
    if (bytes.starts_with("\xFE\xFF")) return Decode(bytes, Encoding::Utf16BE);
    if (options.fileVariables) {
        auto region=filevars::Region(bytes);std::string name;
        int enabled=1;
        const bool parse= !filevars::Integer(region,"enable-local-variables",enabled)||enabled!=0;
        if(parse)for(auto key:{"encoding","charset","coding","fileencoding","/*!40101 SET NAMES "})if(!(name=filevars::String(region,key)).empty())break;
        Encoding declared = Encoding::Utf8;
        if(!name.empty()&&ParseEncodingName(FromUtf8(name),declared)
            && declared!=Encoding::Utf16LE && declared!=Encoding::Utf16BE && declared!=Encoding::Utf16LEPlain && declared!=Encoding::Utf16BEPlain)return Decode(bytes,declared);
    }

    if (bytes.size() >= 2 && bytes.size() % 2 == 0) {
        size_t even = 0, odd = 0;
        for (size_t i = 0; i < (std::min)(bytes.size(), size_t(4096)); i += 2) { even += bytes[i] == 0; odd += bytes[i+1] == 0; }
        if (odd > 2 && odd > even * 4) return Decode(bytes, Encoding::Utf16LEPlain);
        if (even > 2 && even > odd * 4) return Decode(bytes, Encoding::Utf16BEPlain);
        try { Validate(bytes); return Decode(bytes, Encoding::Utf8); } catch(const std::runtime_error&) {}
        int unicodeFlags = IS_TEXT_UNICODE_STATISTICS | IS_TEXT_UNICODE_REVERSE_STATISTICS |
            IS_TEXT_UNICODE_CONTROLS | IS_TEXT_UNICODE_REVERSE_CONTROLS | IS_TEXT_UNICODE_ILLEGAL_CHARS;
        if(options.unicodeDetection)IsTextUnicode(bytes.data(), static_cast<int>((std::min)(bytes.size(), size_t(4096))), &unicodeFlags);
        else unicodeFlags=0;
        if(!(unicodeFlags & IS_TEXT_UNICODE_ILLEGAL_CHARS)) {
            const bool little=(unicodeFlags & (IS_TEXT_UNICODE_STATISTICS | IS_TEXT_UNICODE_CONTROLS))!=0;
            const bool big=(unicodeFlags & (IS_TEXT_UNICODE_REVERSE_STATISTICS | IS_TEXT_UNICODE_REVERSE_CONTROLS))!=0;
            if(bytes.size() >= 16 && little != big) {
                try { return Decode(bytes, little ? Encoding::Utf16LEPlain : Encoding::Utf16BEPlain); }
                catch(const std::runtime_error&) {} // Invalid UTF-16 falls through to byte-oriented detection.
            }
        }
    }
    try { Validate(bytes); return Decode(bytes, Encoding::Utf8); } catch(const std::runtime_error&) {}
    return Decode(bytes, options.fallback);
}
Text ReadForReload(const std::filesystem::path& path,Encoding current,const ReadOptions& options) {
    auto bytes=ReadBytes(path);
    if(bytes.empty())return Decode(bytes,current);
    if(bytes.starts_with("\xEF\xBB\xBF")||bytes.starts_with("\xFF\xFE")||bytes.starts_with("\xFE\xFF"))return Read(path,options);
    const bool unicode=current==Encoding::Utf16LE||current==Encoding::Utf16BE||current==Encoding::Utf16LEPlain||current==Encoding::Utf16BEPlain;
    const bool ascii=std::all_of(bytes.begin(),bytes.end(),[](unsigned char c){return c<128;});
    auto actual=options;
    if(!unicode&&current!=Encoding::Utf8&&current!=Encoding::Utf8Bom)actual.fallback=current;
    Text detected;
    try{detected=Read(path,actual);}catch(const std::runtime_error&){detected=Read(path,options);}
    if(detected.encoding==Encoding::Utf16LEPlain||detected.encoding==Encoding::Utf16BEPlain)return detected;
    if(ascii&&!unicode)return Decode(bytes,current==Encoding::Utf8Bom?Encoding::Utf8:current);
    try{Validate(bytes);return Decode(bytes,Encoding::Utf8);}catch(const std::runtime_error&){}
    if(!unicode&&current!=Encoding::Utf8&&current!=Encoding::Utf8Bom){try{return Decode(bytes,current);}catch(const std::runtime_error&){}}
    return detected;
}
namespace {
std::string EncodeImpl(const Text& text,bool allowLoss) {
    Validate(text.utf8);
    std::string bytes;
    if (text.encoding == Encoding::Utf8Bom) bytes = "\xEF\xBB\xBF";
    bool utf16 = text.encoding == Encoding::Utf16LE || text.encoding == Encoding::Utf16BE
        || text.encoding == Encoding::Utf16LEPlain || text.encoding == Encoding::Utf16BEPlain;
    if (utf16) {
        bool le = text.encoding == Encoding::Utf16LE || text.encoding == Encoding::Utf16LEPlain;
        if (text.encoding == Encoding::Utf16LE || text.encoding == Encoding::Utf16BE) bytes = le ? "\xFF\xFE" : "\xFE\xFF";
        for (wchar_t c : FromUtf8(text.utf8)) { bytes.push_back(static_cast<char>(le ? c & 255 : c >> 8)); bytes.push_back(static_cast<char>(le ? c >> 8 : c & 255)); }
    } else if (text.encoding == Encoding::Utf8 || text.encoding == Encoding::Utf8Bom) bytes += text.utf8;
    else {
        auto wide=FromUtf8(text.utf8);if(wide.empty())return {};
        auto page=CodePage(text.encoding);
        for(size_t start=0;start<wide.size();) {
            auto end=(std::min)(wide.size(),start+ConversionChunk);
            if(end<wide.size() && wide[end-1]>=0xD800 && wide[end-1]<=0xDBFF)--end;
            BOOL usedDefault=FALSE;const bool unicodePage=page==CP_UTF8||page==CP_UTF7||page==54936;
            DWORD flags=page==CP_UTF8?WC_ERR_INVALID_CHARS:unicodePage?0:WC_NO_BEST_FIT_CHARS;
            auto* used=unicodePage?nullptr:&usedDefault;auto count=static_cast<int>(end-start);
            int length=WideCharToMultiByte(page,flags,wide.data()+start,count,nullptr,0,nullptr,used);
            if(!length && GetLastError()==ERROR_INVALID_FLAGS){flags=0;length=WideCharToMultiByte(page,flags,wide.data()+start,count,nullptr,0,nullptr,used);}
            if(!length||(!allowLoss&&usedDefault))Fail("Selected encoding cannot represent every character; original retained.");
            auto offset=bytes.size();bytes.resize(offset+length);
            if(!WideCharToMultiByte(page,flags,wide.data()+start,count,bytes.data()+offset,length,nullptr,used)||(!allowLoss&&usedDefault))Fail("Encoding conversion failed; original retained.");
            start=end;
        }
        if (!allowLoss && Decode(bytes, text.encoding).utf8 != text.utf8) Fail("Encoding conversion would lose information; original retained.");
    }
    return bytes;
}
}
std::string Encode(const Text& text) {return EncodeImpl(text,false);}
std::string EncodeLossy(const Text& text) {return EncodeImpl(text,true);}
void WriteNew(const std::filesystem::path& path,const Text& text) {
    const auto bytes=Encode(text);
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    if(file==INVALID_HANDLE_VALUE)Fail("Cannot create a new backup file without overwriting existing data.");
    bool ok=true;for(size_t offset=0;offset<bytes.size();) {
        DWORD written=0,count=static_cast<DWORD>((std::min)(ConversionChunk,bytes.size()-offset));
        if(!WriteFile(file,bytes.data()+offset,count,&written,nullptr)||written!=count){ok=false;break;}offset+=written;
    }
    if(ok)ok=FlushFileBuffers(file)!=FALSE;CloseHandle(file);
    if(!ok){DeleteFileW(path.c_str());Fail("Cannot write complete backup file.");}
}
namespace {
bool WriteLinkedFile(const std::filesystem::path& path,const std::string& bytes) {
    const auto stream=path.native().find(L':',path.root_path().native().size());
    HANDLE probe=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(probe==INVALID_HANDLE_VALUE)Fail("Cannot inspect destination file identity; original retained.");
    BY_HANDLE_FILE_INFORMATION attributes{};bool inspected=GetFileInformationByHandle(probe,&attributes)!=FALSE;CloseHandle(probe);
    if(!inspected)Fail("Cannot inspect destination hard links; original retained.");
    bool linked=attributes.nNumberOfLinks>1;
    if(!linked && stream==std::wstring::npos)return false;
    HANDLE original=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(original==INVALID_HANDLE_VALUE)Fail("Cannot exclusively update hard-linked file; original retained.");
    HANDLE rollback=INVALID_HANDLE_VALUE;std::filesystem::path backup;
    try {
        GetFileInformationByHandle(original,&attributes);
        for(unsigned i=0;i<100 && rollback==INVALID_HANDLE_VALUE;++i){backup=stream==std::wstring::npos?path:std::filesystem::path(path.native().substr(0,stream));backup+=L".multipad4-rollback-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(i)+L".bak";
            rollback=CreateFileW(backup.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
            if(rollback==INVALID_HANDLE_VALUE&&GetLastError()!=ERROR_FILE_EXISTS)break;
        }
        if(rollback==INVALID_HANDLE_VALUE)Fail("Cannot create hard-link rollback copy; original retained.");
        std::vector<char> buffer(1024*1024);LARGE_INTEGER zero{};
        auto copy=[&](HANDLE source,HANDLE destination){
            if(!SetFilePointerEx(source,zero,nullptr,FILE_BEGIN)||!SetFilePointerEx(destination,zero,nullptr,FILE_BEGIN))return false;
            for(;;){DWORD read=0,written=0;if(!ReadFile(source,buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr))return false;if(!read)break;
                if(!WriteFile(destination,buffer.data(),read,&written,nullptr)||written!=read)return false;}
            return SetEndOfFile(destination)&&FlushFileBuffers(destination);
        };
        if(!copy(original,rollback))Fail("Cannot complete hard-link rollback copy; original retained.");
        bool saved=SetFilePointerEx(original,zero,nullptr,FILE_BEGIN)!=FALSE;
        for(size_t offset=0;saved&&offset<bytes.size();){DWORD written=0,count=static_cast<DWORD>((std::min)(ConversionChunk,bytes.size()-offset));saved=WriteFile(original,bytes.data()+offset,count,&written,nullptr)&&written==count;offset+=written;}
        if(saved)saved=SetEndOfFile(original)&&FlushFileBuffers(original);
        if(!saved){
            bool restored=copy(rollback,original);
            if(restored)restored=SetFileTime(original,&attributes.ftCreationTime,&attributes.ftLastAccessTime,&attributes.ftLastWriteTime)!=FALSE;
            CloseHandle(original);original=INVALID_HANDLE_VALUE;CloseHandle(rollback);rollback=INVALID_HANDLE_VALUE;
            if(restored){DeleteFileW(backup.c_str());Fail("Hard-linked save failed; original restored from rollback copy.");}
            throw std::runtime_error("Save and rollback failed. Recover original bytes from: "+ToUtf8(backup.wstring()));
        }
        CloseHandle(original);original=INVALID_HANDLE_VALUE;CloseHandle(rollback);rollback=INVALID_HANDLE_VALUE;
        if(!DeleteFileW(backup.c_str()))throw std::runtime_error("Hard-linked file saved, but rollback copy could not be removed: "+ToUtf8(backup.wstring()));
        return true;
    }catch(...){
        if(original!=INVALID_HANDLE_VALUE)CloseHandle(original);
        if(rollback!=INVALID_HANDLE_VALUE){CloseHandle(rollback);DeleteFileW(backup.c_str());}
        throw;
    }
}
}
void WriteAtomic(const std::filesystem::path& path, const Text& text) {
    const auto bytes = Encode(text);
    auto absolute = std::filesystem::absolute(path);
    const DWORD originalAttributes = GetFileAttributesW(absolute.c_str());
    const bool stream=absolute.native().find(L':',absolute.root_path().native().size())!=std::wstring::npos;
    if(originalAttributes != INVALID_FILE_ATTRIBUTES && (originalAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)))
        Fail("Destination is read-only or is a directory; original retained.");
    if(originalAttributes != INVALID_FILE_ATTRIBUTES && WriteLinkedFile(absolute,bytes))return;
    if(stream){WriteNew(absolute,text);return;}
    // CREATE_NEW prevents collisions; the temporary file stays on the same volume.
    std::filesystem::path temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (unsigned i = 0; i < 100 && file == INVALID_HANDLE_VALUE; ++i) {
        temporary = absolute;
        temporary += L".multipad4-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(i) + L".tmp";
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) Fail("Cannot create temporary save file.");
    }
    if (file == INVALID_HANDLE_VALUE) Fail("Cannot allocate temporary save file.");
    bool ok=true;
    for(size_t offset=0;offset<bytes.size();) {
        DWORD written=0;DWORD count=static_cast<DWORD>((std::min)(ConversionChunk,bytes.size()-offset));
        if(!WriteFile(file,bytes.data()+offset,count,&written,nullptr) || written!=count){ok=false;break;}
        offset+=written;
    }
    if(ok)ok=FlushFileBuffers(file)!=FALSE;
    CloseHandle(file);
    if (!ok) { DeleteFileW(temporary.c_str()); Fail("Cannot write complete file; original retained."); }
    bool exists = GetFileAttributesW(absolute.c_str()) != INVALID_FILE_ATTRIBUTES;
    ok = exists ? ReplaceFileW(absolute.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr) != FALSE
                : MoveFileExW(temporary.c_str(), absolute.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) { DeleteFileW(temporary.c_str()); Fail("Cannot replace destination; original retained."); }
    if(originalAttributes != INVALID_FILE_ATTRIBUTES) {
        // ReplaceFile merges the destination security descriptor and creation
        // metadata; explicitly preserve user-visible DOS attributes too.
        DWORD attributes = originalAttributes & (FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED |
            FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY);
        if(!attributes)attributes=FILE_ATTRIBUTE_NORMAL;
        if(!SetFileAttributesW(absolute.c_str(), attributes)) Fail("File saved, but its attributes could not be restored.");
    }
}
FileTimes GetTimes(const std::filesystem::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) Fail("Cannot read file times.");
    auto value = [](FILETIME t) { return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return {value(data.ftCreationTime), value(data.ftLastAccessTime), value(data.ftLastWriteTime)};
}
void SetTimes(const std::filesystem::path& path, const FileTimes& times) {
    HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) Fail("Cannot open file to update timestamps.");
    auto time = [](unsigned long long v) { return FILETIME{static_cast<DWORD>(v), static_cast<DWORD>(v >> 32)}; };
    auto c = time(times.creation), a = time(times.access), w = time(times.write);
    bool ok = SetFileTime(file, &c, &a, &w) != FALSE; CloseHandle(file);
    if (!ok) Fail("Cannot update file timestamps.");
}
void Touch(const std::filesystem::path& path) {
    auto times = GetTimes(path); FILETIME now{}; GetSystemTimeAsFileTime(&now);
    times.write = (static_cast<unsigned long long>(now.dwHighDateTime) << 32) | now.dwLowDateTime; SetTimes(path, times);
}
const wchar_t* EncodingName(Encoding e) {
    switch(e) {
    case Encoding::Utf8Bom: return L"UTF-8 BOM";
    case Encoding::Utf16LE: return L"UTF-16 LE";
    case Encoding::Utf16BE: return L"UTF-16 BE";
    case Encoding::Utf16LEPlain: return L"UTF-16 LE";
    case Encoding::Utf16BEPlain: return L"UTF-16 BE";
    case Encoding::Ansi: return L"ANSI";
    case Encoding::Oem: return L"OEM";
    case Encoding::Utf8: return L"UTF-8";
    default: {
        thread_local wchar_t label[128]; CPINFOEXW info{};
        if (GetCPInfoExW(CodePage(e), 0, &info)) { wcsncpy_s(label, info.CodePageName, _TRUNCATE); return label; }
        return L"Unknown encoding";
    }
    }
}
}

