#!/usr/bin/env python3
"""H15 structural rule traversal, original sheets retained for native execution."""
import pathlib
exec(pathlib.Path(__file__).with_name('test_h3.py').read_text().split('# Intrinsic/content and border-box dimensions;')[0])
checks=0
def pair(name,css,control):
 global checks
 a,_,_=run(name,document(css=css));b,_,_=run(name+'_control',document(css=control))
 assert (a/'aether.png').read_bytes()==(b/'aether.png').read_bytes(),name
 checks+=1
for name,wrap,active in [
 ('media_true','@media(min-width:1px){RULE}',True),
 ('media_false','@media(min-width:1000px){RULE}',False),
 ('print','@media print{RULE}',False),
 ('supports_true','@supports(display:flex){RULE}',True),
 ('supports_false','@supports not (display:flex){RULE}',False),
 ('mixed','@media(min-width:1px){@supports(display:flex){RULE}}',True),
 ('mixed_false','@supports not (display:flex){@media(min-width:1px){RULE}}',False),
 ('escaped',r'@m\65 dia(min-width:1px){RULE}',True),
 ('comments','@media/* { } */(min-width:1px){/* } */RULE}',True),
]:
 rule='canvas{width:130px;height:60px}'
 pair(name,wrap.replace('RULE',rule),rule if active else '')
pair('order','@media(min-width:1px){canvas{width:130px;height:60px}}canvas{width:110px;height:40px}','canvas{width:110px;height:40px}')
pair('important','@media(min-width:1px){canvas{width:130px!important;height:60px}}canvas{width:110px}','canvas{width:130px;height:60px}')
pair('strings',r'@media(min-width:1px){canvas{--unused:"} ; {";width:130px;height:60px}}','canvas{width:130px;height:60px}')
# Keyframes use their own selector grammar; they never get H11 no-match exemption.
k='@keyframes k{from{gap:10px}to{gap:30px}}'
for name,defs,expected in [('paused',k,20),('repeat',k+'@keyframes k{from{gap:30px}to{gap:50px}}',40),('inactive',k+'@media print{@keyframes k{from{gap:50px}to{gap:70px}}}',20),('nested','@supports(display:flex){'+k+'}',20)]:
 pair(name,defs+'.row{animation:k 10s linear -5s paused}',f'.row{{gap:{expected}px}}')
pair('unused_keyframes',k,'')
# H11 no-match exception remains top-level only; nested declarations stay checked.
pair('top_absent','#missing{min-height:10%}','')
for name,css in [
 ('nested_absent','@media(min-width:1px){#missing{min-height:10%}}'),
 ('inactive_absent','@media print{#missing{min-height:10%}}'),
 ('inactive_property','@supports not (display:flex){canvas{writing-mode:vertical-rl}}'),
 ('inactive_function','@media print{canvas{width:unknown(1px)}}'),
 ('inactive_resource','@media print{canvas{background-image:url(secret.png)}}'),
 ('unused_keyframe_resource','@keyframes k{from{gap:10px;--bad:url(secret.png)}to{gap:20px}}'),
 ('ignored_keyframe_property','@keyframes k{from{gap:10px;writing-mode:vertical-rl!important}to{gap:20px}}'),
 ('new_keyframe','@keyframes k{from{gap:10px}50%{gap:20px}to{gap:30px}}'),
 ('bad_frame','@keyframes k{canvas{gap:10px}to{gap:20px}}'),
 ('unknown_at','@container x{canvas{width:120px}}'),
 ('nested_rule','@media(min-width:1px){canvas{.child{width:10px}}}'),
 ('missing_close','@media(min-width:1px){canvas{width:120px}'),
 ('extra_close','@media(min-width:1px){canvas{width:120px}}}'),
 ('deep','@media(min-width:1px){'*34+'canvas{width:120px}'+'}'*34),
]:
 run(name,document(css=css),False);checks+=1
(ROOT/'summary.json').write_text(json.dumps(RESULTS,indent=2))
print('H15 PASS',checks,'scenarios;',len(RESULTS),'CLI runs')
