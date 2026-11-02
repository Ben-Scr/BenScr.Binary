param([Parameter(Mandatory)][string]$Package, [Parameter(Mandatory)][string]$Commit)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$archive = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Package))
$expected = @{
    'include/benscr/binary/binary.hpp'='include/benscr/binary/binary.hpp'
    'build/native/BenScr.Binary.props'='package/BenScr.Binary.props'
    'build/native/BenScr.Binary.targets'='package/BenScr.Binary.targets'
    'README.md'='README.md'; 'LICENSE.txt'='LICENSE.txt'; 'icon.png'='branding/BenScr_Icon_Master.png'
}
try {
    foreach ($name in $expected.Keys) {
        $entry = $archive.GetEntry($name)
        if (-not $entry) { throw "Missing package entry $name" }
        $stream=$entry.Open(); $buffer=[IO.MemoryStream]::new()
        try { $stream.CopyTo($buffer); $actual=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($buffer.ToArray())) }
        finally { $stream.Dispose(); $buffer.Dispose() }
        $hash=(Get-FileHash -LiteralPath (Join-Path $root $expected[$name]) -Algorithm SHA256).Hash
        if ($actual -ne $hash) { throw "Package payload differs: $name" }
    }
    $reader=[IO.StreamReader]::new($archive.GetEntry('BenScr.Binary.nuspec').Open())
    try { [xml]$xml=$reader.ReadToEnd() } finally { $reader.Dispose() }
    $metadata=$xml.package.metadata
    if ($metadata.id -ne 'BenScr.Binary' -or $metadata.version -ne '1.0.0' -or $metadata.repository.commit -ne $Commit) { throw 'Package metadata mismatch.' }
    if ($metadata.icon -ne 'icon.png' -or $metadata.license.InnerText -ne 'LICENSE.txt' -or $metadata.readme -ne 'README.md') { throw 'Package asset metadata mismatch.' }
    $allowed = @($expected.Keys) + @('BenScr.Binary.nuspec','[Content_Types].xml','_rels/.rels','.signature.p7s')
    foreach ($entry in $archive.Entries) {
        if ($entry.FullName -notin $allowed -and $entry.FullName -notmatch '^package/services/metadata/core-properties/[^/]+\.psmdcp$') { throw "Unexpected package entry: $($entry.FullName)" }
    }
    $iconHash = (Get-FileHash -LiteralPath (Join-Path $root 'branding/BenScr_Icon_Master.png') -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($iconHash -ne 'a5dcabceb766e13c1840a95130ee98a458e12e3713fd9b07f3ceae75a1f5b993') { throw 'Unexpected icon.' }
    $licenseHash = (Get-FileHash -LiteralPath (Join-Path $root 'LICENSE.txt') -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($licenseHash -ne 'f6600e285e17c48188a65745d7595807e1755c72201e8eef201a9174fca18573') { throw 'Original license bytes changed.' }
    if ($metadata.license.type -ne 'file' -or $metadata.repository.url -ne 'https://github.com/Ben-Scr/BenScr.Binary' -or $metadata.authors -ne 'Ben Schneider') { throw 'Package attribution metadata mismatch.' }
} finally { $archive.Dispose() }
Write-Output 'Exact package payloads, identity, source commit, license, README and icon verified.'
