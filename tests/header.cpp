#include <benscr/binary/binary.hpp>

int Peer();

int main()
{
    BenScr::Binary::Reader reader(std::span<const std::byte>{});
    return reader.Remaining() == 0 ? Peer() : 1;
}
