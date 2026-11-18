# Native capture PNG implementation

`native_png.cpp` is original project code with no additional third-party encoder
or drawing backend. It writes PNG signature, IHDR (8-bit RGBA, noninterlaced),
IDAT and IEND with CRC-32. Each row has PNG filter 0. The zlib stream uses stored
DEFLATE blocks of at most 65535 bytes and Adler-32. Large files are intentional:
this small test encoder optimizes auditability rather than compression ratio.

Input is tight, top-down BGRA8 premultiplied from the public ABI. Each output
channel uses `min(255, (premultiplied * 255 + alpha / 2) / alpha)` in unsigned
integer arithmetic (nearest rounding). Alpha zero yields RGB zero. Conversion
cannot recover color information lost through premultiplication rounding.

The encoder checks nonzero dimensions, maximum 2048x2048, and exact byte length
before multiplication/allocation. Thus each frame is at most 16 MiB and encoded
PNG is slightly larger. Frame, filtered rows, zlib bytes and PNG may coexist;
these are bounded buffers, not a claim that total process or core temporary
memory is 16 MiB. Adapter font bytes are separately limited to aggregate 16 MiB,
and HTML to 65536 bytes. Allocation failures are reported rather than accepted
as a successful capture. The core enforces its own internal quotas.

`test_native_capture` exercises repeat pixel output, rejected inputs, sampled
cancellation points, and a known channel/alpha vector. `check_native_png.py`
decodes with Pillow and compares every RGBA byte against an independent ctypes
consumer of the installed public C ABI, including transparent antialiased text.
This is consistency validation, not visual parity with a browser.
