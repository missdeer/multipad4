#pragma once
#include "EditorDocument.h"
struct EDITLEXER;
const EDITLEXER* DetectObjCAndMatlab(const EditorDocument&,int& dialect);
const EDITLEXER* AutoDetectContent(const EditorDocument&,bool dotFile);
int DetectDocumentMarkupDialect(const EditorDocument&);

int DetectDocumentCSVDelimiter(const EditorDocument&);
