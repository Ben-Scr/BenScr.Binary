# Changelog

## 1.0.0 — 2026-11-02

- Add bounded span readers and writers with independent copyable cursors and borrowed slices.
- Add signed/unsigned 8–64-bit values, explicit little/big endian and IEEE binary32/binary64 transport.
- Add canonical ULEB128 uint64 and ZigZag int64 with strict overflow/truncation validation.
- Guarantee cursor rollback and unchanged write buffers on a failed operation; support byte overlap.
- Ship CMake installation and native NuGet imports with isolated consumer verification.
