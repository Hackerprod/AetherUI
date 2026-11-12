#!/usr/bin/env python3
"""Manual diagnostic regression; no network, no Actions, no renderer support claim."""
import pathlib, subprocess, sys, tempfile, hashlib
binary=str(pathlib.Path(sys.argv[1]).resolve())
fixture=pathlib.Path('tests/visual_compare/fixtures/flexbox_runtime_validation.html')
with tempfile.TemporaryDirectory() as d:
 root=pathlib.Path(d)
 def reject(path,name):
  out=root/name
  assert subprocess.run([binary,'--geometry',str(path),str(out),'1280','800','1']).returncode==3
  assert not (out/'aether.png').exists()
  log=(out/'report.log').read_text()
  assert 'stage_started=style' not in log
  assert 'flexbox_semantic_acceptance=false' in log
  return log
 before=hashlib.sha256(fixture.read_bytes()).hexdigest()
 log=reject(fixture,'original')
 expected={'at_rule:keyframes':5,'at_rule:media':2,'at_rule:supports':1,'function:calc':3,'function:calc-size':1,'function:clamp':1,'function:fit-content':1,'function:max':1,'function:nth-child':8,'function:rgba':2,'function:translateZ':1,'function:var':2}
 for token,count in expected.items(): assert f'token="{token}" count={count}\n' in log,token
 assert sum(x.startswith('inventory_css ') for x in log.splitlines())==28
 assert sum(x.startswith('inventory_element ') for x in log.splitlines())==5
 assert 'type="application/json"' in log
 assert before==hashlib.sha256(fixture.read_bytes()).hexdigest()
 synthetic=root/'syntax.html'
 synthetic.write_text(r'''<style>/* ghost() @unknown */
.a:nth-child(2){width:c\61 lc(2px);content:"ghost() @fake"}
@media (width:2px){.a{height:max(2px,3px)}}
</style><div style="width:var(--x)"></div><script type="application/json">{}</script>''')
 log=reject(synthetic,'synthetic')
 assert 'name="calc"' in log and 'name="ghost"' not in log and 'name="fake"' not in log
 assert 'line=2 column=4 kind=function name="nth-child"' in log
 assert 'name="var"' in log and 'source="inline-dom-' in log
 assert 'blocker=CSS_AT_RULE_UNSUPPORTED' in log
 assert 'blocker=CSS_FUNCTION_UNSUPPORTED' in log
 print('PASS complete fixture counts; escapes; strings/comments excluded; inline CSS; local coordinates; unchanged rejection')
