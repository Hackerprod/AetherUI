#!/usr/bin/env python3
"""Independent PNG decoder + public C ABI oracle (Pillow, ctypes).
Usage: check_native_png.py INSTALLED_DSO FIXTURE_NATIVE_DIR OUTPUT_DIR
OUTPUT_DIR contains profile1.png, profile2.png, channels.png from test_native_capture.
No private engine headers, no host painting, no production encoder reuse.
"""
import ctypes as C
from pathlib import Path
import sys
from PIL import Image

lib = C.CDLL(str(Path(sys.argv[1]).resolve()))
root, output = map(Path, sys.argv[2:4])
class Options(C.Structure):
    _fields_ = [('struct_size', C.c_uint32), ('profile', C.c_uint32),
                ('regular_font', C.c_void_p), ('regular_font_size', C.c_size_t),
                ('bold_font', C.c_void_p), ('bold_font_size', C.c_size_t),
                ('reserved', C.c_uint32 * 4)]
class Info(C.Structure):
    _fields_ = [('struct_size', C.c_uint32), ('width', C.c_int32),
                ('height', C.c_int32), ('format', C.c_uint32),
                ('stride', C.c_uint64), ('byte_count', C.c_uint64), ('sequence', C.c_uint64)]
lib.aether_surface_create.argtypes = [C.c_int32, C.c_int32, C.POINTER(C.c_void_p)]
lib.aether_surface_render_html_ltr.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(Options)]
lib.aether_surface_copy_frame.argtypes = [C.c_void_p, C.POINTER(Info), C.c_void_p, C.c_size_t]
lib.aether_surface_destroy.argtypes = [C.c_void_p]
lib.aether_html_ltr_options_size.restype = C.c_size_t
lib.aether_surface_frame_info_size.restype = C.c_size_t
assert lib.aether_html_ltr_options_size() == C.sizeof(Options)
assert lib.aether_surface_frame_info_size() == C.sizeof(Info)
regular = C.create_string_buffer((root/'fonts/DejaVuSans.ttf').read_bytes())
bold = C.create_string_buffer((root/'fonts/DejaVuSans-Bold.ttf').read_bytes())
for profile, fixture in [(1, 'profile1-transparent.html'), (2, 'profile2-static-controls.html')]:
    html = C.create_string_buffer((root/fixture).read_bytes())
    options = Options(C.sizeof(Options), profile, C.addressof(regular), len(regular)-1, C.addressof(bold), len(bold)-1)
    surface = C.c_void_p()
    assert lib.aether_surface_create(480, 320, C.byref(surface)) == 0
    try:
        assert lib.aether_surface_render_html_ltr(surface, html, len(html)-1, C.byref(options)) == 0
        info = Info(struct_size=C.sizeof(Info))
        assert lib.aether_surface_copy_frame(surface, C.byref(info), None, 0) == 0
        assert (info.width, info.height, info.stride, info.byte_count, info.sequence, info.format) == (480, 320, 1920, 614400, 1, 1)
        raw = C.create_string_buffer(info.byte_count)
        assert lib.aether_surface_copy_frame(surface, C.byref(info), raw, len(raw)) == 0
        expected = bytearray()
        for pos in range(0, len(raw), 4):
            b, g, r, a = raw.raw[pos:pos+4]
            expected.extend([min(255, (channel*255+a//2)//a) if a else 0 for channel in (r,g,b)] + [a])
        image = Image.open(output/f'profile{profile}.png')
        image.load()
        assert image.mode == 'RGBA' and image.size == (480, 320)
        assert image.tobytes() == bytes(expected), 'PNG differs from independently copied ABI frame'
        assert image.getpixel((479,319)) == ((0,0,0,0) if profile == 1 else (232,239,247,255))
        if profile == 1:
            assert any(0<a<255 for a in image.getchannel('A').tobytes()), 'Expected antialiased alpha'
    finally:
        assert lib.aether_surface_destroy(surface) == 0
channels = Image.open(output/'channels.png')
assert channels.mode == 'RGBA' and channels.size == (4,1)
assert channels.tobytes() == bytes([30,20,10,255,128,64,0,128,0,0,0,0,255,255,255,1])
print('PASS Pillow decode, dimensions, BGRA channels, alpha zero/fractional, and complete pixel equality with independent ctypes public-ABI consumer')
