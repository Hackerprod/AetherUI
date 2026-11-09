#!/usr/bin/env python3
"""Offline Linux raster harness. Never starts a browser, listener, or event host."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import platform
import resource
import subprocess
import sys
from xml.sax.saxutils import escape

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

class StageError(Exception):
    def __init__(self, message, verdict="BLOCKED"):
        super().__init__(message)
        self.verdict = verdict

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dotnet', default='dotnet')
    parser.add_argument('--output', type=Path, required=True, help='New directory outside the checkout')
    parser.add_argument('--font-family', default='OpenAI Sans')
    args = parser.parse_args()
    out = args.output.resolve()
    if out == ROOT or ROOT in out.parents:
        parser.error('Use an output directory outside the checkout.')
    if out.exists():
        parser.error('Output must not already exist; stale evidence is never reused.')
    out.mkdir(parents=True)
    env = dict(os.environ)
    env.pop('DISPLAY', None)
    env.pop('WAYLAND_DISPLAY', None)
    env.update(DOTNET_CLI_TELEMETRY_OPTOUT='1', DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1', DOTNET_CLI_HOME=str(out / 'dotnet-home'),
               DOTNET_CLI_USE_MSBUILD_SERVER='0', XDG_CACHE_HOME=str(out / 'cache'), FONTCONFIG_FILE=str(out / 'fonts.conf'))
    (out / 'cache').mkdir()
    (out / 'fonts.conf').write_text('<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd"><fontconfig>'
        f'<cachedir>{escape(str(out / "cache"))}</cachedir>'
        '<include>/etc/fonts/fonts.conf</include><config><rescan><int>0</int></rescan></config></fontconfig>')
    (out / 'NuGet.Config').write_text('<configuration><packageSources><clear /></packageSources></configuration>')
    runs = []
    def run(label, command, timeout=180):
        def limits():
            resource.setrlimit(resource.RLIMIT_CPU, (180, 180))
            resource.setrlimit(resource.RLIMIT_FSIZE, (256 * 1024 * 1024, 256 * 1024 * 1024))
        try:
            p = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, timeout=timeout, preexec_fn=limits)
            code, stdout, stderr = p.returncode, p.stdout, p.stderr
        except subprocess.TimeoutExpired as e:
            code, stdout, stderr = 124, e.stdout or b'', (e.stderr or b'') + b'\nTIMEOUT\n'
        except OSError as e:
            code, stdout, stderr = 127, b'', str(e).encode()
        (out / (label + '.stdout')).write_bytes(stdout)
        (out / (label + '.stderr')).write_bytes(stderr)
        runs.append({'step': label, 'exitCode': code})
        return code, stdout.decode(errors='replace').strip()
    def native_version(lib, symbol):
        try:
            fn = getattr(ctypes.CDLL(lib), symbol)
            fn.restype = ctypes.c_char_p
            return fn().decode()
        except (OSError, AttributeError) as e:
            return 'unavailable: ' + type(e).__name__
    _, commit = run('commit', ['git', 'rev-parse', 'HEAD'])
    _, tree = run('tree', ['git', 'rev-parse', 'HEAD^{tree}'])
    _, state = run('worktree', ['git', 'status', '--porcelain'])
    _, sdk = run('sdk', [args.dotnet, '--version'])
    code, fonts = run('fonts', ['fc-list', '-f', '%{file}\n'])
    font_files = sorted(set(fonts.splitlines())) if code == 0 else []
    pinned = [{'name': Path(f).name, 'sha256': digest(Path(f))} for f in font_files if Path(f).is_file()]
    font_code, selected = run('font-match', ['fc-match', args.font_family, '-f', '%{family}\n%{file}\n'])
    selected_lines = selected.splitlines()
    font_ok = font_code == 0 and len(selected_lines) == 2 and args.font_family.casefold() in [s.strip().casefold() for s in selected_lines[0].split(',')]
    metadata = {'schema': 1, 'commit': commit, 'tree': tree, 'dirty': bool(state), 'os': platform.platform(),
        'architecture': platform.machine(), 'byteOrder': sys.byteorder, 'sdk': sdk,
        'cairo': native_version('libcairo.so.2', 'cairo_version_string'),
        'pango': native_version('libpango-1.0.so.0', 'pango_version_string'),
        'fontFamily': args.font_family, 'fontFamilyResolved': font_ok, 'availableFontFiles': pinned,
        'fontConfigSha256': digest(out / 'fonts.conf'), 'chromium': 'BLOCKED: not invoked',
        'viewportCss': [256, 192], 'dpi': [1, 2], 'repeats': 3,
        'catalogSourceSha256': digest(HERE / 'Fixtures.cs'),
        'sourceHashes': {str(p.relative_to(ROOT)): digest(p) for p in sorted(HERE.glob('*')) if p.is_file()},
        'fontConfigFiles': {str(p.relative_to(Path('/etc/fonts'))): digest(p) for p in sorted(Path('/etc/fonts').rglob('*')) if p.is_file()},
        'limits': {'processCpuSeconds': 180, 'processWallSeconds': 180, 'fileBytes': 268435456,
                   'maximumFixtureSurfaceBytes': 786432}, 'runs': runs}
    exit_code = 2
    try:
        if platform.system() != 'Linux' or platform.machine() != 'x86_64' or sys.byteorder != 'little' or not font_ok:
            raise StageError('Required Linux x64 little-endian environment or explicit installed font unavailable.')
        project = HERE / 'AetherUI.Linux.VisualTests.csproj'
        regression = HERE.parent / 'AetherUI.Linux.RegressionTests' / 'AetherUI.Linux.RegressionTests.csproj'
        for label, proj in [('visual', project), ('regression', regression)]:
            code, _ = run(label + '-restore', [args.dotnet, 'restore', str(proj), '--configfile', str(out / 'NuGet.Config'), '--disable-parallel'])
            if code: raise StageError(label + ' offline restore failed', 'BLOCKED' if code in (124,127) else 'FAIL')
            code, _ = run(label + '-build', [args.dotnet, 'build', str(proj), '-c', 'Release', '--no-restore', '--disable-build-servers', '-m:1', '-p:AetherSkipNativeLibraryValidation=true'])
            if code: raise StageError(label + ' build failed', 'BLOCKED' if code in (124,127) else 'FAIL')
        exit_code, _ = run('visual-run', [args.dotnet, str(HERE / 'bin/Release/net10.0/AetherUI.Linux.VisualTests.dll'), str(out / 'captures'), args.font_family])
        regression_code, _ = run('regression-run', [args.dotnet, str(regression.parent / 'bin/Release/net10.0/AetherUI.Linux.RegressionTests.dll')])
        metadata['regression'] = 'PASS' if regression_code == 0 else 'BLOCKED' if regression_code in (124,127) else 'FAIL'
        if regression_code: exit_code = 1
        if not (out / 'captures/summary.json').is_file(): raise StageError('Visual run produced no summary', 'BLOCKED' if exit_code in (124,127) else 'FAIL')
        summary = json.loads((out / 'captures/summary.json').read_text())
        metadata['linuxRaster'] = summary['linux']
        outcomes = [summary['linux'], metadata['regression']]
        metadata['linux'] = next((v for v in ['FAIL', 'BLOCKED', 'NOT_RUN'] if v in outcomes), 'PASS')
        if metadata['linux'] != 'PASS': exit_code = 1 if metadata['linux'] == 'FAIL' else 2
        metadata['chromium'] = summary['chromium']
        # Optional contact sheet only; captures and PNGs require no Python image package.
        try:
            from PIL import Image, ImageDraw
            pictures = sorted((out / 'captures').glob('*-run0.png'))
            sheet = Image.new('RGB', (4 * 272, ((len(pictures) + 3) // 4) * 224), '#777777')
            draw = ImageDraw.Draw(sheet)
            for i, path in enumerate(pictures):
                im = Image.open(path).convert('RGBA'); im.thumbnail((256, 192))
                x, y = (i % 4) * 272, (i // 4) * 224
                sheet.paste(im, (x, y), im); draw.text((x, y + 194), path.stem, fill='white')
            sheet.save(out / 'contact-sheet.png')
            metadata['contactSheet'] = 'PASS'
        except ImportError:
            metadata['contactSheet'] = 'NOT_RUN: optional Pillow unavailable'
        (out / 'README.txt').write_text(f"Linux raster: {summary['linux']}\nChromium: {summary['chromium']}\n"
            f"Existing offscreen regression: {metadata['regression']}\n"
            'See captures/summary.json and manifest.json. No browser, DOM/layout, events, window or full conformance validated.\n'
            'Identical repetitions demonstrate determinism only. Text tests are coverage smoke tests, not glyph correctness.\n')
    except Exception as e:
        verdict = e.verdict if isinstance(e, StageError) else 'FAIL'
        exit_code = 2 if verdict == 'BLOCKED' else 1
        metadata['failureReason'] = str(e)
        metadata['linux'] = verdict
        metadata.setdefault('regression', 'NOT_RUN')
        (out / 'README.txt').write_text(verdict + ': ' + str(e) + '\nChromium BLOCKED; no complete conformance.\n')
        print(str(e), file=sys.stderr)
    finally:
        metadata['exitCode'] = exit_code
        metadata['artifactHashes'] = {str(p.relative_to(out)): digest(p) for p in sorted((out / 'captures').glob('*')) if p.is_file()}
        (out / 'manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(out)
    return exit_code

if __name__ == '__main__':
    sys.exit(main())
