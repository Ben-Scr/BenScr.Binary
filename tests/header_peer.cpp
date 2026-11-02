#include <benscr/binary/binary.hpp>

int Peer()
{
    BenScr::Binary::Writer writer(std::span<std::byte>{});
    writer.WriteBytes({});
    return writer.Position() == 0 ? 0 : 1;
}
