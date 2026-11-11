import json, unittest
from run_campaigns import HERE,plan
from analyze_campaigns import estimate
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
if __name__=='__main__':unittest.main()
