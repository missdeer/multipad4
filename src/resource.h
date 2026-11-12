#pragma once
#include "../3rdparty/notepad4/src/resource.h"
#define IDR_MAINFRAME IDR_MAINWND
// Application-only MDI commands live outside the upstream command ranges.
#define ID_NEW IDM_FILE_NEW
#define ID_OPEN IDM_FILE_OPEN
#define ID_SAVE IDM_FILE_SAVE
#define ID_SAVEAS IDM_FILE_SAVEAS
#define ID_EXIT IDM_FILE_EXIT
#define ID_UNDO IDM_EDIT_UNDO
#define ID_REDO IDM_EDIT_REDO
#define ID_CUT IDM_EDIT_CUT
#define ID_COPY IDM_EDIT_COPY
#define ID_PASTE IDM_EDIT_PASTE
#define ID_SELECTALL IDM_EDIT_SELECTALL
#define ID_FIND IDM_EDIT_FIND
#define ID_FINDNEXT IDM_EDIT_FINDNEXT
#define ID_WRAP IDM_VIEW_WORDWRAP
#define ID_ABOUT IDM_HELP_ABOUT
#define ID_SAVEALL 45000
#define ID_CLOSE 45001
#define ID_CLOSEALL 45002
#define ID_DOCUMENTS 45003
#define ID_DOCKLEFT 45004
#define ID_DOCKRIGHT 45005
#define ID_DOCKBOTTOM 45006
#define ID_FLOAT 45007
#define ID_NEXTTAB 45008
#define ID_PREVTAB 45009
#define IDR_MDIACCEL 45010
#define ID_MATEPATH 45011
#define ID_MATEPATH_FOLLOW 45012

#include "SearchResources.h"
