"""Freeze real NPC packet/state and SDK enum inputs; no game or native execution."""
import argparse, hashlib, importlib.util, json, os, re, shutil, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
SDK='third_party/plugin-sdk/plugin_sa/game_sa/'
INPUTS=['shared/config.h','shared/network/packet.h','shared/network/packet_types.h','third_party/serialize.h',
 'shared/network/npc_sync.h','shared/network/packets/peds.h','shared/network/packets/players.h',
 'shared/network/CNetworkEntitySerializer.h','shared/network/object_sync.h','shared/network/eNetworkEntityType.h',
 'shared/network/serializable_types.h','shared/math_utils.h']+[SDK+n for n in
 ('ePedState.h','eModelID.h','ePedType.h','CPed.h','CVehicle.h','CWeapon.h','eWeaponType.h','eGlobalSpeechContexts.h')]
SERVER_INPUTS=['server/src/CNetworkPed.h','server/src/CNetworkPed.cpp','server/src/CNetworkPedManager.h','server/src/CNetworkPedManager.cpp','server/src/PacketHandlers/peds.cpp','shared/network/packet_handler.h','shared/network/vehicle_authority.h','client/src/CPacketBuffer.cpp','server/src/CPacketFactory.cpp']
CLIENT_INPUTS=['client/src/CNetworkPed.h','client/src/CNetworkPed.cpp','client/src/CNetworkPedManager.h','client/src/CNetworkPedManager.cpp','client/src/PacketHandlers/peds.cpp','client/src/Hooks/PedHooks.cpp','shared/network/packet_handler.h',SDK+'CCopPed.h',SDK+'eCopType.h']
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def block(text,pattern):
 m=re.search(pattern,text,re.S)
 if not m: raise ValueError(pattern)
 start=text.index('{',m.start());depth=1
 masked=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),text,flags=re.S)
 for end in range(start+1,len(text)):
  depth+=(masked[end]=='{')-(masked[end]=='}')
  if not depth:return text[m.start():end+2]
 raise ValueError('Unbalanced extraction')
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--suite',choices=['codec','server','client','passenger','police','deathserver'],default='codec');p.add_argument('--mutate-generation',action='store_true');p.add_argument('--mutate-replay-time',action='store_true');p.add_argument('--mutate-death-producer',action='store_true');a=p.parse_args();out=a.output.resolve()
 if out.exists() or ROOT/'.cache' not in out.parents:p.error('Fresh worktree .cache directory required')
 inputs=INPUTS+(SERVER_INPUTS if a.suite in ('server','deathserver') else CLIENT_INPUTS if a.suite in ('client','passenger','police') else [])
 out.mkdir(parents=True);before={n:sha(ROOT/n) for n in inputs}
 test_name='death_server_tests.cpp' if a.suite=='deathserver' else 'police_tests.cpp' if a.suite=='police' else 'server_tests.cpp' if a.suite in ('server','deathserver') else 'passenger_counterexamples.cpp' if a.suite=='passenger' else 'client_tests.cpp' if a.suite=='client' else 'tests.cpp'
 support=['run_tests.py',test_name]+(['server_doubles.h'] if a.suite in ('server','deathserver') else ['client_doubles.h'] if a.suite in ('client','passenger','police') else [])
 support_before={n:sha(HERE/n) for n in support}
 for n in inputs:
  target=out/'source'/n;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/n,target)
 read=lambda n:(out/'source'/n).read_text(encoding='utf-8-sig')
 enums=[]
 for n,names in [('ePedState.h',['ePedState']),('eModelID.h',['eModelID']),('ePedType.h',['ePedType','eCharCreatedBy']),('CPed.h',['eMoveState']),('CVehicle.h',['eVehicleType','eDoorLock']),('CWeapon.h',['eWeaponState']),('eWeaponType.h',['eWeaponType']),('eGlobalSpeechContexts.h',['eGlobalSpeechContexts'])]:
  enums += [block(read(SDK+n),r'enum\s+(?:PLUGIN_API\s+)?'+name+r'\b[^{}]*\{') for name in names]
 fragments=['#define PLUGIN_API',*enums]
 math=read('shared/math_utils.h')
 (out/'extracted_math.h').write_text('\n'.join(re.findall(r'const float (?:PI|TWO_PI)[^;]+;',math))+'\n'+block(math,r'inline static float NormalizeRadianAngle\([^{}]*\{'))
 vectors=read('shared/network/serializable_types.h')
 fragments += [block(vectors,r'struct\s+'+n+r'\b[^{}]*\{') for n in ('WorldPositionCompressed','MoveSpeedCompressed','NormalizedVector','RadianAngleCompressed')]
 players=read('shared/network/packets/players.h')
 fragments+=['namespace Packets::Players {\n'+ '\n'.join(block(players,r'struct\s+'+n+r'\b[^{}]*\{') for n in ('SHealthSnapshot','SWeaponSnapshot'))+'\n}']
 fragments+=['#include "network/CNetworkEntitySerializer.h"',re.sub(r'^#include[^\n]*\n|^#pragma once\n','',read('shared/network/packets/peds.h'),flags=re.M)]
 (out/'extracted_packet.h').write_text('\n'.join(fragments))
 includes_only=lambda text:re.sub(r'^#include[^\n]*\n|^#pragma once\n','',text,flags=re.M)
 if a.suite in ('server','deathserver'):
  for name,path in [('server_ped_decl','server/src/CNetworkPed.h'),('server_manager_decl','server/src/CNetworkPedManager.h'),('server_ped','server/src/CNetworkPed.cpp'),('server_manager','server/src/CNetworkPedManager.cpp'),('server_handlers','server/src/PacketHandlers/peds.cpp')]:
   text=includes_only(read(path))
   if name=='server_ped' and a.mutate_generation:
    needle='if (!stamp.Newer(GetStamp())) return false;'
    assert text.count(needle)==1
    text=text.replace(needle,'if (!stamp.State() || stamp.sequence <= m_stateSequence) return false;')
   if name=='server_manager' and a.mutate_replay_time:
    needle='auto packet = ped->m_lastState; packet.serverTime = g_serverTime;'
    assert text.count(needle)==1
    text=text.replace(needle,'auto& packet = ped->m_lastState;')
   if name=='server_manager' and a.mutate_death_producer:
    needle='&& sender->m_vitals.generation == ped->m_deathProducerGeneration'
    assert text.count(needle)==1;text=text.replace(needle,'')
   (out/(name+'.inc')).write_text(text)
  (out/'server_factory.inc').write_text('static uint8_t packetBuffer[10*1024];\n'+block(read('server/src/CPacketFactory.cpp'),r'static void BuildPacketStream\([^{}]*\{'))
  (out/'client_buffer.inc').write_text(block(read('client/src/CPacketBuffer.cpp'),r'void CPacketBuffer::Receive\([^{}]*\{'))
  shutil.copyfile(HERE/'server_doubles.h',out/'server_doubles.h')
 if a.suite in ('client','passenger','police'):
  fragments += [block(read(SDK+'eCopType.h'),r'enum\s+(?:PLUGIN_API\s+)?eCopType\b[^{}]*\{')]
  (out/'extracted_packet.h').write_text('\n'.join(fragments))
  for name,path in [('client_ped_decl','client/src/CNetworkPed.h'),('client_manager_decl','client/src/CNetworkPedManager.h')]:
   (out/(name+'.inc')).write_text(includes_only(read(path)))
  ped=read('client/src/CNetworkPed.cpp');mgr=read('client/src/CNetworkPedManager.cpp');handlers=read('client/src/PacketHandlers/peds.cpp')
  methods=[block(ped,re.escape(signature)+r'[^{}]*\{') for signature in ('CNetworkPed::CNetworkPed(int pedid','CNetworkPed::~CNetworkPed()','bool CNetworkPed::HasValidPed() const','void CNetworkPed::DetachPed()','CNetworkPed* CNetworkPed::CreateHosted(','void CNetworkPed::ApplyWeaponSnapshot(','void CNetworkPed::ApplyReplicaHealth(','void CNetworkPed::CancelClaim()','bool CNetworkPed::NextState(','bool CNetworkPed::AcceptState(','bool CNetworkPed::CanAcceptState(')]
  methods += [block(mgr,re.escape(signature)+r'[^{}]*\{') for signature in ('CNetworkPed* CNetworkPedManager::GetPed(int','CNetworkPed* CNetworkPedManager::GetPed(CEntity*','void CNetworkPedManager::Add(','void CNetworkPedManager::Remove(','void CNetworkPedManager::HandlePedDestruction(','void CNetworkPedManager::RemoveInvalidPeds()','unsigned char CNetworkPedManager::AddToTempList(','bool CNetworkPedManager::AcceptSpawn(','bool CNetworkPedManager::AcceptRemoval(','bool CNetworkPedManager::PinGangWarPedToHost(','void CNetworkPedManager::RequestReset()','void CNetworkPedManager::ProcessPendingReset()','void CNetworkPedManager::Clear()','void CNetworkPedManager::Init()','bool CNetworkPedManager::NativeReady()','bool CNetworkPedManager::GetOwnerDeathIdentity(','bool CNetworkPedManager::Defer(','void CNetworkPedManager::ProcessPendingNative()')]
  for packet in ('PED_SPAWN','PED_CONFIRM','PED_REMOVE','ASSIGN_PED','PED_ONFOOT','PED_DRIVER_UPDATE','PED_PASSENGER_UPDATE','PED_REPLAY','PED_PIN','PED_DEATH'):
   methods += [block(handlers,r'PACKET_HANDLER\(\s*ePacketType::'+packet+r'\b[^{}]*\{')]
  prefix=includes_only(mgr[:mgr.index('CNetworkPed* CNetworkPedManager::GetPed(int')])
  (out/'client_functions.inc').write_text(prefix+'\n\n'+'\n\n'.join(methods))
  shutil.copyfile(HERE/'client_doubles.h',out/'client_doubles.h')
  hook=read('client/src/Hooks/PedHooks.cpp');(out/'cop_control.inc').write_text(block(hook,r'void PedHooks::ProcessCopControl\([^{}]*\{'))
 if a.suite in ('client','passenger','police'):
  reference=ROOT.parent.parent/'gta-reversed/source/game_sa/Entity/Ped/CopPed.cpp'
  native=reference.read_text(encoding='utf-8-sig')
  (out/'cop_native_models.inc').write_text(block(native,r'eModelID CCopPed::GetPedModelForCopType\([^{}]*\{')+'\n'+block(native,r'eModelID ResolveModelForCopType\([^{}]*\{'))
 shutil.copyfile(HERE/test_name,out/'tests.cpp')
 cmd=['cl.exe','/nologo','/std:c++17','/EHsc','/W4','/DNDEBUG','/DNOMINMAX','/DCOOP_CLIENT' if a.suite in ('client','passenger','police') else '/DCOOP_SERVER','tests.cpp','/I'+str(out/'source/shared'),'/I'+str(out/'source/third_party'),'/Fe:tests.exe','/Fo:tests.obj']
 env=r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
 (out/'compile.cmd').write_text('@echo off\ncall "'+env+'" x64_x86 > environment.log 2>&1\nif errorlevel 1 exit /b 1\n'+subprocess.list2cmdline(cmd)+'\n')
 processEnv=os.environ.copy();processEnv.pop('GTA_SA_DIR',None)
 c=subprocess.run(['cmd.exe','/d','/c',str(out/'compile.cmd')],cwd=out,env=processEnv,capture_output=True,text=True)
 (out/'compile.log').write_text(c.stdout+c.stderr)
 result={'ProductionHashes':before,'ExtractedHash':sha(out/'extracted_packet.h'),'TestHash':sha(out/'tests.cpp'),'CompileExitCode':c.returncode,'RuntimeValidated':False}
 if not c.returncode:
  t=subprocess.run([str(out/'tests.exe')],cwd=out,env=processEnv,capture_output=True,text=True);result.update(TestExitCode=t.returncode,Output=t.stdout+t.stderr);(out/'test.log').write_text(result['Output'])
 else:print(c.stdout+c.stderr)
 result['DeathProducerMutation']=a.mutate_death_producer;result['Suite']=a.suite;result['Mutation']=a.mutate_generation;result['ReplayTimeMutation']=a.mutate_replay_time
 result['InputsStable']=before=={n:sha(ROOT/n) for n in inputs}
 result['FrozenInputsMatch']=before=={n:sha(out/'source'/n) for n in inputs}
 result['SupportHashes']=support_before;result['SupportStable']=support_before=={n:sha(HERE/n) for n in support}
 result['ExtractionHashes']={n.name:sha(n) for n in sorted(out.glob('*.inc'))}
 if (out/'client_doubles.h').exists():result['FrozenDoubleHash']=sha(out/'client_doubles.h')
 if (out/'server_doubles.h').exists():result['FrozenDoubleHash']=sha(out/'server_doubles.h')
 result['CompilerCommand']=cmd
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(result.get('Output','Compile failure'))
 return 0 if result['InputsStable'] and result['FrozenInputsMatch'] and result['SupportStable'] and result.get('TestExitCode')==0 else 1
if __name__=='__main__':raise SystemExit(main())
