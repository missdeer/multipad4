void TestAppUpdate(const fs::path& scratch) {
    const auto require = [](bool ok) { if (!ok) throw std::runtime_error("App update check failed."); };
    std::promise<void> resume;
    std::promise<void> finished;
    auto completion = finished.get_future();
    {
        auto request = StartBackgroundRequest([gate = resume.get_future(), finished = std::move(finished)]() mutable {
            gate.get();
            finished.set_value();
        });
        require(request.wait_for(NoWait) == std::future_status::timeout);
    }
    resume.set_value();
    completion.get();

    const auto exe = ExecutablePath();
    constexpr auto fixture = R"({"draft":false,"prerelease":false,"tag_name":"v0.10","assets":[
        {"name":"multipad4-msvc-x64-AVX2.zip","state":"uploaded"},
        {"name":"multipad4-msvc-x64-SSE4.2.zip","state":"uploaded",
         "browser_download_url":"https://github.com/missdeer/multipad4/releases/download/v0.10/multipad4-msvc-x64-SSE4.2.zip",
         "digest":"sha256:1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"}]})";
    constexpr auto asset = "multipad4-msvc-x64-SSE4.2.zip";
    require(SelectRelease(fixture, {0, 9, 0}, asset)->tag == L"v0.10");
    require(!SelectRelease(fixture, {0, 10, 0}, asset));
    require(!SelectRelease(fixture, {1, 0, 0}, asset));
    fs::create_directories(scratch);
    auto update = std::make_unique<StagedAppUpdate>();
    update->directory = packages::NewDirectory(scratch, StagePrefix);
    const auto root = update->directory / L"install";
    const auto stage = update->directory / L"staging";
    const auto contents = stage / L"contents";
    fs::create_directories(root);
    fs::create_directories(contents / L"config");
    fs::create_directories(contents / L"plugins" / L"Base64");
    fs::create_directories(root / L"plugins" / L"disabled" / L"Base64");
    fs::copy_file(exe, root / AppFilename);
    fs::copy_file(exe, contents / AppFilename);
    fs::copy_file(exe.parent_path() / RegistryHelperFilename, contents / RegistryHelperFilename);
    std::ofstream(root / L"user.ini") << "settings";
    std::ofstream(contents / L"config" / L"default.xml") << "defaults";
    std::ofstream(contents / L"plugins" / L"Base64" / L"Base64.dll") << "bundled plugin";
    bool keepStaging = false;
    ApplyUpdate(root, stage, keepStaging);
    require(fs::exists(root / L"config" / L"default.xml") && fs::exists(root / L"user.ini"));
    require(fs::exists(root / RegistryHelperFilename));
    require(fs::exists(root / L"plugins" / L"disabled" / L"Base64" / L"Base64.dll"));
    require(!fs::exists(root / L"plugins" / L"Base64") && !keepStaging);

    const auto original = root / L"user.ini";
    const auto replacement = stage / L"replacement";
    const auto backup = stage / L"user-backup";
    std::ofstream(replacement) << "replacement";
    std::vector<packages::FileMove> moves{{original, replacement, backup}, {root / L"missing", stage / L"missing", {}}};
    bool failed = false;
    try { packages::CommitMoves(moves, stage, keepStaging); }
    catch (const std::exception&) { failed = true; }
    std::string settings;
    std::ifstream(original) >> settings;
    require(failed && settings == "settings" && fs::exists(replacement) && !fs::exists(backup) && !keepStaging);

}
