#!/usr/bin/env python3
"""Clean candidate checkpoint, validated with GCC; not CMake/shared/install certification.

Exit 1 remains mandatory: the historical classifier fails compilation and the
aggregate gate requires independent review even when all selected runs complete.
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys

TESTS = ['style_semantic_diff', 'gradient_angle_runtime', 'length_locale',
         'style_diff_classifier', 'style_diff_classifier_inventory20',
         'style_diff_classifier_inventory20:wrap', 'style_diff_catalog',
         'typography_same_stage', 'typography_same_stage_runtime',
         'typography_same_stage_coverage', 'typography_minimal_channels',
         'style_semantic_diff_oom', 'frozen_backing', 'frozen_backing_oom',
         'immutable_style_revision', 'immutable_style_handle', 'frozen_style',
         'frozen_style_oom', 'style_map_lifetime', 'software_renderer',
         'margin_adjacency', 'auto_margins', 'positioned_auto_margins',
         'scroll_metrics', 'scroll_model']
ORIGINALS = {
    'tests/test_style_diff_classifier.cpp': '1f569dad8b4b716f511a872354288f959ea59df18c5bc78d66f2c2852729fe4d',
    'tests/test_style_diff_catalog.cpp': '7cc36703f9401a77143378f0b6796f8c1e096c387b6057b524b936398ca0f5c1',
}

class CheckpointFailure(Exception):
    pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True)
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    native = pathlib.Path(__file__).resolve().parents[1]
    out = pathlib.Path(args.out).resolve()
    if out == native or native in out.parents:
        raise SystemExit('Output must be outside native source tree')
    out.mkdir(parents=True, exist_ok=False)
    rows = []
    before = None
    primary = None
    final_errors = []
    reached_end = False

    def save(name, data):
        (out / name).write_text(json.dumps(data, indent=2))

    def hashes():
        return {str(p.relative_to(native)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(native.rglob('*')) if p.is_file()}

    def run(name, command):
        print(name, flush=True)
        row = {'step': name, 'argv': command, 'returncode': None}
        try:
            with (out / (name + '.log')).open('wb') as log:
                result = subprocess.run(command, cwd=native, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=300)
            row['returncode'] = result.returncode
            row['status'] = 'PASS' if result.returncode == 0 else 'FAIL'
            if name == 'compile-style_diff_classifier':
                diagnostic = (out / (name + '.log')).read_text()
                exact = (result.returncode != 0 and 'static assertion failed' in diagnostic
                         and '(20 == 19)' in diagnostic and diagnostic.count('error:') == 1)
                if exact:
                    row['status'] = 'BASELINE_COMPILE_FAIL'
                elif result.returncode == 0:
                    row['status'] = 'UNEXPECTED_COMPILE_SUCCESS'
        except subprocess.TimeoutExpired as exc:
            row.update(status='TIMEOUT', timeout_seconds=exc.timeout)
        except KeyboardInterrupt:
            row['status'] = 'INTERRUPTED'
        except OSError as exc:
            row.update(status='ERROR', error=str(exc))
        finally:
            rows.append(row)
            save('results.json', rows)
        if row['status'] not in {'PASS', 'BASELINE_COMPILE_FAIL'}:
            raise CheckpointFailure(name + ': ' + row['status'])
        return row['status']

    try:
        before = hashes()
        save('source-hashes.json', before)
        save('matrix-before-execution.json', [{'test': t, 'status': 'NOT_RUN'} for t in TESTS])
        flags = ['-std=c++20', '-O0', '-g0', '-ffp-contract=off', '-UNDEBUG',
                 '-DAETHER_EFFECT_CAPTURE=0', '-DAETHER_REUSABLE_PAYLOAD_CAPTURE=0',
                 '-DAETHER_HOVER_PROBE=0', '-I' + str(native / 'include'),
                 '-I' + str(native / 'src'),
                 '-I' + str(native / 'src/rendering/preparation/include'), '-pthread']
        save('provenance.json', {'recipe_sha256': hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
                                'cxx': args.cxx, 'out': str(out), 'native': str(native),
                                'flags': flags, 'profiles': TESTS, 'invocation': sys.argv,
                                'toolchain_log': 'toolchain.log'})
        run('toolchain', [args.cxx, '--version'])
        for path, expected in ORIGINALS.items():
            if before.get(path) != expected:
                raise CheckpointFailure('Original identity mismatch: ' + path)
        sources = re.search(r'add_library\(aether_core SHARED\s+(.*?)\n\)',
                            (native / 'CMakeLists.txt').read_text(), re.S).group(1).split()
        objects = {}
        for i, source in enumerate(sources):
            obj = out / ('core-' + str(i) + '.o')
            run('compile-core-' + str(i), [args.cxx, *flags, '-c', str(native / source), '-o', str(obj)])
            objects[source] = str(obj)
        extras = []
        for i, source in enumerate(['native_frame.cpp', 'preparation/paint_snapshot.cpp', 'software/software_renderer.cpp']):
            obj = out / ('extra-' + str(i) + '.o')
            run('compile-extra-' + str(i), [args.cxx, *flags, '-c', str(native / 'src/rendering' / source), '-o', str(obj)])
            extras.append(str(obj))
        for name in TESTS:
            test = name.split(':')[0]
            wrap = name.endswith(':wrap') or test == 'style_diff_classifier'
            source = native / 'tests' / ('test_' + test + '.cpp')
            variant = ['-DAETHER_DIFF_WRAP_MALLOC'] if wrap else []
            if test == 'auto_margins':
                variant += ['-I' + str(native / 'src/layout')]
            obj = out / (name + '.o')
            status = run('compile-' + name, [args.cxx, *flags, *variant, '-c', str(source), '-o', str(obj)])
            if status == 'BASELINE_COMPILE_FAIL':
                continue
            included = '../src/style_runtime/style_runtime_core.cpp' in source.read_text()
            link = [value for key, value in objects.items()
                    if not (included and key == 'src/style_runtime/style_runtime_core.cpp')]
            if test == 'software_renderer':
                link += extras
            wraps = ['-Wl,--wrap=malloc', '-Wl,--wrap=free'] if test in {
                'frozen_backing_oom', 'immutable_style_revision', 'immutable_style_handle', 'frozen_style_oom'} else []
            if wrap:
                wraps = ['-Wl,--wrap=malloc']
            executable = out / name
            run('link-' + name, [args.cxx, str(obj), *link, *wraps, '-pthread', '-o', str(executable)])
            run('test-' + name, [str(executable)])
        reached_end = True
    except KeyboardInterrupt:
        primary = {'type': 'KeyboardInterrupt', 'message': 'Interrupted outside subprocess'}
    except Exception as exc:
        primary = {'type': type(exc).__name__, 'message': str(exc)}
    finally:
        unchanged = False
        try:
            after = hashes()
            unchanged = before is not None and before == after
            save('source-integrity-final.json', {'unchanged': unchanged, 'files': len(after)})
            if not unchanged:
                final_errors.append('Source integrity not verified')
        except Exception as exc:
            final_errors.append('Integrity error: ' + str(exc))
        steps = {row['step']: row for row in rows}
        matrix = []
        for name in TESTS:
            entry = {'test': name}
            for prefix, label in [('compile', 'compile'), ('link', 'link'), ('test', 'run')]:
                step = steps.get(prefix + '-' + name)
                entry[label] = step['status'] if step else 'NOT_RUN'
                entry[label + '_exit'] = step['returncode'] if step else None
            entry['status'] = ('BASELINE_COMPILE_FAIL' if entry['compile'] == 'BASELINE_COMPILE_FAIL'
                               else 'PASS' if all(entry[k] == 'PASS' for k in ['compile', 'link', 'run'])
                               else 'INCOMPLETE_OR_FAIL')
            matrix.append(entry)
        complete = (reached_end and primary is None and not final_errors and unchanged
                    and all(e['status'] == ('BASELINE_COMPILE_FAIL' if e['test'] == 'style_diff_classifier' else 'PASS') for e in matrix))
        try:
            save('matrix-final-derived.json', matrix)
            save('summary.json', {'completed_selected_executions': complete, 'aggregate_exit': 1,
                                  'gate': 'PENDING_INDEPENDENT_REVIEW' if complete else 'BLOCKED',
                                  'primary_failure': primary, 'finalization_errors': final_errors})
        except Exception as exc:
            print('Evidence finalization error: ' + str(exc), file=sys.stderr)
            complete = False
        if complete:
            print('Selected executions completed; original classifier BASELINE_COMPILE_FAIL; final gate pending review')
        else:
            print('Checkpoint incomplete or failed: ' + str(primary) + '; ' + str(final_errors), file=sys.stderr)
    return 1


if __name__ == '__main__':
    raise SystemExit(main())
