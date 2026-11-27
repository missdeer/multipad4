    if(printJobTest){
        dialog.hDC=CreateDCW(L"WINSPOOL",L"Microsoft Print to PDF",nullptr,nullptr);
        dialog.Flags=PD_PAGENUMS;dialog.nFromPage=static_cast<WORD>(printJobTest->from);dialog.nToPage=static_cast<WORD>(printJobTest->to);
        dialogAccepted=dialog.hDC!=nullptr;
    }else
