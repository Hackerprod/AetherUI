import pathlib, subprocess, sys
binary=pathlib.Path(sys.argv[1]).resolve(); root=pathlib.Path(sys.argv[2]);root.mkdir(parents=True,exist_ok=True)
checks=0
base='body{margin:0}.f{display:flex;width:220px;height:90px;align-items:flex-start;gap:4px}.item{width:35px;height:20px;background:blue;flex-shrink:0}.contents{display:contents}'
def run(name,body,css='',ok=True,reason=None):
 global checks
 p=root/(name+'.html'); p.write_text('<html><head><style>'+base+css+'</style></head><body>'+body+'</body></html>');out=root/name
 rc=subprocess.run([str(binary),'--geometry',str(p),str(out),'260','130','1'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 r=(out/'report.log').read_text()
 if ok:
  assert rc.returncode==0 and 'worker_status=GEOMETRY_RENDERED' in r,(name,rc.returncode,r)
  checks+=1;return (out/'aether.png').read_bytes()
 assert rc.returncode!=0 and 'worker_status=GEOMETRY_RENDERED' not in r and not (out/'aether.png').exists(),(name,r)
 if reason:assert reason in r,(name,r)
 checks+=1
items='<div id="a" class="item"></div><div id="b" class="item"></div>'
direct='<section class="f">'+items+'<div class="item"></div></section>'
wrapped='<section class="f"><div class="contents">'+items+'</div><div class="item"></div></section>'
for name,css in [('basic',''),('wrap','.f{width:80px;flex-wrap:wrap}'),('column','.f{flex-direction:column}'),('reverse','.f{flex-direction:row-reverse}'),('order','#a{order:3}#b{order:-1}'),('ties','.item{order:2}.contents{order:-10}'),('ignored','.contents{margin:20px;padding:30px;border:4px solid red;background:red;width:200px;height:100px}')]:
 assert run(name,wrapped,css)==run(name+'-control',direct,css);checks+=1
nested='<section class="f"><div class="contents"><span class="contents">'+items+'</span><div class="contents"></div></div><div class="item"></div></section>'
assert run('nested',nested)==run('nested-control',direct);checks+=1
hidden='<section class="f"><div class="contents"><div class="item" style="display:none"></div>'+items+'</div><div class="item"></div></section>'
assert run('hidden',hidden)==run('hidden-control',direct);checks+=1
assert run('dom-child',wrapped,'.contents > #a{background:red}')==run('dom-child-control',direct,'#a{background:red}');checks+=1
assert run('dom-first',wrapped,'.contents > div:first-of-type{background:red}')==run('dom-first-control',direct,'#a{background:red}');checks+=1
# Inherited visibility travels through the DOM even when its wrapper has no box.
assert run('inherit',wrapped,'.contents{visibility:hidden}#b{visibility:visible}')==run('inherit-control',direct,'#a{visibility:hidden}');checks+=1
# Contents can coexist with an independent, supported canvas in another flex.
canvas='<section style="display:flex;width:220px;align-items:flex-start"><canvas width="10" height="10"></canvas></section>'
assert run('canvas-independent',wrapped+canvas)==run('canvas-independent-control',direct+canvas);checks+=1
for name,css in [('position','.contents{position:relative}'),('transform','.contents{transform:translateZ(0)}'),('opacity','.contents{opacity:.5}'),('clip','.contents{overflow:hidden}'),('child-fixed','#a{position:fixed}'),('child-opacity','#a{opacity:.5}'),('child-transform','#a{transform:translateZ(0)}')]:
 run('reject-'+name,wrapped,css,False)
run('reject-nonflex','<div class="contents">'+items+'</div>',ok=False,reason='CONTENTS_PARENT_UNSUPPORTED')
run('reject-text','<section class="f"><div class="contents">hello'+items+'</div></section>',ok=False)
run('reject-replaced','<section class="f"><canvas class="contents"></canvas></section>',ok=False)
run('reject-table','<section class="f"><div class="contents"><div style="display:table"></div></div></section>',ok=False)
run('reject-pseudo',wrapped,'.contents::before{content:"x"}',False)
# Exact over-limit cases: keep siblings empty to isolate wrapper count.
run('reject-depth33','<section class="f">'+('<div class="contents">'*33)+items+('</div>'*33)+'</section>',ok=False,reason='CONTENTS_')
run('reject-wrappers513','<section class="f">'+('<div class="contents"></div>'*513)+'</section>',ok=False,reason='CONTENTS_WRAPPER_LIMIT')
print('H18 CLI checks',checks)
