"""Actual NativeInit extraction with recorded fixed-address memory, never a game process."""
import argparse,hashlib,json,os,re,shutil,struct,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest().upper()
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();out=args.output.resolve()
 if out.exists()or ROOT/'.cache'not in out.parents:parser.error('Fresh worktree .cache output required')
 out.mkdir(parents=True);inputs=['client/src/CPickupSyncNative.cpp','shared/runtime_diagnostics.h','tests/pickup_bootstrap_tests.cpp','tests/run_pickup_bootstrap_tests.py']
 before={name:sha(ROOT/name)for name in inputs}
 for name in inputs:
  dst=out/'source'/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,dst)
 text=(out/'source/client/src/CPickupSyncNative.cpp').read_text();text=re.sub(r'^#include[^\n]*\n?','',text,flags=re.M)
 # Only native memory reads are mapped to recorded cells. Branches, expected
 # bytes, original allocation and activation decisions remain production code.
 text=text.replace('reinterpret_cast<const void*>(address)','SnapshotAddress(address)')
 # VirtualQuery must keep numeric addresses so its recorded map can validate
 # absent ranges before any dereference. Preserve that actual call boundary.
 text=text.replace('VirtualQuery(SnapshotAddress(address),','VirtualQuery(reinterpret_cast<const void*>(address),')
 text=text.replace('reinterpret_cast<const uint8_t*>(address)','SnapshotAddress(address)').replace('reinterpret_cast<const uint8_t*>(source)','SnapshotAddress(source)')
 (out/'bootstrap_native.inc').write_text(text)
 exe=Path(r'C:\Users\Vu\work\gta-coop\game-lab\gta_sa.exe');binary=exe.read_bytes();assert sha(exe)=='A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26'
 import pefile
 pe=pefile.PE(data=binary);image={hex(address):binary[pe.get_offset_from_rva(address-pe.OPTIONAL_HEADER.ImageBase):][:32].hex()for address in [0x456F20,0x4556C0,0x4555A0,0x4591D0,0x4590F0,0x1564140,0x45902E,0x459095]}
 code='static std::map<uintptr_t,std::array<uint8_t,32>>RecordedImage(){return {\n'+',\n'.join('{'+addr+', {'+','.join(str(byte)for byte in bytes.fromhex(raw))+'}}'for addr,raw in image.items())+'\n};}'
 (out/'bootstrap_image.inc').write_text(code);shutil.copyfile(ROOT/'tests/pickup_bootstrap_tests.cpp',out/'tests.cpp')
 command=['cl.exe','/nologo','/std:c++17','/EHsc','/W4','/WX','/O2','/I'+str(out),str(out/'tests.cpp'),'/Fe:'+str(out/'tests.exe'),'/Fo:'+str(out/'tests.obj')]
 batch=out/'compile.cmd';batch.write_text('@echo off\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\Common7\\Tools\\VsDevCmd.bat" -arch=x86 -host_arch=x64 > environment.log 2>&1\nif errorlevel 1 exit /b 1\n'+subprocess.list2cmdline(command)+'\n')
 env=dict(os.environ);env.pop('GTA_SA_DIR',None);c=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=out,env=env,capture_output=True,text=True);(out/'compile.log').write_text(c.stdout+c.stderr)
 report={'ProductionAndSupportHashes':before,'CompileExitCode':c.returncode,'NativeExeSHA256':sha(exe),'NativeExecution':False,'MemoryReadOverlay':'Only fixed-address reads recorded; actual branches unchanged','ExtractionSHA256':sha(out/'bootstrap_native.inc'),'RecordedImage':image}
 if c.returncode==0:
  result=subprocess.run([str(out/'tests.exe')],cwd=out,env=env,capture_output=True,text=True);report.update(TestExitCode=result.returncode,Output=result.stdout+result.stderr)
  for line in result.stdout.splitlines():
   if line.startswith('JSON '):json.loads(line[5:])
  report['DiagnosticJSONValidated']=True
 else:report['Output']=c.stdout+c.stderr
 report['InputsStable']=before=={name:sha(ROOT/name)for name in inputs};report['Pass']=report['InputsStable']and c.returncode==0 and report.get('TestExitCode')==0
 (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'Pass':report['Pass'],'Output':report['Output'],'Evidence':str(out/'result.json')},indent=2));return 0 if report['Pass']else 1
if __name__=='__main__':raise SystemExit(main())
