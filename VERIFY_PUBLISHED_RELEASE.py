from pathlib import Path
import argparse,configparser,hashlib,json,os,re,time,urllib.request,zipfile
BASE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--metadata',type=Path)
args=parser.parse_args()
if args.metadata:
 metadata=json.loads(args.metadata.read_text(encoding='utf-8'))
else:
 headers={'User-Agent':'CS2-LJ-Overlay-release-verification'}
 if os.environ.get('GH_TOKEN'):headers['Authorization']='Bearer '+os.environ['GH_TOKEN']
 request=urllib.request.Request('https://api.github.com/repos/iknlm/CS2-LJ-Overlay/releases/tags/v1.2',headers=headers)
 with urllib.request.urlopen(request,timeout=90) as response:metadata=json.loads(response.read().decode('utf-8'))
spec=json.loads((BASE/'release-v1.2.json').read_text(encoding='utf-8'))
if metadata['tag_name']!='v1.2' or metadata['target_commitish']!=spec['published_release_commit']:raise SystemExit('Published release target changed')
if metadata['draft']:raise SystemExit('Release is still a draft')
expected_names={'LJOverlay_v1.2_win64.zip','LJOverlay_new.exe','SHA256SUMS.txt'}
if {a['name'] for a in metadata['assets']}!=expected_names:raise SystemExit('Unexpected release asset list')
out=BASE/'build/public_downloads';out.mkdir(parents=True,exist_ok=True)
sha=lambda data:hashlib.sha256(data).hexdigest()
results={}
for asset in metadata['assets']:
 name=asset['name'];url=asset['browser_download_url']
 if not url.startswith('https://github.com/iknlm/CS2-LJ-Overlay/releases/download/v1.2/'):raise SystemExit('Unexpected download host/repository')
 request=urllib.request.Request(url,headers={'User-Agent':'CS2-LJ-Overlay-public-download-verification'})
 for attempt in range(4):
  try:
   request=urllib.request.Request(url+('?'+'verify='+str(time.time_ns()) if attempt else ''),headers={'User-Agent':'CS2-LJ-Overlay-public-download-verification','Cache-Control':'no-cache'})
   with urllib.request.urlopen(request,timeout=30) as response:data=response.read();status=response.status
   break
  except Exception:
   if attempt==3:raise
   time.sleep(2)
 if status!=200 or len(data)!=asset['size'] or 'sha256:'+sha(data)!=asset['digest']:raise SystemExit('Public download differs: '+name)
 (out/name).write_bytes(data);results[name]={'url':url,'sha256':sha(data),'bytes':len(data),'http':status}
for line in (out/'SHA256SUMS.txt').read_text(encoding='ascii').splitlines():
 expected,name=line.split('  ',1)
 if sha((out/name).read_bytes())!=expected:raise SystemExit('Outer SHA failed: '+name)
with zipfile.ZipFile(out/'LJOverlay_v1.2_win64.zip') as z:
 if z.testzip() is not None:raise SystemExit('ZIP CRC failed')
 for name in z.namelist():
  if name.startswith('/') or '..' in Path(name).parts or re.match(r'^[A-Za-z]:',name):raise SystemExit('Unsafe ZIP path')
  if name.lower().endswith(('.csv','.log','.dat','.bak','.tmp','.ini')):raise SystemExit('Personal/debris file in ZIP')
 if z.read('LJOverlay_new.exe')!=(out/'LJOverlay_new.exe').read_bytes():raise SystemExit('Standalone EXE differs from ZIP EXE')
 for line in z.read('SHA256SUMS.txt').decode('utf-8').splitlines():
  expected,name=line.split('  ',1)
  if sha(z.read(name))!=expected:raise SystemExit('Internal SHA failed')
 waves=[n for n in z.namelist() if n.startswith('sounds/') and n.endswith('.wav')]
 if len(waves)!=11:raise SystemExit('Missing sounds')
 for name in waves:
  if z.read(name)!=(BASE/name).read_bytes():raise SystemExit('Bundled sound changed')
 config=configparser.ConfigParser();config.read_string(z.read('lj_overlay.ini.example').decode('utf-8-sig'))
 if config['overlay']['log'] or config['overlay']['player']:raise SystemExit('Personal config values')
 if config['sound']['enabled']!='0':raise SystemExit('Unexpected default sound setting')
 if b'2026 IKNLM' not in z.read('LICENSE'):raise SystemExit('Missing MIT author')
 for name in ['README.txt','README_RU.txt','AUTHORS.md','SOUND_CREDITS.txt','THIRD_PARTY_NOTICES.md']:
  t=z.read(name).decode('utf-8-sig')
  if '\ufffd' in t or re.search(r'\?{2,}',t):raise SystemExit('Corrupt document: '+name)
ns={'__name__':'pe_inspection','__file__':str(BASE/'prepare_release.py')}
exec(compile((BASE/'prepare_release.py').read_text(encoding='utf-8'),'prepare_release.py','exec'),ns)
pe=ns['inspect_pe'](out/'LJOverlay_new.exe')
report={'result':'PASS','release_url':metadata['html_url'],'release_tag':metadata['tag_name'],'release_target':metadata['target_commitish'],'assets':results,'pe':pe,'checks':'anonymous HTTP 200 and GitHub asset digests; outer/internal SHA; ZIP CRC; identical separate and bundled EXE; 11 unchanged WAVs; clean template; no personal/debris files; UTF-8 documents; MIT IKNLM; Windows x64 system-only PE imports'}
(BASE/'reports/RELEASE_PUBLICATION_RESULT.json').write_text(json.dumps(report,indent=2)+chr(10),encoding='utf-8')
print(json.dumps(report,indent=2))
