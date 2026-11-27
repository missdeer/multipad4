        if (sessioncapturetest::baseline) {
        for (LRESULT line = 0; line < d->Send(SCI_GETLINECOUNT); ++line)
            if ((d->Send(SCI_GETFOLDLEVEL, line) & SC_FOLDLEVELHEADERFLAG) && !d->Send(SCI_GETFOLDEXPANDED, line)) collapsed += std::to_wstring(line) + L" ";
        } else
