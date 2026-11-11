#!/usr/bin/env python3
"""Link diagnostic-only shared core, replacing only the counted matcher object.
Usage: link_core.py standard-cmake-build counted-matcher.o output-directory
All other objects/flags are from the standard build; no timings are comparable.
"""
import pathlib,shlex,subprocess,sys
build,obj,out=map(lambda x:pathlib.Path(x).resolve(),sys.argv[1:])
out.mkdir(parents=True,exist_ok=True)
args=shlex.split((build/'CMakeFiles/aether_core.dir/link.txt').read_text())
args=[x for x in args if not x.startswith('-Wl,--dependency-file=')]
args[args.index('-o')+1]=str(out/'libaether_core.so.0.1.0')
needle='CMakeFiles/aether_core.dir/src/style_runtime/selector_matcher.cpp.o'
assert args.count(needle)==1
args[args.index(needle)]=str(obj)
(out/'link-command.txt').write_text(shlex.join(args)+'\n')
result=subprocess.run(args,cwd=build,capture_output=True,text=True)
(out/'link.log').write_text(result.stdout+result.stderr)
result.check_returncode()
for name in ['libaether_core.so.0','libaether_core.so']:
    path=out/name
    if not path.exists():path.symlink_to('libaether_core.so.0.1.0')
