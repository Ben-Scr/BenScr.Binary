#ifndef BENSCR_BINARY_BINARY_HPP
#define BENSCR_BINARY_BINARY_HPP

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>

namespace BenScr::Binary
{
static_assert(CHAR_BIT == 8);
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);

enum class Endian
{
    Little,
    Big
};

enum class ErrorCode
{
    OutOfBounds,
    InvalidEndian,
    TruncatedVarint,
    VarintOverflow,
    NonCanonicalVarint
};

/// A failed operation. Offset() is the cursor position before the operation.
class Error final : public std::runtime_error
{
  public:
    Error(ErrorCode code, std::size_t offset, const char *message)
        : std::runtime_error(message), code(code), offset(offset)
    {
    }

    [[nodiscard]] ErrorCode Code() const noexcept
    {
        return code;
    }
    [[nodiscard]] std::size_t Offset() const noexcept
    {
        return offset;
    }

  private:
    ErrorCode code;
    std::size_t offset;
};

/// Borrows immutable bytes; the underlying storage must outlive this reader and its slices.
/// Successful operations advance the cursor; all failures leave it unchanged.
class Reader
{
  public:
    explicit Reader(std::span<const std::byte> buffer) noexcept : buffer(buffer) {}

    [[nodiscard]] std::size_t Size() const noexcept
    {
        return buffer.size();
    }
    [[nodiscard]] std::size_t Position() const noexcept
    {
        return position;
    }
    [[nodiscard]] std::size_t Remaining() const noexcept
    {
        return buffer.size() - position;
    }
    [[nodiscard]] std::span<const std::byte> Unread() const noexcept
    {
        return buffer.subspan(position);
    }

    /// Positions within [0, Size()]. Does not inspect bytes.
    void Seek(std::size_t offset)
    {
        if (offset > buffer.size())
        {
            throw Error(ErrorCode::OutOfBounds, position, "Reader seek exceeds buffer.");
        }
        position = offset;
    }

    void Skip(std::size_t count)
    {
        Require(count);
        position += count;
    }

    /// Returns a borrowed view of count bytes and advances by count; zero is valid.
    [[nodiscard]] std::span<const std::byte> ReadBytes(std::size_t count)
    {
        Require(count);
        const auto result = buffer.subspan(position, count);
        position += count;
        return result;
    }

    /// A bounded child reader with an independent zero-based cursor; consumes count bytes.
    [[nodiscard]] Reader ReadSlice(std::size_t count)
    {
        return Reader(ReadBytes(count));
    }

    [[nodiscard]] std::uint8_t ReadUInt8()
    {
        Require(1);
        return std::to_integer<std::uint8_t>(buffer[position++]);
    }
    [[nodiscard]] std::int8_t ReadInt8()
    {
        return std::bit_cast<std::int8_t>(ReadUInt8());
    }
    [[nodiscard]] std::uint16_t ReadUInt16(Endian endian)
    {
        return ReadUnsigned<std::uint16_t>(endian);
    }
    [[nodiscard]] std::uint32_t ReadUInt32(Endian endian)
    {
        return ReadUnsigned<std::uint32_t>(endian);
    }
    [[nodiscard]] std::uint64_t ReadUInt64(Endian endian)
    {
        return ReadUnsigned<std::uint64_t>(endian);
    }
    [[nodiscard]] std::int16_t ReadInt16(Endian endian)
    {
        return std::bit_cast<std::int16_t>(ReadUInt16(endian));
    }
    [[nodiscard]] std::int32_t ReadInt32(Endian endian)
    {
        return std::bit_cast<std::int32_t>(ReadUInt32(endian));
    }
    [[nodiscard]] std::int64_t ReadInt64(Endian endian)
    {
        return std::bit_cast<std::int64_t>(ReadUInt64(endian));
    }

    /// Bit-preserving IEEE 754 binary32/binary64 transport; does not validate finite values.
    [[nodiscard]] float ReadFloat(Endian endian)
    {
        return std::bit_cast<float>(ReadUInt32(endian));
    }
    [[nodiscard]] double ReadDouble(Endian endian)
    {
        return std::bit_cast<double>(ReadUInt64(endian));
    }

    /// Reads exactly one canonical unsigned LEB128 integer (1..10 bytes).
    [[nodiscard]] std::uint64_t ReadVarUInt64()
    {
        std::uint64_t result = 0;
        for (std::size_t index = 0; index < 10; ++index)
        {
            if (index == Remaining())
            {
                throw Error(ErrorCode::TruncatedVarint, position, "Truncated unsigned LEB128.");
            }
            const auto byte = std::to_integer<std::uint8_t>(buffer[position + index]);
            if (index == 9 && byte > 1)
            {
                throw Error(ErrorCode::VarintOverflow, position,
                            "Unsigned LEB128 exceeds 64 bits.");
            }
            const auto payload = static_cast<std::uint64_t>(byte & 0x7fU);
            result |= payload << (index * 7);
            if ((byte & 0x80U) == 0)
            {
                if (index > 0 && payload == 0)
                {
                    throw Error(ErrorCode::NonCanonicalVarint, position,
                                "Unsigned LEB128 has redundant leading zero groups.");
                }
                position += index + 1;
                return result;
            }
        }
        // The tenth-byte check rejects continuation before this point.
        throw Error(ErrorCode::VarintOverflow, position, "Unsigned LEB128 exceeds 64 bits.");
    }

    /// ZigZag maps 0,-1,1,-2,... onto 0,1,2,3,... before unsigned LEB128 encoding.
    [[nodiscard]] std::int64_t ReadVarInt64()
    {
        const auto value = ReadVarUInt64();
        const auto magnitude = static_cast<std::int64_t>(value >> 1);
        return (value & 1U) == 0 ? magnitude : -magnitude - 1;
    }

  private:
    void Require(std::size_t count) const
    {
        if (count > Remaining())
        {
            throw Error(ErrorCode::OutOfBounds, position, "Reader operation exceeds buffer.");
        }
    }

    template <typename UInt> [[nodiscard]] UInt ReadUnsigned(Endian endian)
    {
        if (endian != Endian::Little && endian != Endian::Big)
        {
            throw Error(ErrorCode::InvalidEndian, position, "Invalid byte order.");
        }
        Require(sizeof(UInt));
        std::uint64_t result = 0;
        for (std::size_t index = 0; index < sizeof(UInt); ++index)
        {
            const auto shift = (endian == Endian::Little ? index : sizeof(UInt) - index - 1) * 8;
            result |= std::to_integer<std::uint64_t>(buffer[position + index]) << shift;
        }
        position += sizeof(UInt);
        return static_cast<UInt>(result);
    }

    std::span<const std::byte> buffer;
    std::size_t position = 0;
};

/// Borrows mutable bytes. Failures leave both the cursor and all buffer bytes unchanged.
/// Copies of Writer share the same buffer but have independent cursor values.
class Writer
{
  public:
    explicit Writer(std::span<std::byte> buffer) noexcept : buffer(buffer) {}

    [[nodiscard]] std::size_t Size() const noexcept
    {
        return buffer.size();
    }
    [[nodiscard]] std::size_t Position() const noexcept
    {
        return position;
    }
    [[nodiscard]] std::size_t Remaining() const noexcept
    {
        return buffer.size() - position;
    }
    /// Prefix [0, Position()); may include skipped/unmodified bytes after Seek or Skip.
    [[nodiscard]] std::span<const std::byte> Written() const noexcept
    {
        return buffer.first(position);
    }

    void Seek(std::size_t offset)
    {
        if (offset > buffer.size())
        {
            throw Error(ErrorCode::OutOfBounds, position, "Writer seek exceeds buffer.");
        }
        position = offset;
    }

    /// Advances without initializing or changing bytes.
    void Skip(std::size_t count)
    {
        Require(count);
        position += count;
    }

    /// Overlap with this writer's buffer is supported, with memmove semantics.
    void WriteBytes(std::span<const std::byte> source)
    {
        Require(source.size());
        if (!source.empty())
        {
            std::memmove(buffer.data() + position, source.data(), source.size());
        }
        position += source.size();
    }

    void WriteUInt8(std::uint8_t value)
    {
        Require(1);
        buffer[position++] = static_cast<std::byte>(value);
    }
    void WriteInt8(std::int8_t value)
    {
        WriteUInt8(std::bit_cast<std::uint8_t>(value));
    }
    void WriteUInt16(std::uint16_t value, Endian endian)
    {
        WriteUnsigned(value, endian);
    }
    void WriteUInt32(std::uint32_t value, Endian endian)
    {
        WriteUnsigned(value, endian);
    }
    void WriteUInt64(std::uint64_t value, Endian endian)
    {
        WriteUnsigned(value, endian);
    }
    void WriteInt16(std::int16_t value, Endian endian)
    {
        WriteUInt16(std::bit_cast<std::uint16_t>(value), endian);
    }
    void WriteInt32(std::int32_t value, Endian endian)
    {
        WriteUInt32(std::bit_cast<std::uint32_t>(value), endian);
    }
    void WriteInt64(std::int64_t value, Endian endian)
    {
        WriteUInt64(std::bit_cast<std::uint64_t>(value), endian);
    }
    void WriteFloat(float value, Endian endian)
    {
        WriteUInt32(std::bit_cast<std::uint32_t>(value), endian);
    }
    void WriteDouble(double value, Endian endian)
    {
        WriteUInt64(std::bit_cast<std::uint64_t>(value), endian);
    }

    void WriteVarUInt64(std::uint64_t value)
    {
        std::array<std::byte, 10> encoded{};
        std::size_t count = 0;
        do
        {
            auto byte = static_cast<std::uint8_t>(value & 0x7fU);
            value >>= 7;
            if (value != 0)
            {
                byte |= 0x80U;
            }
            encoded[count++] = static_cast<std::byte>(byte);
        } while (value != 0);
        WriteBytes(std::span<const std::byte>(encoded).first(count));
    }

    void WriteVarInt64(std::int64_t value)
    {
        // Unsigned arithmetic avoids negating INT64_MIN or shifting a negative integer.
        const auto bits = static_cast<std::uint64_t>(value);
        WriteVarUInt64((bits << 1) ^ (value < 0 ? UINT64_MAX : UINT64_C(0)));
    }

  private:
    void Require(std::size_t count) const
    {
        if (count > Remaining())
        {
            throw Error(ErrorCode::OutOfBounds, position, "Writer operation exceeds buffer.");
        }
    }

    template <typename UInt> void WriteUnsigned(UInt value, Endian endian)
    {
        if (endian != Endian::Little && endian != Endian::Big)
        {
            throw Error(ErrorCode::InvalidEndian, position, "Invalid byte order.");
        }
        Require(sizeof(UInt));
        for (std::size_t index = 0; index < sizeof(UInt); ++index)
        {
            const auto shift = (endian == Endian::Little ? index : sizeof(UInt) - index - 1) * 8;
            buffer[position + index] = static_cast<std::byte>((value >> shift) & 0xffU);
        }
        position += sizeof(UInt);
    }

    std::span<std::byte> buffer;
    std::size_t position = 0;
};
} // namespace BenScr::Binary

#endif
