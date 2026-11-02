# Provenance

BenScr.Binary 1.0.0 is a new implementation for bounded binary buffers. No historical package or
private repository source is migrated, and there are no bundled third-party implementation files.

The initial header-only CMake/NuGet verification scaffolding is adapted from the public
[BenScr.Signals](https://github.com/Ben-Scr/BenScr.Signals/tree/98f0a89b6b7608673954fe65d7682441285cdef0)
repository at commit `98f0a89b6b7608673954fe65d7682441285cdef0`.
Binary's implementation, API, test suite and usage example are specific to this repository.

The unchanged custom `LICENSE.txt` belongs to Ben Schneider; SHA-256:
`f6600e285e17c48188a65745d7595807e1755c72201e8eef201a9174fca18573`.
It is copied byte-for-byte, including original CRLF line endings.

The original supplied `branding/BenScr_Icon_Master.png` is 1254 × 1254, RGBA, 795819 bytes.
Its SHA-256 is `a5dcabceb766e13c1840a95130ee98a458e12e3713fd9b07f3ceae75a1f5b993`.
It is embedded unchanged as `icon.png`; no resized derivative is shipped.

The independent varint fixture `150 -> 96 01` and ZigZag mapping examples are cross-checked against
the [Protocol Buffers encoding reference](https://protobuf.dev/programming-guides/encoding/)
(checked 2026-11-02). The implementation requires canonical/minimal ULEB128 encodings; the complete
Protobuf wire format and its parser permissiveness are outside this library's contract.

Other fixtures use explicit base-256 integer byte values and IEEE 754 bit patterns. Differential
tests construct reference encodings through quotient/remainder arithmetic and evaluate accepted
varints from the most significant group down; they do not use the library to produce expected bytes.
