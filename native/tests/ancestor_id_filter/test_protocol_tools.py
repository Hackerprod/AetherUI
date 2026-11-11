import json, pathlib, tempfile, unittest
from unittest.mock import patch
from run_campaigns import HERE,plan
from analyze_campaigns import estimate,analyze
class ProtocolTests(unittest.TestCase):
 def test_plan(self):
  p=json.loads((HERE/'frozen_protocol.json').read_text());rows=list(plan(p))
  self.assertEqual(sum(not x['warmup'] for x in rows),1080);self.assertEqual(sum(x['warmup'] for x in rows),192)
  for c in (1,2):
   for cell in p['cells']:
    r=[x for x in rows if x['campaign']==c and x['cell']==cell['name']]
    self.assertEqual(''.join(x['arm'] for x in r[:4]),'ABBA')
    self.assertEqual(len(r),4+cell['pairs']*2)
    self.assertEqual(''.join(x['arm'] for x in r[4:8]),'ABBA' if c==1 else 'BAAB')
 def test_estimators(self):
  b=[[dict(arm=a,metrics={'style':100 if a=='A' else 90}) for a in 'ABBA'] for _ in range(5)]
  x=estimate(b,'style',1,3,'ratio');self.assertEqual(x['CI95'],[.9,.9]);self.assertEqual(x['paired_median_difference'],-10)
  x=estimate(b,'style',2,3,'small');self.assertEqual(x['point'],-13);self.assertEqual(x['CI95'],[-13,-13])
 def test_margin_recomputed(self):
  b=[[dict(arm=a,metrics={'pipeline':v if a=='A' else v+1}) for a in 'BAAB'] for v in (10.,20.,30.,40.,50.)]
  x=estimate(b,'pipeline',2,0,'secondary');self.assertEqual(x['point'],-1);self.assertEqual(x['CI95'],[-1,-1])
 def test_complete_rows_do_not_override_identity_or_completion_failure(self):
  protocol=json.loads((HERE/'frozen_protocol.json').read_text())
  rows=[{**row,'status':'PASS','metrics':{'pipeline':100.,'style':100.,'RSS':1000.}} for row in plan(protocol)]
  self.assertEqual(len(rows),1272)
  for defect in ('end-failed','missing-COMPLETE','missing-end','identity-marker'):
   with self.subTest(defect=defect),tempfile.TemporaryDirectory(prefix='synthetic-protocol-') as temp:
    directory=pathlib.Path(temp)
    (directory/'samples.jsonl').write_text(''.join(json.dumps(row)+'\n' for row in rows))
    (directory/'COMPLETE.json').write_text(json.dumps({'measured':1080,'warmup':192}))
    for phase in ('begin','end'):
     (directory/('linkage-'+phase+'.json')).write_text(json.dumps({'phase':phase,'PASS':True,'errors':[]}))
    if defect=='end-failed':(directory/'linkage-end.json').write_text(json.dumps({'phase':'end','PASS':False,'errors':['shared identity changed']}))
    if defect=='missing-COMPLETE':(directory/'COMPLETE.json').unlink()
    if defect=='missing-end':(directory/'linkage-end.json').unlink()
    if defect=='identity-marker':(directory/'identity-failure.json').write_text('{"status":"NO PASS"}')
    # No bootstrap/estimator should be reached on any failed prerequisite.
    with patch('analyze_campaigns.estimate',side_effect=AssertionError('estimator must not run')) as estimator:
     with self.assertRaises((ValueError,FileNotFoundError)):analyze(directory)
     estimator.assert_not_called()
if __name__=='__main__':unittest.main()
