param(
    [ValidateSet("win32", "win64", "both")]
    [string]$Arch = "both"
)

$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) { $cmake = "cmake" }

$targets = switch ($Arch) {
    "win32" { @("Win32") }
    "win64" { @("x64") }
    "both"  { @("Win32", "x64") }
}

foreach ($a in $targets) {
    $dir = "build/$($a.ToLower())"
    & $cmake -S . -B $dir -A $a
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $cmake --build $dir --config Release --target package
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "Packaged to dist/"
