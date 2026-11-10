#include "Printing.h"
#include <stdexcept>
#include "AppSettings.h"
#include "resource.h"
namespace {
PAGESETUPDLGW setup{sizeof(setup)};
HGLOBAL CloneGlobal(HGLOBAL original) {
    if(!original)return nullptr;
    auto size=GlobalSize(original);auto copy=GlobalAlloc(GMEM_MOVEABLE,size);
    if(!copy)throw std::runtime_error("Cannot allocate printer settings.");
    auto* source=GlobalLock(original);auto* destination=GlobalLock(copy);
    if(!source||!destination){if(source)GlobalUnlock(original);if(destination)GlobalUnlock(copy);GlobalFree(copy);throw std::runtime_error("Cannot copy printer settings.");}
    memcpy(destination,source,size);GlobalUnlock(original);GlobalUnlock(copy);return copy;
}
PrintPreferences preferences;
void PreferencesIO(AppSettings* settings,bool write){
    if(!settings)return;
    auto item=[&](const wchar_t* key,int& value){if(write)settings->WriteString(L"Print",key,std::to_wstring(value));else{auto text=settings->ReadString(L"Print",key,std::to_wstring(value).c_str());value=_wtoi(text.c_str());}};
    item(L"Header",preferences.header);item(L"Footer",preferences.footer);item(L"Color",preferences.color);item(L"Zoom",preferences.zoom);
    preferences.header=(std::clamp)(preferences.header,0,3);preferences.footer=(std::clamp)(preferences.footer,0,1);preferences.color=(std::clamp)(preferences.color,0,5);preferences.zoom=(std::clamp)(preferences.zoom,10,500);
    int left=setup.rtMargin.left,top=setup.rtMargin.top,right=setup.rtMargin.right,bottom=setup.rtMargin.bottom;
    item(L"MarginLeft",left);item(L"MarginTop",top);item(L"MarginRight",right);item(L"MarginBottom",bottom);setup.rtMargin={left,top,right,bottom};
}
UINT_PTR CALLBACK SetupHook(HWND window,UINT message,WPARAM w,LPARAM){
    if(message==WM_INITDIALOG){
        for(int zoom:{50,75,100,125,150,175,200,250,300,400,500}){auto text=std::to_wstring(zoom);SendDlgItemMessageW(window,IDC_PAGESETUP_ZOOMLEVEL,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
        SetDlgItemInt(window,IDC_PAGESETUP_ZOOMLEVEL,preferences.zoom,FALSE);
        auto list=[&](UINT control,UINT resource,int selected){wchar_t text[1024]{};LoadStringW(_Module.GetModuleInstance(),resource,text,1024);std::wstring labels=text;size_t start=0,end=0;while(start<labels.size()){end=labels.find(L'|',start);auto label=labels.substr(start,end==std::wstring::npos?end:end-start);if(!label.empty())SendDlgItemMessageW(window,control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(end==std::wstring::npos)break;start=end+1;}SendDlgItemMessageW(window,control,CB_SETCURSEL,selected,0);};
        list(IDC_PAGESETUP_HEADER_LIST,IDS_PRINT_HEADER,preferences.header);list(IDC_PAGESETUP_FOOTER_LIST,IDS_PRINT_FOOTER,preferences.footer);list(IDC_PAGESETUP_COLOR_MODE_LIST,IDS_PRINT_COLOR,preferences.color);
    }
    if(message==WM_COMMAND && LOWORD(w)==IDOK){BOOL valid=FALSE;auto zoom=GetDlgItemInt(window,IDC_PAGESETUP_ZOOMLEVEL,&valid,FALSE);preferences.zoom=valid?(std::clamp)(static_cast<int>(zoom),10,500):100;
        preferences.header=static_cast<int>(SendDlgItemMessageW(window,IDC_PAGESETUP_HEADER_LIST,CB_GETCURSEL,0,0));preferences.footer=static_cast<int>(SendDlgItemMessageW(window,IDC_PAGESETUP_FOOTER_LIST,CB_GETCURSEL,0,0));preferences.color=static_cast<int>(SendDlgItemMessageW(window,IDC_PAGESETUP_COLOR_MODE_LIST,CB_GETCURSEL,0,0));}
    return 0;
}
}
PrintPreferences CurrentPrintPreferences() { return preferences; }
UINT_PTR CALLBACK PrintSetupHook(HWND window, UINT message, WPARAM w, LPARAM l) { return SetupHook(window,message,w,l); }
void ConfigurePrinting(HWND owner, AppSettings* settings) {
    PreferencesIO(settings,false);auto savedPreferences=preferences;auto savedMargins=setup.rtMargin;
    setup.hwndOwner=owner;setup.hInstance=_Module.GetModuleInstance();
    setup.Flags=PSD_MARGINS|PSD_INHUNDREDTHSOFMILLIMETERS|PSD_ENABLEPAGESETUPHOOK|PSD_ENABLEPAGESETUPTEMPLATE;
    setup.lpPageSetupTemplateName=MAKEINTRESOURCEW(IDD_PAGESETUP);setup.lpfnPageSetupHook=SetupHook;
    if(!setup.rtMargin.left && !setup.rtMargin.right)setup.rtMargin={1500,1500,1500,1500};
    auto candidate=setup;
    wchar_t measurement[8]{};GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT,LOCALE_IMEASURE,measurement,8);
    const bool inches=measurement[0]==L'1';
    if(inches){
        candidate.Flags=(candidate.Flags&~PSD_INHUNDREDTHSOFMILLIMETERS)|PSD_INTHOUSANDTHSOFINCHES;
        candidate.rtMargin={MulDiv(setup.rtMargin.left,1000,2540),MulDiv(setup.rtMargin.top,1000,2540),MulDiv(setup.rtMargin.right,1000,2540),MulDiv(setup.rtMargin.bottom,1000,2540)};
    }
    candidate.hDevMode=CloneGlobal(setup.hDevMode);
    try{candidate.hDevNames=CloneGlobal(setup.hDevNames);}catch(...){if(candidate.hDevMode)GlobalFree(candidate.hDevMode);throw;}
    if(PageSetupDlgW(&candidate)){
        if(inches)candidate.rtMargin={MulDiv(candidate.rtMargin.left,2540,1000),MulDiv(candidate.rtMargin.top,2540,1000),MulDiv(candidate.rtMargin.right,2540,1000),MulDiv(candidate.rtMargin.bottom,2540,1000)};
        candidate.Flags=(candidate.Flags&~PSD_INTHOUSANDTHSOFINCHES)|PSD_INHUNDREDTHSOFMILLIMETERS;
        if(setup.hDevMode)GlobalFree(setup.hDevMode);if(setup.hDevNames)GlobalFree(setup.hDevNames);setup=candidate;PreferencesIO(settings,true);
    }else{
        if(candidate.hDevMode)GlobalFree(candidate.hDevMode);if(candidate.hDevNames)GlobalFree(candidate.hDevNames);
        preferences=savedPreferences;setup.rtMargin=savedMargins;
    }
}
bool PrintDocument(HWND owner, const EditorDocument& document, AppSettings* settings, bool defaultPrinter) {
    PreferencesIO(settings,false);
    PRINTDLGW dialog{sizeof(dialog)};
    dialog.hwndOwner = owner; dialog.Flags = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE | PD_ALLPAGES;
    dialog.nFromPage=dialog.nToPage=dialog.nMinPage=1;dialog.nMaxPage=0xFFFF;
    if(defaultPrinter)dialog.Flags|=PD_RETURNDEFAULT;
    dialog.hDevMode = defaultPrinter ? nullptr : CloneGlobal(setup.hDevMode);
    try{dialog.hDevNames = defaultPrinter ? nullptr : CloneGlobal(setup.hDevNames);}catch(...){if(dialog.hDevMode)GlobalFree(dialog.hDevMode);throw;}
    dialog.nCopies = 1;
    if (document.Send(SCI_GETSELECTIONEMPTY)) dialog.Flags |= PD_NOSELECTION;
    else dialog.Flags |= PD_SELECTION;
    if (!PrintDlgW(&dialog)) {
        if(dialog.hDevMode)GlobalFree(dialog.hDevMode);if(dialog.hDevNames)GlobalFree(dialog.hDevNames);if(dialog.hDC)DeleteDC(dialog.hDC);
        return false;
    }
    if(setup.hDevMode)GlobalFree(setup.hDevMode);if(setup.hDevNames)GlobalFree(setup.hDevNames);
    setup.hDevMode = dialog.hDevMode; setup.hDevNames = dialog.hDevNames;
    HDC dc = dialog.hDC;
    DOCINFOW info{sizeof(info)}; auto title = document.Title(); info.lpszDocName = title.c_str();
    if (StartDocW(dc, &info) <= 0) { DeleteDC(dc); throw std::runtime_error("Cannot start print job."); }
    const int dpiX = GetDeviceCaps(dc, LOGPIXELSX), dpiY = GetDeviceCaps(dc, LOGPIXELSY);
    const int offsetX = GetDeviceCaps(dc, PHYSICALOFFSETX), offsetY = GetDeviceCaps(dc, PHYSICALOFFSETY);
    const int pageWidth = GetDeviceCaps(dc, PHYSICALWIDTH), pageHeight = GetDeviceCaps(dc, PHYSICALHEIGHT);
    RECT margin = setup.rtMargin; if (!margin.left && !margin.right && !margin.top && !margin.bottom) margin = {1500,1500,1500,1500};
    Sci_RangeToFormatFull range{}; range.hdc = dc; range.hdcTarget = dc;
    range.rcPage = {-offsetX, -offsetY, pageWidth-offsetX, pageHeight-offsetY};
    range.rc = {(std::max)(0, MulDiv(margin.left,dpiX,2540)-offsetX), (std::max)(0, MulDiv(margin.top,dpiY,2540)-offsetY),
        (std::min)(GetDeviceCaps(dc,HORZRES), pageWidth-offsetX-MulDiv(margin.right,dpiX,2540)),
        (std::min)(GetDeviceCaps(dc,VERTRES), pageHeight-offsetY-MulDiv(margin.bottom,dpiY,2540))};
    range.chrg.cpMin = (dialog.Flags & PD_SELECTION) ? document.Send(SCI_GETSELECTIONSTART) : 0;
    range.chrg.cpMax = (dialog.Flags & PD_SELECTION) ? document.Send(SCI_GETSELECTIONEND) : document.Send(SCI_GETLENGTH);
    auto magnification=document.Send(SCI_GETPRINTMAGNIFICATION), color=document.Send(SCI_GETPRINTCOLOURMODE);
    document.Send(SCI_SETPRINTMAGNIFICATION,preferences.zoom);document.Send(SCI_SETPRINTCOLOURMODE,preferences.color);
    int pointSize=static_cast<int>(document.Send(SCI_STYLEGETSIZEFRACTIONAL,STYLE_DEFAULT));
    HFONT font=CreateFontW(-MulDiv((std::max)(200,pointSize-200),dpiY,7200),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    HFONT headerFont=CreateFontW(-MulDiv((std::max)(200,pointSize-50),dpiY,7200),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto oldFont=SelectObject(dc,font);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);int footerHeight=metrics.tmHeight+metrics.tmExternalLeading;
    SelectObject(dc,headerFont);GetTextMetricsW(dc,&metrics);int headerHeight=metrics.tmHeight+metrics.tmExternalLeading;
    auto contentTop=range.rc.top,contentBottom=range.rc.bottom;
    if(preferences.header!=3)range.rc.top+=headerHeight+headerHeight/2;
    if(preferences.footer!=1)range.rc.bottom-=footerHeight+footerHeight/2;
    unsigned page=0;
    bool ok = range.rc.right > range.rc.left && range.rc.bottom > range.rc.top;
    do {
        ++page;
        if((dialog.Flags&PD_PAGENUMS) && page>dialog.nToPage)break;
        const bool printPage=!(dialog.Flags&PD_PAGENUMS)||page>=dialog.nFromPage;
        if (!ok || (printPage && StartPage(dc) <= 0)) { ok = false; break; }
        if(printPage){
        SelectObject(dc,headerFont);SetTextColor(dc,RGB(0,0,0));SetBkMode(dc,TRANSPARENT);
        if(preferences.header!=3){
            std::wstring header=document.path.empty()?document.untitled:document.path.filename().wstring(),dateText;
            if(preferences.header<2){wchar_t date[128]{},time[128]{};GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,DATE_SHORTDATE,nullptr,nullptr,date,128,nullptr);dateText=date;if(preferences.header==0){GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,TIME_NOSECONDS,nullptr,nullptr,time,128);dateText+=L" ";dateText+=time;}}
            int nameRight=range.rc.right;
            if(!dateText.empty()){SelectObject(dc,font);SIZE size{};GetTextExtentPoint32W(dc,dateText.c_str(),static_cast<int>(dateText.size()),&size);RECT dateArea{range.rc.right-size.cx,contentTop,range.rc.right,contentTop+headerHeight};DrawTextW(dc,dateText.c_str(),-1,&dateArea,DT_RIGHT|DT_SINGLELINE);nameRight=dateArea.left-MulDiv(12,dpiX,96);}
            SelectObject(dc,headerFont);RECT area{range.rc.left,contentTop,(std::max)(range.rc.left,nameRight),contentTop+headerHeight};DrawTextW(dc,header.c_str(),-1,&area,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
            auto pen=CreatePen(PS_SOLID,1,RGB(0,0,0));auto oldPen=SelectObject(dc,pen);MoveToEx(dc,range.rc.left,range.rc.top-headerHeight/4,nullptr);LineTo(dc,range.rc.right,range.rc.top-headerHeight/4);SelectObject(dc,oldPen);DeleteObject(pen);
        }
        if(preferences.footer!=1){SelectObject(dc,font);auto text=L"Page "+std::to_wstring(page);RECT area{range.rc.left,contentBottom-footerHeight,range.rc.right,contentBottom};DrawTextW(dc,text.c_str(),-1,&area,DT_RIGHT|DT_SINGLELINE);
            auto pen=CreatePen(PS_SOLID,1,RGB(0,0,0));auto oldPen=SelectObject(dc,pen);MoveToEx(dc,range.rc.left,range.rc.bottom+footerHeight/4,nullptr);LineTo(dc,range.rc.right,range.rc.bottom+footerHeight/4);SelectObject(dc,oldPen);DeleteObject(pen);
        }

        }
        auto next = document.Send(SCI_FORMATRANGEFULL, printPage, reinterpret_cast<LPARAM>(&range));
        if (printPage && EndPage(dc) <= 0) { ok = false; break; }
        if (next <= range.chrg.cpMin && range.chrg.cpMin < range.chrg.cpMax) { ok = false; break; }
        range.chrg.cpMin = next;
    } while (range.chrg.cpMin < range.chrg.cpMax);
    document.Send(SCI_FORMATRANGEFULL, FALSE, 0);
    if (ok) ok = EndDoc(dc) > 0; else AbortDoc(dc);
    SelectObject(dc,oldFont);DeleteObject(font);DeleteObject(headerFont);
    document.Send(SCI_SETPRINTMAGNIFICATION,magnification);document.Send(SCI_SETPRINTCOLOURMODE,color);
    DeleteDC(dc);
    if (!ok) throw std::runtime_error("Print job failed.");
    return true;
}
