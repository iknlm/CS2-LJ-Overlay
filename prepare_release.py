"""Build the Windows player ZIP and release assets from an already-tested EXE.
No third-party Python modules, compiler or user settings are needed.
"""
from pathlib import Path
import argparse, hashlib, json, shutil, struct, wave, zipfile

ROOT = Path(__file__).resolve().parent
def digest(data): return hashlib.sha256(data).hexdigest()
def inspect_pe(path):
    data = path.read_bytes()
    if data[:2] != b'MZ': raise ValueError('Not a Windows EXE')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe+4] != b'PE\0\0': raise ValueError('Invalid PE signature')
    machine, count = struct.unpack_from('<HH', data, pe+4)
    optional_size = struct.unpack_from('<H', data, pe+20)[0]
    optional = pe+24
    if machine != 0x8664 or struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise ValueError('Expected Windows x64 PE32+')
    if struct.unpack_from('<H', data, optional+68)[0] != 2:
        raise ValueError('Expected Windows GUI subsystem')
    sections=[]
    for n in range(count):
        offset=optional+optional_size+n*40
        virtual_size, virtual_address, raw_size, raw_offset=struct.unpack_from('<IIII',data,offset+8)
        sections.append((virtual_address,max(virtual_size,raw_size),raw_offset))
    def offset(rva):
        for address,size,raw in sections:
            if address <= rva < address+size: return raw+rva-address
        raise ValueError('Import RVA outside sections')
    import_rva=struct.unpack_from('<I',data,optional+112+8)[0]
    imports=[]
    if import_rva:
        current=offset(import_rva)
        while data[current:current+20] != b'\0'*20:
            name_rva=struct.unpack_from('<I',data,current+12)[0]
            start=offset(name_rva)
            imports.append(data[start:data.index(b'\0',start)].decode('ascii'))
            current+=20
    system_names={'advapi32.dll','gdi32.dll','kernel32.dll','user32.dll','shell32.dll',
        'winmm.dll','ntdll.dll','msvcrt.dll','ucrtbase.dll','ole32.dll','oleaut32.dll',
        'comdlg32.dll','comctl32.dll','ws2_32.dll','rpcrt4.dll','version.dll','bcrypt.dll'}
    for name in imports:
        if name.lower() not in system_names and not name.lower().startswith(('api-ms-win-','ext-ms-win-')):
            raise ValueError('Non-system PE dependency: '+name)
    if 'winmm.dll' not in [s.lower() for s in imports]: raise ValueError('Missing WinMM import')
    return {'machine':'AMD64 (0x8664)','format':'PE32+','subsystem':'Windows GUI',
        'imports':imports,'exe_sha256':digest(data),'exe_bytes':len(data)}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,default=ROOT/'LJOverlay_new.exe')
    parser.add_argument('--output',type=Path,default=ROOT/'dist')
    args=parser.parse_args()
    metadata=inspect_pe(args.exe)
    out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=True)
    archive=out/'LJOverlay_v1.2_win64.zip'
    standalone=out/'LJOverlay_new.exe'
    sums=out/'SHA256SUMS.txt'
    for target in (archive,standalone,sums):
        if target.exists(): raise SystemExit('Refusing to overwrite '+target.name)
    files={'LJOverlay_new.exe':args.exe.read_bytes()}
    docs={'docs/PLAYER_README_EN.txt':'README.txt','docs/PLAYER_README_RU.txt':'README_RU.txt',
        'templates/lj_overlay.ini.example':'lj_overlay.ini.example','LICENSE':'LICENSE',
        'AUTHORS.md':'AUTHORS.md','SOUND_CREDITS.txt':'SOUND_CREDITS.txt',
        'THIRD_PARTY_NOTICES.md':'THIRD_PARTY_NOTICES.md','CHANGELOG.md':'CHANGELOG.md',
        'RELEASE_NOTES_v1.2.md':'RELEASE_NOTES_v1.2.md'}
    for src,dst in docs.items(): files[dst]=(ROOT/src).read_bytes()
    for folder,suffix in [('sounds','.wav'),('licenses','.txt')]:
        for path in sorted((ROOT/folder).glob('*'+suffix)):
            files[folder+'/'+path.name]=path.read_bytes()
    if sum(name.startswith('sounds/') for name in files)!=11: raise ValueError('Expected 11 WAV assets')
    for name in files:
        lowered=name.lower()
        if lowered.endswith(('.csv','.log','.dat','.bak','.tmp')) or lowered=='lj_overlay.ini':
            raise ValueError('Personal/temporary file in package: '+name)
    for path in (ROOT/'sounds').glob('*.wav'):
        with wave.open(str(path),'rb') as audio:
            if audio.getsampwidth()!=2 or audio.getnchannels() not in (1,2) or audio.getcomptype()!='NONE':
                raise ValueError('Unsupported audio format: '+path.name)
    manifest=''.join(digest(data)+'  '+name+'\n' for name,data in sorted(files.items()))
    files['SHA256SUMS.txt']=manifest.encode('utf-8')
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for name,data in sorted(files.items()): z.writestr(name,data)
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None or set(z.namelist()) != set(files): raise ValueError('ZIP integrity failed')
        for name,data in files.items():
            if z.read(name)!=data: raise ValueError('ZIP changed '+name)
        for line in manifest.splitlines():
            expected,name=line.split('  ',1)
            if digest(z.read(name))!=expected: raise ValueError('Internal hash failed: '+name)
    shutil.copyfile(args.exe,standalone)
    sums.write_text(digest(archive.read_bytes())+'  '+archive.name+'\n'+digest(standalone.read_bytes())+'  '+standalone.name+'\n',encoding='ascii')
    metadata.update(zip_bytes=archive.stat().st_size,zip_sha256=digest(archive.read_bytes()),
        zip_files=sorted(files),checks='PASS PE imports, PCM16 assets, no personal data, ZIP CRC/inventory/bytes/internal and external hashes')
    (ROOT/'reports').mkdir(exist_ok=True)
    (ROOT/'reports/RELEASE_PACKAGE_CHECK.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(metadata,indent=2))
if __name__=='__main__': main()
