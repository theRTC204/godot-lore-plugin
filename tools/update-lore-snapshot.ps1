<#
.SYNOPSIS
    Rebuilds the `lore` crate and re-vendors its C API snapshot into this repo.

.DESCRIPTION
    godot-lore-plugin links a vendored snapshot of the Lore C API (lore-capi)
    rather than building it live as part of this project's build. Run this
    script whenever the Lore checkout's public API changes and the snapshot
    under third_party/lore needs to be refreshed.

    Built dynamically (cdylib), not as a staticlib: the staticlib archive
    cbindgen/cargo also produce is unlinked and can run into the hundreds of
    MB to multiple GB (no dead-code elimination happens until something
    actually links against it), whereas the cdylib is already linked and
    stripped down to roughly 30MB. lore.dll is a genuine runtime dependency of
    the built extension and must ship alongside it.

    Requires a local checkout of https://github.com/EpicGames/lore at
    ..\lore relative to this repo (i.e. a sibling directory), or pass
    -LoreRepoPath explicitly.
#>
param(
    [string]$LoreRepoPath = (Join-Path $PSScriptRoot "..\..\lore" | Resolve-Path -ErrorAction SilentlyContinue)
)

$ErrorActionPreference = "Stop"

if (-not $LoreRepoPath -or -not (Test-Path $LoreRepoPath)) {
    throw "Could not locate the Lore checkout. Pass -LoreRepoPath explicitly."
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$vendorInclude = Join-Path $repoRoot "third_party\lore\include"
$vendorLib = Join-Path $repoRoot "third_party\lore\lib"
$vendorBin = Join-Path $repoRoot "third_party\lore\bin"

New-Item -ItemType Directory -Force -Path $vendorInclude, $vendorLib, $vendorBin | Out-Null

Write-Host "Building lore crate (release-lto, cdylib) from $LoreRepoPath ..."
Push-Location $LoreRepoPath
try {
    cargo build --profile release-lto -p lore --lib
    if ($LASTEXITCODE -ne 0) {
        throw "cargo build failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

$builtDll = Join-Path $LoreRepoPath "target\release-lto\lore.dll"
$builtImportLib = Join-Path $LoreRepoPath "target\release-lto\lore.dll.lib"
$builtHeader = Join-Path $LoreRepoPath "lore-capi\lore.h"

foreach ($path in @($builtDll, $builtImportLib, $builtHeader)) {
    if (-not (Test-Path $path)) {
        throw "Expected build output not found: $path"
    }
}

Copy-Item $builtDll (Join-Path $vendorBin "lore.dll") -Force
Copy-Item $builtImportLib (Join-Path $vendorLib "lore.dll.lib") -Force
Copy-Item $builtHeader (Join-Path $vendorInclude "lore.h") -Force

Write-Host "Vendored snapshot updated:"
Write-Host "  $(Join-Path $vendorBin 'lore.dll')"
Write-Host "  $(Join-Path $vendorLib 'lore.dll.lib')"
Write-Host "  $(Join-Path $vendorInclude 'lore.h')"
Write-Host ""
Write-Host "Remember to commit the updated files under third_party/lore/."
