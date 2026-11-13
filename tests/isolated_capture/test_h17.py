import sys,pathlib,subprocess
binary=pathlib.Path(sys.argv[1]).resolve();root=pathlib.Path(sys.argv[2]);root.mkdir(parents=True,exist_ok=True)
checks=0
def capture(name,rule):
 global checks
 html='<html><head><style>body{margin:0}section{display:flex;width:200px;height:30px;align-items:flex-start}i,div{width:20px;height:20px;background:blue}'+rule+'</style></head><body><section><i></i><div id="x" class="x"></div><i class="x"></i><div class="x"></div></section></body></html>'
 p=root/(name+'.html');p.write_text(html);out=root/name
 subprocess.run([str(binary),'--geometry',str(p),str(out),'220','60','1'],check=True)
 r=(out/'report.log').read_text();assert 'worker_status=GEOMETRY_RENDERED' in r,r;checks+=1
 return (out/'aether.png').read_bytes()
for i,sel in enumerate(['div:first-of-type','.x:first-of-type','section > div:first-of-type',r'div:first-of-\74 ype','div:FIRST-OF-TYPE','div:first-of-type:nth-child(2)']):
 assert capture(str(i),sel+'{background:red}')==capture(str(i)+'c','#x{background:red}');checks+=1
assert capture('specificity','div:first-of-type{background:red}.x{background:blue}')==capture('sc','#x{background:red}');checks+=1
print('H17 CLI checks',checks)
