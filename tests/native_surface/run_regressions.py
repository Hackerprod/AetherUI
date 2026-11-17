#!/usr/bin/env python3
"""Regression executables use authentic private objects; public ABI tests use DSO only."""
import json,pathlib,subprocess,sys
out=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'build').resolve(); tests=out/'regressions'; tests.mkdir(exist_ok=True)
flags=['-std=c++20','-O2','-g','-ffp-contract=off','-pthread','-Inative/include','-Inative/src/rendering/preparation/include','-Itests/visual_compare/include']
objs=sorted(map(str,(out/'objects').glob('*.o')))
metrics=tests/'metrics.o'; subprocess.run(['g++',*flags,'-c','native/src/rendering/preparation/preparation_metrics.cpp','-o',str(metrics)],check=True)
sources=['native/tests/test_native_frame.cpp','native/tests/test_native_preparation.cpp','native/tests/test_native_text_snapshot.cpp']
sources+=['native/tests/test_synthetic_a8.cpp','tests/isolated_capture/g1_gradient_tests.cpp','tests/isolated_capture/g1_gradient_native_tests.cpp','tests/isolated_capture/g1_gradient_oom_tests.cpp','tests/isolated_capture/g2_translation_tests.cpp','tests/isolated_capture/g2_independent_tests.cpp']
sources+=sorted(map(str,pathlib.Path('native/tests').glob('test_software_*.cpp')))
sources+=['tests/visual_compare/tests/test_paint_snapshot.cpp','tests/visual_compare/tests/test_aether_command_source.cpp','tests/visual_compare/tests/test_preparation_compat.cpp','tests/visual_compare/tests/test_preparation_metrics.cpp']
results=[]
for src in sources:
 name=pathlib.Path(src).stem; exe=tests/name
 with (tests/(name+'.log')).open('w') as log:
  test_extra=['native/tests/r8_reference.cpp'] if name=='test_software_bands' else []
  test_objs=[o for o in objs if not (name=='test_software_rounded_emitter' and o.endswith('src_rendering_display_list_builder.cpp.o'))]
  cmd=['g++',*flags,src,*test_extra,*test_objs,str(metrics),'-o',str(exe)]
  built=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT).returncode
  code=subprocess.run([str(exe)],stdout=log,stderr=subprocess.STDOUT,cwd=tests).returncode if built==0 else None
 results.append({'test':src,'link':'private authentic production objects + test-only metrics; bands adds r8 reference; rounded emitter includes authentic builder TU instead of builder object','build':built,'run':code}); print(name,'PASS' if code==0 else 'FAIL',flush=True)
for src in ['native/tests/test_abi_c.c','native/tests/test_contextual_abi.c','native/tests/test_smoke.cpp']:
 name=pathlib.Path(src).stem; exe=tests/name
 cmd=(['gcc','-std=c11','-Werror','-Inative/include'] if src.endswith('.c') else ['g++',*flags])+[src,'-L'+str(out),'-Wl,-rpath,'+str(out),'-laether_core','-o',str(exe)]
 with (tests/(name+'.log')).open('w') as log:
  built=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT).returncode
  code=subprocess.run([str(exe)],stdout=log,stderr=subprocess.STDOUT,cwd=tests).returncode if built==0 else None
 results.append({'test':src,'link':'shared DSO only','build':built,'run':code}); print(name,'PASS' if code==0 else 'FAIL',flush=True)
(tests/'results.json').write_text(json.dumps(results,indent=2)+'\n')
sys.exit(any(x['run']!=0 for x in results))
