# Public API 1.0.0

All declarations are in `BenScr::Binary` and `<benscr/binary/binary.hpp>`.
`docs/public-header-baseline.json` pins the complete normalized public header, including inline
implementation. Any header modification requires explicit review and baseline refresh.

## Types

- `enum class Endian { Little, Big }`.
- `enum class ErrorCode { OutOfBounds, InvalidEndian, TruncatedVarint, VarintOverflow, NonCanonicalVarint }`.
- `Error : std::runtime_error`: constructor `(ErrorCode, std::size_t offset, const char* message)`;
  `Code() -> ErrorCode`, `Offset() -> std::size_t`, both const/noexcept. The offset is relative to
  the current reader/writer's span and records its cursor before the failed operation.
- `Reader(std::span<const std::byte>)`, explicit/noexcept, initial cursor zero.
- `Writer(std::span<std::byte>)`, explicit/noexcept, initial cursor zero.

## Common cursor operations

`Size()`, `Position()`, `Remaining()` return `std::size_t` and are const/noexcept.
`Seek(std::size_t offset)` and `Skip(std::size_t count)` check their bounds before mutation.
Out-of-bounds operations throw `Error(OutOfBounds, startingPosition, diagnostic)`.
Copy/move are ordinary span-and-cursor value operations; neither takes ownership or invalidates
the other cursor. Moved-from objects remain usable as copied cursors.

## Reader

| Method | Return |
| --- | --- |
| `Unread() const noexcept` | `std::span<const std::byte>`, unconsumed suffix |
| `ReadBytes(std::size_t count)` | `std::span<const std::byte>`, consumes count |
| `ReadSlice(std::size_t count)` | `Reader`, consumes count in parent; child cursor zero |
| `ReadUInt8()`, `ReadInt8()` | `std::uint8_t`, `std::int8_t` |
| `ReadUInt16(Endian)`, `ReadUInt32(Endian)`, `ReadUInt64(Endian)` | Matching unsigned fixed-width type |
| `ReadInt16(Endian)`, `ReadInt32(Endian)`, `ReadInt64(Endian)` | Matching signed fixed-width type |
| `ReadFloat(Endian)`, `ReadDouble(Endian)` | `float`, `double` |
| `ReadVarUInt64()`, `ReadVarInt64()` | `std::uint64_t`, `std::int64_t` |

All result-returning library methods are `[[nodiscard]]`. Insufficient fixed-width/slice input is
`OutOfBounds`. Varint input ending before a terminator is `TruncatedVarint`; a tenth byte greater
than 1 is `VarintOverflow` even if no eleventh byte exists. A multi-byte encoding ending with payload
zero is `NonCanonicalVarint`. A valid terminator ends the operation; following bytes are untouched.

## Writer

All write methods return void. `Written() const noexcept` returns a borrowed immutable view
`std::span<const std::byte>` of the prefix `[0, Position())`.

- `WriteBytes(std::span<const std::byte>)` supports exact, forward and backward overlap.
- `WriteUInt8(std::uint8_t)`, `WriteInt8(std::int8_t)`.
- `WriteUInt16(std::uint16_t, Endian)`, `WriteUInt32(std::uint32_t, Endian)`,
  `WriteUInt64(std::uint64_t, Endian)`.
- `WriteInt16(std::int16_t, Endian)`, `WriteInt32(std::int32_t, Endian)`,
  `WriteInt64(std::int64_t, Endian)`.
- `WriteFloat(float, Endian)`, `WriteDouble(double, Endian)`.
- `WriteVarUInt64(std::uint64_t)`, `WriteVarInt64(std::int64_t)`.

Every write checks the complete destination capacity. A rejected operation does not partially write.
`Skip` changes only the cursor. The caller owns initialization of gaps created by `Seek`/`Skip`.
An invalid Endian enum value raises `InvalidEndian` before bounds checks on both Reader and Writer.

## Ownership, numerical representation and synchronization

Read the README contracts before retaining a span or sharing buffers between threads. Signed fixed
integers use their C++20 two's-complement representation. Varint signed mapping uses unsigned
arithmetic, so INT64_MIN does not need to be negated. Floats use bit transport and require IEEE
binary32/binary64; no NaN classification is performed. The library does not change floating-point
environment flags, rounding mode, allocation limits or thread-local/global state.
