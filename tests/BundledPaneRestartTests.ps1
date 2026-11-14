param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class PaneRestartWindows {
    public delegate bool Callback(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumWindows(Callback callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr window, Callback callback, IntPtr parameter);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder name, int count);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr window, int index);
    [DllImport("user32.dll")] public static extern uint GetDpiForSystem();
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
    public static IntPtr Find(int pid, string type) {
        IntPtr found = IntPtr.Zero;
        Callback check = (window, parameter) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            var name = new StringBuilder(256); GetClassName(window, name, name.Capacity);
            if (owner == pid && name.ToString() == type) found = window;
            return true;
        };
        EnumWindows((window, parameter) => { check(window, parameter); EnumChildWindows(window, check, parameter); return true; }, IntPtr.Zero);
        return found;
    }
}
'@
$source = Split-Path (Resolve-Path $Executable) -Parent
$root = Join-Path ([IO.Path]::GetTempPath()) ('MultiPad4-pane-restart-' + [Guid]::NewGuid().ToString('N'))
$process = $null
New-Item -ItemType Directory -Path (Join-Path $root 'plugins/Config') -Force | Out-Null
try {
    Copy-Item -LiteralPath $Executable -Destination (Join-Path $root 'multipad4.exe')
    Get-ChildItem -LiteralPath $source -Filter '*.dll' | Copy-Item -Destination $root
    foreach ($plugin in @('Matepath', 'OpenDocuments')) {
        Copy-Item -LiteralPath (Join-Path $source "plugins/$plugin") -Destination (Join-Path $root 'plugins') -Recurse
    }
    foreach ($visible in @(1, 0)) { foreach ($docked in @(0, 1)) {
        foreach ($plugin in @('Matepath', 'OpenDocuments')) {
            $side = if ($plugin -eq 'Matepath') { 1 } else { 3 }
            $dpi = [PaneRestartWindows]::GetDpiForSystem()
            $ini = "[$plugin]`r`nDockLayout=1 $visible $docked $dpi 180 160 520 620 $side 0 0 237 319 0`r`n"
            [IO.File]::WriteAllText((Join-Path $root "plugins/Config/$plugin.ini"), $ini, [Text.Encoding]::Unicode)
        }
        # Two independent processes exercise both initial restore and the state
        # written at shutdown, using only isolated copies of the bundled DLLs.
        foreach ($launch in @(1, 2)) {
            $info = [Diagnostics.ProcessStartInfo]::new()
            $info.FileName = Join-Path $root 'multipad4.exe'
            $info.Arguments = '-n'
            $info.WorkingDirectory = $root
            $info.UseShellExecute = $false
            $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
            $info.EnvironmentVariables['MULTIPAD4_SETTINGS'] = Join-Path $root 'settings.ini'
            $process = [Diagnostics.Process]::Start($info)
            $timer = [Diagnostics.Stopwatch]::StartNew()
            do {
                $main = [PaneRestartWindows]::Find($process.Id, 'MultiPad4.MainFrame')
                $matepath = [PaneRestartWindows]::Find($process.Id, 'MultiPad4.MatepathPane')
                $documents = [PaneRestartWindows]::Find($process.Id, 'MultiPad4.OpenDocumentsPane')
                if ($process.HasExited -or $timer.ElapsedMilliseconds -gt 15000) { throw 'Bundled pane startup failed' }
                Start-Sleep -Milliseconds 50
            } until ($main -ne [IntPtr]::Zero -and $matepath -ne [IntPtr]::Zero -and $documents -ne [IntPtr]::Zero)
            foreach ($pane in @($matepath, $documents)) {
                $style = [PaneRestartWindows]::GetWindowLong($pane, -16)
                if ((($style -band 0x10000000) -ne 0) -ne ($visible -eq 1)) { throw 'Pane visibility was not restored' }
                if ((($style -band 0x40000000) -ne 0) -ne ($visible -eq 1 -and $docked -eq 1)) { throw 'Pane docking was not restored' }
            }
            [PaneRestartWindows]::PostMessage($main, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
            # WTL's chained frame destroy handler can post WM_QUIT with 1.
            if (!$process.WaitForExit(15000) -or $process.ExitCode -notin @(0, 1)) { throw "Bundled pane shutdown failed: exit=$($process.ExitCode) visible=$visible docked=$docked launch=$launch" }
            $process.Dispose(); $process = $null
            foreach ($plugin in @('Matepath', 'OpenDocuments')) {
                $saved = Get-Content -LiteralPath (Join-Path $root "plugins/Config/$plugin.ini") | Where-Object { $_ -like 'DockLayout=*' }
                $parts = $saved.Substring(11).Split(' ')
                if ($parts[1] -ne "$visible" -or $parts[2] -ne "$docked") { throw "Lost $plugin visibility/docking on shutdown" }
                if ($docked -eq 1 -and $parts[8] -ne $(if ($plugin -eq 'Matepath') { '1' } else { '3' })) { throw "Lost $plugin docking side" }
                if (($parts[4..7] -join ' ') -ne '180 160 520 620') { throw "Lost $plugin floating rectangle" }
            }
        }
    } }
    Write-Output 'PASS: both bundled plugins retained visible/hidden, docked/floating, side and rectangle across process restarts.'
} finally {
    if ($null -ne $process) { if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }; $process.Dispose() }
    # The target is a directly-created, uniquely named directory under TEMP.
    Remove-Item -LiteralPath $root -Recurse -Force
}
