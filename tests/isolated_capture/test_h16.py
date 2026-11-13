import sys,pathlib,subprocess,re,hashlib
binary=pathlib.Path(sys.argv[1]).resolve(); root=pathlib.Path(sys.argv[2]); root.mkdir(parents=True,exist_ok=True)
checks=0
def run(name,css,extra='',inline=False):
 global checks
 html=(f'<div style="{css}"></div>' if inline else '<style>'+css+'</style>')+'<div style="display:flex;width:200px;height:40px;align-items:flex-start"><canvas width="20" height="10"></canvas></div>'+extra
 html='<html><head></head><body>'+html+'</body></html>'
 path=root/(name+'.html');path.write_text(html)
 out=root/name; p=subprocess.run([str(binary),'--geometry',str(path),str(out),'240','120','1'])
 report=(out/'report.log').read_text();assert 'diagnostic_format=H16-v1' in report
 checks+=1;return report,out
for name,css in [('simple','canvas{min-height:3%}'),('map','/*é*/ #absent{min-height:3%} canvas{min-height:3%}'),('repeated','#absent{min-height:3%} canvas{min-height:3%}'),('nested','@media(min-width:1px){canvas{min-height:3%}}'),('list','canvas,div{min-height:3%}')]:
 report,_=run(name,css); expected=css.encode().rfind(b'min-height')
 lines=[x for x in report.splitlines() if x.startswith('diagnostic_v1 ') and 'CANVAS_LAYOUT_PROFILE_UNSUPPORTED' in x]
 assert len(lines)==1,(name,report)
 line=lines[0]; assert f'begin={expected} ' in line,(name,line,expected)
 assert 'property="min-height"' in line and 'selector_sha256=' in line
 assert '3%' not in line and 'coverage=PARTIAL' in line;checks+=4
report,_=run('inline','min-height:3%',inline=True)
assert 'source="inline-dom-' in report and 'begin=0 ' in report;checks+=1
report,_=run('function','canvas{width:unsupported(SECRET_VALUE)}')
line=next(x for x in report.splitlines() if x.startswith('diagnostic_v1 ') and 'CSS_FUNCTION_UNSUPPORTED' in x)
assert 'begin=13 ' in line and 'SECRET_VALUE' not in line;checks+=1
report,_=run('malformed','canvas{width:}')
assert 'diagnostic_v1 ' in report and 'coverage=PARTIAL' in report;checks+=1
report,out=run('allowed','#absent{min-height:3%}')
assert 'worker_status=GEOMETRY_RENDERED' in report and not any(x.startswith('diagnostic_v1 ') for x in report.splitlines());checks+=1
print('H16 checks',checks)
if len(sys.argv)>3:
 baseline=pathlib.Path(sys.argv[3]).resolve()
 for source in sorted(root.glob('*.html')):
  out=root/(source.stem+'-baseline');subprocess.run([str(baseline),'--geometry',str(source),str(out),'240','120','1'])
  new=(root/source.stem/'report.log').read_text();old=(out/'report.log').read_text()
  for prefix in ['worker_status=','reason=','blocker=']:
   assert [x for x in new.splitlines() if x.startswith(prefix)]==[x for x in old.splitlines() if x.startswith(prefix)],(source,prefix)
  if (out/'aether.png').exists():assert (out/'aether.png').read_bytes()==(root/source.stem/'aether.png').read_bytes()
 print('H16 baseline admission/PNG comparison PASS')
