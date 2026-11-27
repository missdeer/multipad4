struct PrintJobTest {
    std::filesystem::path output;
    unsigned from=1,to=0xffff,pages=0,pens=0;
    bool baseline=false;
};
bool PrintDocumentForTest(HWND owner,const EditorDocument& document,AppSettings* settings,PrintJobTest& job);
