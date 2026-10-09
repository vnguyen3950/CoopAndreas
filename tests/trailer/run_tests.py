"""Frozen actual trailer services/codecs with native doubles, plus a required owner-guard mutation."""
import argparse, hashlib, json, os, re, shutil, subprocess, sys
from pathlib import Path

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--msvc-env',type=Path,default=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'))
    parser.add_argument('--protocol-only',action='store_true')
    parser.add_argument('--server-only',action='store_true')
    a=parser.parse_args();owned=Path(__file__).resolve().parent;source=owned.parents[1];out=a.output.resolve()
    if source/'.cache'/'trailer-headless' not in out.parents or out.exists(): parser.error('Output must be a new .cache/trailer-headless child')
    out.mkdir(parents=True);snapshot=out/'source'
    inputs=['shared/network/trailer_sync.h','shared/network/packets/trailers.h','shared/network/packet.h','shared/network/packet_types.h',
            'shared/network/object_sync.h','shared/network/packets/vehicles.h','client/src/PacketHandlers/vehicles.cpp','client/src/CNetworkVehicleManager.cpp','client/src/CNetworkVehicle.h','client/src/CNetworkVehicle.cpp','third_party/serialize.h','client/src/CTrailerSync.cpp','client/src/CTrailerSync.h',
            'client/src/CTrailerSyncNative.cpp','server/src/CTrailerSync.cpp','server/src/CTrailerSync.h']
    if a.protocol_only: inputs=[n for n in inputs if n.startswith('shared/') or n=='third_party/serialize.h']
    if a.server_only: inputs=[n for n in inputs if not n.startswith('client/')]
    hashes={n:sha(source/n) for n in inputs}
    for n in inputs:
        (snapshot/n).parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/n,snapshot/n)
    tests=['trailer_core_tests','trailer_codec_tests','trailer_client_tests','trailer_server_tests','trailer_constructor_tests','trailer_birth_codec_tests']
    if a.protocol_only: tests=['trailer_core_tests','trailer_codec_tests']
    if a.server_only: tests=['trailer_core_tests','trailer_codec_tests','trailer_server_tests']
    support=([] if a.protocol_only else ['server_doubles.h'] if a.server_only else ['client_doubles.h','server_doubles.h'])+[n+'.cpp' for n in tests]
    supports={n:sha(owned/n) for n in support}
    for n in support:shutil.copyfile(owned/n,out/n)
    extraction={}
    for side in ([] if a.protocol_only else ['server'] if a.server_only else ['client','server']):
        t=(snapshot/f'{side}/src/CTrailerSync.cpp').read_text();t=re.sub(r'^#include[^\n]*\n','',t,flags=re.M)
        p=out/f'extracted_{side}.inc';p.write_text(t);extraction[side]=sha(p)
    if not a.protocol_only and not a.server_only:
        vehicle=(source/'shared/network/packets/vehicles.h').read_text()
        fixtures=[]
        for name in ['VehicleSpawn','VehicleConfirm','VehicleRemove','AssignVehicleSyncer']:
            start=vehicle.index('class '+name+' : public Packet');end=vehicle.index('\n};',start)+3;body=vehicle[start:end]
            start_serialize=body.index('    template <typename Stream>');head=body[:start_serialize]
            lines=[line for line in body[start_serialize:].splitlines() if 'if (Stream::IsWriting' in line or 'serialize_int(stream, vehicleid,' in line or
                'serialize_int(stream,generation,' in line or 'serialize_int(stream,requestToken,' in line or 'serialize_int(stream,syncerId,' in line]
            fixtures.append(head+'    template<class Stream> bool Serialize(Stream& stream){\n'+'\n'.join(lines)+'\nreturn true;}};')
        header='#pragma once\nnamespace Config {constexpr int MAX_SERVER_VEHICLES=255;}\nnamespace Packets::Vehicles {\n'+'\n'.join(fixtures)+'\n}\n'
        (out/'vehicle_birth_extracted.h').write_text(header);extraction['vehicle_birth_codec']=sha(out/'vehicle_birth_extracted.h')
        constructors=(source/'client/src/CNetworkVehicle.cpp').read_text()
        start=constructors.index('CNetworkVehicle::CNetworkVehicle(');end=constructors.index('CNetworkVehicle::~CNetworkVehicle()',start)
        (out/'vehicle_constructor_extracted.inc').write_text(constructors[start:end]);extraction['actual_constructor_create']=sha(out/'vehicle_constructor_extracted.inc')
        begin_hosted=constructors.index('CNetworkVehicle* CNetworkVehicle::CreateHosted(');end_hosted=constructors.index('bool CNetworkVehicle::HasValidVehicle()',begin_hosted)
        (out/'vehicle_hosted_extracted.inc').write_text(constructors[begin_hosted:end_hosted]);extraction['actual_create_hosted']=sha(out/'vehicle_hosted_extracted.inc')
        managers=(source/'client/src/CNetworkVehicleManager.cpp').read_text()
        getters=managers[managers.index('CNetworkVehicle* CNetworkVehicleManager::GetVehicle(int id)'):]
        begin_temp=managers.index('uint8_t CNetworkVehicleManager::AddToTempList(');b=managers.index('void CNetworkVehicleManager::RemoveHostedUnused()',begin_temp)
        getters+=managers[begin_temp:b]
        (out/'vehicle_manager_getters.inc').write_text(getters);extraction['vehicle_manager_getters']=sha(out/'vehicle_manager_getters.inc')
        handlers=(source/'client/src/PacketHandlers/vehicles.cpp').read_text();generated=[]
        for name in ['VehicleSpawn','VehicleConfirm','VehicleRemove','AssignVehicleSyncer']:
            match=re.search(r'PACKET_HANDLER\([^\n]*Packets::Vehicles::'+name+r'\*\s*(\w+)\)',handlers)
            if not match:raise ValueError('Handler shape changed: '+name)
            start=handlers.index('{',match.end());depth=1;end=start+1
            while depth:
                if handlers[end]=='{':depth+=1
                elif handlers[end]=='}':depth-=1
                end+=1
            generated.append('void Receive'+name+'(Packets::Vehicles::'+name+'* '+match[1]+')'+handlers[start:end])
        (out/'vehicle_client_handlers.inc').write_text('\n'.join(generated));extraction['vehicle_client_handlers']=sha(out/'vehicle_client_handlers.inc')
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
    if not a.protocol_only and not a.server_only:
        p=out/'extracted_client.inc';original=p.read_text();needle='!Owned(Identity(p))'
        if original.count(needle)!=1:raise ValueError('Native attach ownership guard changed')
        p.write_text(original.replace(needle,'false /* mutation: disable parent ownership */'))
        shutil.copyfile(out/'trailer_client_tests.cpp',out/'trailer_mutation_tests.cpp');build_run('trailer_mutation_tests',True);p.write_text(original)
    stable=all(sha(source/n)==h for n,h in hashes.items()) and all(sha(owned/n)==h for n,h in supports.items())
    passed=stable and all(x.get('CompileExitCode')==0 and ((x.get('TestExitCode')!=0) if x.get('ExpectedFailure') else x.get('TestExitCode')==0) for x in results)
    report={'ProductionHashes':hashes,'SupportHashes':supports,'ExtractionHashes':extraction,'InputsStable':stable,'GTA_SA_DIR_Cleared':True,
            'RuntimeValidated':False,'Tests':results,'Pass':passed}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('Pass:',passed,'Evidence:',out/'result.json');return 0 if passed else 1
if __name__=='__main__':sys.exit(main())
