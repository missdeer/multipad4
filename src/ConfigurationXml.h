#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <stdexcept>
struct ConfigurationValidationError : std::runtime_error {
    unsigned line, column;
    ConfigurationValidationError(unsigned row,unsigned col,const std::string& reason):std::runtime_error(reason),line(row),column(col){}
};
struct ConfigurationXmlNode {
    std::wstring name, text;
    std::map<std::wstring,std::wstring> attributes;
    std::vector<ConfigurationXmlNode> children;
    unsigned line = 0, column = 0;
    std::wstring Attribute(const wchar_t* key, const std::wstring& fallback = {}) const;
    const ConfigurationXmlNode& Child(const wchar_t* key) const;
};
ConfigurationXmlNode ParseConfigurationXml(const std::string& bytes, const std::filesystem::path& source);
ConfigurationXmlNode ReadConfigurationXml(const std::filesystem::path& source);
std::string EmbeddedConfiguration(unsigned resource);
std::string ConfigurationUtf8(const std::wstring& value);
std::wstring ConfigurationWide(const std::string& value);
uint64_t ConfigurationInteger(const std::wstring& value, uint64_t maximum = UINT64_MAX);
void ValidateLegacyStyle(const std::wstring& value);
