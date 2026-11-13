"""Manual H20 capture gate and independent physical-coordinate PNG oracle."""
import pathlib, subprocess, sys
binary=pathlib.Path(sys.argv[1]).resolve();root=pathlib.Path(sys.argv[2]);root.mkdir(parents=True,exist_ok=True);checks=0
base='body{margin:0}.f{width:180px;height:100px}.a{width:30px;height:20px;background:red;flex-shrink:0}.b{width:50px;height:20px;background:blue;flex-shrink:0}'
body='<section class="f"><div class="a"></div><div class="b"></div></section>'
def run(name,css,html=body,ok=True):
 global checks
 p=root/(name+'.html');p.write_text('<html><head><style>'+base+css+'</style></head><body>'+html+'</body></html>');out=root/name
 r=subprocess.run([str(binary),'--geometry',str(p),str(out),'240','120','1'],capture_output=True)
 report=(out/'report.log').read_text();checks+=1
 if not ok:
  assert r.returncode!=0 and not (out/'aether.png').exists(),(name,report)
  if name.startswith('reject-logical-'):assert 'DIRECTION_AUTHORED_PROFILE_UNPROVEN' in report,(name,report)
  return
 assert r.returncode==0 and 'worker_status=GEOMETRY_RENDERED' in report,(name,report)
 return (out/'aether.png').read_bytes()
def matrix(name,extra,xy):
 global checks
 actual=run(name,'.f{display:flex;direction:rtl;gap:10px;align-items:flex-start;'+extra+'}')
 (ax,ay),(bx,by)=xy
 reference=run(name+'-physical','.f{position:relative}.a,.b{position:absolute}.a{left:%dpx;top:%dpx}.b{left:%dpx;top:%dpx}'%(ax,ay,bx,by))
 assert actual==reference,name;checks+=1
matrix('row','',((150,0),(90,0)))
matrix('row-reverse','flex-direction:row-reverse',((0,0),(40,0)))
matrix('column','flex-direction:column',((150,0),(130,30)))
matrix('column-reverse','flex-direction:column-reverse',((150,80),(130,50)))
matrix('row-end','justify-content:flex-end',((60,0),(0,0)))
matrix('reverse-start','flex-direction:row-reverse;justify-content:start',((90,0),(130,0)))
matrix('reverse-end','flex-direction:row-reverse;justify-content:end',((0,0),(40,0)))
matrix('column-end','flex-direction:column;align-items:flex-end',((0,0),(0,30)))
matrix('column-center','flex-direction:column;align-items:center',((75,0),(65,30)))
# Inheritance, override and final cascade must preserve exact raster output.
a=run('inherit','.f{display:flex;direction:rtl;gap:10px;align-items:flex-start}.a,.b{direction:inherit}')
assert a==run('inherit-control','.f{display:flex;direction:rtl;gap:10px;align-items:flex-start}');checks+=1
assert run('override','.f{direction:rtl;direction:ltr;display:flex;gap:10px;align-items:flex-start}')==run('ltr','.f{display:flex;gap:10px;align-items:flex-start}');checks+=1
run('nested','.f{display:flex;direction:rtl;align-items:flex-start}.a{display:flex}', '<section class="f"><div class="a"><div style="width:10px;height:10px;background:green"></div></div><div class="b"></div></section>')
for name,css in [
 ('wrap-reverse','flex-wrap:wrap-reverse'),('contents','display:contents'),('intrinsic','width:min-content'),('position','position:absolute'),('overflow','overflow:hidden'),('scroll','overflow:scroll'),('around','justify-content:space-around'),('baseline','align-items:baseline'),('self-start','align-items:self-start'),('vertical','writing-mode:sideways-rl'),('unicode','unicode-bidi:bidi-override'),('flow','display:flow-root'),('all','all:initial'),('variable','direction:var(--d)'),('transform','transform:translateX(1px)'),('opacity','opacity:.5')]:
 run('reject-'+name,'.f{display:flex;direction:rtl;'+css+'}',ok=False)
run('reject-inactive','.f{display:flex;direction:rtl}@media(min-width:999px){.missing{overflow:scroll}}',ok=False)
run('reject-root','body{direction:rtl}.f{display:flex}',ok=False)
run('reject-block-subtree','.f{direction:rtl}',ok=False)
run('reject-dir','.f{display:flex}',body.replace('class="f"','class="f" dir="rtl"'),False)
# No logical property may be silently dropped in a direction-aware capture.
logical=[]
for family in ['margin','padding','inset','scroll-margin','scroll-padding']:
 for axis in ['inline','block']:
  logical += [(family+'-'+axis,'20px')]
  logical += [(family+'-'+axis+'-'+edge,'20px') for edge in ['start','end']]
for axis in ['inline','block']:
 for prefix in ['', 'min-', 'max-', 'contain-intrinsic-']:
  logical += [(prefix+axis+'-size','20px')]
 for edge in ['', '-start', '-end']:
  for suffix,value in [('', '3px solid green'),('-width','3px'),('-style','solid'),('-color','green')]:
   logical += [('border-'+axis+edge+suffix,value)]
 logical += [('overflow-'+axis,'hidden'),('overscroll-behavior-'+axis,'contain')]
for a in ['start','end']:
 for b in ['start','end']:logical += [('border-'+a+'-'+b+'-radius','4px')]
for i,(prop,value) in enumerate(logical):
 run('reject-logical-'+str(i),'.f{display:flex;direction:rtl} .a{'+prop+':'+value+'}',ok=False)
for name,css in [
 ('escaped',r'.a{m\61 rgin-inl\69 ne-st\61 rt:20px}'),
 ('case','.a{MARGIN-INLINE-START:20px}'),
 ('important','.a{margin-inline-start:20px!important}'),
 ('variable','.a{--m:20px;margin-inline-start:var(--m)}'),
 ('overridden','.a{margin-inline-start:20px;margin-inline-start:0}'),
 ('unmatched','.missing{padding-block:20px}'),
 ('inactive','@media(min-width:999px){.a{inset-inline:20px}}'),
 ('supports','@supports not (display:flex){.a{border-inline:3px solid green}}')]:
 run('reject-logical-'+name,'.f{display:flex;direction:rtl}'+css,ok=False)
run('reject-logical-inline','.f{display:flex;direction:rtl}',body.replace('class="a"','class="a" style="margin-inline-start:var(--m,20px)!important"'),False)
# Physical operation is supported and observably different, not merely admitted.
plain=run('physical-plain','.f{display:flex;direction:rtl;gap:10px;align-items:flex-start}')
physical=run('physical-right','.f{display:flex;direction:rtl;gap:10px;align-items:flex-start}.a{margin-right:20px}')
assert plain!=physical;checks+=1
run('custom-name','.f{display:flex;direction:rtl;--inline-size:20px}')
# Blink css_properties.json5 aliases are real CSS, not arbitrary unknown names.
aliases=[]
for side in ['before','after','start','end']:
 for kind in ['margin','padding']:aliases.append(('-webkit-'+kind+'-'+side,'20px'))
 aliases.append(('-webkit-border-'+side,'3px solid green'))
 for suffix,value in [('width','3px'),('style','solid'),('color','green')]:
  aliases.append(('-webkit-border-'+side+'-'+suffix,value))
for prefix in ['', 'min-', 'max-']:
 for axis in ['width','height']:aliases.append(('-webkit-'+prefix+'logical-'+axis,'20px'))
for i,(prop,value) in enumerate(aliases):
 run('reject-logical-alias-'+str(i),'.f{display:flex;direction:rtl}.a{'+prop+':'+value+'}',ok=False)
for name,css in [
 ('escaped',r'.a{-webkit-m\61 rgin-be\66 ore:20px}'),
 ('case','.a{-WEBKIT-PADDING-AFTER:20px}'),
 ('important','.a{-webkit-border-before:3px solid green!important}'),
 ('variable','.a{--m:20px;-webkit-margin-before:var(--m)}'),
 ('override','.a{-webkit-logical-width:20px;-webkit-logical-width:30px}'),
 ('unmatched','.missing{-webkit-margin-after:20px}'),
 ('inactive','@media(min-width:999px){.a{-webkit-padding-before:20px}}'),
 ('supports','@supports not (display:flex){.a{-webkit-border-after:3px solid green}}'),
 ('unknown','.a{future-property:20px}'),
 ('unknown-vendor','.a{-vendor-new-layout:20px}')]:
 run('reject-logical-alias-'+name,'.f{display:flex;direction:rtl}'+css,ok=False)
run('reject-logical-alias-inline','.f{display:flex;direction:rtl}',body.replace('class="a"','class="a" style="-webkit-margin-before:20px!important"'),False)
# Closed names alone are not enough: valid CSS but unimplemented values refuse.
for i,decl in enumerate(['background:paint(foo)','background:red no-repeat','margin:anchor-size(width)','margin:1px junk','width:stretch','border:3px double red','border-color:light-dark(red,blue)','padding:var(--p,20px)','gap:normal','box-sizing:padding-box','flex:content','order:1.5','flex-grow:NaN','color:currentColor']):
 run('reject-logical-value-'+str(i),'.f{display:flex;direction:rtl}.a{'+decl+'}',ok=False)
# Physical names and simple values remain operational after closing the gate.
for i,decl in enumerate(['margin-top:4px','padding:2px 3px','border:2px solid green','border-right:3px solid #00f','gap:3px 5px','box-sizing:border-box','flex:0 0 20px','order:2']):
 run('allow-physical-'+str(i),'.f{display:flex;direction:rtl;align-items:flex-start}.a{'+decl+'}')
# Every accepted non-custom property uses literal spelling, never a more
# permissive decoder than its native consumer. Exercise names AND values.
lexical=[('direction','rtl'),('display','flex'),('flex-direction','column'),
 ('flex-wrap','wrap'),('justify-content','flex-end'),('align-items','center'),
 ('align-self','center'),('align-content','stretch'),('position','static'),
 ('overflow','visible'),('overflow-x','visible'),('overflow-y','visible'),
 ('box-sizing','border-box'),('background','green'),('background-color','green'),
 ('color','green'),('width','30px'),('height','20px'),('min-width','0px'),
 ('min-height','0px'),('max-width','100px'),('max-height','100px'),
 ('flex-basis','auto'),('flex-grow','1'),('flex-shrink','0'),('order','2'),
 ('flex','0 0 auto'),('gap','2px'),('row-gap','2px'),('column-gap','2px')]
for stem in ['margin','padding']:
 for side in ['', '-left','-right','-top','-bottom']:lexical.append((stem+side,'2px'))
for side in ['', '-left','-right','-top','-bottom']:
 for suffix,val in [('', '2px solid green'),('-width','2px'),('-style','solid'),('-color','green')]:
  lexical.append(('border'+side+suffix,val))
for i,(prop,val) in enumerate(lexical):
 run('allow-literal-'+str(i),'.f{display:flex;direction:rtl;align-items:flex-start}.a{'+prop+':'+val+'}')
 for name,changed in [('escaped',chr(92)+format(ord(prop[0]),'x')+' '+prop[1:]),('case',prop.upper())]:
  run('reject-logical-name-'+name+str(i),'.f{display:flex;direction:rtl}.a{'+changed+':'+val+'}',ok=False)
 # Escape first character, regardless of whether it is a numeric or identifier token.
 escaped=chr(92)+format(ord(val[0]),'x')+' '+val[1:]
 run('reject-logical-value-escape-'+str(i),'.f{display:flex;direction:rtl}.a{'+prop+':'+escaped+'}',ok=False)
for name,css in [
 ('flex-direction',r'.f{flex-direction:\63 olumn}'),
 ('align',r'.f{align-items:\66 lex-end}'),
 ('color',r'.a{background:r\65 d}'),
 ('important',r'.f{flex-direction:\63 olumn!important}'),
 ('case','.f{flex-direction:COLUMN}'),
 ('comments','.a{border:2px/**/solid green}'),
 ('overridden',r'.f{flex-direction:\63 olumn;flex-direction:row}'),
 ('inactive',r'@media(min-width:999px){.f{flex-direction:\63 olumn}}'),
 ('unmatched',r'.missing{flex-direction:\63 olumn}')]:
 run('reject-logical-serialization-'+name,'.f{display:flex;direction:rtl}'+css,ok=False)
run('reject-logical-serialization-inline','.f{display:flex;direction:rtl}',body.replace('class="f"',r'class="f" style="flex-direction:\63 olumn"'),False)
# Float-domain proof: parser-accepted double is insufficient for native floats.
length_props=['width','height','min-width','min-height','max-width','max-height','flex-basis','gap','row-gap','column-gap']
for stem in ['margin','padding']:
 for side in ['', '-left','-right','-top','-bottom']:length_props.append(stem+side)
for side in ['', '-left','-right','-top','-bottom']:length_props.append('border'+side+'-width')
for i,prop in enumerate(length_props):
 for j,value in enumerate(['1e100px','-1e100px','3.4028236e38px','1e-100px','-1e-100px','1e-400px','65537px','0.0000001px']):
  run('reject-logical-numeric-'+str(i)+'-'+str(j),'.f{display:flex;direction:rtl}.a{'+prop+':'+value+'}',ok=False)
for i,prop in enumerate(['flex-grow','flex-shrink']):
 for j,value in enumerate(['1e100','3.4028236e38','1e-100','-1e-100','1e-400','65537','0.0000001','-1']):
  run('reject-logical-factor-'+str(i)+'-'+str(j),'.f{display:flex;direction:rtl}.a{'+prop+':'+value+'}',ok=False)
for i,value in enumerate(['1e100 1 20px','1 1e100 20px','1 1 1e100px','1e-100 1 20px','1 1 1e-100px']):
 run('reject-logical-flex-numeric-'+str(i),'.f{display:flex;direction:rtl}.a{flex:'+value+'}',ok=False)
for i,value in enumerate(['2147483648','-2147483649','1e100','1e-100']):
 run('reject-logical-order-numeric-'+str(i),'.f{display:flex;direction:rtl}.a{order:'+value+'}',ok=False)
for i,value in enumerate(['65536px','0.000001px','1e-6px','-0px','0e100px']):
 run('allow-numeric-'+str(i),'.f{display:flex;direction:rtl}.a{margin-right:'+value+'}')
for i,value in enumerate(['65536','0.000001','1e-6','-0','0e100']):
 run('allow-factor-'+str(i),'.f{display:flex;direction:rtl}.a{flex-grow:'+value+'}')
# Multi-item sums/products at admitted bounds reach the existing paint size
# guard and must still fail closed without publishing an unsupported image.
large='<section class="f">'+''.join('<div class="a"></div>' for _ in range(24))+'</section>'
run('bounded-sums-products','.f{display:flex;direction:rtl;width:65536px;height:100px;gap:65536px;align-items:flex-start}.a{width:65536px;margin-left:-65536px;padding:1px;flex-grow:65536;flex-shrink:0.000001}',large,False)
# Per-property keyword contract: never infer all size grammars from width.
for prop,allowed in [('width',{'auto'}),('height',{'auto'}),('flex-basis',{'auto'}),('min-width',set()),('min-height',set()),('max-width',{'none'}),('max-height',{'none'})]:
 for value in ['auto','none','initial','inherit','unset','revert','normal','content','min-content','max-content']:
  run('size-domain-'+prop+'-'+value,'.f{display:flex;direction:rtl}.a{'+prop+':60px;'+prop+':'+value+';'+prop+':20px}',ok=value in allowed)
 for value in ['0','20px']:
  run('size-px-'+prop+'-'+value,'.f{display:flex;direction:rtl}.a{'+prop+':'+value+'}')
for prop in ['min-width','min-height','max-width','max-height']:
 for i,css in enumerate(['.a{'+prop+':auto!important}', '.absent{'+prop+':auto}', '@media(min-width:9999px){.a{'+prop+':auto}}', '.a{'+prop+':auto;'+prop+':20px}', '.a{--n:auto;'+prop+':var(--n)}']):
  run('reject-logical-size-contract-'+prop+str(i),'.f{display:flex;direction:rtl}'+css,ok=False)
# Cross-family keyword matrix. Final safe override separates authored gate
# acceptance from resolved-scene restrictions; negative declarations still fail.
keyword_domains = {
 'direction':'ltr rtl initial inherit unset',
 'display':'block flex none initial inherit unset',
 'flex-direction':'row row-reverse column column-reverse initial inherit unset',
 'flex-wrap':'nowrap wrap initial',
 'justify-content':'start end flex-start flex-end center space-between initial inherit unset',
 'align-items':'start end flex-start flex-end center stretch initial',
 'align-self':'auto start end flex-start flex-end center stretch initial',
 'align-content':'stretch initial',
 'position':'static initial',
 'overflow':'visible initial','overflow-x':'visible initial','overflow-y':'visible initial',
 'box-sizing':'content-box border-box',
}
keyword_universe=set('auto none normal content baseline space-around space-evenly wrap-reverse revert'.split())
for domain in keyword_domains.values(): keyword_universe.update(domain.split())
for prop,domain in keyword_domains.items():
 allowed=set(domain.split()); safe=domain.split()[0]
 for value in sorted(keyword_universe):
  run('keyword-family-'+prop+'-'+value,'.f{display:flex;direction:rtl}.a{'+prop+':'+value+';'+prop+':'+safe+'}',ok=value in allowed)
print('H20 CLI checks',checks)
