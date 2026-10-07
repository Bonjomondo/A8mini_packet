param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [string]$GStreamerRoot = $env:GSTREAMER_1_0_ROOT_MSVC_X86_64,
    [switch]$Build,
    [string]$QmlglSourceDir,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$ProbeArgs
)
$ErrorActionPreference = "Stop"
$projectDir = Split-Path $PSScriptRoot -Parent
Set-Location $projectDir
if (-not $GStreamerRoot) { throw "Provide -GStreamerRoot (MSVC x86_64 Runtime + Development)" }
$env:PATH = "$QtRoot\bin;$GStreamerRoot\bin;$env:PATH"
$env:PKG_CONFIG_PATH = "$GStreamerRoot\lib\pkgconfig"
$env:QT_PLUGIN_PATH = "$QtRoot\plugins"
$env:QML2_IMPORT_PATH = "$QtRoot\qml"
$env:GST_PLUGIN_PATH_1_0 = "$projectDir\build-win\gst-plugins\Release;$projectDir\build-win\gst-plugins"
$env:GST_PLUGIN_SYSTEM_PATH_1_0 = "$GStreamerRoot\lib\gstreamer-1.0"
$env:GST_REGISTRY = "$projectDir\build-win\gst-registry-x64.bin"
if ($Build) {
    $configureArgs = @("-S", ".", "-B", "build-win", "-A", "x64", "-DCMAKE_PREFIX_PATH=$QtRoot")
    if ($QmlglSourceDir) { $configureArgs += "-DA8_QMLGL_SOURCE_DIR=$QmlglSourceDir" }
    & cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
    & cmake --build build-win --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}
$exe = "$projectDir\build-win\tools\a8mini_probe\Release\a8mini_probe.exe"
if (-not (Test-Path $exe)) { throw "Build the Probe first with -Build" }
& $exe @ProbeArgs
exit $LASTEXITCODE
