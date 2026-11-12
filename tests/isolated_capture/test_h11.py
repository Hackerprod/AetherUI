#!/usr/bin/env python3
"""H11 absence proof CLI: untouched authored CSS, exact canvas PNG controls."""
import pathlib,subprocess,sys,hashlib,json
binary=str(pathlib.Path(sys.argv[1]).resolve());root=pathlib.Path(sys.argv[2]);root.mkdir();count=0;results=[]
base='body{margin:0;background-color:#112233}.row{display:flex;flex-direction:row;flex-wrap:nowrap;width:240px;height:100px;align-items:center}.peer{width:20px;height:20px;flex-grow:0;flex-shrink:0;background-color:#00ff00}canvas{flex-grow:0;border:2px solid #ff0000}'
body='<div class="row"><canvas width="100" height="50"></canvas><div class="peer"></div></div>'
def run(css='',ok=True,tail='',extrahead='',inline='',content=body):
 global count
 count+=1;src=root/f'{count}.html';out=root/str(count)
 src.write_text('<html><head>'+extrahead+'<style>'+base+css+'</style></head><body '+inline+'>'+content+tail+'</body></html>')
 p=subprocess.run([binary,'--geometry',str(src),str(out),'300','180','1'],capture_output=True);log=(out/'report.log').read_text()
 assert p.returncode==(0 if ok else 3),(count,css,log)
 fields=dict(x.split('=',1)for x in log.splitlines()if '='in x)
 assert fields['chromium']=='NOT_RUN' and fields['authored_css_validation']=='NOT_RUN'
 for k in ['child_cpu_seconds','child_peak_rss_kib','process_wall_ms']:assert float(fields[k])>=0
 results.append({'case':count,'ok':ok,'omitted':fields.get('canvas_unmatched_rules_omitted'),'cpu':fields['child_cpu_seconds']})
 if not ok:assert not(out/'aether.png').exists()and not(out/'aether.png.pending').exists();return
 assert fields['style_0_compiled_bytes_sha256']==hashlib.sha256((base+css).encode()).hexdigest()
 return (out/'aether.png').read_bytes(),fields['canvas_0_content']
control=run()
for selector in ['#unrelated','.missing','missing','#none canvas','#none > canvas','canvas + .missing','canvas ~ .missing','canvas:nth-child(4)','#none, .missing',r'#\75 nrelated','[missing]','[id="none"]','#none/*x*/ > /*y*/canvas']:
 assert run(selector+'{min-height:72%}')==control
for prop in ['min-width:72px','max-width:72px','max-height:72px','aspect-ratio:2','align-self:baseline','position:sticky']:
 assert run('#unrelated{'+prop+'}')==control
for prop in ['direction:rtl','writing-mode:vertical-rl']:
 run('#unrelated{'+prop+'}',False)
# H13 uses percentage limits here: finite px limits are now supported.
# Matching anywhere, including hidden and metadata, retains old H3 check.
for selector in ['canvas','.row','.peer','body','head','style','html','#unrelated, canvas','canvas:nth-child(1)',r'c\61 nvas']:
 run(selector+'{min-height:72%}',False)
run('#unrelated{min-height:72%}',False,tail='<div id="unrelated" style="display:none"></div>')
run('#unrelated{min-height:72%}',False,extrahead='<meta id="unrelated">')
run('#unrelated{min-height:72%}',False,content='<div id="unrelated"></div>'+body)
run('#unrelated{min-height:72%}',False,content='<div><div id="unrelated"></div>'+body+'</div>')
run('',False,tail='<div style="min-height:72%"></div>')
run('',False,inline='style="min-height:72%"')
# Global controls retained even if selector has no matches; includes inactive CSS.
for css in ['#unrelated{width:unknown(1)}','#unrelated{background:url(x.png)}','#unrelated{width:var(--x)}','@media print{#unrelated{min-height:72%}}','@supports(display:flex){#unrelated{min-height:72%}}','#unrelated:hover{min-height:72%}','#unrelated::before{min-height:72%}','#unrelated:not(:hover){min-height:72%}','#unrelated, {min-height:72%}','#unrelated{min-height:72%','a a a a a{min-height:72%}']:
 run(css,False)
# DOM and rule budgets fail conservatively, never converting unknown to no match.
run('#unrelated{min-height:72%}',False,tail='<div></div>'*1030)
run('#unrelated{min-height:72%}'*513,False)
# An actual variable/inheritance declaration remains checked on its matching rule.
run('body{--h:72px}canvas{min-height:var(--h)}',False)
run('body{min-height:72%}canvas{min-height:inherit}',False)
# No omission changes a second independent canvas either.
assert run('.row{height:80px}#unrelated{min-height:72%}',content=body+body)==run('.row{height:80px}',content=body+body)
original=pathlib.Path('tests/visual_compare/fixtures/flexbox_runtime_validation.html');assert hashlib.sha256(original.read_bytes()).hexdigest()=='eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837'
p=subprocess.run([binary,'--geometry',str(original),str(root/'original'),'300','180','1'],capture_output=True);assert p.returncode==3
(root/'metrics.json').write_text(json.dumps(results,indent=2))
print(f'H11 CLI PNG/metrics cases={count}; original rejected intact')
