# Migration

Version 1.0.0 introduces a new package, repository and API. No prior BenScr.Binary package or
historical API is replaced. No obsolete forwarding classes or compatibility facades are shipped.

Use `BenScr.Binary` for NuGet, `BenScrBinary` for CMake discovery, `BenScr::Binary` for the namespace
and CMake target, and `benscr/binary/binary.hpp` for includes.

When moving from hand-written parsing, supply byte order explicitly and decide whether your wire
protocol uses unsigned LEB128, ZigZag+LEB128 or another signed representation. Nonminimal varints
are deliberately rejected; validate compatibility with a permissive legacy writer before replacing it.
Reader and Writer do not own buffers, grow output or make a multi-call record transactional.
