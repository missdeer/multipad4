param([Parameter(Mandatory=$true)][string]$PlanPath, [switch]$NoRestart, [switch]$Quiet)
$ErrorActionPreference = 'Stop'
$taskDirectory = Split-Path -LiteralPath $PlanPath
$plan = Get-Content -LiteralPath $PlanPath -Raw -Encoding UTF8 | ConvertFrom-Json
$log = Join-Path $taskDirectory 'result.log'
$restart = $false
$succeeded = $false
try {
    $process = Get-Process -Id $plan.pid -ErrorAction SilentlyContinue
    if ($process -and -not $process.WaitForExit(120000)) { throw 'Editor did not exit; plugin operation cancelled.' }
    $restart = $true
    $root = [IO.Path]::GetFullPath($plan.root).TrimEnd('\')
    $staging = Join-Path $root ('.multipad4-stage-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $staging -Force | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $operations = @()
    $names = @{}
    foreach ($plugin in $plan.plugins) {
        $name = [string]$plugin.folder
        if ([string]::IsNullOrWhiteSpace($name) -or $name -match '[\\/:*?"<>|\x00-\x1f]' -or $name -match '[. ]$' -or $name -in @('.', '..', 'Config', 'disabled', 'APIs') -or $name -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)') { throw 'Invalid plugin folder.' }
        if ($names.ContainsKey($name)) { throw 'Duplicate plugin operation.' }; $names[$name] = $true
        $parent = $root
        if ($plugin.disabled) { $parent = Join-Path $root 'disabled' }
        $target = Join-Path $parent $name
        # Never follow directory junctions while moving installed plugin files.
        foreach ($path in @($root, $parent, $target)) {
            if ((Test-Path -LiteralPath $path) -and ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Reparse point rejected: $path" }
        }
        $source = $null
        switch ($plan.operation) {
            { $_ -in 'install','update' } {
                if ($plan.operation -eq 'install' -and (Test-Path -LiteralPath $target)) { throw "Plugin already exists: $name" }
                if ($plan.operation -eq 'update' -and !(Test-Path -LiteralPath $target)) { throw "Plugin not found: $name" }
                if ($plugin.hash -notmatch '^[a-fA-F0-9]{64}$' -or $plugin.url -notmatch '^https://') { throw "Invalid package metadata: $name" }
                $zip = Join-Path $staging ($name + '.zip')
                $client = New-Object Net.WebClient
                try { $client.Headers['User-Agent'] = 'MultiPad4-PluginAdmin'; $client.DownloadFile($plugin.url, $zip) } finally { $client.Dispose() }
                if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $plugin.hash) { throw "SHA-256 mismatch: $name" }
                $source = Join-Path $staging $name
                New-Item -ItemType Directory -Path $source | Out-Null
                # Reject escaping, ADS, rooted and symlink ZIP entries before extraction.
                $archive = [IO.Compression.ZipFile]::OpenRead($zip)
                try {
                    foreach ($entry in $archive.Entries) {
                        $destination = [IO.Path]::GetFullPath((Join-Path $source $entry.FullName))
                        if ($entry.FullName -match '[:\x00]' -or [IO.Path]::IsPathRooted($entry.FullName) -or !$destination.StartsWith($source + '\', [StringComparison]::OrdinalIgnoreCase) -or (($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000) { throw 'Unsafe ZIP entry.' }
                    }
                } finally { $archive.Dispose() }
                [IO.Compression.ZipFile]::ExtractToDirectory($zip, $source)
                $dll = Join-Path $source ($name + '.dll')
                if (!(Test-Path -LiteralPath $dll)) {
                    $nested = Join-Path $source $name
                    if (!(Test-Path -LiteralPath (Join-Path $nested ($name + '.dll')))) { throw "Package does not contain $name.dll at its root." }
                    $source = $nested
                }
                $binary = [IO.File]::ReadAllBytes((Join-Path $source ($name + '.dll')))
                if ($binary.Length -lt 64 -or [BitConverter]::ToUInt16($binary, 0) -ne 0x5A4D) { throw 'Invalid plugin PE file.' }
                $pe = [BitConverter]::ToInt32($binary, 0x3c)
                if ($pe -lt 0 -or $pe + 6 -gt $binary.Length -or [BitConverter]::ToUInt32($binary, $pe) -ne 0x4550 -or [BitConverter]::ToUInt16($binary, $pe + 4) -ne 0x8664) { throw 'Plugin is not an x64 DLL.' }
                if ($plan.operation -eq 'update') {
                    # Preserve user files, then overlay the verified package.
                    if (Get-ChildItem -LiteralPath $target -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | Select-Object -First 1) { throw 'Installed plugin contains a reparse point.' }
                    $merged = Join-Path $staging ($name + '.merged')
                    Copy-Item -LiteralPath $target -Destination $merged -Recurse -Force
                    Get-ChildItem -LiteralPath $source -Force | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $merged -Recurse -Force }
                    $source = $merged
                }
            }
            'disable' { $source = $target; $target = Join-Path (Join-Path $root 'disabled') $name }
            'enable' { $source = $target; $target = Join-Path $root $name }
            'remove' { if (!(Test-Path -LiteralPath $target)) { throw "Plugin not found: $name" } }
            default { throw 'Unknown plugin operation.' }
        }
        if ($plan.operation -in 'disable','enable') {
            if (!(Test-Path -LiteralPath $source) -or (Test-Path -LiteralPath $target)) { throw "Missing source or destination already exists: $name" }
            $destinationParent = Split-Path -Parent $target
            if ((Test-Path -LiteralPath $destinationParent) -and ((Get-Item -LiteralPath $destinationParent).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Destination is a reparse point.' }
        }
        $operations += [PSCustomObject]@{ Target=$target; Source=$source; Backup=(Join-Path $staging ($name + '.backup')); BackedUp=$false; Moved=$false }
    }
    try {
        foreach ($op in $operations) {
            New-Item -ItemType Directory -Path (Split-Path -Parent $op.Target) -Force | Out-Null
            if ($plan.operation -in 'update','remove') { Move-Item -LiteralPath $op.Target -Destination $op.Backup; $op.BackedUp = $true }
            if ($op.Source) { Move-Item -LiteralPath $op.Source -Destination $op.Target; $op.Moved = $true }
        }
    } catch {
        $failure = $_
        [array]::Reverse($operations)
        foreach ($op in $operations) {
            if ($op.Moved) { Move-Item -LiteralPath $op.Target -Destination $op.Source }
            if ($op.BackedUp) { Move-Item -LiteralPath $op.Backup -Destination $op.Target }
        }
        throw $failure
    }
    # Staging has a literal, verified direct-child path under the plugin root.
    if ((Split-Path -Parent $staging) -ne $root) { throw 'Invalid staging cleanup path.' }
    Remove-Item -LiteralPath $staging -Recurse -Force
    $succeeded = $true
    'Plugin operation completed.' | Set-Content -LiteralPath $log
} catch {
    $_ | Out-String | Set-Content -LiteralPath $log
    if (!$Quiet) {
        Add-Type -AssemblyName PresentationFramework
        [System.Windows.MessageBox]::Show("Plugin operation failed: $($_.Exception.Message)`nLog: $log", 'MultiPad4 Plugins Admin') | Out-Null
    }
} finally {
    if ($restart -and !$NoRestart) { Start-Process -FilePath $plan.exe -ArgumentList @('/n', '/session', ('"' + $plan.session + '"')) -WorkingDirectory (Split-Path -Parent $plan.exe) }
}

if (!$succeeded) { exit 1 }
