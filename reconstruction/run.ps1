Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$target = Join-Path $PSScriptRoot 'apply-1.2-low-latency.ps1'
$text = Get-Content $target -Raw
if ($text.Contains('fetch --no-tags --depth=1')) {
    $text = $text.Replace('fetch --no-tags --depth=1', 'fetch --no-tags')
    Set-Content -Path $target -Value $text -Encoding utf8
}

& $target
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ''
Write-Host 'Reconstruction completed. Build with the repository CMake workflow or open the generated project in your normal toolchain.'
