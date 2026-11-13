import pathlib, subprocess, sys
binary=pathlib.Path(sys.argv[1]).resolve(); root=pathlib.Path(sys.argv[2]); root.mkdir(parents=True,exist_ok=True); checks=0
base='body{margin:0}.f{display:flex;width:180px;height:70px}.i{width:35px;height:20px;background:blue;flex-shrink:0}'
body='<section id="host" class="f"><div class="i"></div><div class="i"></div></section>'
def run(name, css='', html=body, ok=True, reason=None):
 global checks
 p=root/(name+'.html');p.write_text('<html><head><style>'+base+css+'</style></head><body>'+html+'</body></html>');out=root/name
 rc=subprocess.run([str(binary),'--geometry',str(p),str(out),'240','120','1'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 r=(out/'report.log').read_text()
 if ok:
  assert rc.returncode==0 and 'worker_status=GEOMETRY_RENDERED' in r,(name,r)
  checks+=1;return (out/'aether.png').read_bytes()
 assert rc.returncode!=0 and 'worker_status=GEOMETRY_RENDERED' not in r and not (out/'aether.png').exists(),(name,r)
 if reason:assert reason in r,(name,r)
 checks+=1
control=run('control')
for name,css in [
 ('basic','.f::first-letter{float:left;font-size:40px;color:red}'),
 ('historical','.f:first-letter{float:left;font-size:40px;color:red}'),
 ('escape',r'.f::first-\6c etter{background:red;width:90px;height:90px}'),
 ('host-not-styled','.f::first-letter{display:block;background:red;padding:50px}'),
 ('child-not-styled','.f::first-letter{color:red;background:red}'),
 ('conditional-true','@media(min-width:1px){.f::first-letter{color:red}}'),
 ('conditional-false','@media(min-width:999px){.f::first-letter{color:red}}'),
 ('supports','@supports(display:flex){.f::first-letter{color:red}}'),
 ('mixed-list','.f::first-letter,.missing::first-letter{color:red}'),
 ('specificity','.f::first-letter{background:red!important}'),
 ('cascade','.f{display:block}.f{display:flex}.f::first-letter{color:red}'),
 ('no-match','.absent::first-letter{font-size:40px}'),
 ('child-combinator','body > .f::first-letter{background:red}'),
 ('descendant','body .f::first-letter{background:red}')]:
 assert run(name,css)==control,name;checks+=1
for name,css,html in [
 ('inline-flex-unproven','.f{display:inline-flex}.f::first-letter{color:red}',body),
 ('block-host','.f{display:block}.f::first-letter{color:red}',body),
 ('block-in-list','.f::first-letter,.i::first-letter{color:red}',body),
 ('child-of-flex','.i::first-letter{color:red}',body),
 ('contents','.f::first-letter{color:red}','<div style="display:flex"><section class="f" style="display:contents"><div class="i"></div></section></div>'),
 ('unknown-host','canvas::first-letter{color:red}','<canvas style="display:flex"></canvas>'),
 ('state','.f:hover::first-letter{color:red}',body),
 ('nested',':is(.f)::first-letter{color:red}',body),
 ('chained','.f::first-letter::before{content:"x"}',body),
 ('functional','.f::first-letter(){color:red}',body),
 ('unsupported-inactive','@media(min-width:999px){.f::first-letter{background:url(secret)}}',body),
 ('unsupported-function','.f::first-letter{width:unknown(1)}',body),
 ('important-block','.f{display:block!important}.f{display:flex}.f::first-letter{color:red}',body),
 ('metadata','head::first-letter{color:red}',body),
 ('too-many',','.join('.x'+str(i)+'::first-letter' for i in range(129))+'{color:red}',body)]:run('reject-'+name,css,html,False)
# A canvas exercises H3 without weakening its source checks.
cbody='<section class="f" style="align-items:flex-start"><canvas style="width:10px;height:10px"></canvas></section>'
assert run('canvas','.f::first-letter{float:left;font-size:40px;color:red}',cbody)==run('canvas-control','',cbody);checks+=1
run('reject-dom-limit','.f::first-letter{color:red}',body+'<div></div>'*1025,False,'FIRST_LETTER_APPLICABILITY_UNPROVEN')
run('reject-depth-limit','.f::first-letter{color:red}','<div>'*33+body+'</div>'*33,False,'FIRST_LETTER_APPLICABILITY_UNPROVEN')
# Valid CSS forms not implemented natively must not fall back to an earlier flex.
for i,value in enumerate(['flow-root','block flow','inline flow-root','list-item','inline flex','grid','inline-grid','revert-layer','flow-root!important',r'flow-\72 oot']):
 run('reject-display-'+str(i),'.f{display:'+value+'}.f::first-letter{color:red}',body,False,'FIRST_LETTER_DISPLAY_SYNTAX_UNPROVEN')
run('reject-display-inline','.f::first-letter{color:red}',body.replace('class="f"','class="f" style="display:flow-root"'),False,'FIRST_LETTER_DISPLAY_SYNTAX_UNPROVEN')
run('reject-display-inactive','@media(min-width:999px){.f{display:flow-root}}.f::first-letter{color:red}',body,False,'FIRST_LETTER_DISPLAY_SYNTAX_UNPROVEN')
run('reject-all','.f{all:initial}.f::first-letter{color:red}',body,False,'FIRST_LETTER_DISPLAY_SYNTAX_UNPROVEN')
print('H19 CLI checks',checks)
