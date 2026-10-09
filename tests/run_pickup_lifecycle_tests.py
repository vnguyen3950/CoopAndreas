"""Compile frozen actual pickup registry/codecs. Root owns final enum allocation."""
import argparse,hashlib,json,subprocess,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
INPUTS=('shared/network/pickup_lifecycle.h','shared/network/packets/pickups.h','shared/network/packet.h','shared/network/packet_types.h','third_party/serialize.h')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);parser.add_argument('--mutation',action='store_true');args=parser.parse_args();out=args.output.resolve()
    if out.exists()or ROOT/'.cache'not in out.parents:parser.error('Choose a new worktree .cache directory')
    out.mkdir(parents=True);before={name:sha(ROOT/name)for name in INPUTS}
    for name in INPUTS:
        target=out/'source'/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,target)
    enum=out/'source/shared/network/packet_types.h';text=enum.read_text(encoding='utf-8-sig')
    if 'PICKUP_HELLO'not in text:
        text=text.replace('    PACKET_ID_MAX','    PICKUP_HELLO,\n    PICKUP_STATE,\n    PICKUP_ACTION,\n    PACKET_ID_MAX')
        text=text.replace('"FIRE_BIND"};','"FIRE_BIND", "PICKUP_HELLO", "PICKUP_STATE", "PICKUP_ACTION"};')
        enum.write_text(text,encoding='utf-8')
    if args.mutation:
        path=out/'source/shared/network/pickup_lifecycle.h';text=path.read_text();needle='row->stage!=Stage::Active||'
        assert text.count(needle)==1;path.write_text(text.replace(needle,'',1),encoding='utf-8')
    test=ROOT/'tests/pickup_lifecycle_tests.cpp';testHash=sha(test);shutil.copyfile(test,out/'tests.cpp')
    cmd=['cl.exe','/nologo','/std:c++17','/EHsc','/W4','/WX','/O2','/I'+str(out/'source/shared'),'/I'+str(out/'source/third_party'),str(out/'tests.cpp'),'/Fe:'+str(out/'tests.exe'),'/Fo:'+str(out/'tests.obj')]
    env=r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
    (out/'compile.cmd').write_text('@echo off\ncall "'+env+'" -arch=x86 -host_arch=x64 > environment.log 2>&1\nif errorlevel 1 exit /b 1\n'+subprocess.list2cmdline(cmd)+'\n')
    c=subprocess.run(['cmd.exe','/d','/c',str(out/'compile.cmd')],cwd=out,capture_output=True,text=True);(out/'compile.log').write_text(c.stdout+c.stderr,encoding='utf-8')
    record={'ProductionHashes':before,'TestHash':testHash,'CompilerCommand':cmd,'CompileExitCode':c.returncode,'RuntimeValidated':False,'EnumOverlayOnly':'Reserved names added to frozen test copy; production enum untouched','Mutation':args.mutation}
    if not c.returncode:
        run=subprocess.run([str(out/'tests.exe')],cwd=out,capture_output=True,text=True);record.update(TestExitCode=run.returncode,Output=run.stdout+run.stderr);(out/'test.log').write_text(record['Output'],encoding='utf-8')
    record['InputsStable']=before=={name:sha(ROOT/name)for name in INPUTS}and sha(test)==testHash
    record['Pass']=record['InputsStable']and c.returncode==0 and record.get('TestExitCode')==(1 if args.mutation else 0)
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8');print(json.dumps({'Pass':record['Pass'],'Output':record.get('Output',c.stdout+c.stderr),'Evidence':str(out/'result.json')},indent=2))
    return 0 if record['Pass']else 1
if __name__=='__main__':raise SystemExit(main())
