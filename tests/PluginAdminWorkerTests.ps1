param([string]$Repository = (Split-Path -Parent $PSScriptRoot), [switch]$Online)
$ErrorActionPreference='Stop'
$testRoot = Join-Path $Repository ('tmp/plugin-admin-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$worker = Join-Path $Repository 'src/PluginAdminWorker.ps1'
$root = Join-Path $testRoot 'plugins'
New-Item -ItemType Directory -Path (Join-Path $root 'Fixture') -Force | Out-Null
'original' | Set-Content (Join-Path $root 'Fixture/Fixture.dll')
function Run-Plan($operation, $plugins, $expectSuccess=$true) {
    $plan = @{pid=2147483647; exe='unused'; root=$root; session='unused'; operation=$operation; plugins=@($plugins)}
    $planPath = Join-Path $testRoot 'plan.json'
    $plan | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $planPath -Encoding UTF8
    & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $worker -PlanPath $planPath -NoRestart -Quiet
    if (($LASTEXITCODE -eq 0) -ne $expectSuccess) { throw (Get-Content (Join-Path $testRoot 'result.log') -Raw) }
}
$plugin = @{folder='Fixture'; disabled=$false}
Run-Plan 'disable' $plugin
if (!(Test-Path (Join-Path $root 'disabled/Fixture/Fixture.dll')) -or (Test-Path (Join-Path $root 'Fixture'))) {throw 'Disable failed'}
Run-Plan 'enable' @{folder='Fixture'; disabled=$true}
if ((Get-Content (Join-Path $root 'Fixture/Fixture.dll')) -ne 'original') {throw 'Enable lost contents'}
Run-Plan 'remove' @($plugin, @{folder='Missing'; disabled=$false}) $false
if (!(Test-Path (Join-Path $root 'Fixture/Fixture.dll'))) {throw 'Failed batch changed existing plugin'}
Run-Plan 'remove' @{folder='../Fixture'; disabled=$false} $false
Run-Plan 'remove' @($plugin, $plugin) $false
if (!(Test-Path (Join-Path $root 'Fixture/Fixture.dll'))) {throw 'Duplicate operation changed existing plugin'}
Run-Plan 'install' @{folder='New'; disabled=$false; url='https://example.invalid/plugin.zip'; hash='invalid'} $false
Run-Plan 'remove' $plugin
if (Test-Path (Join-Path $root 'Fixture')) {throw 'Remove failed'}
Write-Output "PASS plugin worker enable/disable/remove, batch preflight, duplicate/path/hash rejection. Artifacts: $testRoot"

if ($Online) {
    $catalog = Get-Content (Join-Path $Repository '3rdparty/notepad-plus-plus/PluginsAdmin/pl.x64.json') -Raw | ConvertFrom-Json
    $entry = $catalog.'npp-plugins' | Where-Object {$_.'folder-name' -eq 'NppExport'}
    $package = @{folder='NppExport'; disabled=$false; url=$entry.repository; hash=$entry.id}
    Run-Plan 'install' $package
    $dll = Join-Path $root 'NppExport/NppExport.dll'
    if (!(Test-Path $dll)) {throw 'Online install failed'}
    $before = (Get-FileHash $dll).Hash
    $userFile = Join-Path $root 'NppExport/user-settings.ini'
    'keep-me' | Set-Content $userFile
    $package.hash = '0' * 64
    Run-Plan 'update' $package $false
    if ((Get-FileHash $dll).Hash -ne $before) {throw 'Hash failure damaged installed plugin'}
    $package.hash = $entry.id
    Run-Plan 'update' $package
    if ((Get-FileHash $dll).Hash -ne $before) {throw 'Online update produced wrong plugin'}
    if ((Get-Content $userFile) -ne 'keep-me') {throw 'Update lost user-owned plugin files'}
    Run-Plan 'remove' $package
    Write-Output 'PASS online x64 package download/install/update and SHA-256 mismatch protection.'
}
