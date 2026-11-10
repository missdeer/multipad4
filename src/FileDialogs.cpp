#include "FileDialogs.h"
#include "DialogResizeGrip.h"
#include "MainFrame.h"
#include "FileEncodingCatalog.h"
#include "FileEncodingGroups.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
namespace {
std::wstring Field(HWND window, int id) {
    HWND control = GetDlgItem(window,id); auto length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length)+1,L'\0'); GetWindowTextW(control,text.data(),length+1); text.resize(length); return text;
}
struct EncodingContext { files::Encoding value; };
bool IsAvailableEncoding(files::Encoding encoding) {
    return static_cast<unsigned>(encoding)<0x10000 || IsValidCodePage(files::CodePage(encoding));
}
std::wstring EncodingTreeLabel(files::Encoding encoding) {
    wchar_t resource[256]{};
    for(const auto& entry:EncodingCatalog)if(entry.encoding==encoding){LoadStringW(_Module.GetModuleInstance(),entry.label,resource,256);break;}
    // Encoding_AddToTreeView uses the long name before ';'. The second field
    // is Encoding_GetLabel's compact name for status and other short labels.
    if(auto* separator=wcschr(resource,L';'))*separator=L'\0';
    std::wstring label=resource[0]?resource:files::EncodingName(encoding);
    if(encoding==files::Encoding::Ansi || encoding==files::Encoding::Oem){
        UINT systemPage=encoding==files::Encoding::Ansi?GetACP():GetOEMCP();
        label+=systemPage==CP_UTF8?L" (UTF-8, ":L" (";
        label+=std::to_wstring(files::CodePage(encoding))+L")";
    }
    return label;
}
HIMAGELIST CreateEncodingTreeImages(HWND window) {
    UINT dpi=GetDpiForWindow(window);if(!dpi)dpi=96;
    // Match upstream GetBitmapResourceIdForDPI/ResizeImageForDPI.
    int resource=IDB_ENCODING16;
    if(dpi>120)resource+=(std::min)(static_cast<int>((dpi+23)/48),6)-2;
    auto bitmap=static_cast<HBITMAP>(LoadImageW(_Module.GetModuleInstance(),MAKEINTRESOURCEW(resource),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION));
    BITMAP info{};if(!bitmap || !GetObjectW(bitmap,sizeof(info),&info)){if(bitmap)DeleteObject(bitmap);return nullptr;}
    int size=static_cast<int>(dpi*16/96);size+=size&1;
    if(size!=info.bmHeight){
        auto scaled=static_cast<HBITMAP>(CopyImage(bitmap,IMAGE_BITMAP,MulDiv(size,info.bmWidth,info.bmHeight),size,LR_CREATEDIBSECTION));
        if(scaled){DeleteObject(bitmap);bitmap=scaled;}else size=info.bmHeight;
    }
    auto images=ImageList_Create(size,size,ILC_COLOR32|ILC_MASK,3,0);
    if(images)ImageList_AddMasked(images,bitmap,CLR_DEFAULT);
    DeleteObject(bitmap);
    if(images){
        SHFILEINFOW folder{};
        if(SHGetFileInfoW(L"Icon",FILE_ATTRIBUTE_DIRECTORY,&folder,sizeof(folder),SHGFI_ICON|SHGFI_USEFILEATTRIBUTES|(size<=16?SHGFI_SMALLICON:SHGFI_LARGEICON))){
            ImageList_AddIcon(images,folder.hIcon);DestroyIcon(folder.hIcon);
        }
    }
    return images;
}
INT_PTR CALLBACK EncodingProcedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    auto* context=reinterpret_cast<EncodingContext*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG) {
        context=reinterpret_cast<EncodingContext*>(l);SetWindowLongPtrW(window,DWLP_USER,l);
        PositionDialogResizeGrip(window,IDC_RESIZEGRIP);
        wchar_t prompt[256]{};LoadStringW(_Module.GetModuleInstance(),IDS_SELRECT_CURRENT_ENCODING,prompt,256);
        SetDlgItemTextW(window,IDC_ENCODING_LABEL,prompt);
        HWND tree=GetDlgItem(window,IDC_ENCODINGLIST);
        TreeView_SetImageList(tree,CreateEncodingTreeImages(window),TVSIL_NORMAL);
        HTREEITEM selected=nullptr;
        auto insert=[&](HTREEITEM parent,files::Encoding encoding){
            auto label=EncodingTreeLabel(encoding);
            TVINSERTSTRUCTW item{};item.hParent=parent;item.hInsertAfter=TVI_LAST;
            item.item.mask=TVIF_TEXT|TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE;item.item.pszText=label.data();item.item.lParam=static_cast<LPARAM>(encoding);
            item.item.iImage=IsAvailableEncoding(encoding)?0:1;item.item.iSelectedImage=item.item.iImage;
            auto node=TreeView_InsertItem(tree,&item);if(encoding==context->value)selected=node;
        };
        insert(TVI_ROOT,files::Encoding::Ansi);insert(TVI_ROOT,files::Encoding::Oem);
        std::vector<const FileEncodingGroup*> groups;for(const auto& group:FileEncodingGroups)groups.push_back(&group);
        if(groups.size()>3)std::sort(groups.begin()+2,groups.end()-1,[](const auto* left,const auto* right){wchar_t a[256]{},b[256]{};LoadStringW(_Module.GetModuleInstance(),left->label,a,256);LoadStringW(_Module.GetModuleInstance(),right->label,b,256);return wcscmp(a,b)<0;});
        for(const auto* group:groups){wchar_t label[256]{};LoadStringW(_Module.GetModuleInstance(),group->label,label,256);
            TVINSERTSTRUCTW item{};item.hParent=TVI_ROOT;item.hInsertAfter=TVI_LAST;item.item.mask=TVIF_TEXT|TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE;item.item.pszText=label;item.item.lParam=-1;item.item.iImage=2;item.item.iSelectedImage=2;
            auto parent=TreeView_InsertItem(tree,&item);
            bool expand=group==groups[0] || group==groups[1];
            for(auto encoding:group->encodings){
                insert(parent,encoding);
                if(encoding==context->value || files::CodePage(encoding)==files::CodePage(files::Encoding::Ansi))expand=true;
            }
            if(expand)TreeView_Expand(tree,parent,TVE_EXPAND);
        }
        TreeView_SelectItem(tree,selected);if(selected)TreeView_EnsureVisible(tree,selected);
        EnableWindow(GetDlgItem(window,IDOK),selected!=nullptr);CWindow(window).CenterWindow(GetParent(window));return TRUE;
    }
    if(!context)return FALSE;
    if(message==WM_DESTROY){
        auto images=TreeView_SetImageList(GetDlgItem(window,IDC_ENCODINGLIST),nullptr,TVSIL_NORMAL);
        if(images)ImageList_Destroy(images);return FALSE;
    }
    if(message==WM_SIZE){PositionDialogResizeGrip(window,IDC_RESIZEGRIP);return TRUE;}
    if(message==WM_NOTIFY){
        auto* header=reinterpret_cast<NMHDR*>(l);if(header->idFrom!=IDC_ENCODINGLIST)return FALSE;
        if(header->code==TVN_SELCHANGEDW){auto* notification=reinterpret_cast<NMTREEVIEWW*>(l);
            EnableWindow(GetDlgItem(window,IDOK),notification->itemNew.lParam>=0);
            if(notification->itemNew.lParam<0)TreeView_Expand(header->hwndFrom,notification->itemNew.hItem,TVE_EXPAND);return TRUE;}
        if(header->code==NM_DBLCLK)w=IDOK;else return FALSE;
    }else if(message!=WM_COMMAND)return FALSE;
    if(LOWORD(w)==IDCANCEL){EndDialog(window,IDCANCEL);return TRUE;}
    if(LOWORD(w)==IDOK){HWND tree=GetDlgItem(window,IDC_ENCODINGLIST);TVITEMW item{};item.mask=TVIF_PARAM;item.hItem=TreeView_GetSelection(tree);
        if(item.hItem && TreeView_GetItem(tree,&item) && item.lParam>=0){
            auto encoding=static_cast<files::Encoding>(item.lParam);
            if(IsAvailableEncoding(encoding)){context->value=encoding;EndDialog(window,IDOK);}
            else {MessageBeep(MB_ICONWARNING);SetFocus(tree);}
        }return TRUE;}
    return FALSE;
}
struct BrowseButtonImage {
    HIMAGELIST images = nullptr;
    ~BrowseButtonImage() { if (images) ImageList_Destroy(images); }
    void Attach(HWND window, UINT control) {
        const UINT dpi = GetDpiForWindow(window);
        const int resource = IDB_OPEN_FOLDER16 + std::clamp(static_cast<int>((dpi + 23) / 48) - 2, 0, 4);
        auto bitmap = static_cast<HBITMAP>(LoadImageW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(resource), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
        BITMAP info{};
        if (!bitmap) return;
        if (!GetObjectW(bitmap, sizeof(info), &info)) { DeleteObject(bitmap); return; }
        int size = MulDiv(16, dpi, 96); size += size & 1;
        if (size != info.bmHeight) {
            auto scaled = static_cast<HBITMAP>(CopyImage(bitmap, IMAGE_BITMAP, size, size, LR_CREATEDIBSECTION));
            if (scaled) { DeleteObject(bitmap); bitmap = scaled; } else size = info.bmHeight;
        }
        images = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, 1, 0);
        if (images) ImageList_AddMasked(images, bitmap, CLR_DEFAULT);
        DeleteObject(bitmap);
        BUTTON_IMAGELIST image{}; image.himl = images; image.uAlign = BUTTON_IMAGELIST_ALIGN_CENTER;
        SendDlgItemMessageW(window, control, BCM_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(&image));
    }
};
struct TextContext { UINT resource; std::wstring text; MainFrame* frame=nullptr; SIZE initial{}; std::vector<std::pair<HWND,RECT>> controls; BrowseButtonImage browse; };
INT_PTR CALLBACK TextProcedure(HWND window,UINT message,WPARAM w,LPARAM l){
    auto* context=reinterpret_cast<TextContext*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG){context=reinterpret_cast<TextContext*>(l);SetWindowLongPtrW(window,DWLP_USER,l);
        PositionDialogResizeGrip(window,context->resource==IDD_RUN?IDC_RESIZEGRIP3:IDC_RESIZEGRIP);
        if(context->resource==IDD_RUN)context->browse.Attach(window,IDC_SEARCHEXE);
        SetDlgItemTextW(window,IDC_COMMANDLINE,context->text.c_str());EnableWindow(GetDlgItem(window,IDOK),TRUE);
        RECT client{};GetClientRect(window,&client);context->initial={client.right,client.bottom};
        for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);context->controls.emplace_back(child,r);}
        if(context->frame){auto key=std::to_wstring(context->resource);int saved=_wtoi(context->frame->settings.ReadString(L"Dialog Widths",key.c_str()).c_str());if(saved>client.right){RECT r{};GetWindowRect(window,&r);SetWindowPos(window,nullptr,0,0,saved+r.right-r.left-client.right,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER);}}
        CWindow(window).CenterWindow(GetParent(window));return TRUE;}
    if(!context)return FALSE;
    if(message==WM_GETMINMAXINFO){auto* info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize.x=context->initial.cx+16;info->ptMinTrackSize.y=context->initial.cy+40;info->ptMaxTrackSize.y=info->ptMinTrackSize.y;return TRUE;}
    if(message==WM_SIZE&&context->initial.cx){int dx=LOWORD(l)-context->initial.cx;for(const auto& entry:context->controls){auto r=entry.second;int id=GetDlgCtrlID(entry.first);if(id==IDOK||id==IDCANCEL||id==IDC_SEARCHEXE){r.left+=dx;r.right+=dx;}else if(id==IDC_COMMANDLINE||id==IDC_RUNDESC||id==IDC_FAVORITESDESCR)r.right+=dx;SetWindowPos(entry.first,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);}PositionDialogResizeGrip(window,context->resource==IDD_RUN?IDC_RESIZEGRIP3:IDC_RESIZEGRIP);return TRUE;}
    if(message==WM_DESTROY&&context->frame){RECT r{};GetClientRect(window,&r);auto key=std::to_wstring(context->resource);context->frame->settings.WriteString(L"Dialog Widths",key.c_str(),std::to_wstring(r.right));return FALSE;}
    if(message!=WM_COMMAND)return FALSE;
    if(LOWORD(w)==IDC_SEARCHEXE && context->resource==IDD_RUN){CFileDialog dialog(TRUE,L"exe",nullptr,OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR,L"Programs\0*.exe;*.com;*.bat;*.cmd\0All files\0*.*\0\0",window);
        if(dialog.DoModal()==IDOK)SetDlgItemTextW(window,IDC_COMMANDLINE,(L"\""+std::wstring(dialog.m_szFileName)+L"\"").c_str());return TRUE;}
    if(LOWORD(w)==IDCANCEL){EndDialog(window,IDCANCEL);return TRUE;}
    if(LOWORD(w)==IDOK){context->text=Field(window,IDC_COMMANDLINE);if(!context->text.empty())EndDialog(window,IDOK);return TRUE;}
    return FALSE;
}
std::filesystem::path FavoritesDirectory(MainFrame& frame, bool applications=false) {
    auto directory=frame.settings.ReadString(L"Files",applications?L"OpenWithDirectory":L"FavoritesDirectory");
    auto path=directory.empty()?frame.settings.path.parent_path()/(applications?L"OpenWith":L"Favorites"):std::filesystem::path(directory);
    std::filesystem::create_directories(path);return path;
}
std::filesystem::path ResolveShortcut(const std::filesystem::path& path){
    if(_wcsicmp(path.extension().c_str(),L".lnk")!=0)return path;
    CComPtr<IShellLinkW> link;if(FAILED(link.CoCreateInstance(CLSID_ShellLink)))return path;
    CComQIPtr<IPersistFile> persist(link);if(!persist || FAILED(persist->Load(path.c_str(),STGM_READ)))return path;
    wchar_t target[32768]{};if(SUCCEEDED(link->GetPath(target,32768,nullptr,SLGP_RAWPATH))&&target[0])return target;return path;
}
struct HistoryContext { MainFrame& frame; bool favorites; std::vector<std::wstring> paths; std::wstring selected; bool applications=false; SIZE initialSize{}; std::vector<std::pair<HWND,RECT>> controls; BrowseButtonImage browse; };
INT_PTR CALLBACK HistoryProcedure(HWND window,UINT message,WPARAM w,LPARAM l){
    auto* context=reinterpret_cast<HistoryContext*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG){context=reinterpret_cast<HistoryContext*>(l);SetWindowLongPtrW(window,DWLP_USER,l);
        PositionDialogResizeGrip(window,context->favorites?IDC_RESIZEGRIP3:IDC_RESIZEGRIP);
        if(context->favorites)context->browse.Attach(window,IDC_GETFAVORITESDIR);
        RECT client{};GetClientRect(window,&client);context->initialSize={client.right,client.bottom};
        for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);context->controls.emplace_back(child,r);}
        auto key=context->applications?L"OpenWith":context->favorites?L"Favorites":L"Recent";
        auto saved=context->frame.settings.ReadString(L"Dialog Sizes",key);int width=0,height=0;if(swscanf_s(saved.c_str(),L"%d,%d",&width,&height)==2 && width>=client.right && height>=client.bottom){RECT r{};GetWindowRect(window,&r);SetWindowPos(window,nullptr,0,0,width+r.right-r.left-client.right,height+r.bottom-r.top-client.bottom,SWP_NOMOVE|SWP_NOZORDER);}
        HWND list=GetDlgItem(window,IDC_FILEMRU);SHFILEINFOW systemInfo{};auto images=reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(L"",FILE_ATTRIBUTE_NORMAL,&systemInfo,sizeof(systemInfo),SHGFI_SYSICONINDEX|SHGFI_SMALLICON|SHGFI_USEFILEATTRIBUTES));if(images)ListView_SetImageList(list,images,LVSIL_SMALL);LVCOLUMNW column{};column.mask=LVCF_WIDTH;column.cx=700;ListView_InsertColumn(list,0,&column);
        for(size_t i=0;i<context->paths.size();++i){LVITEMW item{};item.mask=LVIF_TEXT|LVIF_IMAGE;item.iItem=static_cast<int>(i);SHFILEINFOW fileInfo{};bool useAttributes=PathIsUNCW(context->paths[i].c_str())||GetFileAttributesW(context->paths[i].c_str())==INVALID_FILE_ATTRIBUTES;auto iconPath=useAttributes?std::filesystem::path(context->paths[i]).filename().wstring():context->paths[i];if(SHGetFileInfoW(iconPath.c_str(),useAttributes?FILE_ATTRIBUTE_NORMAL:0,&fileInfo,sizeof(fileInfo),SHGFI_SYSICONINDEX|SHGFI_SMALLICON|(useAttributes?SHGFI_USEFILEATTRIBUTES:0)))item.iImage=fileInfo.iIcon;auto label=context->favorites?std::filesystem::path(context->paths[i]).stem().wstring():context->paths[i];item.pszText=label.data();ListView_InsertItem(list,&item);}
        if(context->favorites){SetWindowTextW(window,context->applications?L"Open with...":L"Favorites");}
        else {CheckDlgButton(window,IDC_SAVEMRU,context->frame.settings.Get(IDM_VIEW_NOSAVERECENT,1)?BST_CHECKED:BST_UNCHECKED);SetDlgItemInt(window,IDC_MRU_COUNT_VALUE,context->frame.settings.Get(IDM_FILE_RECENT,32),FALSE);}
        CWindow(window).CenterWindow(GetParent(window));return TRUE;}
    if(!context)return FALSE;
    if(message==WM_GETMINMAXINFO){auto* info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize={context->initialSize.cx+16,context->initialSize.cy+40};return TRUE;}
    if(message==WM_SIZE && context->initialSize.cx){int dx=LOWORD(l)-context->initialSize.cx,dy=HIWORD(l)-context->initialSize.cy;
        for(const auto& entry:context->controls){auto r=entry.second;int id=GetDlgCtrlID(entry.first);if(id==IDC_FILEMRU){r.right+=dx;r.bottom+=dy;}else{r.top+=dy;r.bottom+=dy;if(id==IDOK||id==IDCANCEL||id==IDC_RESIZEGRIP||id==IDC_RESIZEGRIP3){r.left+=dx;r.right+=dx;}}
            SetWindowPos(entry.first,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);}PositionDialogResizeGrip(window,context->favorites?IDC_RESIZEGRIP3:IDC_RESIZEGRIP);return TRUE;}
    if(message==WM_DESTROY){
        RECT client{};GetClientRect(window,&client);auto key=context->applications?L"OpenWith":context->favorites?L"Favorites":L"Recent";context->frame.settings.WriteString(L"Dialog Sizes",key,std::to_wstring(client.right)+L","+std::to_wstring(client.bottom));return FALSE;}
    auto saveRecentOptions=[&](){if(!context->favorites){context->frame.settings.Set(IDM_VIEW_NOSAVERECENT,IsDlgButtonChecked(window,IDC_SAVEMRU)?1:0);BOOL valid=FALSE;int maximum=GetDlgItemInt(window,IDC_MRU_COUNT_VALUE,&valid,FALSE);if(valid)context->frame.settings.Set(IDM_FILE_RECENT,(std::clamp)(maximum,32,4096));}};
    if(message==WM_CLOSE){saveRecentOptions();EndDialog(window,IDCANCEL);return TRUE;}
    if(context->favorites && message==WM_COMMAND && LOWORD(w)==IDC_GETFAVORITESDIR){
        BROWSEINFOW browse{};browse.hwndOwner=window;browse.lpszTitle=L"Select favorites directory";browse.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
        auto item=SHBrowseForFolderW(&browse);if(item){wchar_t path[32768]{};if(SHGetPathFromIDListW(item,path)){context->frame.settings.WriteString(L"Files",context->applications?L"OpenWithDirectory":L"FavoritesDirectory",path);EndDialog(window,IDRETRY);}CoTaskMemFree(item);}return TRUE;}
    if(message==WM_NOTIFY){auto* n=reinterpret_cast<NMHDR*>(l);
        if(n->code==LVN_ITEMCHANGED)EnableWindow(GetDlgItem(window,IDOK),ListView_GetNextItem(GetDlgItem(window,IDC_FILEMRU),-1,LVNI_SELECTED)>=0);
        if(!context->favorites&&n->idFrom==IDC_EMPTY_MRU && (n->code==NM_CLICK||n->code==NM_RETURN)){
            for(int i=0;i<(std::clamp)(context->frame.settings.Get(IDM_FILE_RECENT,32),32,4096);++i)context->frame.settings.WriteString(L"Recent Files",std::to_wstring(i).c_str(),L"");
            context->paths.clear();auto list=GetDlgItem(window,IDC_FILEMRU);ListView_DeleteAllItems(list);
            if(auto* current=context->frame.Active();current&&!current->path.empty()){
                context->paths.push_back(current->path.wstring());context->frame.settings.WriteString(L"Recent Files",L"0",context->paths.back());
                LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=0;item.pszText=context->paths.back().data();ListView_InsertItem(list,&item);
            }
            EnableWindow(GetDlgItem(window,IDOK),FALSE);return TRUE;
        }
        if(n->code!=NM_DBLCLK)return FALSE;w=IDOK;
    }else if(message!=WM_COMMAND)return FALSE;
    if(LOWORD(w)==IDOK){int index=ListView_GetNextItem(GetDlgItem(window,IDC_FILEMRU),-1,LVNI_SELECTED);if(index<0)return TRUE;context->selected=context->paths[index];
        if(!context->favorites){context->frame.settings.Set(IDM_VIEW_NOSAVERECENT,IsDlgButtonChecked(window,IDC_SAVEMRU)?1:0);BOOL ok=FALSE;int count=GetDlgItemInt(window,IDC_MRU_COUNT_VALUE,&ok,FALSE);if(ok)context->frame.settings.Set(IDM_FILE_RECENT,(std::clamp)(count,32,4096));}
        saveRecentOptions();EndDialog(window,IDOK);return TRUE;}
    if(LOWORD(w)==IDCANCEL){saveRecentOptions();EndDialog(window,IDCANCEL);return TRUE;}return FALSE;
}
}
bool SelectFileEncoding(HWND owner,files::Encoding& encoding){EncodingContext context{encoding};if(DialogBoxParamW(_Module.GetModuleInstance(),MAKEINTRESOURCEW(IDD_SELECT_ENCODING),owner,EncodingProcedure,reinterpret_cast<LPARAM>(&context))!=IDOK)return false;encoding=context.value;return true;}
bool ShouldUseSystemMRU(const MainFrame& frame) {
    if (!frame.settings.persistent) return false;
    return frame.runtime.useSystemMRU >= 0 ? frame.runtime.useSystemMRU != 0 : frame.settings.Get(60050, 1) != 0;
}
void RecordOpenedFile(MainFrame& frame,const std::filesystem::path& path){
    if(ShouldUseSystemMRU(frame))SHAddToRecentDocs(SHARD_PATHW,path.c_str());
    if(!frame.settings.Get(IDM_VIEW_NOSAVERECENT,1))return;std::vector<std::wstring> paths{path.wstring()};
    int count=(std::clamp)(frame.settings.Get(IDM_FILE_RECENT,32),32,4096);
    for(int i=0;i<count;++i){auto old=frame.settings.ReadString(L"Recent Files",std::to_wstring(i).c_str());if(!old.empty() && CompareStringOrdinal(old.c_str(),-1,path.c_str(),-1,TRUE)!=CSTR_EQUAL)paths.push_back(old);}
    for(int i=0;i<count;++i)frame.settings.WriteString(L"Recent Files",std::to_wstring(i).c_str(),static_cast<size_t>(i)<paths.size()?paths[i]:L"");
}
bool SelectHistoryFile(MainFrame& frame,bool favorites){HistoryContext context{frame,favorites,{},{}};auto section=favorites?L"Favorites":L"Recent Files";
    if(favorites){for(const auto& item:std::filesystem::directory_iterator(FavoritesDirectory(frame)))context.paths.push_back(item.path().wstring());std::sort(context.paths.begin(),context.paths.end());}
    else for(int i=0;i<(std::clamp)(frame.settings.Get(IDM_FILE_RECENT,32),32,4096);++i){auto value=frame.settings.ReadString(section,std::to_wstring(i).c_str());if(!value.empty())context.paths.push_back(value);}
    auto result=DialogBoxParamW(_Module.GetModuleInstance(),MAKEINTRESOURCEW(favorites?IDD_FAVORITES:IDD_FILEMRU),frame.m_hWnd,HistoryProcedure,reinterpret_cast<LPARAM>(&context));
    if(result==IDRETRY)return SelectHistoryFile(frame,favorites);if(result!=IDOK)return false;
    auto selected=ResolveShortcut(context.selected);if(std::filesystem::is_directory(selected)){CFileDialog dialog(TRUE,nullptr,nullptr,OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR,L"All files\0*.*\0\0",frame.m_hWnd);auto folder=selected.wstring();dialog.m_ofn.lpstrInitialDir=folder.c_str();return dialog.DoModal()==IDOK && frame.Open(dialog.m_szFileName);}return frame.Open(selected);
}
bool AddFavoriteFile(MainFrame& frame){auto* d=frame.Active();if(!d || d->path.empty())return false;
    TextContext context{IDD_ADDTOFAV,d->path.filename().wstring(),&frame};
    if(DialogBoxParamW(_Module.GetModuleInstance(),MAKEINTRESOURCEW(IDD_ADDTOFAV),frame.m_hWnd,TextProcedure,reinterpret_cast<LPARAM>(&context))!=IDOK)return false;
    if(context.text.find_first_of(L"\\/:*?\"<>|")!=std::wstring::npos || context.text==L"." || context.text==L"..")throw std::runtime_error("Invalid favorites item name.");
    auto path=FavoritesDirectory(frame)/(context.text+L".lnk");
    if(std::filesystem::exists(path) && frame.MessageBox(L"Replace the existing favorite?",L"Favorites",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK)return false;
    CComPtr<IShellLinkW> link;if(FAILED(link.CoCreateInstance(CLSID_ShellLink)))throw std::runtime_error("Cannot create favorite shortcut.");
    link->SetPath(d->path.c_str());link->SetWorkingDirectory(d->path.parent_path().c_str());CComQIPtr<IPersistFile> persist(link);
    if(!persist || FAILED(persist->Save(path.c_str(),TRUE)))throw std::runtime_error("Cannot save favorite shortcut.");return true;
}
void ManageFavoriteFiles(MainFrame& frame){auto directory=FavoritesDirectory(frame);ShellExecuteW(frame.m_hWnd,L"open",directory.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}

bool RunFileDialog(MainFrame& frame){TextContext context{IDD_RUN,frame.settings.ReadString(L"Run",L"Command"),&frame};
    if(DialogBoxParamW(_Module.GetModuleInstance(),MAKEINTRESOURCEW(IDD_RUN),frame.m_hWnd,TextProcedure,reinterpret_cast<LPARAM>(&context))!=IDOK)return false;
    auto* d=frame.Active();if(d && frame.settings.Get(IDM_VIEW_SAVEBEFORERUNNINGTOOLS,1) && d->Modified() && !frame.Save(*d))return false;
    std::wstring command=context.text;if(d){auto replace=[&](const std::wstring& token,const std::wstring& value){size_t position=0;while((position=command.find(token,position))!=std::wstring::npos){command.replace(position,token.size(),value);position+=value.size();}};replace(L"$(FilePath)",d->path.wstring());replace(L"$(FileDir)",d->path.parent_path().wstring());replace(L"$(FileName)",d->path.filename().wstring());}
    ExecuteExternalCommand(frame,command);
    frame.settings.WriteString(L"Run",L"Command",context.text);return true;
}
namespace {
struct DefaultsContext { MainFrame& frame; bool encoding; files::Encoding value; };
constexpr int encodingControls[]={IDC_NOUNICODEDETECTION,IDC_ASCIIASUTF8,IDC_ANSIASUTF8,IDC_NFOASOEM,IDC_ENCODINGFROMFILEVARS};
constexpr const wchar_t* encodingKeys[]={L"SkipUnicodeDetection",L"ASCIIAsUTF8",L"ANSIAsUTF8",L"NFOAsOEM",L"IgnoreEncodingTags"};
constexpr int eolControls[]={IDC_WARNINCONSISTENTEOLS,IDC_CONSISTENTEOLS,IDC_AUTOSTRIPBLANKS};
constexpr const wchar_t* eolKeys[]={L"WarnInconsistentEOL",L"ConsistentEOL",L"StripTrailingBlanks"};
INT_PTR CALLBACK DefaultsProcedure(HWND window,UINT message,WPARAM w,LPARAM l){
    auto* context=reinterpret_cast<DefaultsContext*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG){context=reinterpret_cast<DefaultsContext*>(l);SetWindowLongPtrW(window,DWLP_USER,l);
        if(context->encoding){SetDlgItemTextW(window,IDC_ENCODING_LABEL,files::EncodingName(context->value));
            for(int i=0;i<5;++i)CheckDlgButton(window,encodingControls[i],context->frame.settings.ReadString(L"File Settings",encodingKeys[i],i==1?L"1":L"0")==L"1"?BST_CHECKED:BST_UNCHECKED);
        }else{for(auto label:{L"Windows (CR+LF)",L"Classic Mac OS (CR)",L"Unix/macOS (LF)"})SendDlgItemMessageW(window,IDC_EOLMODELIST,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
            SendDlgItemMessageW(window,IDC_EOLMODELIST,CB_SETCURSEL,context->frame.settings.Get(IDM_LINEENDINGS_SETDEFAULT,SC_EOL_CRLF),0);
            for(int i=0;i<3;++i)CheckDlgButton(window,eolControls[i],context->frame.settings.ReadString(L"File Settings",eolKeys[i],L"0")==L"1"?BST_CHECKED:BST_UNCHECKED);
        }CWindow(window).CenterWindow(GetParent(window));return TRUE;}
    if(!context)return FALSE;
    if(message==WM_NOTIFY && context->encoding){auto* notification=reinterpret_cast<NMHDR*>(l);if(notification->idFrom==IDC_ENCODING_LINK && (notification->code==NM_CLICK||notification->code==NM_RETURN)){
        if(SelectFileEncoding(window,context->value))SetDlgItemTextW(window,IDC_ENCODING_LABEL,files::EncodingName(context->value));return TRUE;}return FALSE;}
    if(message!=WM_COMMAND)return FALSE;
    if(LOWORD(w)==IDCANCEL){EndDialog(window,IDCANCEL);return TRUE;}
    if(LOWORD(w)==IDOK){if(context->encoding){context->frame.settings.Set(IDM_ENCODING_SETDEFAULT,static_cast<int>(context->value));for(int i=0;i<5;++i)context->frame.settings.WriteString(L"File Settings",encodingKeys[i],IsDlgButtonChecked(window,encodingControls[i])?L"1":L"0");}
        else{context->frame.settings.Set(IDM_LINEENDINGS_SETDEFAULT,static_cast<int>(SendDlgItemMessageW(window,IDC_EOLMODELIST,CB_GETCURSEL,0,0)));for(int i=0;i<3;++i)context->frame.settings.WriteString(L"File Settings",eolKeys[i],IsDlgButtonChecked(window,eolControls[i])?L"1":L"0");}
        EndDialog(window,IDOK);return TRUE;}return FALSE;
}
}
void ConfigureDefaultEncoding(MainFrame& frame){DefaultsContext context{frame,true,static_cast<files::Encoding>(frame.settings.Get(IDM_ENCODING_SETDEFAULT,static_cast<int>(files::Encoding::Ansi)))};DialogBoxParamW(_Module.GetModuleInstance(),MAKEINTRESOURCEW(IDD_DEFENCODING),frame.m_hWnd,DefaultsProcedure,reinterpret_cast<LPARAM>(&context));}
void ConfigureDefaultLineEndings(MainFrame& frame){DefaultsContext context{frame,false,files::Encoding::Utf8};DialogBoxParamW(_Module.GetModuleInstance(),MAKEINTRESOURCEW(IDD_DEFEOLMODE),frame.m_hWnd,DefaultsProcedure,reinterpret_cast<LPARAM>(&context));}

bool OpenWithFileDialog(MainFrame& frame){
    auto* document=frame.Active();if(!document || document->path.empty())return false;
    if(frame.settings.Get(IDM_VIEW_SAVEBEFORERUNNINGTOOLS,1) && document->Modified() && !frame.Save(*document))return false;
    HistoryContext context{frame,true,{},{},true};
    for(const auto& item:std::filesystem::directory_iterator(FavoritesDirectory(frame,true)))context.paths.push_back(item.path().wstring());
    std::sort(context.paths.begin(),context.paths.end());
    auto result=DialogBoxParamW(_Module.GetModuleInstance(),MAKEINTRESOURCEW(IDD_OPENWITH),frame.m_hWnd,HistoryProcedure,reinterpret_cast<LPARAM>(&context));
    if(result==IDRETRY)return OpenWithFileDialog(frame);if(result!=IDOK)return false;
    auto executable=ResolveShortcut(context.selected);auto parameter=L"\""+document->path.wstring()+L"\"";
    if(reinterpret_cast<INT_PTR>(ShellExecuteW(frame.m_hWnd,L"open",executable.c_str(),parameter.c_str(),document->path.parent_path().c_str(),SW_SHOWNORMAL))<=32)throw std::runtime_error("Cannot open file with selected application.");return true;
}

void ExecuteExternalCommand(MainFrame& frame,std::wstring command){
    auto* d=frame.Active();
    auto expandedSize=ExpandEnvironmentStringsW(command.c_str(),nullptr,0);
    if(expandedSize){std::wstring expanded(expandedSize,L'\0');ExpandEnvironmentStringsW(command.c_str(),expanded.data(),expandedSize);expanded.resize(expandedSize-1);command=std::move(expanded);}
    auto first=command.find_first_not_of(L" \t");if(first==std::wstring::npos)return;
    std::wstring executable,arguments;
    size_t end=first;
    if(command[first]==L'"'){end=command.find(L'"',first+1);if(end==std::wstring::npos)throw std::runtime_error("Unmatched quote in executable path.");executable=command.substr(first+1,end-first-1);++end;}
    else{end=command.find_first_of(L" \t",first);executable=command.substr(first,end==std::wstring::npos?end:end-first);}
    if(end!=std::wstring::npos){auto start=command.find_first_not_of(L" \t",end);if(start!=std::wstring::npos)arguments=command.substr(start);}
    if(_wcsicmp(executable.c_str(),L"notepad4")==0||_wcsicmp(executable.c_str(),L"notepad4.exe")==0||_wcsicmp(executable.c_str(),L"multipad4")==0||_wcsicmp(executable.c_str(),L"multipad4.exe")==0){wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);executable=path;}
    auto directory=d&&!d->path.empty()?d->path.parent_path().wstring():L"";
    SHELLEXECUTEINFOW execute{sizeof(execute)};execute.hwnd=frame.m_hWnd;execute.fMask=SEE_MASK_DOENVSUBST;execute.lpFile=executable.c_str();execute.lpParameters=arguments.c_str();execute.lpDirectory=directory.empty()?nullptr:directory.c_str();execute.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&execute))throw std::runtime_error("Cannot run the specified command.");
}
