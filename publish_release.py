"""Publish only the task-owned v1.2 draft after exact asset verification."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile,time,urllib.request,urllib.parse,zipfile

ROOT=Path(__file__).resolve().parent
REPO='iknlm/CS2-LJ-Overlay'
TAG='v1.2'
NAMES=['LJOverlay_v1.2_win64.zip','LJOverlay_new.exe','SHA256SUMS.txt']
def api(endpoint,method='GET',data=None):
    args=['gh','api',endpoint]
    if method!='GET':args+=['--method',method]
    if data is not None:args+=['--input','-']
    out=subprocess.check_output(args,input=json.dumps(data) if data is not None else None,text=True,encoding='utf-8')
    return json.loads(out) if out.strip() else None
def checksum(data):return hashlib.sha256(data).hexdigest()
def fingerprint(r):
    return {k:r[k] for k in ['id','tag_name','target_commitish','name','body','draft','prerelease','created_at','published_at','updated_at']} | {'assets':[{k:a.get(k) for k in ['id','name','size','digest','updated_at']} for a in r['assets']]}
def main():
    spec=json.loads((ROOT/'release-v1.2.json').read_text(encoding='utf-8'))
    owned_id=spec.get('owned_draft_release_id')
    current=api('repos/'+REPO+'/releases/'+str(owned_id)) if owned_id else None
    if current and not current['draft']:
        if current['tag_name']!=TAG or current['target_commitish']!=spec['published_release_commit']:
            raise RuntimeError('Unexpected published v1.2 target')
        old=api('repos/'+REPO+'/releases/tags/v1.1')
        old_tag=api('repos/'+REPO+'/git/ref/tags/v1.1')['object']['sha']
        baseline=fingerprint(old)
        subprocess.run([sys.executable,str(ROOT/'VERIFY_PUBLISHED_RELEASE.py')],check=True)
        notes=(ROOT/'RELEASE_NOTES_v1.2.md').read_text(encoding='utf-8').strip()
        for temporary_id in spec.get('owned_temporary_draft_ids',[]):
            if temporary_id in (current['id'],old['id']):raise RuntimeError('Unsafe temporary draft ID')
            try:temporary=api('repos/'+REPO+'/releases/'+str(temporary_id))
            except subprocess.CalledProcessError as error:
                if str(json.loads(error.output or '{}').get('status'))=='404':continue
                raise
            if temporary['draft'] and temporary['tag_name'].startswith('untagged-') and temporary['body'].strip()==notes:
                api('repos/'+REPO+'/releases/'+str(temporary_id),'DELETE')
        if fingerprint(api('repos/'+REPO+'/releases/tags/v1.1'))!=baseline:raise RuntimeError('v1.1 changed')
        if api('repos/'+REPO+'/git/ref/tags/v1.1')['object']['sha']!=old_tag:raise RuntimeError('v1.1 tag changed')
        print('PASS existing published v1.2 verified; its assets and tags were not modified; v1.1 unchanged')
        return
    target=os.environ['GITHUB_SHA']
    dist=ROOT/'dist'
    expected={n:checksum((dist/n).read_bytes()) for n in NAMES}
    checks=dict((line.split('  ',1)[1],line.split('  ',1)[0]) for line in (dist/'SHA256SUMS.txt').read_text(encoding='ascii').splitlines())
    if checks!={n:expected[n] for n in NAMES[:2]}:raise RuntimeError('Outer manifest mismatch')
    with zipfile.ZipFile(dist/NAMES[0]) as z:
        if z.testzip() is not None:raise RuntimeError('ZIP CRC failed')
        for line in z.read('SHA256SUMS.txt').decode('utf-8').splitlines():
            value,name=line.split('  ',1)
            if checksum(z.read(name))!=value:raise RuntimeError('Internal manifest mismatch')
    spec=json.loads((ROOT/'release-v1.2.json').read_text(encoding='utf-8'))
    old=api('repos/'+REPO+'/releases/tags/v1.1')
    old_tag=api('repos/'+REPO+'/git/ref/tags/v1.1')['object']['sha']
    baseline=fingerprint(old)
    notes=(ROOT/'RELEASE_NOTES_v1.2.md').read_text(encoding='utf-8')
    owned_id=spec.get('owned_draft_release_id')
    if owned_id:
        existing=api('repos/'+REPO+'/releases/'+str(owned_id))
        if not existing['draft']:raise RuntimeError('Release is already published; refusing modification')
        if existing['tag_name']!=TAG and not existing['tag_name'].startswith('untagged-'):
            raise RuntimeError('Owned draft tag changed outside this task')
        if existing['body'].strip()!=notes.strip():raise RuntimeError('Owned draft notes changed outside this task')
        if any(a['name'] not in NAMES for a in existing['assets']):raise RuntimeError('Unrecognized draft asset')
        draft=api('repos/'+REPO+'/releases/'+str(owned_id),'PATCH',{'tag_name':TAG,'target_commitish':target,'draft':True})
        for asset in existing['assets']:
            api('repos/'+REPO+'/releases/assets/'+str(asset['id']),'DELETE')
    else:
        refs=api('repos/'+REPO+'/git/matching-refs/tags/v1.2')
        if any(r['ref']=='refs/tags/v1.2' for r in refs):raise RuntimeError('v1.2 tag already exists')
        draft=api('repos/'+REPO+'/releases','POST',{'tag_name':TAG,'target_commitish':target,'name':'CS2 LJ Overlay v1.2 - Windows x64','body':notes,'draft':True,'prerelease':False})
    if not draft['draft'] or draft['tag_name']!=TAG:raise RuntimeError('Unexpected draft response')
    release_id=draft['id']
    print('Prepared owned draft',release_id,draft['tag_name'],flush=True)
    uploaded_assets=[]
    for name in NAMES:
        url='https://uploads.github.com/repos/'+REPO+'/releases/'+str(release_id)+'/assets?'+urllib.parse.urlencode({'name':name})
        request=urllib.request.Request(url,data=(dist/name).read_bytes(),method='POST',headers={'Authorization':'Bearer '+os.environ['GH_TOKEN'],'Accept':'application/vnd.github+json','Content-Type':'application/zip' if name.endswith('.zip') else 'application/octet-stream','User-Agent':'CS2-LJ-Overlay-release-publisher','X-GitHub-Api-Version':'2022-11-28'})
        with urllib.request.urlopen(request,timeout=180) as response:asset=json.loads(response.read().decode('utf-8'))
        if asset['name']!=name or asset.get('size')!=(dist/name).stat().st_size:raise RuntimeError('Uploaded asset metadata mismatch')
        if asset.get('digest') and asset['digest']!='sha256:'+expected[name]:raise RuntimeError('Server asset digest mismatch')
        uploaded_assets.append(asset)
        print('Uploaded verified metadata',name,asset['id'],flush=True)
    if {a['name'] for a in uploaded_assets}!=set(NAMES):raise RuntimeError('Wrong uploaded asset set')
    with tempfile.TemporaryDirectory(prefix='lj-v12-verify-') as folder:
        for asset in uploaded_assets:
            name=asset['name']
            with (Path(folder)/name).open('wb') as output:
                subprocess.run(['gh','api','repos/'+REPO+'/releases/assets/'+str(asset['id']),'-H','Accept: application/octet-stream'],stdout=output,check=True)
            if checksum((Path(folder)/name).read_bytes())!=expected[name]:raise RuntimeError('Uploaded draft bytes mismatch: '+name)
            print('Verified draft download',name,flush=True)
    # Explicitly retain tag_name on every draft edit; do not look drafts up by tag.
    published=api('repos/'+REPO+'/releases/'+str(release_id),'PATCH',{'tag_name':TAG,'target_commitish':target,'draft':False,'make_latest':'true'})
    if published['draft'] or published['tag_name']!=TAG:raise RuntimeError('Release was not published as v1.2')
    links={}
    for asset in uploaded_assets:
        name=asset['name'];url='https://github.com/'+REPO+'/releases/download/'+TAG+'/'+name;last=None
        for attempt in range(10):
            try:
                request=urllib.request.Request(url,headers={'User-Agent':'CS2-LJ-Overlay-release-check'})
                with urllib.request.urlopen(request,timeout=90) as response:data=response.read()
                if checksum(data)!=expected[name]:raise RuntimeError('Public download hash mismatch')
                links[name]=url
                print('Verified public download',name,flush=True)
                break
            except Exception as error:last=error;time.sleep(3)
        else:raise RuntimeError('Public download failed: '+name) from last
    if fingerprint(api('repos/'+REPO+'/releases/tags/v1.1'))!=baseline:raise RuntimeError('v1.1 release changed')
    if api('repos/'+REPO+'/git/ref/tags/v1.1')['object']['sha']!=old_tag:raise RuntimeError('v1.1 tag changed')
    # Remove only never-published, untagged temporary drafts created by this task.
    for temporary_id in spec.get('owned_temporary_draft_ids',[]):
        if temporary_id in (release_id,old['id']):raise RuntimeError('Unsafe temporary draft identifier')
        temporary=api('repos/'+REPO+'/releases/'+str(temporary_id))
        if temporary['draft'] and temporary['tag_name'].startswith('untagged-') and temporary['body'].strip()==notes.strip():
            api('repos/'+REPO+'/releases/'+str(temporary_id),'DELETE')
    result={'result':'PASS','tag':TAG,'commit':target,'release_url':published['html_url'],'asset_urls':links,'asset_sha256':expected,'v1.1':'unchanged','checks':'draft and public download hashes, exact asset list, v1.1 release and tag preserved'}
    (ROOT/'reports/RELEASE_PUBLICATION_RESULT.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))
if __name__=='__main__':
    if '--probe-state' in sys.argv:
        spec=json.loads((ROOT/'release-v1.2.json').read_text(encoding='utf-8'))
        current=api('repos/'+REPO+'/releases/'+str(spec['owned_draft_release_id'])) if spec.get('owned_draft_release_id') else None
        print('published='+('true' if current and not current['draft'] else 'false'))
    else:main()

