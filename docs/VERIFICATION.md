# Verification and release procedure

`tools/Verify.ps1` requires a clean committed checkout (or explicit development `-AllowDirty`).
It checks the normalized public-header SHA-256, builds warnings-as-errors, runs CTest and the example,
installs and moves the CMake prefix to a fresh temporary directory, compiles and executes a consumer
against that relocated installation, packs the NuGet archive and audits every allowed payload.
On Windows it also restores a native packages.config consumer from a single local source into an
empty package directory and executes Debug|x64 and Release|x64 builds.

The contract executable has 17 named groups: fixed-width independent vectors/boundaries, signed
limits, IEEE raw bits, canonical and invalid varints, every short fixed read/write capacity,
atomicity, invalid byte-order enums, cursor/slice bounds, overlapping writes and empty buffers.
20,000 fixed-seed value inputs compare quotient/remainder reference encodings and 30,000 random
byte strings compare acceptance, error classification, consumed count and rollback.
The second CTest executable compiles the public header first in two translation units.
The complete consumer example covers framing, both byte orders, floats, varints and bounded slicing.

CI matrix:

| Runner | Compiler | Extra execution |
| --- | --- | --- |
| windows-2022 | MSVC v143 / VS 2022 | Native NuGet Debug and Release |
| ubuntu-24.04 | GCC 14 | ASan and UBSan tests and relocated consumer |
| ubuntu-24.04 | Clang 18 | ASan and UBSan tests and relocated consumer |

Workflows pin action revisions, .NET SDK 10.0.303 and CMake 3.31.6. Local Windows verification uses
VS 2026 with v143 and stable CMake 4.3.1-msvc1. Supported runtime behavior is limited to the tested
x64 platforms; no ARM, 32-bit or big-endian host execution is claimed. Both wire byte orders are
covered on every runner. Sanitizers are configured for Linux and not claimed by the local MSVC run.

Each verification run produces JUnit results, a `.nupkg`, exact payload audit and release manifest
under a unique ignored `artifacts/verify-<id>/`. `artifacts/latest-verification.txt` identifies the
latest successful run. Development verification does not constitute a release.

Before publication, require the final commit's complete CI matrix and the exact package's manifest
and payload audit. Resolve `packages/BenScr.Binary/1.0.0` to that commit. The `.nuspec` must match
the package identity, version, source commit and required assets. Retain that exact archive for
publication and release attachment; do not silently replace it with an unverified rebuild.

After the public version is available, execute:

```powershell
pwsh -File tools/VerifyNuGetConsumer.ps1 -Published -ExpectedPackage '<absolute verified nupkg path>'
```

The native consumer restores only from nuget.org with a new package directory and no HTTP cache,
executes both configurations, then compares public source/build/docs/branding payloads against the
verified archive. NuGet repository signing may change the outer ZIP; payload comparison accounts
for this. A separate package page check confirms the embedded icon is displayed correctly.
This procedure distinguishes configured checks from executed evidence; package construction alone
does not imply publication or a successful public consumer run.
