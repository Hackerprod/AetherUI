#!/usr/bin/env python3
"""H13 finite canvas constraints; PNG controls are native, not Chromium."""
import pathlib
exec(pathlib.Path(__file__).with_name('test_h3.py').read_text().split('# Intrinsic/content and border-box dimensions;')[0])
BASE += 'canvas{width:100px;height:50px}'
checks=0
for name,css,w,h,extra in [
 ('min','min-width:140px;min-height:80px',140,80,''),
 ('max','max-width:60px;max-height:30px',60,30,''),
 ('min_wins','min-width:140px;max-width:60px;min-height:70px;max-height:20px',140,70,''),
 ('inactive','min-width:20px;max-width:150px;min-height:10px;max-height:90px',100,50,''),
 ('zero','max-width:0px;max-height:0px',0,0,''),
 ('padding','min-width:140px;max-height:30px',140,30,'padding:3px;'),
 ('borderbox','min-width:140px;max-height:30px',140,30,'padding:3px;box-sizing:border-box;'),
 ('floor','max-width:2px;max-height:2px',2,2,'padding:3px;box-sizing:border-box;'),
 ('none','min-width:140px;max-height:30px',140,30,'border:20px none red;box-sizing:border-box;'),
 ('hidden','min-width:140px;max-height:30px',140,30,'border:20px hidden red;box-sizing:border-box;'),
 ('escape',r'min-w\69 dth:140px;max-height:30px',140,30,''),
 ('cascade','min-width:120px;min-width:140px!important;min-width:160px;max-height:30px',140,30,''),
]:
 a,_,_=run(name,document(css='canvas{'+extra+css+'}'))
 b,_,_=run(name+'_control',document(css='canvas{'+extra+f'width:{w}px;height:{h}px'+'}'))
 assert (a/'aether.png').read_bytes()==(b/'aether.png').read_bytes(),name
 checks+=1
for name,css,control in [
 ('parent_min','.row{width:400px;min-width:500px}', '.row{width:500px}'),
 ('parent_max','.row{width:800px;max-width:500px}', '.row{width:500px}'),
 ('parent_height','.row{height:100px;min-height:180px}', '.row{height:180px}'),
 ('sibling_min','.peer{min-width:50px}', '.peer{width:50px}'),
 ('sibling_max','.peer{max-width:10px}', '.peer{width:10px}'),
 ('sibling_conflict','.peer{min-width:50px;max-width:10px}', '.peer{width:50px}'),
]:
 a,_,_=run(name,document(css=css));b,_,_=run(name+'_control',document(css=control))
 assert (a/'aether.png').read_bytes()==(b/'aether.png').read_bytes(),name
 checks+=1
# HTML dimensions do not make CSS auto dimensions explicit; when CSS really is
# explicit, zero or a different intrinsic ratio must not transfer an aspect ratio.
for name,attrs in [('zero_intrinsic','width="0" height="0"'),('different_ratio','width="300" height="10"')]:
 a,_,_=run(name,document(attrs=attrs,css='canvas{min-width:140px;max-height:30px}'))
 b,_,_=run(name+'_control',document(attrs=attrs,css='canvas{width:140px;height:30px}'))
 assert (a/'aether.png').read_bytes()==(b/'aether.png').read_bytes();checks+=1
# Inline source is checked using exactly the same limited grammar.
a,_,_=run('inline_limit',document(attrs='width="100" height="50" style="min-width:140px;max-height:30px"'))
b,_,_=run('inline_limit_control',document(css='canvas{width:140px;height:30px}'))
assert (a/'aether.png').read_bytes()==(b/'aether.png').read_bytes();checks+=1
for name,css in [
 ('canvas_var','canvas{--w:100px;width:var(--w);max-width:60px}'),
 ('escaped_unit',r'canvas{min-width:140p\78}'),
 ('auto_width','canvas{width:auto;min-width:140px}'),
 ('auto_height','canvas{height:auto;max-height:30px}'),
 ('auto_both','canvas{width:auto;height:auto;min-height:20px}'),
 ('percentage','canvas{min-width:10%}'),
 ('math','canvas{min-width:calc(10px + 2px)}'),
 ('var_limit','canvas{--w:20px;min-width:var(--w)}'),
 ('intrinsic','canvas{min-width:min-content}'),
 ('relative','canvas{max-height:2em}'),
 ('negative','canvas{min-width:-1px}'),
 ('overflow','canvas{min-width:1e99px}'),
 ('underflow','canvas{min-width:1e-999px}'),
 ('hex','canvas{min-width:0x1px}'),
 ('shrink','.row{width:100px}canvas{min-width:30px}'),
 ('overflow_min','.row{max-width:120px}canvas{min-width:140px}'),
 ('peer_shrink','.row{width:120px}.peer{flex-shrink:1;min-width:5px}'),
]:
 run(name,document(css=css),False);checks+=1
(ROOT/'summary.json').write_text(json.dumps(RESULTS,indent=2))
print('H13 PASS',checks,'scenarios;',len(RESULTS),'CLI runs')
