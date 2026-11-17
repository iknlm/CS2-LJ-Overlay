"""Publish v1.2 using gh on GitHub Actions, then verify public download bytes.
Existing published releases and tags are never overwritten or deleted.
"""
from pathlib import Path
import hashlib, json, os, subprocess, tempfile, time, urllib.request, urllib.parse, zipfile

ROOT=Path(__file__).resolve().parent
REPO='iknlm/CS2-LJ-Overlay'
TAG='v1.2'
NAMES=['LJOverlay_v1.2_win64.zip','LJOverlay_new.exe','SHA256SUMS.txt']
def api(endpoint,method='GET',data=None):
    command=['gh','api',endpoint]
    if method!='GET':command+=['--method',method]
    if data is not None:command+=['--input','-']
    output=subprocess.check_output(command,input=json.dumps(data) if data is not None else None,text=True,encoding='utf-8')
    return json.loads(output) if output.strip() else None
def gh(*args):
    subprocess.run(['gh',*map(str,args)],check=True)
def fingerprint(release):
    return {k:release[k] for k in ['id','tag_name','target_commitish','name','body','draft','prerelease','created_at','published_at','updated_at']} | {
        'assets':[{k:asset.get(k) for k in ['id','name','size','digest','updated_at']} for asset in release['assets']]}
def checksum(data):return hashlib.sha256(data).hexdigest()
def main():
    target=os.environ['GITHUB_SHA']
    dist=ROOT/'dist'
    for name in NAMES:
        if not (dist/name).is_file():raise RuntimeError('Missing verified asset: '+name)
    expected={name:checksum((dist/name).read_bytes()) for name in NAMES}
    checks={}
    for line in (dist/'SHA256SUMS.txt').read_text().splitlines():
        value,name=line.split('  ',1);checks[name]=value
    if checks!={name:expected[name] for name in NAMES[:2]}:raise RuntimeError('Outer manifest mismatch')
    with zipfile.ZipFile(dist/NAMES[0]) as z:
        if z.testzip() is not None:raise RuntimeError('ZIP CRC failed')
        for line in z.read('SHA256SUMS.txt').decode('utf-8').splitlines():
            value,name=line.split('  ',1)
            if checksum(z.read(name))!=value:raise RuntimeError('ZIP internal manifest failed')
    old=api('repos/'+REPO+'/releases/tags/v1.1')
    old_tag=api('repos/'+REPO+'/git/ref/tags/v1.1')
    baseline=fingerprint(old)
    releases=api('repos/'+REPO+'/releases?per_page=100')
    existing=next((r for r in releases if r['tag_name']==TAG),None)
    if existing and not existing['draft']:
        raise RuntimeError('Published v1.2 already exists; refusing to modify it')
    if existing:
        spec=json.loads((ROOT/'release-v1.2.json').read_text(encoding='utf-8'))
        if existing['id']!=spec.get('owned_draft_release_id'):
            raise RuntimeError('Unrecognized existing draft; refusing changes')
        if existing['body'].strip()!=(ROOT/'RELEASE_NOTES_v1.2.md').read_text(encoding='utf-8').strip():
            raise RuntimeError('Draft notes changed outside this publication task')
        if any(a['name'] not in NAMES for a in existing['assets']):
            raise RuntimeError('Unrecognized asset in the owned draft')
        # Only this task-created, never-published v1.2 draft may be resumed.
        api('repos/'+REPO+'/releases/'+str(existing['id']),'PATCH',{'target_commitish':target})
        for asset in existing['assets']:
            api('repos/'+REPO+'/releases/assets/'+str(asset['id']),'DELETE')
    else:
        refs=api('repos/'+REPO+'/git/matching-refs/tags/v1.2')
        if any(r['ref']=='refs/tags/v1.2' for r in refs):raise RuntimeError('v1.2 tag already exists; refusing to move it')
        api('repos/'+REPO+'/releases','POST',{'tag_name':TAG,'target_commitish':target,
            'name':'CS2 LJ Overlay v1.2 - Windows x64','body':(ROOT/'RELEASE_NOTES_v1.2.md').read_text(encoding='utf-8'),
            'draft':True,'prerelease':False})
    draft=next(r for r in api('repos/'+REPO+'/releases?per_page=100') if r['tag_name']==TAG)
    release_id=draft['id']
    for name in NAMES:
        url='https://uploads.github.com/repos/'+REPO+'/releases/'+str(release_id)+'/assets?'+urllib.parse.urlencode({'name':name})
        request=urllib.request.Request(url,data=(dist/name).read_bytes(),method='POST',headers={
            'Authorization':'Bearer '+os.environ['GH_TOKEN'],'Accept':'application/vnd.github+json',
            'Content-Type':'application/zip' if name.endswith('.zip') else 'application/octet-stream',
            'User-Agent':'CS2-LJ-Overlay-release-publisher','X-GitHub-Api-Version':'2022-11-28'})
        with urllib.request.urlopen(request,timeout=180) as response:
            uploaded=json.loads(response.read().decode('utf-8'))
        if uploaded['name']!=name:raise RuntimeError('Upload returned the wrong asset name')
    draft=api('repos/'+REPO+'/releases/'+str(release_id))
    if not draft['draft'] or {a['name'] for a in draft['assets']}!=set(NAMES):
        raise RuntimeError('Draft release asset list mismatch')
    with tempfile.TemporaryDirectory(prefix='lj-v12-verify-') as folder:
        for name in NAMES:
            asset=next(a for a in draft['assets'] if a['name']==name)
            with (Path(folder)/name).open('wb') as output:
                subprocess.run(['gh','api','repos/'+REPO+'/releases/assets/'+str(asset['id']),'-H','Accept: application/octet-stream'],stdout=output,check=True)
            if checksum((Path(folder)/name).read_bytes())!=expected[name]:
                raise RuntimeError('Uploaded draft asset hash mismatch: '+name)
    # Publish only after the uploaded draft bytes match the tested local package.
    published=api('repos/'+REPO+'/releases/'+str(release_id),'PATCH',{'draft':False,'make_latest':'true'})
    if published['draft']:raise RuntimeError('Release remains draft')
    links={}
    for asset in published['assets']:
        name=asset['name'];url=asset['browser_download_url'];last=None
        for attempt in range(10):
            try:
                request=urllib.request.Request(url,headers={'User-Agent':'CS2-LJ-Overlay-release-check'})
                with urllib.request.urlopen(request,timeout=90) as response:
                    data=response.read()
                if checksum(data)!=expected[name]:raise RuntimeError('Public asset hash mismatch: '+name)
                links[name]=url;break
            except Exception as error:
                last=error;time.sleep(3)
        else:raise RuntimeError('Public download failed: '+name) from last
    if fingerprint(api('repos/'+REPO+'/releases/tags/v1.1'))!=baseline:
        raise RuntimeError('v1.1 metadata/assets changed')
    if api('repos/'+REPO+'/git/ref/tags/v1.1')['object']['sha']!=old_tag['object']['sha']:
        raise RuntimeError('v1.1 tag changed')
    result={'result':'PASS','tag':TAG,'commit':target,'release_url':published['html_url'],
        'asset_urls':links,'asset_sha256':expected,'v1.1':'unchanged',
        'checks':'draft download hashes, public download hashes, release asset list, v1.1 release and tag invariants'}
    (ROOT/'reports/RELEASE_PUBLICATION_RESULT.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
