param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class InstanceWindows {
    public delegate bool Callback(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string name, string title);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr window, Callback callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumWindows(Callback callback, IntPtr parameter);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder name, int count);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
    public static IntPtr MainWindow(int processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, parameter) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            var name = new StringBuilder(256); GetClassName(window, name, name.Capacity);
            if (owner == processId && name.ToString() == "MultiPad4.MainFrame") { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static string Title(IntPtr window) {
        var text = new StringBuilder(4096); GetWindowText(window, text, text.Capacity); return text.ToString();
    }
    public static IntPtr[] Editors(IntPtr window) {
        var editors = new List<IntPtr>();
        EnumChildWindows(window, (child, parameter) => {
            var name = new StringBuilder(256); GetClassName(child, name, name.Capacity);
            if (name.ToString() == "Scintilla") editors.Add(child); return true;
        }, IntPtr.Zero);
        return editors.ToArray();
    }
}
'@
if ([InstanceWindows]::FindWindow('MultiPad4.MainFrame', [System.Management.Automation.Language.NullString]::Value) -ne [IntPtr]::Zero) {
    Write-Output 'SKIP: Close existing MultiPad4 windows to run the process integration test.'
    exit 77
}
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('MultiPad4-instance-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$settings = Join-Path $testRoot 'settings.ini'
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
function Start-Editor([string]$arguments = '', [string]$editorExecutable = $Executable) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $editorExecutable
    $info.Arguments = $arguments
    $info.WorkingDirectory = $testRoot
    $info.UseShellExecute = $false
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $info.EnvironmentVariables['MULTIPAD4_SETTINGS'] = $settings
    $process = [Diagnostics.Process]::Start($info)
    $processes.Add($process)
    return $process
}
function Wait-Until([scriptblock]$condition, [string]$message) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while (!( & $condition )) {
        if ($timer.ElapsedMilliseconds -gt 15000) { throw $message }
        Start-Sleep -Milliseconds 50
    }
}
function Assert-Forwarded([string]$arguments, [string]$filename) {
    $child = Start-Editor $arguments
    if (!$child.WaitForExit(15000)) { throw "Launch created a persistent second process: $arguments" }
    if ($child.ExitCode -ne 0) { throw "Forwarding failed: $arguments" }
    if ($filename) {
        Wait-Until { [InstanceWindows]::Title($script:window).Contains($filename) } "File was not activated: $filename"
    }
}
function Stop-Editors {
    foreach ($process in $processes) {
        if (!$process.HasExited) {
            $process.Refresh()
            $handle = [InstanceWindows]::MainWindow($process.Id)
            if ($handle -ne [IntPtr]::Zero) {
                [void][InstanceWindows]::PostMessage($handle, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
            }
            if (!$process.WaitForExit(5000)) { $process.Kill(); $process.WaitForExit() }
        }
        $process.Dispose()
    }
    $processes.Clear()
}
try {
    "[Options]`r`nCommand40040=1`r`nCommand40423=0" | Set-Content -LiteralPath $settings -Encoding Unicode
    $first = Start-Editor
    Wait-Until { [InstanceWindows]::MainWindow($first.Id) -ne [IntPtr]::Zero } 'First window did not start'
    $script:window = [InstanceWindows]::MainWindow($first.Id)
    Assert-Forwarded '' ''
    $unicodeFile = ([string][char]0x4E2D) + ' space.txt'
    [IO.File]::WriteAllText((Join-Path $testRoot $unicodeFile), "first`nsecond`nthird", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $testRoot 'other.txt'), 'other', [Text.UTF8Encoding]::new($false))
    Assert-Forwarded ('/g 2,3 /ro "' + $unicodeFile + '"') $unicodeFile
    $matching = @([InstanceWindows]::Editors($window) | Where-Object {
        [InstanceWindows]::SendMessage($_, 2006, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 18
    })
    if ($matching.Count -ne 1) { throw 'Forwarded file contents were not loaded exactly once' }
    if ([InstanceWindows]::SendMessage($matching[0], 2008, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -ne 8 -or
        [InstanceWindows]::SendMessage($matching[0], 2140, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -ne 1) {
        throw 'Forwarded line/column or read-only option was lost'
    }
    $count = @([InstanceWindows]::Editors($window)).Count
    Assert-Forwarded ('"' + (Join-Path $testRoot $unicodeFile) + '"') $unicodeFile
    if (@([InstanceWindows]::Editors($window)).Count -ne $count) { throw 'Repeated file created a duplicate document' }
    Assert-Forwarded ('+ "' + $unicodeFile + '" other.txt') 'other.txt'
    if (@([InstanceWindows]::Editors($window)).Count -ne $count + 1) { throw 'Multiple files were not forwarded correctly' }
    $separate = Start-Editor '/n'
    Wait-Until { [InstanceWindows]::MainWindow($separate.Id) -ne [IntPtr]::Zero } '/n no longer creates a new window'
    Stop-Editors

    # With both preferences disabled, normal launches must remain independent.
    "[Options]`r`nCommand40040=0`r`nCommand40423=0" | Set-Content -LiteralPath $settings -Encoding Unicode
    $one = Start-Editor
    Wait-Until { [InstanceWindows]::MainWindow($one.Id) -ne [IntPtr]::Zero } 'Disabled preference: first launch failed'
    $two = Start-Editor
    Wait-Until { [InstanceWindows]::MainWindow($two.Id) -ne [IntPtr]::Zero } 'Disabled preference still reuses the window'
    Stop-Editors

    # Simultaneous launches must elect one process, even before its HWND exists.
    "[Options]`r`nCommand40040=1`r`nCommand40423=0" | Set-Content -LiteralPath $settings -Encoding Unicode
    $one = Start-Editor ('"' + $unicodeFile + '"')
    $two = Start-Editor 'other.txt'
    Wait-Until { $one.HasExited -or $two.HasExited } 'Concurrent startup left two processes running'
    $survivor = if ($one.HasExited) { $two } else { $one }
    Wait-Until { $handle = [InstanceWindows]::MainWindow($survivor.Id); $handle -ne [IntPtr]::Zero -and @([InstanceWindows]::Editors($handle)).Count -eq 2 } 'Concurrent startup lost an open request'
    Write-Output 'PASS: empty launch, Unicode/relative/absolute paths, CLI options, duplicate/multiple files, /n, disabled setting, concurrent startup.'
    Stop-Editors

    # The marker in the working directory must not affect the ordinary copy.
    New-Item -ItemType File -Path (Join-Path $testRoot '.portable') | Out-Null
    "[Options]`r`nCommand40040=1`r`nCommand45164=1" | Set-Content -LiteralPath $settings -Encoding Unicode
    $ordinary = Start-Editor 'other.txt'
    Wait-Until { [InstanceWindows]::Title([InstanceWindows]::MainWindow($ordinary.Id)).Contains('other.txt') } 'Ordinary startup failed'
    $script:window = [InstanceWindows]::MainWindow($ordinary.Id)
    $portable = Join-Path $testRoot 'Portable copy'
    $data = Join-Path $portable 'data'
    New-Item -ItemType Directory -Path $data | Out-Null
    $portableExe = Join-Path $portable 'MultiPad4.exe'
    Copy-Item -LiteralPath $Executable -Destination $portableExe
    New-Item -ItemType File -Path (Join-Path $portable '.portable') | Out-Null
    $portableSettings = Join-Path $data 'MultiPad4.ini'
    "[Options]`r`nCommand45164=1`r`nCommand40493=1`r`n[AutoSave]`r`nPeriodMilliseconds=50" |
        Set-Content -LiteralPath $portableSettings -Encoding Unicode

    $process = Start-Editor -editorExecutable $portableExe
    Wait-Until { @([InstanceWindows]::Editors([InstanceWindows]::MainWindow($process.Id))).Count -eq 1 } 'Portable copy did not open its own draft'
    $portableWindow = [InstanceWindows]::MainWindow($process.Id)
    Assert-Forwarded ('"' + $unicodeFile + '"') $unicodeFile
    $editor = [InstanceWindows]::Editors($portableWindow)[0]
    $draft = 'portable draft'
    foreach ($character in $draft.ToCharArray()) {
        [void][InstanceWindows]::SendMessage($editor, 0x102, [IntPtr][int]$character, [IntPtr]::Zero)
    }
    $backupDirectory = Join-Path $data 'AutoSave'
    Wait-Until { (Test-Path -LiteralPath $backupDirectory) -and
        @(Get-ChildItem -LiteralPath $backupDirectory -Filter 'Untitled*.bak').Count -eq 1 } 'Portable draft was not backed up'
    $reuse = Start-Editor -editorExecutable $portableExe
    if (!$reuse.WaitForExit(15000) -or $reuse.ExitCode -ne 0) { throw 'Portable copy did not reuse its own window' }
    [void][InstanceWindows]::PostMessage($portableWindow, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
    if (!$process.WaitForExit(15000) -or $process.ExitCode -ne 0) { throw 'Portable copy did not close cleanly' }
    if (!(Test-Path -LiteralPath ($portableSettings + '.session'))) { throw 'Portable session was not saved alongside its settings' }

    $moved = Join-Path $testRoot 'Moved copy'
    Move-Item -LiteralPath $portable -Destination $moved
    $process = Start-Editor -editorExecutable (Join-Path $moved 'MultiPad4.exe')
    Wait-Until { [InstanceWindows]::Title([InstanceWindows]::MainWindow($process.Id)).StartsWith('* ') } 'Moved draft was not marked as unsaved'
    $editor = [InstanceWindows]::Editors([InstanceWindows]::MainWindow($process.Id))[0]
    $length = [InstanceWindows]::SendMessage($editor, 2006, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($length -ne $draft.Length) { throw 'Moved draft lost its text' }
    for ($index = 0; $index -lt $length; ++$index) {
        $character = [InstanceWindows]::SendMessage($editor, 2007, [IntPtr]$index, [IntPtr]::Zero).ToInt32()
        if ($character -ne [int]$draft[$index]) { throw 'Moved draft contents changed' }
    }
    $backup = Get-ChildItem -LiteralPath (Join-Path $moved 'data/AutoSave') -Filter 'Untitled*.bak' | Select-Object -First 1
    if (!$backup -or [IO.File]::ReadAllText($backup.FullName) -ne $draft) { throw 'Moved backup contents changed' }
    [void][InstanceWindows]::PostMessage($script:window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
    if (!$ordinary.WaitForExit(15000) -or $ordinary.ExitCode -ne 0) { throw 'Ordinary copy did not close cleanly' }
    $ordinary = Start-Editor
    Wait-Until { [InstanceWindows]::Title([InstanceWindows]::MainWindow($ordinary.Id)).Contains($unicodeFile) } 'Ordinary copy did not restore its own session'
    Write-Output 'PASS: marker location, ordinary and portable window isolation, portable reuse, timed backup and both session restores.'
} finally {
    Stop-Editors
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
