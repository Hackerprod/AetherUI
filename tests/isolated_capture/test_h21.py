"""H21 source/computed admission checks; geometry oracle lives in native tests."""
import pathlib, subprocess, sys
binary=pathlib.Path(sys.argv[1]).resolve();root=pathlib.Path(sys.argv[2]);root.mkdir(parents=True,exist_ok=True);checks=0
base='body{margin:0}.f{display:flex;width:180px;height:100px;writing-mode:vertical-rl;align-items:flex-start}.a{width:30px;height:20px;background:red;flex-shrink:0}.b{width:50px;height:20px;background:blue;flex-shrink:0}'
body='<section class="f"><div class="a"></div><div class="b"></div></section>'
def run(name,extra='',html=body,ok=True):
 global checks
 p=root/(name+'.html');p.write_text('<html><head><style>'+base+extra+'</style></head><body>'+html+'</body></html>');out=root/name
 r=subprocess.run([str(binary),'--geometry',str(p),str(out),'240','180','1'],capture_output=True)
 report=(out/'report.log').read_text();checks+=1
 if ok: assert r.returncode==0 and 'worker_status=GEOMETRY_RENDERED' in report,(name,report)
 else: assert r.returncode!=0 and not (out/'aether.png').exists(),(name,report)
for mode in ['horizontal-tb','vertical-rl','vertical-lr']:
 for direction in ['ltr','rtl']:
  for flex in ['row','row-reverse','column','column-reverse']:
   run(mode+direction+flex,'.f{writing-mode:'+mode+';direction:'+direction+';flex-direction:'+flex+';row-gap:7px;column-gap:13px}')
for name,css in [
 ('orthogonal','.a{writing-mode:horizontal-tb}'),('orthogonal-opposite','.a{writing-mode:vertical-lr}'),
 ('block','.f{display:block}'),('root','body{writing-mode:vertical-rl}'),('wrap','.f{flex-wrap:wrap}'),
 ('auto-size','.a{width:auto}'),('percent','.a{height:20%}'),('auto-margin','.a{margin:auto}'),
 ('contents','.a{display:contents}'),('hidden','.a{display:none}'),('nested','.a{display:flex}'),
 ('scroll','.a{overflow:scroll}'),('baseline','.f{align-items:baseline}'),('logical','.a{inline-size:30px}'),
 ('unknown','.a{-vendor-test:1}'),('inactive','@media(min-width:999px){.missing{writing-mode:sideways-rl}}'),
 ('escaped','.f{writing-mode:vertical\\2d rl}'),('inherit-literal','.a{writing-mode:inherit}'),
 ('var','.f{--m:vertical-rl;writing-mode:var(--m)}')]:run('reject-'+name,css,ok=False)
run('reject-subtree',html=body.replace('<div class="a"></div>','<div class="a"><div></div></div>'),ok=False)
run('reject-text',html=body.replace('<div class="a"></div>','<div class="a">text</div>'),ok=False)
run('reject-canvas',html=body+'<canvas></canvas>',ok=False)
print('H21 capture checks:',checks)
