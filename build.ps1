$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
try {
    $compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
    if ($compiler) { $compilerPath = $compiler.Source }
    elseif (Test-Path -LiteralPath 'C:/msys64/mingw64/bin/g++.exe') { $compilerPath = 'C:/msys64/mingw64/bin/g++.exe' }
    elseif (Test-Path -LiteralPath 'C:/msys64/ucrt64/bin/g++.exe') { $compilerPath = 'C:/msys64/ucrt64/bin/g++.exe' }
    else { throw 'Install the MSYS2 MinGW-w64 C++ toolchain, or use CMake with Visual Studio 2022. The included build/Zeldoh.exe can already be played without a compiler.' }
    New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot 'build') -Force | Out-Null
    & $compilerPath '-std=c++17' '-O2' '-Wall' '-Wextra' '-Wpedantic' '-municode' '-mwindows' '-static' '-static-libgcc' '-static-libstdc++' (Join-Path $PSScriptRoot 'src/main.cpp') '-o' (Join-Path $PSScriptRoot 'build/Zeldoh.exe') '-lgdi32' '-luser32' '-lgdiplus'
    if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed.' }
    Write-Host 'Built build/Zeldoh.exe' -ForegroundColor Green
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
