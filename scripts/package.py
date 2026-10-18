#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Package an existing standalone build without including machine configuration."""
from pathlib import Path
import argparse
import hashlib
import json
import plistlib
import shutil
import subprocess
import zipfile

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--dist',type=Path,help='Output directory (default: repository dist/)')
args=parser.parse_args()
info=plistlib.loads((root/'src/Info.plist').read_bytes())
version=info['CFBundleVersion']
assert plistlib.loads((root/'build/itlmt7922.kext/Contents/Info.plist').read_bytes())==info
app_info=plistlib.loads((root/'build/ItlWiFi7922.app/Contents/Info.plist').read_bytes())
assert app_info['CFBundleShortVersionString']==version
binary=(root/'build/itlmt7922.kext/Contents/MacOS/itlmt7922').read_bytes()
assert version.encode()+b'\0' in binary
for firmware in sorted((root/'fw').glob('*.bin')):
    assert binary.count(firmware.read_bytes())==1,firmware.name
subprocess.run(['codesign','--verify','--deep','--strict',str(root/'build/ItlWiFi7922.app')],check=True)
dist=args.dist.resolve() if args.dist else root/'dist'
dist.mkdir(parents=True,exist_ok=True)
name='itlmt7922-'+version
package=dist/name
if package.exists():shutil.rmtree(package)
package.mkdir()
for artifact in ('itlmt7922.kext','ItlWiFi7922.app'):
    shutil.copytree(root/'build'/artifact,package/artifact,ignore=shutil.ignore_patterns('._*','.DS_Store'))
for filename in ('README.md','README.zh-CN.md','CONTRIBUTING.md','LICENSE','NOTICE.md'):
    shutil.copy2(root/filename,package/filename)
for folder in ('docs','licenses'):
    shutil.copytree(root/folder,package/folder,ignore=shutil.ignore_patterns('._*','.DS_Store'))
shutil.copy2(root/'fw/LICENCE.mediatek',package/'licenses/LICENCE.mediatek')
shutil.copy2(root/'fw/SHA256SUMS',package/'licenses/FIRMWARE-SHA256SUMS')
shutil.copy2(root/'scripts/collect-logs.sh',package/'collect-logs.sh')
def create_zip(source,output):
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for path in sorted(source.rglob('*')):
            if path.is_file():archive.write(path,path.relative_to(source.parent))
    with zipfile.ZipFile(output) as archive:assert archive.testzip() is None
all_in_one=dist/(name+'.zip')
create_zip(package,all_in_one)
archives=[all_in_one]
for artifact in ('itlmt7922.kext','ItlWiFi7922.app'):
    output=dist/(artifact+'-'+version+'.zip')
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        paths=list((package/artifact).rglob('*'))+list((package/'licenses').rglob('*'))+list((package/'docs').rglob('*'))
        paths.extend(package/name for name in ('LICENSE','NOTICE.md','CONTRIBUTING.md','README.md','README.zh-CN.md'))
        paths.append(package/'collect-logs.sh')
        for path in sorted(paths):
            if path.is_file():archive.write(path,path.relative_to(package))
    with zipfile.ZipFile(output) as archive:assert archive.testzip() is None
    archives.append(output)
checksums={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(archives)}
(dist/'SHA256SUMS.txt').write_text(''.join(digest+'  '+name+'\n' for name,digest in checksums.items()))
manifest={'version':version,'architecture':'x86_64','kernel_bundle':info['CFBundleIdentifier'],
          'firmware_embedded':True,'binary_sha256':hashlib.sha256(binary).hexdigest(),'assets':checksums}
try:
    manifest['source_commit']=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],stderr=subprocess.DEVNULL).decode().strip()
except subprocess.CalledProcessError:
    pass
(dist/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
for name in checksums:print(dist/name)
