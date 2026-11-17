from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parent
spec=json.loads((ROOT/'release-v1.2.json').read_text(encoding='utf-8'))
if spec['tag']!='v1.2' or spec['repository']!='iknlm/CS2-LJ-Overlay':raise SystemExit('Wrong release target')
for name,expected in spec['locally_tested_files'].items():
 data=(ROOT/name).read_bytes()
 if not name.lower().endswith('.wav'):data=data.replace(b'\r\n',b'\n')
 if hashlib.sha256(data).hexdigest()!=expected:raise SystemExit('Locally tested source/resource changed: '+name)
report=(ROOT/'reports/TEST_RESULTS.txt').read_text(encoding='utf-8-sig')
if 'PASS all 10 current test executables' not in report or 'FAIL:' in report:raise SystemExit('Local full regression gate is not PASS')
runtime=(ROOT/'reports/RELEASE_ZIP_RUNTIME_TEST.txt').read_text(encoding='utf-8-sig')
if 'PASS native Windows x64 EXE starts' not in runtime or 'PASS live append' not in runtime:raise SystemExit('Actual ZIP runtime gate is not PASS')
print('PASS release source/resource hashes match locally tested full regression and extracted ZIP runtime evidence.')
