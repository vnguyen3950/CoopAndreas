"""Frozen actual fire services/codecs with native doubles, plus a required owner-guard mutation."""
import argparse, hashlib, json, os, re, shutil, subprocess, sys
from pathlib import Path

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--msvc-env',type=Path,default=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'))
    parser.add_argument('--protocol-only',action='store_true')
    a=parser.parse_args();owned=Path(__file__).resolve().parent;source=owned.parents[1];out=a.output.resolve()
    if source/'.cache'/'fire-headless' not in out.parents or out.exists(): parser.error('Output must be a new .cache/fire-headless child')
    out.mkdir(parents=True);snapshot=out/'source'
    inputs=['shared/network/fire_sync.h','shared/network/packets/fires.h','shared/network/packet.h','shared/network/packet_types.h',
            'shared/network/object_sync.h','third_party/serialize.h','client/src/CFireSync.cpp','client/src/CFireSync.h',
            'client/src/CFireSyncNative.cpp','server/src/CFireSync.cpp','server/src/CFireSync.h']
    if a.protocol_only: inputs=[n for n in inputs if n.startswith('shared/') or n=='third_party/serialize.h']
    hashes={n:sha(source/n) for n in inputs}
    for n in inputs:
        (snapshot/n).parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/n,snapshot/n)
    tests=['fire_core_tests','fire_codec_tests','fire_client_tests','fire_server_tests']
    if a.protocol_only: tests=['fire_core_tests','fire_codec_tests']
    support=([] if a.protocol_only else ['client_doubles.h','server_doubles.h'])+[n+'.cpp' for n in tests]
    supports={n:sha(owned/n) for n in support}
    for n in support:shutil.copyfile(owned/n,out/n)
    extraction={}
    for side in ([] if a.protocol_only else ['client','server']):
        t=(snapshot/f'{side}/src/CFireSync.cpp').read_text();t=re.sub(r'^#include[^\n]*\n','',t,flags=re.M)
        p=out/f'extracted_{side}.inc';p.write_text(t);extraction[side]=sha(p)
    if not a.protocol_only:
        t=(snapshot/'client/src/CFireSyncNative.cpp').read_text()
        body=t.split('void __cdecl DamageHook(CPed* ped,void* response)',1)[1].split('\n}\n}',1)[0]+'\n}\n'
        p=out/'extracted_damage.inc';p.write_text('void __cdecl DamageHook(CPed* ped,void* response)'+body);extraction['native_damage']=sha(p)
    env=os.environ.copy();env.pop('GTA_SA_DIR',None);results=[]
    def build_run(name,expect_failure=False):
        cmd=['cl.exe','/nologo','/std:c++17','/EHsc','/W4','/WX','/O2']
        for p in [snapshot,snapshot/'shared',snapshot/'third_party',out]:cmd+=['/I'+str(p)]
        cmd+=[str(out/(name+'.cpp')),'/Fe:'+str(out/(name+'.exe')),'/Fo:'+str(out/(name+'.obj'))]
        batch=out/(name+'.cmd');batch.write_text('@echo off\ncall "'+str(a.msvc_env)+'" -arch=x64 -host_arch=x64 > "'+str(out/(name+'-env.log'))+'" 2>&1\nif errorlevel 1 exit /b 1\n'+subprocess.list2cmdline(cmd)+'\n')
        p=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=out,env=env,capture_output=True,text=True)
        (out/(name+'-compile.log')).write_text(p.stdout+p.stderr);item={'Test':name,'CompileExitCode':p.returncode,'Command':cmd}
        if p.returncode:print(p.stdout+p.stderr)
        else:
            p=subprocess.run([str(out/(name+'.exe'))],cwd=out,env=env,capture_output=True,text=True)
            item.update(TestExitCode=p.returncode,Output=p.stdout+p.stderr,ExpectedFailure=expect_failure)
            (out/(name+'-test.log')).write_text(item['Output']);print(name,item['Output'].strip())
        results.append(item)
    for n in tests:build_run(n)
    if not a.protocol_only:
        # Mutate the actual service boundary, not a duplicate test policy.
        p=out/'extracted_client.inc';t=p.read_text();needle='    if (!Ready()) return true;\n    if (!ValidNative(ped)'
        if t.count(needle)!=1:parser.error('Damage guard mutation shape changed')
        p.write_text(t.replace(needle,'    if (ped) return true; // targeted mutation: disable owner/lifetime gate\n    if (!Ready()) return true;\n    if (!ValidNative(ped)',1))
        shutil.copyfile(out/'fire_client_tests.cpp',out/'fire_mutation_tests.cpp');build_run('fire_mutation_tests',True);p.write_text(t)
    stable=all(sha(source/n)==h for n,h in hashes.items()) and all(sha(owned/n)==h for n,h in supports.items())
    passed=stable and all(x.get('CompileExitCode')==0 and ((x.get('TestExitCode')!=0) if x.get('ExpectedFailure') else x.get('TestExitCode')==0) for x in results)
    report={'ProductionHashes':hashes,'SupportHashes':supports,'ExtractionHashes':extraction,'InputsStable':stable,'GTA_SA_DIR_Cleared':True,
            'RuntimeValidated':False,'Tests':results,'Pass':passed}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('Pass:',passed,'Evidence:',out/'result.json');return 0 if passed else 1
if __name__=='__main__':sys.exit(main())
