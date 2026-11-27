# Applied only to the Scintilla test variant.
string(PREPEND text "#include \"LexerMetrics.h\"\n")
string(REPLACE "instance->Lex(start, len, styleStart, pdoc);"
"lexerMetrics::Scope metrics(start, len);\n\t\t\tinstance->Lex(start, len, styleStart, pdoc);\n\t\t\tmetrics.Fold();" text "${text}")
string(REPLACE "instance->Fold(start, len, styleStart, pdoc);" "instance->Fold(start, len, styleStart, pdoc);\n\t\t\tmetrics.EndFold();" text "${text}")
string(REPLACE "endStyled = position;" "lexerMetrics::StartStyling(position);\n\tendStyled = position;" text "${text}")
string(REPLACE "enteredStyling++;" "lexerMetrics::Submit(length);\n\tenteredStyling++;" text "${text}")
string(REPLACE "cb.GetCharRange(buffer, position, lengthRetrieve);"
"lexerMetrics::Fetch(position, lengthRetrieve);\n\t\tcb.GetCharRange(buffer, position, lengthRetrieve);" text "${text}")
string(REPLACE "return cbView[position];" "lexerMetrics::Fetch(position, 1);\n\t\treturn cbView[position];" text "${text}")
string(REPLACE "return cbView.CharAt(position);" "lexerMetrics::Fetch(position, 1);\n\t\treturn cbView.CharAt(position);" text "${text}")
string(REPLACE "len = endPos_ - startPos_;" "len = endPos_ - startPos_;\n\tlexerMetrics::Fetch(startPos_, len);" text "${text}")
