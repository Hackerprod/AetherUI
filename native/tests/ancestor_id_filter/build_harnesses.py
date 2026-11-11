#!/usr/bin/env python3
"""Build ONLY harnesses; prerequisite standard Release shared arm is not rebuilt.
Explicit compiler/flags must match the separately recorded arm build manifest.
Private matcher/parser are separate translation units and LTO is disabled.
"""
import argparse, hashlib, json, pathlib, subprocess
HERE=pathlib.Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=pathlib.Path,required=True,help='arm repository root')
p.add_argument('--library-dir',type=pathlib.Path,required=True)
p.add_argument('--output',type=pathlib.Path,required=True)
p.add_argument('--compiler',default='g++')
p.add_argument('--candidate',action='store_true')
a=p.parse_args();src=a.source.resolve();lib=a.library_dir.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
flags=['-std=gnu++20','-O3','-DNDEBUG','-fPIC','-fvisibility=hidden','-fvisibility-inlines-hidden','-ffp-contract=off','-Wall','-Wextra','-pedantic','-fno-lto']
include=['-I'+str(src/'native/include'),'-I'+str(src/'native/src/style_runtime')]
link=['-L'+str(lib),'-Wl,-rpath,'+str(lib),'-laether_core']
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
commands=[];objects={}
for kind,files in [('public',[HERE/'public_control.cpp']),('private',[HERE/'private_control.cpp',src/'native/src/style_runtime/selector_matcher.cpp',src/'native/src/style_runtime/selector_parser.cpp'])]:
 object_dir=out/(kind+'-objects');object_dir.mkdir();kind_objects=[]
 for source in files:
  obj=object_dir/(source.stem+'.o')
  command=[a.compiler,*flags,*include,*(['-DAETHER_ANCESTOR_FILTER_CANDIDATE'] if a.candidate and kind=='private' else []),'-c',str(source),'-o',str(obj)]
  commands.append(command)
  result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
  (object_dir/(source.stem+'-build.log')).write_bytes(result.stdout)
  (out/'commands.json').write_text(json.dumps(commands,indent=2))
  if result.returncode:raise SystemExit(result.returncode)
  kind_objects.append(obj)
  objects[str(obj.relative_to(out))]={'sha256':digest(obj),'source':str(source),'source_sha256':digest(source),'command':command}
 command=[a.compiler,*flags,*map(str,kind_objects),*link,'-o',str(out/kind)]
 commands.append(command)
 result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 (out/(kind+'-link.log')).write_bytes(result.stdout)
 (out/'commands.json').write_text(json.dumps(commands,indent=2))
 if result.returncode:raise SystemExit(result.returncode)
library=(lib/'libaether_core.so').resolve(strict=True)
manifest={'tooling_revision':2,'commands':commands,'candidate':a.candidate,'compiler':subprocess.check_output([a.compiler,'--version'],text=True),'flags':flags,'objects':objects,'library_path':str(library),'library_sha256':digest(library),'binaries':{k:digest(out/k) for k in ('public','private')},'sources':{str(f):digest(f) for f in (HERE/'public_control.cpp',HERE/'private_control.cpp',src/'native/src/style_runtime/selector_match_context.h',src/'native/src/style_runtime/selector_matcher.cpp',src/'native/src/style_runtime/selector_parser.cpp')}}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
