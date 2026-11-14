#pragma once
#include "EditorDocument.h"
#include "LanguageConfiguration.h"
const LanguageDefinition* DetectObjCAndMatlab(const EditorDocument&,int& dialect);
const LanguageDefinition* AutoDetectContent(const EditorDocument&,bool dotFile);
int DetectDocumentMarkupDialect(const EditorDocument&);

int DetectDocumentCSVDelimiter(const EditorDocument&);
