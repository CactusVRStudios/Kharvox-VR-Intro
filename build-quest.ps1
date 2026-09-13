param(
    [string]$AndroidRoot,
    [string]$Sdk,
    [string]$Ndk,
    [string]$JavaHome
)
$ErrorActionPreference = 'Stop'
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE)" }
}
Push-Location $PSScriptRoot
try {
    if (-not $AndroidRoot -and (-not $Sdk -or -not $Ndk -or -not $JavaHome)) {
        $AndroidRoot = Get-ChildItem 'C:/Program Files/Unity/Hub/Editor/*/Editor/Data/PlaybackEngines/AndroidPlayer' -Directory |
            Where-Object { Test-Path "$($_.FullName)/NDK/toolchains/llvm" } |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $Sdk) { $Sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { "$AndroidRoot/SDK" } }
    if (-not $Ndk) { $Ndk = if ($env:ANDROID_NDK_HOME) { $env:ANDROID_NDK_HOME } else { "$AndroidRoot/NDK" } }
    if (-not $JavaHome) { $JavaHome = if ($env:JAVA_HOME) { $env:JAVA_HOME } else { "$AndroidRoot/OpenJDK" } }
    foreach ($required in @("$Sdk/platforms/android-34/android.jar", "$Ndk/build/cmake/android.toolchain.cmake", "$JavaHome/bin/java.exe")) {
        if (-not (Test-Path $required)) { throw "Missing Android tool: $required. Pass -Sdk, -Ndk and -JavaHome." }
    }
    $buildTools = Get-ChildItem "$Sdk/build-tools" -Directory | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
    $ninja = Get-ChildItem "$Sdk/cmake/*/bin/ninja.exe" | Select-Object -Last 1 -ExpandProperty FullName
    if (-not $ninja) { $ninja = (Get-Command ninja -ErrorAction Stop).Source }
    Invoke-Checked cmake @('-S', '.', '-B', 'build', '-A', 'x64')
    Invoke-Checked cmake @('--build', 'build', '--config', 'Release', '--target', 'QuestAssetBaker', '--parallel')
    Invoke-Checked "$PSScriptRoot/build/Release/QuestAssetBaker.exe" @('build/quest-assets')
    $revision = 'b76b80adaf65ac3ad6cc1ce61974fb29a5d02352'
    $archive = "$PSScriptRoot/build/openxr-sdk.zip"
    $source = "$PSScriptRoot/build/openxr-source/OpenXR-SDK-$revision"
    if (-not (Test-Path $archive)) {
        Invoke-WebRequest "https://codeload.github.com/KhronosGroup/OpenXR-SDK/zip/$revision" -OutFile $archive
    }
    if ((Get-FileHash $archive).Hash -ne 'A7DB9DC8C941B3520723C4D224A0070467BAB085CD78C072A2C31AA10EEEB922') { throw 'OpenXR source hash mismatch' }
    if (-not (Test-Path "$source/CMakeLists.txt")) {
        [IO.Compression.ZipFile]::ExtractToDirectory($archive, "$PSScriptRoot/build/openxr-source")
    }
    Invoke-Checked cmake @('-S', 'quest', '-B', 'build/quest', '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninja",
        "-DCMAKE_TOOLCHAIN_FILE=$Ndk/build/cmake/android.toolchain.cmake", '-DANDROID_ABI=arm64-v8a',
        '-DANDROID_PLATFORM=android-29', '-DANDROID_STL=c++_static', '-DCMAKE_BUILD_TYPE=MinSizeRel',
        "-DOPENXR_SOURCE_DIR=$source", '-Wno-dev')
    Invoke-Checked cmake @('--build', 'build/quest', '--parallel')
    New-Item -ItemType Directory -Force build/quest-package, Release/Quest, .local | Out-Null
    Copy-Item build/quest/libcracktro.so build/quest-package/libcracktro.so -Force
    Invoke-Checked "$Ndk/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe" @('--strip-unneeded', 'build/quest-package/libcracktro.so')

    Invoke-Checked "$buildTools/aapt2.exe" @('link', '--manifest', 'quest/AndroidManifest.xml',
        '-I', "$Sdk/platforms/android-34/android.jar", '-o', 'build/quest-package/base.apk')
    Copy-Item build/quest-package/base.apk build/quest-package/unsigned.apk -Force
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::Open("$PSScriptRoot/build/quest-package/unsigned.apk", [IO.Compression.ZipArchiveMode]::Update)
    try {
        $files = [ordered]@{
            'lib/arm64-v8a/libcracktro.so' = 'build/quest-package/libcracktro.so'
            'assets/text.bin' = 'build/quest-assets/text.bin'
            'assets/copper.bin' = 'build/quest-assets/copper.bin'
            'assets/ninja.mod' = 'assets/cracktro/vr/ninja.mod'
        }
        foreach ($file in $files.GetEnumerator()) {
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, "$PSScriptRoot/$($file.Value)", $file.Key, [IO.Compression.CompressionLevel]::SmallestSize) | Out-Null
        }
    } finally { $zip.Dispose() }
    Invoke-Checked "$buildTools/zipalign.exe" @('-f', '4', 'build/quest-package/unsigned.apk', 'build/quest-package/aligned.apk')
    $key = "$PSScriptRoot/.local/quest-sideload.keystore"
    if (-not (Test-Path $key)) {
        Invoke-Checked "$JavaHome/bin/keytool.exe" @('-genkeypair', '-keystore', $key, '-storepass', 'android', '-keypass', 'android',
            '-alias', 'cracktro', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000', '-dname', 'CN=KHARVOX Intro Local Sideload', '-noprompt')
    }
    Invoke-Checked "$JavaHome/bin/java.exe" @('-jar', "$buildTools/lib/apksigner.jar", 'sign', '--ks', $key,
        '--ks-key-alias', 'cracktro', '--ks-pass', 'pass:android', '--key-pass', 'pass:android',
        '--v1-signing-enabled', 'false', '--v2-signing-enabled', 'true', '--v3-signing-enabled', 'false', '--v4-signing-enabled', 'false',
        '--out', 'Release/Quest/KHARVOX Intro.apk', 'build/quest-package/aligned.apk')
    Invoke-Checked "$JavaHome/bin/java.exe" @('-jar', "$buildTools/lib/apksigner.jar", 'verify', '--verbose', 'Release/Quest/KHARVOX Intro.apk')
    Invoke-Checked "$buildTools/zipalign.exe" @('-c', '4', 'Release/Quest/KHARVOX Intro.apk')
    $elf = & "$Ndk/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe" -h -d --dyn-syms build/quest-package/libcracktro.so
    if ($LASTEXITCODE -or -not ($elf -match 'AArch64') -or -not ($elf -match 'DEFAULT\s+\d+ ANativeActivity_onCreate')) { throw 'Invalid native Quest library' }
    if ($elf -match 'NEEDED.*(libc\+\+_shared|libopenxr_loader)') { throw 'Unexpected shared runtime dependency' }
    $archive = [IO.Compression.ZipFile]::OpenRead("$PSScriptRoot/Release/Quest/KHARVOX Intro.apk")
    try {
        $libraries = @($archive.Entries | Where-Object FullName -Like 'lib/*')
        if ($libraries.Count -ne 1 -or $libraries[0].FullName -ne 'lib/arm64-v8a/libcracktro.so') { throw 'APK must contain exactly one arm64 library' }
        if ($archive.Entries | Where-Object FullName -Match '\.dex$|\.ttf$|\.wav$') { throw 'Unexpected bulky runtime asset' }
        $contents = @($archive.Entries | ForEach-Object { [ordered]@{ name=$_.FullName; bytes=$_.Length; compressedBytes=$_.CompressedLength } })
    } finally { $archive.Dispose() }
    $apk = Get-Item "Release/Quest/KHARVOX Intro.apk"
    [ordered]@{ apk=$apk.Name; bytes=$apk.Length; sha256=(Get-FileHash $apk.FullName).Hash;
        abi='arm64-v8a'; minSdk=29; targetSdk=32; openxrRevision=$revision; contents=$contents } |
        ConvertTo-Json -Depth 4 | Set-Content build/quest-package/BUILD-INFO.json
    Write-Host ('Quest APK: {0:N0} bytes ({1:N1} KiB)' -f $apk.Length, ($apk.Length / 1KB))
} finally { Pop-Location }

