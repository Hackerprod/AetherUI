#!/usr/bin/env python3
"""Offline artifacts. No browser launch, no HTML execution, no native raster claims."""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import sys
import time


def compare_rgba(width: int, height: int, reference: bytes, candidate: bytes, tolerance: int = 0) -> dict:
    if width <= 0 or height <= 0 or not 0 <= tolerance <= 255:
        raise ValueError('positive image dimensions and tolerance 0..255 are required')
    size = width * height * 4
    if len(reference) != size or len(candidate) != size:
        raise ValueError('RGBA buffer size does not match dimensions')
    different = total = squared = maximum = 0
    diff = bytearray(size)
    for offset in range(0, size, 4):
        delta = [abs(reference[offset + c] - candidate[offset + c]) for c in range(4)]
        different += max(delta) > tolerance
        total += sum(delta)
        squared += sum(v * v for v in delta)
        maximum = max(maximum, max(delta))
        # Magenta diagnostic intensity includes alpha-only changes; it is not a screenshot.
        value = max(delta)
        diff[offset:offset + 4] = bytes((value, 0, value, 255))
    return {'width': width, 'height': height, 'channels': 'RGBA', 'pixels': width * height,
            'different_pixels': different, 'different_fraction': different / (width * height),
            'pixel_tolerance': tolerance, 'mae': total / size,
            'rmse': math.sqrt(squared / size), 'max_channel_delta': maximum, 'diff_bytes': bytes(diff)}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def diff_images(reference: Path, candidate: Path, output: Path, tolerance: int, allowed_fraction: float) -> dict:
    from PIL import Image
    if reference.resolve() == candidate.resolve():
        raise ValueError('reference and candidate must be distinct files')
    if not 0 <= allowed_fraction <= 1:
        raise ValueError('max-different-fraction must be 0..1')
    started = time.perf_counter()
    with Image.open(reference) as r, Image.open(candidate) as c:
        if r.size != c.size:
            raise ValueError(f'viewport mismatch: {r.size} versus {c.size}; images will not be resized')
        if r.width * r.height > 32_000_000:
            raise ValueError('input exceeds 32 megapixel diagnostic limit')
        result = compare_rgba(r.width, r.height, r.convert('RGBA').tobytes(), c.convert('RGBA').tobytes(), tolerance)
        output.mkdir(parents=True, exist_ok=True)
        Image.frombytes('RGBA', r.size, result.pop('diff_bytes')).save(output / 'diff.png')
    result.update(status='match' if result['different_fraction'] <= allowed_fraction else 'difference',
                  max_different_fraction=allowed_fraction, reference_sha256=sha(reference),
                  candidate_sha256=sha(candidate), elapsed_ms=(time.perf_counter() - started) * 1000,
                  scope='pixel comparison only; renderer identity, DPI, fonts, resources and state require independent verification')
    (output / 'report.log').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return result


def geometry_image(source: Path, output: Path, label: str) -> dict:
    from PIL import Image, ImageDraw, ImageFont
    with source.open(newline='', encoding='utf-8') as stream:
        rows = list(csv.DictReader(stream))
    for row in rows:
        for key in ('x', 'y', 'width', 'height'):
            row[key] = float(row[key])
            if not math.isfinite(row[key]):
                raise ValueError(f'non-finite geometry in {key}')
        if row['width'] < 0 or row['height'] < 0:
            raise ValueError('negative geometry size')
    image = Image.new('RGB', (1600, 1080), '#0b1220')
    draw = ImageDraw.Draw(image)
    try:
        title = ImageFont.truetype('DejaVuSans.ttf', 28)
        body = ImageFont.truetype('DejaVuSans.ttf', 18)
        small = ImageFont.truetype('DejaVuSans.ttf', 15)
    except OSError:
        title = body = small = ImageFont.load_default()
    draw.text((40, 30), 'AetherUI | Geometría nativa Flexbox', font=title, fill='#e5edf9')
    draw.text((40, 78), label + ' | Coordenadas del pipeline C++ real; texto medido de forma aproximada.', font=body, fill='#93c5fd')
    draw.text((40, 112), 'DIAGNÓSTICO DE CAJAS. NO ES RASTER DIRECT2D NI CAPTURA DE CHROMIUM.', font=body, fill='#fbbf24')
    palette = ['#60a5fa', '#34d399', '#fbbf24', '#c084fc', '#fb7185', '#22d3ee']
    cases = ['FLEX-001', 'FLEX-022', 'FLEX-023', 'FLEX-024', 'FLEX-025']
    exported = 0
    for index, case in enumerate(cases):
        x0 = 40 + (index % 2) * 780
        y0 = 165 + (index // 2) * 290
        draw.rounded_rectangle((x0, y0, x0 + 740, y0 + 260), radius=12, fill='#152238', outline='#334155', width=2)
        subset = [r for r in rows if r['case_id'] == case and
                  ('flex-target' in r['class'].split() or 'item' in r['class'].split())]
        draw.text((x0 + 18, y0 + 12), case, font=body, fill='#e5edf9')
        if not subset:
            draw.text((x0 + 18, y0 + 60), 'Sin geometría exportada. Revisar report.log.', font=body, fill='#fb7185')
            continue
        left = min(r['x'] for r in subset); top = min(r['y'] for r in subset)
        right = max(r['x'] + r['width'] for r in subset); bottom = max(r['y'] + r['height'] for r in subset)
        scale = min(680 / max(right - left, 1), 145 / max(bottom - top, 1), 1.5)
        draw.text((x0 + 18, y0 + 40), f"Origen ({left:.1f}, {top:.1f}), escala del diagrama {scale:.2f}x", font=small, fill='#94a3b8')
        item_number = 0
        for i, row in enumerate(subset):
            x = x0 + 25 + (row['x'] - left) * scale
            y = y0 + 76 + (row['y'] - top) * scale
            w, h = row['width'] * scale, row['height'] * scale
            color = palette[i % len(palette)]
            draw.rectangle((x, y, x + w, y + h), outline=color, width=2)
            if 'item' in row['class'].split() and w > 34 and h > 14:
                item_number += 1
                draw.text((x + 4, y + 4), str(item_number), font=small, fill=color)
            exported += 1
        sizes = ' | '.join(f"{r['width']:.0f}×{r['height']:.0f}" for r in subset if 'item' in r['class'].split())
        draw.text((x0 + 18, y0 + 235), 'Tamaños de items: ' + sizes, font=small, fill='#94a3b8')
    draw.text((40, 1040), 'Cobertura semántica limitada a estos cinco casos. La página contiene 403 ejemplos.', font=small, fill='#94a3b8')
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)
    return {'status': 'diagnostic-only', 'source_sha256': sha(source), 'source_rows': len(rows),
            'exported_boxes': exported, 'raster_parity': 'not-run', 'file': output.name}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    diff = commands.add_parser('diff', help='Compare two real, independently captured PNGs without resizing')
    diff.add_argument('--reference', type=Path, required=True); diff.add_argument('--candidate', type=Path, required=True)
    diff.add_argument('--output', type=Path, required=True)
    diff.add_argument('--pixel-tolerance', type=int, default=0)
    diff.add_argument('--max-different-fraction', type=float, default=0)
    geometry = commands.add_parser('geometry', help='Draw labelled diagnostic boxes from native CSV, not a screenshot')
    geometry.add_argument('--source', type=Path, required=True); geometry.add_argument('--output', type=Path, required=True)
    geometry.add_argument('--label', default='Viewport registrado en report.log')
    args = parser.parse_args(argv)
    try:
        if args.command == 'diff':
            result = diff_images(args.reference, args.candidate, args.output, args.pixel_tolerance, args.max_different_fraction)
        else:
            result = geometry_image(args.source, args.output, args.label)
        print(json.dumps(result, ensure_ascii=False))
        return 1 if result['status'] == 'difference' else 0
    except (ValueError, OSError, ImportError) as error:
        print(json.dumps({'status': 'blocked', 'error': str(error), 'visualParity': 'not-run'}), file=sys.stderr)
        return 2

if __name__ == '__main__':
    raise SystemExit(main())
