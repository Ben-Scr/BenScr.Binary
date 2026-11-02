#include <benscr/binary/binary.hpp>

#include <array>
#include <iostream>

int main()
{
    namespace Binary = BenScr::Binary;
    std::array<std::byte, 64> storage{};
    Binary::Writer writer(storage);
    writer.WriteUInt16(0xb15c, Binary::Endian::Big);
    writer.WriteUInt8(1);
    writer.WriteVarUInt64(150);
    writer.WriteFloat(21.5f, Binary::Endian::Little);
    writer.WriteVarInt64(-500);
    writer.WriteVarUInt64(3);
    writer.WriteBytes(std::array{std::byte{0xa1}, std::byte{0xb2}, std::byte{0xc3}});

    Binary::Reader reader(writer.Written());
    if (reader.ReadUInt16(Binary::Endian::Big) != 0xb15c || reader.ReadUInt8() != 1 ||
        reader.ReadVarUInt64() != 150 || reader.ReadFloat(Binary::Endian::Little) != 21.5f ||
        reader.ReadVarInt64() != -500)
    {
        return 1;
    }
    const auto length = reader.ReadVarUInt64();
    if (length > reader.Remaining())
    {
        return 2;
    }
    auto payload = reader.ReadSlice(static_cast<std::size_t>(length));
    if (payload.ReadUInt8() != 0xa1 || payload.ReadUInt16(Binary::Endian::Big) != 0xb2c3 ||
        payload.Remaining() != 0 || reader.Remaining() != 0)
    {
        return 3;
    }
    std::cout << "Binary verified.\n";
}
