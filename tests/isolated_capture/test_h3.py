#!/usr/bin/env python3
"""Manual H3 integration gates. Real CLI/PNG; no Chromium equivalence claim.
Usage: python3 tests/isolated_capture/test_h3.py BUILD/aether_capture [EVIDENCE_DIR]
The evidence directory must not exist. Without it, results are temporary.
"""
import hashlib
import json
import pathlib
import struct
import subprocess
import sys
import tempfile
import zlib

BINARY = str(pathlib.Path(sys.argv[1]).resolve())
TEMP = None
if len(sys.argv) > 2:
    ROOT = pathlib.Path(sys.argv[2]).resolve()
    ROOT.mkdir()
else:
    TEMP = tempfile.TemporaryDirectory(prefix='aether-h3-')
    ROOT = pathlib.Path(TEMP.name)
RESULTS = []


def png(path):
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    pos, packed, size = 8, bytearray(), None
    while pos < len(data):
        n = struct.unpack('>I', data[pos:pos + 4])[0]
        kind, payload = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        assert zlib.crc32(kind + payload) == struct.unpack('>I', data[pos + 8 + n:pos + 12 + n])[0]
        if kind == b'IHDR':
            w, h, depth, color, compression, filtering, interlace = struct.unpack('>IIBBBBB', payload)
            assert (depth, color, compression, filtering, interlace) == (8, 6, 0, 0, 0)
            size = (w, h)
        if kind == b'IDAT':
            packed += payload
        pos += 12 + n
    assert size is not None
    w, h = size
    raw = zlib.decompress(packed)
    assert len(raw) == h * (1 + w * 4)
    rows, previous = [], bytearray(w * 4)
    for y in range(h):
        offset = y * (1 + w * 4)
        filt, row = raw[offset], bytearray(raw[offset + 1:offset + 1 + w * 4])
        assert 0 <= filt <= 4
        for i in range(len(row)):
            a = row[i - 4] if i >= 4 else 0
            b = previous[i]
            c = previous[i - 4] if i >= 4 else 0
            if filt == 1:
                row[i] = (row[i] + a) & 255
            elif filt == 2:
                row[i] = (row[i] + b) & 255
            elif filt == 3:
                row[i] = (row[i] + ((a + b) // 2)) & 255
            elif filt == 4:
                p = a + b - c
                ds = (abs(p-a), abs(p-b), abs(p-c))
                predictor = (a, b, c)[ds.index(min(ds))]
                row[i] = (row[i] + predictor) & 255
        rows.append(row)
        previous = row
    return w, h, rows


def pixel(image, x, y):
    return tuple(image[2][y][x * 4:x * 4 + 4])


def bounds(image, color):
    coords = [(x, y) for y, row in enumerate(image[2])
              for x in range(image[0]) if tuple(row[x * 4:x * 4 + 4]) == color]
    assert coords, ('missing color', color)
    xs, ys = zip(*coords)
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def run(name, html, success=True, reason=None):
    source, out = ROOT / (name + '.html'), ROOT / name
    source.write_text(html)
    completed = subprocess.run([BINARY, '--geometry', str(source), str(out), '900', '240', '1'],
                               capture_output=True, text=True, timeout=20)
    assert completed.returncode == (0 if success else 3), (name, completed.returncode, completed.stdout, completed.stderr,
                                                           (out / 'report.log').read_text() if (out / 'report.log').exists() else '')
    log = (out / 'report.log').read_text()
    assert 'css_eligibility=Unknown' in log
    assert 'flexbox_semantic_acceptance=false' in log and 'chromium=NOT_RUN' in log
    fields = dict(line.split('=', 1) for line in log.splitlines() if '=' in line and not line.startswith('inventory_'))
    for key in ('process_wall_ms', 'child_cpu_seconds', 'child_peak_rss_kib'):
        assert float(fields[key]) >= 0, (name, key)
    if success:
        assert (out / 'aether.png').exists(), name
        assert 'canvas_mode=ENABLED_INITIAL_EMPTY' in log and 'javascript_execution=NONE' in log
        for stage in ('parse', 'style', 'layout', 'display', 'raster', 'png'):
            assert 'stage_started=' + stage in log
            assert float(fields[stage + '_wall_ms']) >= 0
    else:
        assert not (out / 'aether.png').exists(), name
        assert not (out / 'aether.png.pending').exists(), name
        if reason:
            assert reason in log, (name, reason, log)
    RESULTS.append({'name': name, 'success_expected': success,
                    **{k: fields[k] for k in ('process_wall_ms', 'child_cpu_seconds', 'child_peak_rss_kib')}})
    return out, log, fields


BASE = '''body{margin:0px;background-color:#112233}.row{display:flex;flex-direction:row;flex-wrap:nowrap;
width:800px;height:200px;gap:10px;align-items:center}.peer{width:20px;height:20px;flex-grow:0;flex-shrink:0;background-color:#00ff00}
canvas{flex-grow:0;border:2px solid #ff0000}'''


def document(attrs='width="100" height="50"', css='', fallback='fallback text with real nonzero width', tail=''):
    return '<html><head><style>' + BASE + css + '</style></head><body><div class="row"><canvas ' + attrs + '>' + fallback + '</canvas><div class="peer"></div></div>' + tail + '</body></html>'


# Intrinsic/content and border-box dimensions; sibling offset proves flex basis.
for name, attrs, css, expected in [
    ('intrinsic', 'width="100" height="50"', '', (104, 54)),
    ('default', '', '', (304, 154)),
    ('plus_prefix', 'width="  +100junk" height="50.9"', '', (104, 54)),
    ('invalid_default', 'width="-1" height="bogus"', '', (304, 154)),
    ('overflow_default', 'width="2147483648" height="999999999999999999999999999"', '', (304, 154)),
    ('css_two_axes', 'width="100" height="50"', 'canvas{width:80px;height:30px}', (84, 34)),
    ('ratio_width', 'width="100" height="50"', 'canvas{width:80px;height:auto}', (84, 44)),
    ('ratio_height', 'width="100" height="50"', 'canvas{width:auto;height:40px}', (84, 44)),
    ('padding_content', 'width="100" height="50"', 'canvas{padding:3px}', (110, 60)),
    ('padding_border', 'width="100" height="50"', 'canvas{box-sizing:border-box;padding:3px;width:90px;height:40px}', (90, 40)),
    ('ratio_border', 'width="100" height="50"', 'canvas{box-sizing:border-box;padding:3px;width:90px;height:auto}', (90, 50)),
    ('border_below_decoration', 'width="100" height="50"', 'canvas{box-sizing:border-box;padding:3px;width:2px;height:2px}', (10, 10)),
    ('zero_intrinsic', 'width="0" height="0"', '', (4, 4)),
    ('zero_css_defined', 'width="0" height="0"', 'canvas{width:20px;height:10px}', (24, 14)),
]:
    out, _, _ = run(name, document(attrs, css))
    image = png(out / 'aether.png')
    box = bounds(image, (255, 0, 0, 255))
    assert (box[2]-box[0], box[3]-box[1]) == expected, (name, box, expected)
    assert box[0] == 0 and box[1] == (200-expected[1]) // 2, (name, box)
    peer = bounds(image, (0, 255, 0, 255))
    assert peer == (expected[0]+10, 90, expected[0]+30, 110), (name, peer)
    if expected[0] > 10 and expected[1] > 10:
        assert pixel(image, 7, box[1]+7) == (17, 34, 51, 255), name

out, _, _ = run('unused_border_width', document(css='canvas{border-style:none;border-width:20px}'))
assert bounds(png(out / 'aether.png'), (0, 255, 0, 255)) == (110, 90, 130, 110)
out, _, _ = run('negative_numeric_margin', document(css='canvas{margin-left:10px;margin-right:-5px}'))
assert bounds(png(out / 'aether.png'), (0, 255, 0, 255)) == (119, 90, 139, 110)

out, _, _ = run('transparent_page', document(css='body{background-color:transparent}'))
assert pixel(png(out / 'aether.png'), 10, 90) == (0, 0, 0, 0)
out, _, _ = run('paint_background', document(css='canvas{background-color:#0000ff}'))
assert pixel(png(out / 'aether.png'), 10, 90) == (0, 0, 255, 255)
out, _, _ = run('opacity', document(css='canvas{opacity:0.5}'))
image = png(out / 'aether.png')
assert pixel(image, 10, 90) == (17, 34, 51, 255)
assert all(abs(a-b) <= 1 for a, b in zip(pixel(image, 0, 80), (136, 17, 25, 255)))
out, _, _ = run('clip', document(css='.row{height:40px;overflow:hidden}'))
image = png(out / 'aether.png')
assert pixel(image, 0, 10) == (255, 0, 0, 255)
assert pixel(image, 0, 40) == (17, 34, 51, 255)
a, _, _ = run('repeat_a', document())
b, _, _ = run('repeat_b', document())
assert (a / 'aether.png').read_bytes() == (b / 'aether.png').read_bytes()
for display in ('inline', 'inline-block', 'block'):
    run('display_' + display, document(css='canvas{display:' + display + '}'))
for align in ('flex-start', 'center'):
    out, _, _ = run('align_' + align, document(css='canvas{align-self:' + align + '}'))
    assert bounds(png(out / 'aether.png'), (255, 0, 0, 255))[1] == (0 if align == 'flex-start' else 73)

for typ in ('application/json', 'APPLICATION/JSON', 'Application/Json'):
    run('json_' + typ.replace('/', '_'), document(tail='<script type="' + typ + '">arbitrary invalid JSON is inert</script>'))
run('json_hidden_ancestor', document(tail='<div style="display:none"><script type="application/json" style="display:block">{}</script></div>'))
run('json_visible', document(tail='<script type="application/json" style="display:block">{}</script>'), False, 'VISIBLE_SCRIPT_DATA_UNSUPPORTED')
for name, tail in [('script_src', '<script type="application/json" src="x">{}</script>'),
                   ('script_classic', '<script>1</script>'), ('script_module', '<script type="module">1</script>'),
                   ('script_importmap', '<script type="importmap">{}</script>')]:
    run(name, document(tail=tail), False)
for name, attrs, fallback in [('handler', 'width="100" height="50" ONcLiCk="x()"', 'text'),
                               ('fallback_script', 'width="100" height="50"', '<script>1</script>'),
                               ('fallback_img', 'width="100" height="50"', '<img src="external.png">'),
                               ('fallback_handler', 'width="100" height="50"', '<span OnLoAd="x()">text</span>')]:
    run(name, document(attrs=attrs, fallback=fallback), False)

# Authored rejection must not depend on computed fields that discard these declarations.
for index, declaration in enumerate([
    'writing-mode:vertical-rl', 'WRITING-MODE:horizontal-tb', r'writ\69 ng-mode:vertical-rl',
    'direction:ltr', 'direction/**/:rtl', r'd\69 rection:rtl', 'aspect-ratio:auto', r'aspect\2d ratio:2',
    'inline-size:10px', 'block-size:10px', 'min-inline-size:0px', 'max-block-size:none',
    'all:initial', 'min-width:auto', 'min-height:1px', 'max-width:100px', 'max-height:100px',
]):
    run('authored_' + str(index), document(css='.unmatched{' + declaration + '}'), False)
    run('inline_authored_' + str(index), document(tail='<div style="' + declaration + '"></div>'), False)
run('comment_not_declaration', document(css='/* direction:rtl; writing-mode:vertical-rl */ canvas{min-width:0px;max-width:none}'))
run('dir_html', document(attrs='width="100" height="50" dir="ltr"'), False)
for index, css in enumerate([
    'canvas{display:none}', 'canvas{display:contents}', '.row{display:none}',
    '.row{flex-direction:column}', '.row{flex-direction:row-reverse}', '.row{flex-wrap:wrap}',
    '.row{align-items:stretch}', '.row{align-items:baseline}',
    'canvas{align-self:stretch}', 'canvas{align-self:baseline}', 'canvas{align-self:flex-end}',
    'canvas{margin:auto}', 'canvas{position:absolute}', 'canvas{width:50%}',
    'canvas{flex-grow:1}', 'canvas{flex-basis:20px}', '.row{width:50px}',
    'canvas{transform:translateX(2px)}',
]):
    run('profile_' + str(index), document(css=css), False)
# Independent review regression: sibling border-box widths must include the
# decoration floor, not only authored width. Also prove final siblings did not
# shrink, and use typed column gap rather than the legacy scalar gap.
for index, css in enumerate([
    '.row{width:150px}.peer{box-sizing:border-box;width:1px;padding:100px;flex-shrink:1}',
    '.row{width:150px}.peer{box-sizing:border-box;width:1px;padding:100px;flex-shrink:0}',
    '.row{width:150px}.peer{box-sizing:border-box;width:1px;border:100px solid green}',
    '.row{width:150px}.peer{box-sizing:content-box;width:1px;padding:100px}',
    '.row{width:150px}.peer{box-sizing:border-box;width:1px;padding-left:200px}',
    '.row{width:150px}.peer{width:100px;flex-shrink:1}',
    '.row{width:150px;column-gap:40px}',
    '.row{width:150px;gap:0px 40px}',
    '.row{width:150px;column-gap:30%}',
    '.row{width:150px}.peer{margin-right:40px}',
]):
    run('review_no_deficit_' + str(index), document(css=css), False, 'CANVAS_LAYOUT_PROFILE_UNSUPPORTED')
run('review_floor_fits', document(css='.row{width:400px}.peer{box-sizing:border-box;width:1px;padding:100px}'))
run('review_column_gap_fits', document(css='.row{width:400px;column-gap:40px}'))
run('review_row_gap_not_main', document(css='.row{width:150px;gap:0px;row-gap:200px}'))

run('zero_ratio_auto', document(attrs='width="0" height="50"', css='canvas{width:20px;height:auto}'), False)
run('dimension_limit', document(attrs='width="8193" height="1"', css='canvas{width:1px;height:1px}'), False, 'CANVAS_DIMENSION_LIMIT')

# Same registry size for small and enormous intrinsic areas. No bitmap allocation.
registry = {}
for count in (1, 16, 256):
    for dim in (1, 8192):
        html = '<html><head><style>body{margin:0}.row{display:flex;flex-direction:row;align-items:flex-start;width:800px}canvas{width:1px;height:1px;flex-grow:0}</style></head><body><div class="row">'
        html += ('<canvas width="%s" height="%s">fallback</canvas>' % (dim, dim)) * count + '</div></body></html>'
        _, _, fields = run('many_%s_%s' % (count, dim), html)
        assert int(fields['canvas_entries']) == count
        assert int(fields['canvas_bitmap_bytes']) == 0 and int(fields['canvas_content_commands']) == 0
        size = int(fields['canvas_registry_bytes'])
        assert size > 0
        if count in registry:
            assert registry[count] == size
        registry[count] = size
assert registry[16] == registry[1] * 16 and registry[256] == registry[1] * 256
run('too_many', html.replace('</div>', '<canvas width="1" height="1"></canvas></div>'), False, 'CANVAS_COUNT_LIMIT')

fixture = pathlib.Path('tests/visual_compare/fixtures/flexbox_runtime_validation.html')
original = fixture.read_bytes()
assert hashlib.sha256(original).hexdigest() == 'eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837'
_, log, _ = run('original', original.decode(), False)
assert 'CSS_FUNCTION_UNSUPPORTED' in log and 'TEXT_RENDERER_UNAVAILABLE' in log
assert 'stage_started=layout' not in log
assert fixture.read_bytes() == original
(ROOT / 'summary.json').write_text(json.dumps(RESULTS, indent=2) + '\n')
print('PASS H3 integration:', len(RESULTS), 'real CLI cases; pixels, gates, limits, metrics, original unchanged')
if TEMP:
    TEMP.cleanup()
else:
    print('Evidence:', ROOT)
