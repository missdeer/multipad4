#pragma once
#include "FileDialogTests.h"
#include "../src/FileEncodingGroups.h"

namespace encodingtreepresentationtest {
inline void Require(bool value, std::source_location location=std::source_location::current()) {
    if(!value)throw std::runtime_error("Encoding tree presentation regression at line "+std::to_string(location.line()));
}
inline std::wstring Resource(UINT id) {
    wchar_t text[256]{};Require(LoadStringW(_Module.GetModuleInstance(),id,text,256)>0);return text;
}
inline TVITEMW Item(HWND tree, HTREEITEM node, wchar_t (&text)[256]) {
    TVITEMW item{};item.hItem=node;item.mask=TVIF_TEXT|TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_STATE;
    item.stateMask=TVIS_EXPANDED;item.pszText=text;item.cchTextMax=256;
    Require(node && TreeView_GetItem(tree,&item));return item;
}
inline std::wstring ExpectedLeaf(files::Encoding encoding) {
    auto found=std::find_if(std::begin(EncodingCatalog),std::end(EncodingCatalog),[&](const auto& entry){return entry.encoding==encoding;});
    Require(found!=std::end(EncodingCatalog));
    auto text=Resource(found->label);text.resize(text.find(L';')==std::wstring::npos?text.size():text.find(L';'));
    if(encoding==files::Encoding::Ansi || encoding==files::Encoding::Oem){
        auto systemPage=encoding==files::Encoding::Ansi?GetACP():GetOEMCP();
        text+=systemPage==CP_UTF8?L" (UTF-8, ":L" (";
        text+=std::to_wstring(files::CodePage(encoding))+L")";
    }
    return text;
}
inline void Check(HWND dialog, files::Encoding current) {
    wchar_t prompt[256]{};GetDlgItemTextW(dialog,IDC_ENCODING_LABEL,prompt,256);
    Require(prompt==Resource(IDS_SELRECT_CURRENT_ENCODING));
    HWND tree=GetDlgItem(dialog,IDC_ENCODINGLIST);
    auto images=TreeView_GetImageList(tree,TVSIL_NORMAL);Require(images!=nullptr);
    int count=ImageList_GetImageCount(images);Require(count==3);
    int width=0,height=0;Require(ImageList_GetIconSize(images,&width,&height)!=FALSE);
    UINT dpi=GetDpiForWindow(dialog);int expectedSize=static_cast<int>(dpi*16/96);expectedSize+=expectedSize&1;
    Require(width==expectedSize && height==expectedSize);
    auto checkLeaf=[&](HTREEITEM node,files::Encoding encoding){
        wchar_t text[256]{};auto item=Item(tree,node,text);
        Require(item.lParam==static_cast<LPARAM>(encoding));Require(text==ExpectedLeaf(encoding));
        Require(!TreeView_GetChild(tree,node));
        int expectedImage=static_cast<unsigned>(encoding)<0x10000 || IsValidCodePage(files::CodePage(encoding))?0:1;
        Require(item.iImage==expectedImage && item.iSelectedImage==expectedImage && item.iImage<count);
    };
    auto node=TreeView_GetRoot(tree);checkLeaf(node,files::Encoding::Ansi);
    node=TreeView_GetNextSibling(tree,node);checkLeaf(node,files::Encoding::Oem);
    std::vector<const FileEncodingGroup*> groups;
    for(const auto& group:FileEncodingGroups)groups.push_back(&group);
    std::sort(groups.begin()+2,groups.end()-1,[](const auto* a,const auto* b){return Resource(a->label)<Resource(b->label);});
    for(size_t i=0;i<groups.size();++i){
        node=TreeView_GetNextSibling(tree,node);wchar_t text[256]{};auto item=Item(tree,node,text);
        Require(text==Resource(groups[i]->label));Require(item.lParam<0 && item.iImage==2 && item.iSelectedImage==2);
        if(i<2)Require((item.state&TVIS_EXPANDED)!=0);
        auto child=TreeView_GetChild(tree,node);
        for(auto encoding:groups[i]->encodings){checkLeaf(child,encoding);child=TreeView_GetNextSibling(tree,child);}
        Require(child==nullptr);
    }
    Require(TreeView_GetNextSibling(tree,node)==nullptr);
    wchar_t selectedText[256]{};auto selected=Item(tree,TreeView_GetSelection(tree),selectedText);
    Require(selected.lParam==static_cast<LPARAM>(current));Require(IsWindowEnabled(GetDlgItem(dialog,IDOK))!=FALSE);
    auto parent=TreeView_GetParent(tree,selected.hItem);
    if(parent){wchar_t text[256]{};Require((Item(tree,parent,text).state&TVIS_EXPANDED)!=0);}
    CaptureQAWindow(dialog,L"Select-Encoding-presentation");
    // A group can expand but cannot be accepted as a document encoding.
    auto group=TreeView_GetNextSibling(tree,TreeView_GetNextSibling(tree,TreeView_GetRoot(tree)));
    // Collapsing the current leaf's parent can move the native selection to
    // the group without TVN_SELCHANGED. Start outside it so selecting the
    // collapsed group exercises the dialog's selection handler.
    Require(TreeView_SelectItem(tree,TreeView_GetRoot(tree))!=FALSE);
    TreeView_Expand(tree,group,TVE_COLLAPSE);
    Require(TreeView_SelectItem(tree,group)!=FALSE);
    Require(!IsWindowEnabled(GetDlgItem(dialog,IDOK)));
    wchar_t groupText[256]{};Require((Item(tree,group,groupText).state&TVIS_EXPANDED)!=0);
    SendMessageW(dialog,WM_COMMAND,IDOK,0);Require(IsWindow(dialog)!=FALSE);
    // Cancel after a different valid selection must preserve the caller's value.
    auto alternative=TreeView_GetRoot(tree);
    if(current==files::Encoding::Ansi)alternative=TreeView_GetNextSibling(tree,alternative);
    TreeView_SelectItem(tree,alternative);Require(IsWindowEnabled(GetDlgItem(dialog,IDOK))!=FALSE);
    PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);
}
}
inline void RunEncodingTreePresentationTests(MainFrame& frame) {
    using namespace encodingtreepresentationtest;
    filedialogtest::owner=frame.m_hWnd;
    for(auto current:{files::Encoding::Utf8,files::Encoding::Utf8Bom,files::Encoding::Utf16LE,files::Encoding::Utf16BE,
        files::Encoding::Utf16LEPlain,files::Encoding::Utf16BEPlain,files::Encoding::Ansi,files::Encoding::Oem,
        static_cast<files::Encoding>(0x10000+932)}){
        auto selected=current;
        filedialogtest::Run([&](HWND dialog){Check(dialog,current);},[&]{Require(!SelectFileEncoding(frame.m_hWnd,selected));});
        Require(selected==current);
    }
}
