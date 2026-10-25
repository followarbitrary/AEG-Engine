#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [switch]$ConfigureOnly,
    [switch]$SkipConfigure,
    [switch]$NoTests,
    [switch]$GpuTests,
    [string]$Target = 'all',
    [ValidateRange(1, 256)][int]$Jobs = [Math]::Min([Environment]::ProcessorCount, 8)
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw '找不到 vswhere.exe。请安装 Visual Studio 2026（含“使用 C++ 的桌面开发”）。'
}
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsRoot) { throw '未找到带 MSVC x64 工具的 Visual Studio。' }
$vsRoot = $vsRoot.Trim()
$developerCommand = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
$cmakeDirectory = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaDirectory = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$cmake = Join-Path $cmakeDirectory 'cmake.exe'
$ctest = Join-Path $cmakeDirectory 'ctest.exe'
foreach ($requiredPath in @($developerCommand, $cmake, $ctest, (Join-Path $ninjaDirectory 'ninja.exe'))) {
    if (-not (Test-Path -LiteralPath $requiredPath)) { throw "缺少构建工具：$requiredPath" }
}

# Capture VS's environment from a child cmd, then apply it only to build children.
# Neither the caller's PATH nor its VULKAN_SDK is changed.
$environmentLines = & $env:ComSpec /d /c "call `"$developerCommand`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'VsDevCmd.bat 初始化失败。' }
$buildEnvironment = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') { $buildEnvironment[$Matches[1]] = $Matches[2] }
}
$buildEnvironment['PATH'] = "$cmakeDirectory;$ninjaDirectory;" + $buildEnvironment['PATH']
$buildEnvironment['VSLANG'] = '1033'
if (-not $buildEnvironment.ContainsKey('VULKAN_SDK') -or -not $buildEnvironment['VULKAN_SDK']) {
    $installedSdk = 'E:\VulkanSDK\1.4.357.0'
    if (Test-Path -LiteralPath (Join-Path $installedSdk 'Include\vulkan\vulkan.h')) {
        $buildEnvironment['VULKAN_SDK'] = $installedSdk
    } else { throw '请安装 Vulkan SDK，并设置 VULKAN_SDK。' }
}
$sdkCompiler = Join-Path $buildEnvironment['VULKAN_SDK'] 'Bin\glslc.exe'
if (-not (Test-Path -LiteralPath $sdkCompiler)) { throw "找不到 GLSL 编译器：$sdkCompiler" }

function Invoke-BuildTool([string]$Executable, [string[]]$ToolArguments) {
    Write-Host ('> ' + $Executable + ' ' + ($ToolArguments -join ' '))
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.WorkingDirectory = $projectRoot
    $start.UseShellExecute = $false
    foreach ($pair in $buildEnvironment.GetEnumerator()) { $start.Environment[$pair.Key] = $pair.Value }
    foreach ($argument in $ToolArguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($start)
    try {
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { throw "$Executable 失败，退出码 $($process.ExitCode)。" }
    } finally { $process.Dispose() }
}

$preset = $Configuration.ToLowerInvariant()
$gpuOption = if ($GpuTests) { 'ON' } else { 'OFF' }
if (-not $SkipConfigure) { Invoke-BuildTool $cmake @('--preset', $preset, "-DAEG_ENABLE_GPU_TESTS=$gpuOption") }
if ($ConfigureOnly) { return }
Invoke-BuildTool $cmake @('--build', '--preset', $preset, '--target', $Target, '--parallel', "$Jobs")
if (-not $NoTests) { Invoke-BuildTool $ctest @('--preset', $preset) }
