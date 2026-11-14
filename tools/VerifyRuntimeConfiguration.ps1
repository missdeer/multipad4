param(
    [Parameter(Mandatory = $true)]
    [string]$Directory
)

$ErrorActionPreference = 'Stop'
$sourceDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../config'))
$packageDirectory = [IO.Path]::GetFullPath((Join-Path $Directory 'config'))

foreach ($required in @('languages/default.xml', 'themes/light.xml', 'themes/dark.xml', 'NOTEPAD4-LICENSE.txt')) {
    $source = Join-Path $sourceDirectory $required
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required configuration source is missing: $source"
    }
}

$files = @(Get-ChildItem -LiteralPath $sourceDirectory -File -Recurse)
foreach ($file in $files) {
    $relative = $file.FullName.Substring($sourceDirectory.Length).TrimStart('\', '/')
    $packaged = Join-Path $packageDirectory $relative
    if (!(Test-Path -LiteralPath $packaged -PathType Leaf)) {
        throw "Packaged configuration is missing: $packaged"
    }
    if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $packaged -Algorithm SHA256).Hash) {
        throw "Packaged configuration differs from its source: $packaged"
    }
}
Write-Output "Verified $($files.Count) runtime configuration files in $packageDirectory"
