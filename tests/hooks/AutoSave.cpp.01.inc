    if (auto gate = autosavetest::beforeWrite.load()) gate(request.path, request.overwrite && !request.path.empty());
