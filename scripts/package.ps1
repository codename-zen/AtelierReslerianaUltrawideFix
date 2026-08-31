# Builds the release bundle: everything a player needs, extracted straight into
# the game folder, with no build tools and nothing else to download.
#
# Both this project and Ultimate ASI Loader are MIT, and both licences ship in
# licenses/ inside the archive. The loader's text is fetched from its repository
# rather than summarised or copied by hand.
#
#   pwsh -File scripts/package.ps1
#
# Output: dist/AtelierReslerianaUltrawideFix.zip

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$asi = Join-Path $root 'build/Release/AtelierReslerianaFix.asi'
$ini = Join-Path $root 'AtelierReslerianaFix.ini'
$dist = Join-Path $root 'dist'
$staging = Join-Path $dist 'staging'

if (-not (Test-Path $asi)) {
    throw "$asi is missing. Build it first: cmake --build build --config Release"
}

Remove-Item $staging -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $staging -Force | Out-Null

Copy-Item $asi $staging
Copy-Item $ini $staging

# The loader has to carry a name UnityPlayer.dll actually imports. version.dll,
# winmm.dll and winhttp.dll all qualify; dinput8.dll does not, because this game
# never loads it.
$loaderZip = Join-Path $dist 'ultimate-asi-loader.zip'
$loaderDir = Join-Path $dist 'ultimate-asi-loader'
Invoke-WebRequest -Uri 'https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest/download/Ultimate-ASI-Loader_x64.zip' -OutFile $loaderZip
Remove-Item $loaderDir -Recurse -Force -ErrorAction SilentlyContinue
Expand-Archive -Path $loaderZip -DestinationPath $loaderDir -Force

$loaderDll = Get-ChildItem -Path $loaderDir -Filter '*.dll' | Select-Object -First 1
if (-not $loaderDll) { throw 'No DLL found in the Ultimate ASI Loader archive.' }
Copy-Item $loaderDll.FullName (Join-Path $staging 'version.dll')

# MIT requires the licence and copyright notice to travel with the binary.
$licenceDir = Join-Path $staging 'licenses'
New-Item -ItemType Directory -Path $licenceDir -Force | Out-Null
Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/master/license' `
    -OutFile (Join-Path $licenceDir 'Ultimate-ASI-Loader-LICENSE.txt')

@'
version.dll in this archive is Ultimate ASI Loader by ThirteenAG, redistributed
unmodified apart from its filename, under the MIT licence in
Ultimate-ASI-Loader-LICENSE.txt.

Source: https://github.com/ThirteenAG/Ultimate-ASI-Loader
'@ | Set-Content -Path (Join-Path $licenceDir 'README.txt') -Encoding utf8

Copy-Item (Join-Path $root 'LICENSE') (Join-Path $licenceDir 'AtelierReslerianaUltrawideFix-LICENSE.txt')

$zip = Join-Path $dist 'AtelierReslerianaUltrawideFix.zip'
Remove-Item $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zip

Remove-Item $staging -Recurse -Force
Remove-Item $loaderZip, $loaderDir -Recurse -Force

Write-Host "Packaged $zip"
Get-ChildItem $zip | Format-List Name, Length, LastWriteTime
