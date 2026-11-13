#!/usr/bin/env python3
"""Source inventory, NOT CSS eligibility or computed-style analysis."""
import hashlib, json, pathlib, re
p=pathlib.Path(__file__).parents[1]/'visual_compare/fixtures/flexbox_runtime_validation.html'
b=p.read_bytes();s=b.decode('utf-8')
# Restrict to style blocks, not script strings, and don't match flex-direction.
css='\n'.join(re.findall(r'<style\b[^>]*>(.*?)</style>',s,re.I|re.S))
r={'source_bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'readiness':'Unknown',
   'eligibility':'Unknown','scope':'raw-source inventory only; declarations may be overridden or unsupported',
   'declarations':{k:sorted(set(re.findall(r'(?i)(?<![\w-])'+k+r'\s*:\s*([^;}]+)',css))) for k in ('font-family','font-size','font-weight','font-style','white-space','direction','writing-mode','font-feature-settings','font-variation-settings')},
   'first_letter_mentions':len(re.findall(r':first-letter',s,re.I)),
   'blockers':['No complete computed shaping context or CSS provenance','No SDK/provider capability certificate','No cross-node grouping integrated into layout','No resolved source font for Arial/Helvetica/sans-serif'],
   'shaping':'NOT_RUN','png':'NOT_RUN','chromium':'NOT_RUN','timing_ms':'NOT_MEASURED'}
assert r['sha256']=='eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837'
print(json.dumps(r,indent=2))
