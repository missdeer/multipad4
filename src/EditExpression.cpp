// Windows ActiveScript evaluation follows notepad4 Bridge.cpp's engine order.
#include "EditExpression.h"
#include "FileIO.h"
#include <atlbase.h>
#include <atlcomcli.h>
#include <activscp.h>
#include <stdexcept>
namespace {
class Site final : public IActiveScriptSite {
  public:
    std::wstring error;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **result) override {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (iid == IID_IUnknown || iid == IID_IActiveScriptSite) {
            *result = static_cast<IActiveScriptSite *>(this);
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return 1;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        return 1;
    }
    HRESULT STDMETHODCALLTYPE GetLCID(LCID *locale) override {
        *locale = LOCALE_USER_DEFAULT;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetItemInfo(LPCOLESTR, DWORD, IUnknown **, ITypeInfo **) override {
        return TYPE_E_ELEMENTNOTFOUND;
    }
    HRESULT STDMETHODCALLTYPE GetDocVersionString(BSTR *version) override {
        *version = SysAllocString(L"1");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnScriptTerminate(const VARIANT *, const EXCEPINFO *) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnStateChange(SCRIPTSTATE) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnScriptError(IActiveScriptError *scriptError) override {
        EXCEPINFO info{};
        if (SUCCEEDED(scriptError->GetExceptionInfo(&info))) {
            if (info.bstrDescription)
                error = info.bstrDescription;
            SysFreeString(info.bstrSource);
            SysFreeString(info.bstrDescription);
            SysFreeString(info.bstrHelpFile);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnEnterScript() override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnLeaveScript() override {
        return S_OK;
    }
};
} // namespace
std::string EvaluateExpression(const std::string &text, bool math) {
    auto expression = files::FromUtf8(text);
    if (math)
        expression = L"with(Math){\n" + expression + L"}";
    constexpr GUID chakra = {0x1b7cd997, 0xe5ff, 0x4932, {0xa7, 0xa6, 0x2a, 0x9e, 0x63, 0x6d, 0xa3, 0x85}};
    ATL::CComPtr<IActiveScript> engine;
    HRESULT status = CoCreateInstance(chakra, nullptr, CLSCTX_INPROC_SERVER, IID_IActiveScript,
                                      reinterpret_cast<void **>(&engine.p));
    if (FAILED(status)) {
        CLSID clsid{};
        status = CLSIDFromProgID(L"JavaScript", &clsid);
        if (SUCCEEDED(status))
            status = engine.CoCreateInstance(clsid);
    }
    if (FAILED(status))
        throw std::runtime_error("No Windows JavaScript engine is installed.");
    Site site;
    status = engine->SetScriptSite(&site);
    ATL::CComPtr<IActiveScriptParse> parser;
    if (SUCCEEDED(status))
        status = engine->QueryInterface(IID_IActiveScriptParse, reinterpret_cast<void **>(&parser.p));
    if (SUCCEEDED(status))
        status = parser->InitNew();
    ATL::CComVariant value;
    if (SUCCEEDED(status))
        status = parser->ParseScriptText(expression.c_str(), nullptr, nullptr, nullptr, 0, 0, SCRIPTTEXT_ISEXPRESSION,
                                         &value, nullptr);
    if (FAILED(status)) {
        engine->Close();
        throw std::runtime_error(site.error.empty() ? "JavaScript expression failed." : files::ToUtf8(site.error));
    }
    if (value.vt == VT_DISPATCH && value.pdispVal) {
        LPOLESTR name = const_cast<LPOLESTR>(L"toString");
        DISPID identifier{};
        status = value.pdispVal->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &identifier);
        if (SUCCEEDED(status)) {
            DISPPARAMS args{};
            ATL::CComVariant stringValue;
            status = value.pdispVal->Invoke(identifier, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &args, &stringValue,
                                            nullptr, nullptr);
            if (SUCCEEDED(status))
                value = stringValue;
        }
    }
    ATL::CComVariant result;
    status = VariantChangeType(&result, &value, 0, VT_BSTR);
    engine->Close();
    if (FAILED(status))
        throw std::runtime_error("Expression result cannot be converted to text.");
    return files::ToUtf8(result.bstrVal ? std::wstring(result.bstrVal, SysStringLen(result.bstrVal)) : std::wstring{});
}
