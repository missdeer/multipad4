#pragma once
#include "../src/Printing.h"
#include <chrono>
#include <fstream>
inline void RunPrintingSeparatorTests(MainFrame& frame,const std::filesystem::path& directory) {
    auto require=[](bool ok){if(!ok)throw std::runtime_error("Printing separator assertion");};
    auto before=[](HDC dc,int left,int right,int y){auto pen=CreatePen(PS_SOLID,1,RGB(0,0,0));auto old=SelectObject(dc,pen);MoveToEx(dc,left,y,nullptr);LineTo(dc,right,y);SelectObject(dc,old);DeleteObject(pen);};
    HDC dc=CreateCompatibleDC(nullptr);require(dc!=nullptr);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=600;info.bmiHeader.biHeight=-800;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels=nullptr;auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);require(bitmap&&pixels);
    auto oldBitmap=SelectObject(dc,bitmap);auto red=CreatePen(PS_DASH,3,RGB(255,0,0));auto oldPen=SelectObject(dc,red);
    constexpr size_t bytes=600*800*4;
    for(int scale:{1,2}){
        SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,600,800,nullptr);SetViewportExtEx(dc,600/scale,800/scale,nullptr);
        memset(pixels,255,bytes);before(dc,30,570,60);before(dc,30,570,740);GdiFlush();
        std::vector<unsigned char> expected(static_cast<unsigned char*>(pixels),static_cast<unsigned char*>(pixels)+bytes);
        memset(pixels,255,bytes);DrawPrintSeparator(dc,30,570,60);DrawPrintSeparator(dc,30,570,740);GdiFlush();
        require(memcmp(expected.data(),pixels,bytes)==0&&GetCurrentObject(dc,OBJ_PEN)==red);
    }
    SetMapMode(dc,MM_TEXT);std::filesystem::create_directories(directory);
    std::ofstream report(directory/L"printing-separator-pairs.csv");report<<"mode,sample,pages,milliseconds,created_pens,deleted_pens\n";
    const auto objects=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int sample=0;sample<7;++sample)for(bool baseline:{true,false}){
        auto begin=std::chrono::steady_clock::now();
        for(int page=0;page<10000;++page){if(baseline){before(dc,30,570,60);before(dc,30,570,740);}else{DrawPrintSeparator(dc,30,570,60);DrawPrintSeparator(dc,30,570,740);}}
        GdiFlush();require(GetCurrentObject(dc,OBJ_PEN)==red);
        report<<(baseline?"per-rule-pen":"stock-pen")<<','<<sample<<",10000,"<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<','<<(baseline?20000:0)<<','<<(baseline?20000:0)<<'\n';
    }
    require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==objects);
    report.close();require(bool(report));SelectObject(dc,oldPen);DeleteObject(red);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);
    if(GetEnvironmentVariableW(L"MULTIPAD4_PRINT_DRIVER_TEST",nullptr,0)){
        auto document=frame.NewDocument();require(document!=nullptr);document->path=L"print-job.txt";
        std::string text;for(int line=0;line<2000;++line)text+="Line "+std::to_string(line)+" printing body verification\r\n";document->Load({text});
        AppSettings settings;settings.Initialize(true);settings.WriteString(L"Print",L"Header",L"2");settings.WriteString(L"Print",L"Footer",L"0");
        std::ofstream jobs(directory/L"print-driver-pairs.csv");jobs<<"mode,range,sample,milliseconds,pages,pens\n";
        for(int sample=0;sample<3;++sample)for(bool baseline:{true,false})for(bool subset:{false,true}){
            PrintJobTest job;job.baseline=baseline;job.from=subset?2:1;job.to=subset?3:0xffff;
            job.output=std::filesystem::absolute(directory/(std::wstring(baseline?L"before":L"after")+(subset?L"-range-":L"-all-")+std::to_wstring(sample)+L".pdf"));
            std::filesystem::remove(job.output);auto begin=std::chrono::steady_clock::now();require(PrintDocumentForTest(frame,*document,&settings,job));
            jobs<<(baseline?"before":"after")<<','<<(subset?"2-3":"all")<<','<<sample<<','<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<','<<job.pages<<','<<job.pens<<'\n';
            require(job.pages==(subset?2:job.pages)&&job.pages>0&&job.pens==(baseline?job.pages*2:0));require(std::filesystem::file_size(job.output)>0);
        }
        jobs.close();require(bool(jobs));document->Send(SCI_SETSAVEPOINT);
    }

}
