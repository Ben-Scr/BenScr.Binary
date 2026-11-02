# BenScr.Binary

Created by Ben Schneider — https://github.com/Ben-Scr

Bounded C++20 binary readers and writers for file headers, network payloads and compact records.
Read or write fixed-width integers in either byte order, IEEE 754 floats, canonical unsigned LEB128
and ZigZag signed varints. Borrow slices without copying or allocating storage.

The library is header-only: its small buffer operations can inline into consumers, with no separate
binary, ABI or CRT selection. There are no third-party runtime dependencies.

## Example

```cpp
#include <benscr/binary/binary.hpp>
#include <array>

namespace Binary = BenScr::Binary;

std::array<std::byte, 32> storage{};
Binary::Writer writer(storage);
writer.WriteUInt16(0xb15c, Binary::Endian::Big);
writer.WriteVarUInt64(150);            // 96 01
writer.WriteVarInt64(-500);            // e7 07 (ZigZag 999)
writer.WriteFloat(21.5f, Binary::Endian::Little);

Binary::Reader reader(writer.Written());
const auto magic = reader.ReadUInt16(Binary::Endian::Big);
const auto sequence = reader.ReadVarUInt64();
const auto delta = reader.ReadVarInt64();
const auto temperature = reader.ReadFloat(Binary::Endian::Little);
```

See [`examples/binary.cpp`](examples/binary.cpp) for an executable framed-payload example.

## Install

### Native NuGet / Visual Studio

```powershell
Install-Package BenScr.Binary -Version 1.0.0
```

Use a native C++ `.vcxproj` project with `packages.config`, then select C++20 or later.
The package supplies `build/native/BenScr.Binary.props` and `.targets` to add its include path.
Those imports must be present in the project; the Visual Studio NuGet installer adds them.
`nuget restore` alone downloads an existing package reference and does not edit the project.
See `tools/VerifyNuGetConsumer.ps1` for a complete `.vcxproj` and restoration example.
There is no managed assembly and no C# `PackageReference` integration.

Windows x64 Debug and Release consumers use MSVC v143. The header uses the consumer's runtime
configuration (`/MD`, `/MDd`, `/MT` or `/MTd`); this package ships no compiled runtime libraries.
The verification consumers execute Release and Debug with the Visual Studio default runtime.

### CMake

```sh
cmake -S . -B out/build -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --config Release
ctest --test-dir out/build -C Release --output-on-failure
cmake --install out/build --config Release --prefix /path/to/install
```

In a consuming project, provide that prefix through `CMAKE_PREFIX_PATH`:

```cmake
find_package(BenScrBinary 1 CONFIG REQUIRED)
target_link_libraries(MyApplication PRIVATE BenScr::Binary)
```

Alternatively, use `add_subdirectory` and the same target. Tests and examples default off as a
subproject. `BUILD_TESTING`, `BENSCR_BINARY_BUILD_TESTS` and `BENSCR_BINARY_BUILD_EXAMPLES` control
development targets. CMake 3.28+ and C++20 are required.

## Contracts

| Area | Behavior |
| --- | --- |
| Fixed integers | UInt/Int 8, 16, 32, 64. Multi-byte operations require explicit `Endian`. Signed values use two's complement. No unaligned loads. |
| Floating point | IEEE 754 binary32/binary64, including signed zero, infinities and NaN payloads. Transfers use `bit_cast`; no arithmetic, validation or NaN normalization. |
| Unsigned varint | Canonical ULEB128, 1–10 bytes, entire uint64 range. An overlong zero group is rejected. A tenth byte greater than `01`, including continuation, is overflow. |
| Signed varint | ZigZag followed by canonical ULEB128, including INT64_MIN. This is **not** signed LEB128. |
| Reader | `ReadBytes` borrows a span; `ReadSlice` returns a bounded reader with its own cursor. Both consume the requested number of bytes. |
| Writer | `WriteBytes` supports overlap with `memmove` semantics. Fixed-width and varint writes preflight the complete operation. |
| Cursor | Starts at zero. `Seek` accepts 0 through `Size()` inclusive. `Skip` advances without reading or initializing bytes. Counts are checked without overflowing. |
| Failure | Throws `Binary::Error`; `Code()` identifies the error and `Offset()` gives the operation's starting cursor. Cursor and buffer are unchanged. Exception construction may throw `std::bad_alloc` with the same state guarantee. |
| Lifetime | Reader, Writer, views and slices borrow caller storage. Keep storage alive, valid and suitably synchronized. Copying a cursor shares storage but copies its position independently. |
| Threads | Independent readers may share immutable storage. Mutating a cursor or accessing bytes concurrently with writes requires external synchronization. |

`Written()` means the prefix ending at the current cursor, not a high-water mark. After `Seek` or
`Skip`, that prefix may contain untouched bytes. Returned views observe later writes to their storage.
Empty spans and zero-byte operations are valid. Operations never resize buffers; successful operations
do not allocate. No transaction across multiple calls is implied: earlier successful calls stay committed.

Byte order is validated before capacity. Fixed-width insufficient data reports `OutOfBounds`;
varints report `TruncatedVarint`, `VarintOverflow` or `NonCanonicalVarint`. Errors refer to the
start of the operation, including failures after examining multiple bytes. See [`docs/API.md`](docs/API.md).

IEEE representation requirements are checked at compile time. Bit-preservation assumes ordinary
IEEE floating-point compiler settings and is tested on MSVC, GCC and Clang; do not enable fast-math
or otherwise request transformations that disregard NaNs/signed zero. For protocols requiring raw
bits independently of floating-point settings, use UInt32/UInt64 methods.

The varint and ZigZag layout follows the [Protocol Buffers integer encoding](https://protobuf.dev/programming-guides/encoding/).
This library additionally requires minimal varint encodings. It does not parse Protobuf messages,
provide schema validation, encryption, framing or automatic length-prefix allocation.

## Verify and release

`pwsh -File tools/Verify.ps1` runs the tests and example, checks the public-header baseline,
installs and relocates CMake metadata, builds an isolated consumer, packs and audits the exact
NuGet archive. On Windows it also restores the package into an empty directory and executes native
Debug/Release consumers. Supply `-Generator 'Visual Studio 17 2022' -Toolset v143` on VS 2022.

CI covers Windows 2022 / MSVC v143 and Ubuntu 24.04 / GCC 14 and Clang 18 with ASan+UBSan.
CI uses CMake 3.31.6 and .NET SDK 10.0.303 solely to package the native headers. Local development
also uses VS 2026 with v143. The executable contract suite contains 17 groups and 50,000 fixed-seed
reference/malformed inputs. Evidence is emitted under ignored `artifacts/verify-*/`; see
[`docs/VERIFICATION.md`](docs/VERIFICATION.md) for release and post-publication commands.

## License and provenance

Copyright Ben Schneider. The original custom terms are in [`LICENSE.txt`](LICENSE.txt).
The package embeds the unchanged shared `BenScr_Icon_Master.png` as `icon.png`.
Implementation and test provenance are recorded in [`docs/PROVENANCE.md`](docs/PROVENANCE.md).
