#!/usr/bin/env python3
"""Build standalone pure tests from actual product and a pinned pre-change oracle.
No OOM instrumentation is linked into differential, poison or timing executables.
Generated copies only rename private symbols, poison scratch, or replace its allocator.
"""
import argparse, hashlib, json, os, pathlib, re, subprocess
p=argparse.ArgumentParser();p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--timing',action='store_true');p.add_argument('--build-only',action='store_true');p.add_argument('--cxx',default=os.environ.get('CXX','g++'));p.add_argument('--sanitize',action='store_true');a=p.parse_args()
here=pathlib.Path(__file__).resolve().parent;native=here.parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
base=(here/'baseline.cpp').read_text();product=(native/'src/layout/flex_resolver.cpp').read_text()
assert hashlib.sha256(base.split('\n',1)[1].encode()).hexdigest()=='9592fc592de63a2d4ae7cb79fe004357883f2df663720b58633ec8db9fe2ad53'
names=['aether_flex_resolve_base_size','aether_flex_resolve_automatic_minimum','aether_flex_resolve_hypothetical_size','aether_resolve_flexible_lengths']
def renamed(s,prefix):
 return ''.join('#define '+name+' '+prefix+name+'\n' for name in names)+s
sources={'baseline':renamed(base,'baseline_'),'model':renamed(product,'model_')}
anchor='if (violations.size() < line.item_indices.size())'
# Poison after resizing and before all per-iteration accesses.
poison=sources['model'];match=re.search(r'if \(violations.size\(\) < line.item_indices.size\(\)\)\s*\{?\s*violations.resize\(line.item_indices.size\(\)\);\s*\}?',poison)
assert match, 'Product scratch resize anchor changed: review generator before proceeding'
sources['poison']=poison[:match.end()]+'\n        std::fill(violations.begin(), violations.end(), std::numeric_limits<double>::quiet_NaN());\n'+poison[match.end():]
for label,source in [('baseline',base),('model',product)]:
 sources['ledger_'+label]='#include "ledger.h"\n'+renamed(source,'ledger_'+label+'_').replace('std::vector<double>','std::vector<double, LedgerAllocator<double>>')
for name,s in sources.items():(out/(name+'.cpp')).write_text(s)
flags=['-std=c++20','-O3','-DNDEBUG','-pthread','-ffp-contract=off','-I'+str(native/'include'),'-I'+str(native/'src/layout'),'-I'+str(here)]
if a.sanitize:flags=['-std=c++20','-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined','-pthread']+flags[4:]
manifest={'product_sha256':hashlib.sha256(product.encode()).hexdigest(),'baseline_sha256':hashlib.sha256(base.split('\n',1)[1].encode()).hexdigest(),'compiler':subprocess.check_output([a.cxx,'--version'],text=True).splitlines()[0],'flags':flags,'sources':{k:hashlib.sha256(v.encode()).hexdigest() for k,v in sources.items()}}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
programs={'differential':(['baseline','model'],'differential.cpp'),'poison':(['baseline','poison'],'differential.cpp'),'oom':(['model'],'oom.cpp'),'memory':(['ledger_baseline','ledger_model'],'memory.cpp')}
if a.timing:programs={'timing':(['baseline','model'],'timing.cpp')}
for name,(src,test) in programs.items():
 cmd=[a.cxx]+flags+[str(out/(s+'.cpp')) for s in src]+[str(here/test),'-o',str(out/name)]
 with (out/(name+'-build.log')).open('w') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
 if not a.build_only:
  for n in range(5 if a.timing else 1):
   with (out/(name+(f'-{n+1}' if a.timing else '')+'.log')).open('w') as log:subprocess.run([str(out/name)],stdout=log,stderr=subprocess.STDOUT,check=True)
   print(name,n+1,'PASS',flush=True)
