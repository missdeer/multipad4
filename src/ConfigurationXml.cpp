#include "ConfigurationXml.h"
#include <windows.h>
#include <objbase.h>
#include <xmllite.h>
#include <shlwapi.h>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <charconv>
#include <sstream>
#include <set>
#include <cmath>
namespace {
template<class T> struct Release { void operator()(T* value) const { if(value)value->Release(); } };
}
std::string ConfigurationUtf8(const std::wstring& value) {
    if(value.empty())return {};
    int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(!size)throw std::runtime_error("Invalid UTF-16");
    std::string result(size,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr);return result;
}
std::wstring ConfigurationWide(const std::string& value) {
    if(value.empty())return {};
    int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(!size)throw std::runtime_error("Invalid UTF-8");
    std::wstring result(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size);return result;
}
uint64_t ConfigurationInteger(const std::wstring& value,uint64_t maximum) {
    auto text=ConfigurationUtf8(value);uint64_t number=0;
    auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
    if(text.empty()||parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||number>maximum)throw std::runtime_error("Invalid integer: "+text);
    return number;
}
void ValidateLegacyStyle(const std::wstring& value) {
    if(value.size()>8192)throw std::runtime_error("Style definition too large");
    auto trim=[](std::wstring text){auto begin=text.find_first_not_of(L" \t\r\n");if(begin==std::wstring::npos)return std::wstring{};return text.substr(begin,text.find_last_not_of(L" \t\r\n")-begin+1);};
    std::wistringstream stream(value);std::wstring item;
    const std::set<std::wstring> flags={L"bold",L"italic",L"underline",L"strike",L"strikeout",L"overline",L"eolfilled",L"notbold",L"notitalic"};
    while(std::getline(stream,item,L';')){
        item=trim(item);if(item.empty()||item.front()==L'#')continue;auto colon=item.find(L':');auto key=trim(item.substr(0,colon));auto text=colon==std::wstring::npos?L"":trim(item.substr(colon+1));
        if(key==L"fore"||key==L"back"){
            if(text==L"inherit")continue;
            if(text.size()!=7||text.front()!=L'#'||text.find_first_not_of(L"0123456789abcdefABCDEF",1)!=std::wstring::npos)throw std::runtime_error("Invalid legacy style color");
        }else if(key==L"size"||key==L"alpha"||key==L"outline"||key==L"weight"||key==L"charset"){
            wchar_t* end=nullptr;double number=wcstod(text.c_str(),&end);if(text.empty()||*end||!std::isfinite(number)||number< -512||number>65535)throw std::runtime_error("Invalid legacy style numeric value");
        }else if(key==L"font"){if(text.size()>128)throw std::runtime_error("Invalid legacy style font");}
        else if(key!=L"case"&&!flags.contains(key)&&!key.starts_with(L"x-"))throw std::runtime_error("Unknown legacy style property");
    }
}
std::wstring ConfigurationXmlNode::Attribute(const wchar_t* key,const std::wstring& fallback) const {
    auto found=attributes.find(key);return found==attributes.end()?fallback:found->second;
}
const ConfigurationXmlNode& ConfigurationXmlNode::Child(const wchar_t* key) const {
    const ConfigurationXmlNode* result=nullptr;
    for(const auto& child:children)if(child.name==key){if(result)throw std::runtime_error("Duplicate element");result=&child;}
    if(!result)throw std::runtime_error("Missing element: "+ConfigurationUtf8(key));return *result;
}
ConfigurationXmlNode ParseConfigurationXml(const std::string& bytes,const std::filesystem::path& source) {
    if(bytes.empty()||bytes.size()>8*1024*1024)throw std::runtime_error("XML size must be 1..8 MiB");
    std::unique_ptr<IStream,Release<IStream>> stream(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()),static_cast<UINT>(bytes.size())));
    IXmlReader* raw=nullptr;
    if(!stream||FAILED(CreateXmlReader(__uuidof(IXmlReader),reinterpret_cast<void**>(&raw),nullptr)))throw std::runtime_error("Cannot create XmlLite reader");
    std::unique_ptr<IXmlReader,Release<IXmlReader>> reader(raw);
    if(FAILED(reader->SetProperty(XmlReaderProperty_DtdProcessing,DtdProcessing_Prohibit))||FAILED(reader->SetProperty(XmlReaderProperty_XmlResolver,0))||FAILED(reader->SetInput(stream.get())))throw std::runtime_error("Cannot secure XML input");
    ConfigurationXmlNode document;std::vector<ConfigurationXmlNode*> stack{&document};
    XmlNodeType type{};HRESULT status;unsigned nodes=0;
    try {
        while((status=reader->Read(&type))==S_OK) {
            const wchar_t* value=nullptr;UINT length=0;
            if(type==XmlNodeType_Element) {
                if(++nodes>100000||stack.size()>32)throw std::runtime_error("XML structure limit exceeded");
                reader->GetLocalName(&value,&length);ConfigurationXmlNode node;node.name.assign(value,length);
                reader->GetLineNumber(&node.line);reader->GetLinePosition(&node.column);
                if(reader->MoveToFirstAttribute()==S_OK)do {
                    reader->GetLocalName(&value,&length);std::wstring key(value,length);reader->GetValue(&value,&length);
                    if(node.attributes.size()>=64||!node.attributes.emplace(key,std::wstring(value,length)).second)throw std::runtime_error("Duplicate or excessive XML attributes");
                }while(reader->MoveToNextAttribute()==S_OK);
                reader->MoveToElement();stack.back()->children.push_back(std::move(node));
                if(!reader->IsEmptyElement())stack.push_back(&stack.back()->children.back());
            } else if(type==XmlNodeType_EndElement) {
                if(stack.size()<2)throw std::runtime_error("Invalid XML nesting");stack.pop_back();
            } else if(type==XmlNodeType_Text||type==XmlNodeType_CDATA||type==XmlNodeType_Whitespace) {
                reader->GetValue(&value,&length);if(stack.back()->text.size()+length>2*1024*1024)throw std::runtime_error("XML text limit exceeded");stack.back()->text.append(value,length);
            } else if(type==XmlNodeType_DocumentType)throw std::runtime_error("DTD and entities are prohibited");
        }
        if(FAILED(status)||stack.size()!=1||document.children.size()!=1)throw std::runtime_error("Malformed XML");
    }catch(const std::exception& error){UINT line=0,column=0;reader->GetLineNumber(&line);reader->GetLinePosition(&column);throw std::runtime_error(ConfigurationUtf8(source.wstring())+":"+std::to_string(line)+":"+std::to_string(column)+": "+error.what());}
    return std::move(document.children.front());
}
ConfigurationXmlNode ReadConfigurationXml(const std::filesystem::path& source) {
    std::ifstream input(source,std::ios::binary|std::ios::ate);if(!input)throw std::runtime_error("Cannot read "+ConfigurationUtf8(source.wstring()));
    auto length=input.tellg();if(length<=0||length>8*1024*1024)throw std::runtime_error("Invalid XML file size");
    std::string data(static_cast<size_t>(length),'\0');input.seekg(0);if(!input.read(data.data(),length))throw std::runtime_error("Cannot read XML file");return ParseConfigurationXml(data,source);
}
std::string EmbeddedConfiguration(unsigned id) {
    auto module=GetModuleHandleW(nullptr);auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);
    if(!resource)throw std::runtime_error("Missing embedded XML resource");
    auto data=LoadResource(module,resource);auto bytes=static_cast<const char*>(LockResource(data));return std::string(bytes,SizeofResource(module,resource));
}
