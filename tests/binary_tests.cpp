#include <benscr/binary/binary.hpp>

#include <algorithm>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace Binary = BenScr::Binary;
namespace
{
std::size_t checks = 0;

void Check(bool condition, const char *message)
{
    ++checks;
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename Action>
void ExpectError(Action action, Binary::ErrorCode code, std::size_t offset)
{
    try
    {
        action();
    }
    catch (const Binary::Error &error)
    {
        Check(error.Code() == code, "Wrong error code");
        Check(error.Offset() == offset, "Wrong error offset");
        Check(std::string(error.what()).size() > 0, "Missing diagnostic");
        return;
    }
    throw std::runtime_error("Expected Binary::Error");
}

std::vector<std::byte> Bytes(std::initializer_list<unsigned int> values)
{
    std::vector<std::byte> result;
    for (auto value : values)
    {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

// Independent reference uses quotient/remainder, rather than bit assembly.
std::vector<std::byte> ReferenceFixed(std::uint64_t value, std::size_t width, Binary::Endian endian)
{
    std::vector<std::byte> result(width);
    for (std::size_t index = 0; index < width; ++index)
    {
        result[index] = static_cast<std::byte>(value % 256);
        value /= 256;
    }
    if (endian == Binary::Endian::Big)
    {
        std::reverse(result.begin(), result.end());
    }
    return result;
}

std::vector<std::byte> ReferenceVarint(std::uint64_t value)
{
    std::vector<std::byte> result;
    do
    {
        const auto digit = value % 128;
        value /= 128;
        result.push_back(static_cast<std::byte>(digit + (value != 0 ? 128 : 0)));
    } while (value != 0);
    return result;
}

void KnownEndianVectors()
{
    const auto bytes = Bytes({0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef});
    Binary::Reader big(bytes);
    Binary::Reader little(bytes);
    Check(big.ReadUInt64(Binary::Endian::Big) == UINT64_C(0x0123456789abcdef), "Big vector");
    Check(little.ReadUInt64(Binary::Endian::Little) == UINT64_C(0xefcdab8967452301),
          "Little vector");
    big.Seek(0);
    Check(big.ReadUInt16(Binary::Endian::Big) == 0x0123U, "16-bit vector");
    Check(big.ReadUInt32(Binary::Endian::Big) == 0x456789abU, "32-bit vector");
    std::array<std::byte, 8> output{};
    Binary::Writer writer(output);
    writer.WriteUInt64(UINT64_C(0x0123456789abcdef), Binary::Endian::Big);
    Check(std::equal(output.begin(), output.end(), bytes.begin()), "Endian encoding vector");
}

template <typename UInt, typename Write, typename Read> void FixedWidth(Write write, Read read)
{
    std::vector<UInt> values{0, 1, std::numeric_limits<UInt>::max()};
    for (unsigned bit = 1; bit < sizeof(UInt) * 8; ++bit)
    {
        const auto boundary = static_cast<UInt>(UINT64_C(1) << bit);
        values.push_back(static_cast<UInt>(boundary - 1));
        values.push_back(boundary);
        values.push_back(static_cast<UInt>(boundary + 1));
    }
    for (const auto endian : {Binary::Endian::Little, Binary::Endian::Big})
    {
        for (auto value : values)
        {
            std::array<std::byte, sizeof(UInt) + 2> storage{};
            storage.fill(std::byte{0xcc});
            Binary::Writer writer(std::span<std::byte>(storage).subspan(1, sizeof(UInt)));
            write(writer, value, endian);
            const auto expected = ReferenceFixed(value, sizeof(UInt), endian);
            Check(std::equal(expected.begin(), expected.end(), storage.begin() + 1),
                  "Width vector");
            Check(storage.front() == std::byte{0xcc} && storage.back() == std::byte{0xcc},
                  "Guard bytes");
            Binary::Reader reader(expected);
            Check(read(reader, endian) == value, "Fixed independent decode");
            Check(reader.Remaining() == 0 && writer.Remaining() == 0, "Fixed cursor");
        }
    }
}

void UInt16Boundaries()
{
    FixedWidth<std::uint16_t>([](auto &w, auto v, auto e) { w.WriteUInt16(v, e); },
                              [](auto &r, auto e) { return r.ReadUInt16(e); });
}
void UInt32Boundaries()
{
    FixedWidth<std::uint32_t>([](auto &w, auto v, auto e) { w.WriteUInt32(v, e); },
                              [](auto &r, auto e) { return r.ReadUInt32(e); });
}
void UInt64Boundaries()
{
    FixedWidth<std::uint64_t>([](auto &w, auto v, auto e) { w.WriteUInt64(v, e); },
                              [](auto &r, auto e) { return r.ReadUInt64(e); });
}

void SignedAndByteBoundaries()
{
    for (const auto endian : {Binary::Endian::Little, Binary::Endian::Big})
    {
        std::array<std::byte, 32> storage{};
        Binary::Writer writer(storage);
        writer.WriteInt8(INT8_MIN);
        writer.WriteInt8(INT8_MAX);
        writer.WriteInt16(INT16_MIN, endian);
        writer.WriteInt16(INT16_MAX, endian);
        writer.WriteInt32(INT32_MIN, endian);
        writer.WriteInt32(INT32_MAX, endian);
        writer.WriteInt64(INT64_MIN, endian);
        writer.WriteInt64(INT64_MAX, endian);
        writer.WriteUInt8(UINT8_MAX);
        Binary::Reader reader(writer.Written());
        Check(reader.ReadInt8() == INT8_MIN && reader.ReadInt8() == INT8_MAX, "Signed8");
        Check(reader.ReadInt16(endian) == INT16_MIN && reader.ReadInt16(endian) == INT16_MAX,
              "Signed16");
        Check(reader.ReadInt32(endian) == INT32_MIN && reader.ReadInt32(endian) == INT32_MAX,
              "Signed32");
        Check(reader.ReadInt64(endian) == INT64_MIN && reader.ReadInt64(endian) == INT64_MAX,
              "Signed64");
        Check(reader.ReadUInt8() == UINT8_MAX, "Unsigned8");
        Check(storage.front() == std::byte{0x80}, "Signed8 two's complement");
        Check(storage[endian == Binary::Endian::Big ? 2 : 3] == std::byte{0x80},
              "Signed16 sign bit");
        Check(storage[endian == Binary::Endian::Big ? 6 : 9] == std::byte{0x80},
              "Signed32 sign bit");
        Check(storage[endian == Binary::Endian::Big ? 14 : 21] == std::byte{0x80},
              "Signed64 sign bit");
    }
    for (unsigned value = 0; value <= 255; ++value)
    {
        std::array<std::byte, 1> storage{};
        Binary::Writer writer(storage);
        writer.WriteUInt8(static_cast<std::uint8_t>(value));
        Binary::Reader reader(storage);
        Check(reader.ReadUInt8() == value, "All byte values");
    }
}

void FloatBits()
{
    // IEEE 754: zeros, subnormal endpoints, normal, infinities, quiet/signaling NaNs with payload.
    const std::array<std::uint32_t, 11> singles{0,           0x80000000U, 1,           0x007fffffU,
                                                0x00800000U, 0x3f800000U, 0x7f800000U, 0xff800000U,
                                                0x7fc12345U, 0xffc54321U, 0x7f800001U};
    const std::array<std::uint64_t, 11> doubles{0,
                                                UINT64_C(0x8000000000000000),
                                                1,
                                                UINT64_C(0x000fffffffffffff),
                                                UINT64_C(0x0010000000000000),
                                                UINT64_C(0x3ff0000000000000),
                                                UINT64_C(0x7ff0000000000000),
                                                UINT64_C(0xfff0000000000000),
                                                UINT64_C(0x7ff812345678abcd),
                                                UINT64_C(0xfff8abcdef123456),
                                                UINT64_C(0x7ff0000000000001)};
    for (const auto endian : {Binary::Endian::Little, Binary::Endian::Big})
    {
        for (auto bits : singles)
        {
            const auto reference = ReferenceFixed(bits, 4, endian);
            Binary::Reader reader(reference);
            const auto value = reader.ReadFloat(endian);
            Check(std::bit_cast<std::uint32_t>(value) == bits, "Float decode raw bits");
            std::array<std::byte, 4> bytes{};
            Binary::Writer writer(bytes);
            writer.WriteFloat(value, endian);
            Check(std::equal(bytes.begin(), bytes.end(), reference.begin()),
                  "Float encode raw bits");
        }
        for (auto bits : doubles)
        {
            const auto reference = ReferenceFixed(bits, 8, endian);
            Binary::Reader reader(reference);
            const auto value = reader.ReadDouble(endian);
            Check(std::bit_cast<std::uint64_t>(value) == bits, "Double decode raw bits");
            std::array<std::byte, 8> bytes{};
            Binary::Writer writer(bytes);
            writer.WriteDouble(value, endian);
            Check(std::equal(bytes.begin(), bytes.end(), reference.begin()),
                  "Double encode raw bits");
        }
    }
}

void KnownVarintVectors()
{
    // 150 -> 96 01: https://protobuf.dev/programming-guides/encoding/#varints
    const std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> vectors{
        {0, Bytes({0})},
        {1, Bytes({1})},
        {127, Bytes({0x7f})},
        {128, Bytes({0x80, 1})},
        {150, Bytes({0x96, 1})},
        {300, Bytes({0xac, 2})},
        {16383, Bytes({0xff, 0x7f})},
        {16384, Bytes({0x80, 0x80, 1})},
        {UINT64_MAX, Bytes({0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 1})},
        {UINT64_C(1) << 63, Bytes({0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 1})}};
    for (const auto &[value, bytes] : vectors)
    {
        Binary::Reader reader(bytes);
        Check(reader.ReadVarUInt64() == value && reader.Remaining() == 0, "Known varint decode");
        std::array<std::byte, 10> storage{};
        Binary::Writer writer(storage);
        writer.WriteVarUInt64(value);
        Check(std::ranges::equal(writer.Written(), bytes), "Known varint encode");
        for (std::size_t count = 0; count < bytes.size(); ++count)
        {
            Binary::Reader truncated(std::span<const std::byte>(bytes).first(count));
            ExpectError([&] { (void)truncated.ReadVarUInt64(); },
                        Binary::ErrorCode::TruncatedVarint, 0);
            Check(truncated.Position() == 0, "Truncated varint rollback");
        }
    }
}

void VarintBoundaries()
{
    for (unsigned bit = 1; bit < 64; ++bit)
    {
        const auto boundary = UINT64_C(1) << bit;
        for (auto value : {boundary - 1, boundary, boundary + 1})
        {
            const auto reference = ReferenceVarint(value);
            std::array<std::byte, 10> output{};
            Binary::Writer writer(output);
            writer.WriteVarUInt64(value);
            Check(std::ranges::equal(writer.Written(), reference), "Varint boundary encode");
            Binary::Reader reader(reference);
            Check(reader.ReadVarUInt64() == value, "Varint boundary decode");
        }
    }
}

void ZigZagVectors()
{
    const std::array<std::pair<std::int64_t, std::uint64_t>, 9> vectors{
        {{0, 0},
         {-1, 1},
         {1, 2},
         {-2, 3},
         {-500, 999},
         {INT32_MAX, UINT64_C(4294967294)},
         {INT32_MIN, UINT64_C(4294967295)},
         {INT64_MAX, UINT64_MAX - 1},
         {INT64_MIN, UINT64_MAX}}};
    for (auto [value, encoded] : vectors)
    {
        const auto reference = ReferenceVarint(encoded);
        Binary::Reader reader(reference);
        Check(reader.ReadVarInt64() == value, "ZigZag decode");
        std::array<std::byte, 10> output{};
        Binary::Writer writer(output);
        writer.WriteVarInt64(value);
        Check(std::ranges::equal(writer.Written(), reference), "ZigZag encode");
    }
}

void MalformedVarints()
{
    for (std::size_t width = 2; width <= 10; ++width)
    {
        std::vector<std::byte> bytes(width, std::byte{0x80});
        bytes.back() = std::byte{0};
        for (unsigned first : {0x80U, 0x81U, 0xffU})
        {
            bytes.front() = static_cast<std::byte>(first);
            Binary::Reader reader(bytes);
            ExpectError([&] { (void)reader.ReadVarUInt64(); },
                        Binary::ErrorCode::NonCanonicalVarint, 0);
            Check(reader.Position() == 0, "Noncanonical rollback");
        }
    }
    for (unsigned last = 2; last <= 255; ++last)
    {
        std::vector<std::byte> bytes(12, std::byte{0xff});
        bytes[10] = static_cast<std::byte>(last);
        Binary::Reader reader(bytes);
        reader.Skip(1);
        ExpectError([&] { (void)reader.ReadVarUInt64(); }, Binary::ErrorCode::VarintOverflow, 1);
        Check(reader.Position() == 1, "Overflow rollback at nonzero position");
        ExpectError([&] { (void)reader.ReadVarInt64(); }, Binary::ErrorCode::VarintOverflow, 1);
        Check(reader.Position() == 1, "Signed overflow rollback");
    }
    const auto bytes = Bytes({0x01, 0x02});
    Binary::Reader reader(bytes);
    Check(reader.ReadVarUInt64() == 1 && reader.ReadVarUInt64() == 2, "Stops at terminator");
}

void TruncatedFixedReads()
{
    const std::vector<std::pair<std::size_t, std::function<void(Binary::Reader &)>>> reads{
        {1, [](auto &r) { (void)r.ReadUInt8(); }},
        {1, [](auto &r) { (void)r.ReadInt8(); }},
        {2, [](auto &r) { (void)r.ReadUInt16(Binary::Endian::Little); }},
        {2, [](auto &r) { (void)r.ReadInt16(Binary::Endian::Big); }},
        {4, [](auto &r) { (void)r.ReadUInt32(Binary::Endian::Big); }},
        {4, [](auto &r) { (void)r.ReadInt32(Binary::Endian::Little); }},
        {8, [](auto &r) { (void)r.ReadUInt64(Binary::Endian::Little); }},
        {8, [](auto &r) { (void)r.ReadInt64(Binary::Endian::Big); }},
        {4, [](auto &r) { (void)r.ReadFloat(Binary::Endian::Little); }},
        {8, [](auto &r) { (void)r.ReadDouble(Binary::Endian::Big); }}};
    for (const auto &[width, action] : reads)
    {
        for (std::size_t available = 0; available < width; ++available)
        {
            std::vector<std::byte> bytes(available + 1, std::byte{0xff});
            Binary::Reader reader(bytes);
            reader.Skip(1);
            ExpectError([&] { action(reader); }, Binary::ErrorCode::OutOfBounds, 1);
            Check(reader.Position() == 1, "Fixed read rollback");
        }
    }
}

void WriterAtomicity()
{
    const std::vector<std::pair<std::size_t, std::function<void(Binary::Writer &)>>> writes{
        {1, [](auto &w) { w.WriteUInt8(255); }},
        {1, [](auto &w) { w.WriteInt8(-1); }},
        {2, [](auto &w) { w.WriteUInt16(65535, Binary::Endian::Little); }},
        {2, [](auto &w) { w.WriteInt16(-1, Binary::Endian::Big); }},
        {4, [](auto &w) { w.WriteUInt32(UINT32_MAX, Binary::Endian::Big); }},
        {4, [](auto &w) { w.WriteInt32(-1, Binary::Endian::Little); }},
        {8, [](auto &w) { w.WriteUInt64(UINT64_MAX, Binary::Endian::Little); }},
        {8, [](auto &w) { w.WriteInt64(-1, Binary::Endian::Big); }},
        {4, [](auto &w) { w.WriteFloat(1.0f, Binary::Endian::Little); }},
        {8, [](auto &w) { w.WriteDouble(1.0, Binary::Endian::Big); }},
        {10, [](auto &w) { w.WriteVarUInt64(UINT64_MAX); }},
        {10, [](auto &w) { w.WriteVarInt64(INT64_MIN); }},
        {3, [](auto &w) { w.WriteBytes(Bytes({1, 2, 3})); }}};
    for (const auto &[width, action] : writes)
    {
        for (std::size_t available = 0; available < width; ++available)
        {
            std::vector<std::byte> bytes(available + 1, std::byte{0xab});
            const auto before = bytes;
            Binary::Writer writer(bytes);
            writer.Skip(1);
            ExpectError([&] { action(writer); }, Binary::ErrorCode::OutOfBounds, 1);
            Check(writer.Position() == 1 && bytes == before, "Atomic writer failure");
        }
    }
    for (unsigned bit = 0; bit < 64; ++bit)
    {
        auto value = UINT64_C(1) << bit;
        const auto width = ReferenceVarint(value).size();
        std::vector<std::byte> bytes(width - 1, std::byte{0xef});
        const auto before = bytes;
        Binary::Writer writer(bytes);
        ExpectError([&] { writer.WriteVarUInt64(value); }, Binary::ErrorCode::OutOfBounds, 0);
        Check(bytes == before && writer.Position() == 0, "All varint widths atomic");
    }
}

void InvalidEndian()
{
    const auto invalid = static_cast<Binary::Endian>(99);
    std::array<std::byte, 16> bytes{};
    Binary::Reader reader(bytes);
    Binary::Writer writer(bytes);
    reader.Skip(1);
    writer.Skip(1);
    ExpectError([&] { (void)reader.ReadUInt16(invalid); }, Binary::ErrorCode::InvalidEndian, 1);
    ExpectError([&] { (void)reader.ReadUInt32(invalid); }, Binary::ErrorCode::InvalidEndian, 1);
    ExpectError([&] { (void)reader.ReadUInt64(invalid); }, Binary::ErrorCode::InvalidEndian, 1);
    ExpectError([&] { (void)reader.ReadFloat(invalid); }, Binary::ErrorCode::InvalidEndian, 1);
    ExpectError([&] { (void)reader.ReadDouble(invalid); }, Binary::ErrorCode::InvalidEndian, 1);
    ExpectError([&] { writer.WriteUInt16(1, invalid); }, Binary::ErrorCode::InvalidEndian, 1);
    ExpectError([&] { writer.WriteUInt32(1, invalid); }, Binary::ErrorCode::InvalidEndian, 1);
    ExpectError([&] { writer.WriteUInt64(1, invalid); }, Binary::ErrorCode::InvalidEndian, 1);
    ExpectError([&] { writer.WriteFloat(1, invalid); }, Binary::ErrorCode::InvalidEndian, 1);
    ExpectError([&] { writer.WriteDouble(1, invalid); }, Binary::ErrorCode::InvalidEndian, 1);
    Check(reader.Position() == 1 && writer.Position() == 1, "Invalid endian cursor");
    Check(std::ranges::all_of(bytes, [](auto b) { return b == std::byte{}; }),
          "Invalid endian writes");
}

void CursorsAndSlices()
{
    auto bytes = Bytes({1, 2, 3, 4, 5});
    Binary::Reader reader(bytes);
    Check(reader.Size() == 5 && reader.Remaining() == 5, "Reader initial state");
    reader.Skip(1);
    auto child = reader.ReadSlice(2);
    Check(reader.Position() == 3 && child.Position() == 0 && child.Size() == 2, "Slice positions");
    Check(child.ReadUInt16(Binary::Endian::Big) == 0x0203, "Bounded child");
    ExpectError([&] { (void)child.ReadUInt8(); }, Binary::ErrorCode::OutOfBounds, 2);
    auto copy = reader;
    Check(copy.ReadUInt8() == 4 && reader.Position() == 3, "Independent copied cursor");
    Check(reader.Unread().data() == bytes.data() + 3, "Borrowed unread");
    const auto view = reader.ReadBytes(1);
    bytes[3] = std::byte{9};
    Check(view[0] == std::byte{9}, "Borrowed view observes source");
    for (auto invalid : {std::size_t(6), std::numeric_limits<std::size_t>::max()})
    {
        ExpectError([&] { reader.Seek(invalid); }, Binary::ErrorCode::OutOfBounds, 4);
        ExpectError([&] { reader.Skip(invalid); }, Binary::ErrorCode::OutOfBounds, 4);
        ExpectError([&] { (void)reader.ReadBytes(invalid); }, Binary::ErrorCode::OutOfBounds, 4);
        ExpectError([&] { (void)reader.ReadSlice(invalid); }, Binary::ErrorCode::OutOfBounds, 4);
        Check(reader.Position() == 4, "Invalid cursor rollback");
    }
    reader.Seek(5);
    Check(reader.ReadBytes(0).empty() && reader.Unread().empty(), "End zero-length read");
    reader.Seek(0);
    Check(reader.ReadUInt8() == 1, "Rewind");
    Binary::Writer writer(bytes);
    writer.Seek(3);
    const auto before = bytes;
    ExpectError([&] { writer.Seek(6); }, Binary::ErrorCode::OutOfBounds, 3);
    ExpectError([&] { writer.Skip(std::numeric_limits<std::size_t>::max()); },
                Binary::ErrorCode::OutOfBounds, 3);
    Check(writer.Written().size() == 3 && writer.Size() == 5, "Writer prefix");
    writer.Skip(2);
    writer.WriteBytes({});
    Check(bytes == before && writer.Remaining() == 0, "Skip does not initialize");
}

void OverlapAndEmpty()
{
    auto bytes = Bytes({1, 2, 3, 4, 5});
    Binary::Writer writer(bytes);
    writer.Skip(1);
    writer.WriteBytes(std::span<const std::byte>(bytes).first(4));
    Check(bytes == Bytes({1, 1, 2, 3, 4}), "Overlap right");
    writer.Seek(0);
    writer.WriteBytes(std::span<const std::byte>(bytes).subspan(1));
    Check(bytes == Bytes({1, 2, 3, 4, 4}), "Overlap left");
    writer.Seek(0);
    writer.WriteBytes(bytes);
    Check(bytes == Bytes({1, 2, 3, 4, 4}), "Exact self copy");
    Binary::Writer emptyWriter(std::span<std::byte>{});
    Binary::Reader emptyReader(std::span<const std::byte>{});
    emptyWriter.Skip(0);
    emptyWriter.Seek(0);
    emptyWriter.WriteBytes({});
    emptyReader.Skip(0);
    emptyReader.Seek(0);
    Check(emptyReader.ReadSlice(0).Size() == 0, "Empty slice");
    Check(emptyWriter.Written().empty() && emptyReader.Unread().empty(), "Empty spans");
    ExpectError([&] { emptyWriter.WriteUInt8(0); }, Binary::ErrorCode::OutOfBounds, 0);
    ExpectError([&] { (void)emptyReader.ReadUInt8(); }, Binary::ErrorCode::OutOfBounds, 0);
}

void ReferenceFuzz()
{
    std::mt19937_64 random(UINT64_C(0xb10b1a5e));
    for (std::size_t iteration = 0; iteration < 20000; ++iteration)
    {
        const auto value = random();
        const auto reference = ReferenceVarint(value);
        Binary::Reader reader(reference);
        Check(reader.ReadVarUInt64() == value, "Fuzz reference decode");
        std::array<std::byte, 10> output{};
        Binary::Writer writer(output);
        writer.WriteVarUInt64(value);
        Check(std::ranges::equal(writer.Written(), reference), "Fuzz reference encode");
        const auto endian = iteration % 2 == 0 ? Binary::Endian::Little : Binary::Endian::Big;
        const auto fixed = ReferenceFixed(value, 8, endian);
        Binary::Reader fixedReader(fixed);
        Check(fixedReader.ReadUInt64(endian) == value, "Fuzz fixed reference decode");
        writer.Seek(0);
        writer.WriteUInt64(value, endian);
        Check(std::ranges::equal(writer.Written(), fixed), "Fuzz fixed reference encode");
        const auto signedValue = std::bit_cast<std::int64_t>(value);
        writer.Seek(0);
        writer.WriteVarInt64(signedValue);
        Binary::Reader zigzag(writer.Written());
        Check(zigzag.ReadVarInt64() == signedValue, "Fuzz ZigZag");
    }
}

void MalformedFuzz()
{
    std::mt19937_64 random(UINT64_C(0xdec0de));
    for (std::size_t iteration = 0; iteration < 30000; ++iteration)
    {
        std::vector<std::byte> input(static_cast<std::size_t>(random() % 13));
        for (auto &byte : input)
        {
            byte = static_cast<std::byte>(random() % 256);
        }
        // Determine acceptance independently, then evaluate the base-128 digits in reverse.
        auto code = Binary::ErrorCode::TruncatedVarint;
        std::size_t consumed = 0;
        bool accepted = false;
        for (std::size_t index = 0; index < input.size() && index < 10; ++index)
        {
            const auto digit = std::to_integer<unsigned>(input[index]);
            if (index == 9 && digit > 1)
            {
                code = Binary::ErrorCode::VarintOverflow;
                break;
            }
            if (digit < 128)
            {
                if (index > 0 && digit == 0)
                {
                    code = Binary::ErrorCode::NonCanonicalVarint;
                }
                else
                {
                    accepted = true;
                    consumed = index + 1;
                }
                break;
            }
        }
        Binary::Reader reader(input);
        if (accepted)
        {
            std::uint64_t expected = 0;
            for (std::size_t index = consumed; index > 0; --index)
            {
                expected = expected * 128 + std::to_integer<unsigned>(input[index - 1]) % 128;
            }
            Check(reader.ReadVarUInt64() == expected, "Fuzz malformed acceptance value");
            Check(reader.Position() == consumed, "Fuzz consumed length");
        }
        else
        {
            ExpectError([&] { (void)reader.ReadVarUInt64(); }, code, 0);
            Check(reader.Position() == 0, "Fuzz malformed rollback");
        }
    }
}
} // namespace

int main()
{
    const std::vector<std::pair<const char *, void (*)()>> tests{
        {"known endian vectors", KnownEndianVectors},
        {"uint16 boundaries", UInt16Boundaries},
        {"uint32 boundaries", UInt32Boundaries},
        {"uint64 boundaries", UInt64Boundaries},
        {"signed and byte boundaries", SignedAndByteBoundaries},
        {"IEEE 754 bits", FloatBits},
        {"known varint vectors", KnownVarintVectors},
        {"varint boundaries", VarintBoundaries},
        {"ZigZag vectors", ZigZagVectors},
        {"malformed varints", MalformedVarints},
        {"truncated fixed reads", TruncatedFixedReads},
        {"writer atomicity", WriterAtomicity},
        {"invalid endian", InvalidEndian},
        {"cursors and slices", CursorsAndSlices},
        {"overlap and empty", OverlapAndEmpty},
        {"reference fuzz", ReferenceFuzz},
        {"malformed fuzz", MalformedFuzz}};
    for (const auto &[name, test] : tests)
    {
        try
        {
            test();
        }
        catch (const std::exception &error)
        {
            std::cerr << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << tests.size() << " contract cases passed; " << checks
              << " checks; 50000 fixed-seed fuzz inputs.\n";
}
