# SPDX-License-Identifier: MIT
# Copyright (c) 2026 IKNLM
from pathlib import Path
import hashlib, shutil, zipfile
ROOT = Path(__file__).resolve().parent
NAME = "LJOverlay_v1.1_CommunityEdition_win64"
release = ROOT / "release"
release.mkdir(exist_ok=True)
stage = release / NAME
source = ROOT / "CommunityEdition"
for path in (stage, source):
    path.mkdir(exist_ok=True)
    if any(path.iterdir()):
        raise SystemExit(f"Refusing to overwrite nonempty output: {path}")
docs = ["README_RU.txt", "README_EN.txt", "SOUND_CREDITS.txt", "LICENSE", "AUTHORS.md", "README.md",
        "CHANGELOG.txt", "BUILD_LOG.txt", "VERIFICATION_RU.txt"]
for name in docs + ["LJOverlay_new.exe"]:
    shutil.copy2(ROOT / name, stage / name)
for folder in ["sounds", "licenses"]:
    shutil.copytree(ROOT / folder, stage / folder)
for name in docs + ["lj_overlay.cpp", "sound_system.h", "BUILD_EXE.bat",
        "STOP_OLD_OVERLAY.ps1", ".gitignore", "sample_jump_console.log",
        "verification_test.cpp", "sound_test.cpp", "sound_settings_test.cpp",
        "SOUND_SETTINGS_TEST_LOG.txt", "EXE_SMOKE_TEST_LOG.txt", "SOUND_TEST_LOG.txt",
        "COMMUNITY_TEST_LOG.txt", "COMMUNITY_PARSER_TEST.txt", "SOURCE_CHANGES.diff",
        "generate_sounds.py", "prepare_release.py"]:
    shutil.copy2(ROOT / name, source / name)
for folder in ["sounds", "licenses"]:
    shutil.copytree(ROOT / folder, source / folder)
(source / "sound_source").mkdir()
for name in ["Godlike.wav", "Ownage.wav"]:
    shutil.copy2(ROOT / "sound_source" / name, source / "sound_source" / name)
files = sorted(p for p in stage.rglob("*") if p.is_file())
manifest = "".join(hashlib.sha256(p.read_bytes()).hexdigest()+"  "+p.relative_to(stage).as_posix()+"\n" for p in files)
(stage / "SHA256SUMS.txt").write_text(manifest, encoding="utf-8")
archive = release / (NAME + ".zip")
with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for p in sorted(stage.rglob("*")):
        if p.is_file(): z.write(p, NAME+"/"+p.relative_to(stage).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    expected = {NAME+"/"+p.relative_to(stage).as_posix() for p in stage.rglob("*") if p.is_file()}
    assert set(z.namelist()) == expected
    for item in z.infolist():
        assert z.read(item.filename) == (release / item.filename).read_bytes()
        assert not item.filename.endswith((".ini", ".csv", ".tmp", ".bak", ".disabled", ".bmp", ".png", ".7z"))
    assert sum(x.endswith(".exe") for x in z.namelist()) == 1
    assert sum(x.endswith(".wav") for x in z.namelist()) == 11
    listing = "\n".join(f"{i.file_size:>9}  {i.filename}" for i in z.infolist())+"\n"
(release / "ZIP_CONTENTS.txt").write_text(listing, encoding="utf-8")
(release / "ZIP_SHA256.txt").write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+"  "+archive.name+"\n", encoding="utf-8")
print("PASS CRC, exact allowlist, byte comparison, one EXE, eleven WAVs, no personal/temporary files")
print(archive)
