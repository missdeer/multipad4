namespace {thread_local PrintJobTest* printJobTest=nullptr;}
bool PrintDocumentForTest(HWND owner,const EditorDocument& document,AppSettings* settings,PrintJobTest& job) {
    struct Reset {~Reset(){printJobTest=nullptr;}} reset;
    printJobTest=&job;return PrintDocument(owner,document,settings);
}
