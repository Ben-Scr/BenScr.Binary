param(
    [string]$CMake = 'cmake',
    [string]$Generator = '',
    [string]$Toolset = '',
    [switch]$Sanitizers,
    [switch]$AllowDirty
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
function Invoke-Checked([string]$command, [string[]]$arguments) {
    & $command @arguments
    if ($LASTEXITCODE -ne 0) { throw "$command failed with exit code $LASTEXITCODE" }
}
$dirty = git status --porcelain
if ($dirty -and -not $AllowDirty) { throw 'Verification requires a clean checkout.' }
$commit = (git rev-parse HEAD).Trim()
$baseline = Get-Content -LiteralPath (Join-Path $root 'docs/public-header-baseline.json') -Raw | ConvertFrom-Json
$headerText = [IO.File]::ReadAllText((Join-Path $root $baseline.header)).Replace("`r`n", "`n")
$headerHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($headerText))).ToLowerInvariant()
if ($headerHash -ne $baseline.sha256) { throw 'Public header changed without reviewed baseline update.' }
$run = Join-Path $root ('artifacts/verify-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$build = Join-Path $root 'out/verify'
$configuration = 'Release'
$configure = @('-S', $root, '-B', $build)
if ($Generator -notmatch 'Visual Studio') { $configure += '-DCMAKE_BUILD_TYPE=Release' }
if ($Generator) { $configure += @('-G', $Generator) }
if ($Toolset) { $configure += @('-T', $Toolset) }
if ($IsWindows -and $Generator -match 'Visual Studio') { $configure += @('-A', 'x64') }
$configure += '-DBENSCR_BINARY_SANITIZERS=' + $(if ($Sanitizers) { 'ON' } else { 'OFF' })
Invoke-Checked $CMake $configure
Invoke-Checked $CMake @('--build', $build, '--config', $configuration, '--parallel', '2')
$ctest = Join-Path (Split-Path (Get-Command $CMake).Source) $(if ($IsWindows) { 'ctest.exe' } else { 'ctest' })
Invoke-Checked $ctest @('--test-dir', $build, '-C', $configuration, '--output-on-failure', '--output-junit', (Join-Path $run 'tests.xml'))
$exe = if ($IsWindows) { Join-Path $build 'Release/binary_example.exe' } else { Join-Path $build 'binary_example' }
Invoke-Checked $exe @()

# Move the installation before compiling a consumer outside the checkout.
$consumer = Join-Path ([IO.Path]::GetTempPath()) ('benscr-binary-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $consumer | Out-Null
$prefix = Join-Path $run 'install'
Invoke-Checked $CMake @('--install', $build, '--config', $configuration, '--prefix', $prefix)
$relocated = Join-Path $consumer 'relocated'
$sourcePath = [IO.Path]::GetFullPath($prefix)
$destinationPath = [IO.Path]::GetFullPath($relocated)
if (-not $sourcePath.StartsWith([IO.Path]::GetFullPath($run) + [IO.Path]::DirectorySeparatorChar) -or
    -not $destinationPath.StartsWith([IO.Path]::GetFullPath($consumer) + [IO.Path]::DirectorySeparatorChar)) {
    throw 'Installation move must stay within this verification run and its temporary consumer directory.'
}
Move-Item -LiteralPath $sourcePath -Destination $destinationPath
Copy-Item -LiteralPath (Join-Path $root 'examples/binary.cpp') -Destination (Join-Path $consumer 'main.cpp')
@'
cmake_minimum_required(VERSION 3.28)
project(Consumer LANGUAGES CXX)
find_package(BenScrBinary 1 CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE BenScr::Binary)
'@ | Set-Content -LiteralPath (Join-Path $consumer 'CMakeLists.txt') -Encoding utf8
$consumerBuild = Join-Path $consumer 'build'
$consumerConfigure = @('-S', $consumer, '-B', $consumerBuild, "-DCMAKE_PREFIX_PATH=$relocated")
if ($Generator -notmatch 'Visual Studio') { $consumerConfigure += '-DCMAKE_BUILD_TYPE=Release' }
if ($Generator) { $consumerConfigure += @('-G', $Generator) }
if ($Toolset) { $consumerConfigure += @('-T', $Toolset) }
if ($IsWindows -and $Generator -match 'Visual Studio') { $consumerConfigure += @('-A','x64') }
if ($Sanitizers) {
    $consumerConfigure += '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
    $consumerConfigure += '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
}
Invoke-Checked $CMake $consumerConfigure
Invoke-Checked $CMake @('--build', $consumerBuild, '--config', $configuration, '--parallel', '2')
$consumerExe = if ($IsWindows) { Join-Path $consumerBuild 'Release/consumer.exe' } else { Join-Path $consumerBuild 'consumer' }
Invoke-Checked $consumerExe @()

Invoke-Checked 'dotnet' @('restore', 'package/BenScr.Binary.csproj', '--locked-mode')
Invoke-Checked 'dotnet' @('pack', 'package/BenScr.Binary.csproj', '-c', 'Release', '--no-build', '-o', $run, "-p:NuspecProperties=commit=$commit")
$package = Join-Path $run 'BenScr.Binary.1.0.0.nupkg'
Invoke-Checked 'pwsh' @('-NoProfile', '-File', 'tools/AuditPackage.ps1', '-Package', $package, '-Commit', $commit)
if ($IsWindows) {
    Invoke-Checked 'pwsh' @('-NoProfile', '-File', 'tools/VerifyNuGetConsumer.ps1', '-Source', $run, '-ExpectedPackage', $package)
    Copy-Item -LiteralPath (Join-Path $root 'artifacts/nuget-consumer-local.json') -Destination $run
}
$manifest = [ordered]@{
    schema_version=1; package_id='BenScr.Binary'; version='1.0.0'; commit=$commit
    repository='https://github.com/Ben-Scr/BenScr.Binary'; release_tag='packages/BenScr.Binary/1.0.0'
    package_file='BenScr.Binary.1.0.0.nupkg'; package_sha256=(Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash.ToLowerInvariant()
    status=$(if ($dirty) {'development_verified'} else {'ready_to_publish'}); published=$false
    contract_cases=17; deterministic_fuzz_inputs=50000; ctest_cases=2; cmake_consumer='passed_relocated_install'; native_nuget_consumer=$(if ($IsWindows) {'passed_packages_config_vcxproj'} else {'not_run_windows_only'})
    sanitizers=$(if ($Sanitizers) {'ASan_and_UBSan_passed'} else {'not_enabled'})
    verified_at_utc=[DateTime]::UtcNow.ToString('o')
    cmake_version=((& $CMake --version | Select-Object -First 1) -as [string])
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $run 'release-manifest.json') -Encoding utf8
Set-Content -LiteralPath (Join-Path $root 'artifacts/latest-verification.txt') -Value $run -Encoding utf8
Write-Output "Verified artifacts: $run"
