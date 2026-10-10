"""Freeze actual shop guards/VM send functions, native Buy and wallet observers."""
import argparse, hashlib, json, os, re, shutil, struct, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
WORKSPACE=ROOT.parent.parent if ROOT.parent.name=='worktrees' else ROOT.parent
INPUTS=['client/src/Hooks/PlayerHooks.cpp','client/src/Hooks/MissionAudioHooks.cpp','client/src/COpCodeSync.cpp','client/src/COpCodeSync.h','client/src/CTaskSequenceSync.cpp','client/src/CSessionSync.h','client/src/CSessionSync.cpp','shared/network/session_sync.h','shared/config.h','third_party/plugin-sdk/shared/PluginBase.h','third_party/plugin-sdk/plugin_sa/game_sa/CPlayerPed.cpp','third_party/plugin-sdk/plugin_sa/game_sa/CRunningScript.h','third_party/plugin-sdk/plugin_sa/game_sa/CTheScripts.h','third_party/plugin-sdk/plugin_sa/game_sa/eScriptCommands.h']+['scm/scripts/'+n+'.txt' for n in ('BARB','CLOTH','TATTO','AMUNAT','JFUD')]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def body(text,pattern):
 m=re.search(pattern,text,re.S);assert m,pattern
 start=text.index('{',m.start());depth=1
 mask=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),text,flags=re.S)
 for i in range(start+1,len(text)):
  depth+=(mask[i]=='{')-(mask[i]=='}')
  if not depth:return text[m.start():i+1]
 raise ValueError(pattern)
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--mutate-shop-list',action='store_true');a=p.parse_args();out=a.output.resolve()
 assert not out.exists() and ROOT/'.cache' in out.parents
 inputs=INPUTS+(['client/src/CGuestShopAccess.cpp','client/src/CGuestShopAccess.h'] if (ROOT/'client/src/CGuestShopAccess.cpp').exists() else [])
 out.mkdir(parents=True);hashes={n:sha(ROOT/n) for n in inputs}
 for n in inputs:
  target=out/'source'/n;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/n,target)
 read=lambda n:(out/'source'/n).read_text(encoding='utf-8-sig')
 native={}
 for name in ('Shopping.cpp','Shopping.h'):
  src=WORKSPACE/'gta-reversed/source/game_sa'/name;native[str(src)]=sha(src);shutil.copyfile(src,out/name)
 exe=WORKSPACE/'game-lab/gta_sa.exe';native[str(exe)]=sha(exe);assert sha(exe)=='A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26'
 disk=exe.read_bytes();pe=struct.unpack_from('<I',disk,0x3c)[0];ct=struct.unpack_from('<H',disk,pe+6)[0];sz=struct.unpack_from('<H',disk,pe+20)[0];base=struct.unpack_from('<I',disk,pe+52)[0];sects=[struct.unpack_from('<8sIIII',disk,pe+24+sz+40*i) for i in range(ct)]
 def bytes_at(va,ct):
  for _,vsz,rva,rsz,raw in sects:
   if rva<=va-base<rva+max(vsz,rsz):return disk[raw+va-base-rva:raw+va-base-rva+ct]
  raise ValueError(va)
 bindings={hex(va):bytes_at(va,ct).hex() for va,ct in [(0x489595,9),(0x489597,5),(0x4895B0,5),(0x4895B5,5),(0x4895C3,7),(0x4577E6,5),(0x609590,12),(0x49BF70,12)]}
 assert bindings['0x489595']=='6a018bcee8e2aafdff' and bindings['0x4895b5']=='84c00f95c2' and bindings['0x4895b0']=='e8dbff1700' and bindings['0x4577e6']=='e8a51d1b00'
 (out/'native-bindings.json').write_text(json.dumps(bindings,indent=2))
 (out/'shopping.inc').write_text(body((out/'Shopping.cpp').read_text(),r'void CShopping::Buy\([^{}]*\{'))
 (out/'price_sections.h').write_text(body((out/'Shopping.h').read_text(),r'enum ePriceSection\b[^{}]*\{')+';')
 session=read('client/src/CSessionSync.cpp')
 (out/'wallet.inc').write_text('\n'.join(body(session,re.escape(sig)+r'[^{}]*\{') for sig in ('bool CashReady()','uint64_t Lifecycle()','bool Submit(','void WriteMoney()','void CaptureMoney()')))
 hooks=read('client/src/Hooks/PlayerHooks.cpp');opcode=read('client/src/COpCodeSync.cpp');audio=read('client/src/Hooks/MissionAudioHooks.cpp')
 old=body(hooks,r'bool __fastcall CPlayerPed__CanPlayerStartMission_Hook\([^{}]*\{')
 (out/'host_gate.inc').write_text(old)
 (out/'consume_money.inc').write_text('\n'.join(body(session,re.escape(sig)+r'[^{}]*\{') for sig in ('bool CSessionSync::IsWalletReadyForLocalService()','bool CSessionSync::NeedsOpcodeCapture(','bool CSessionSync::ConsumeOpcode(')))
 if 'client/src/CGuestShopAccess.cpp' in inputs:
  service=re.sub(r'^#include[^\n]*\n','',read('client/src/CGuestShopAccess.cpp'),flags=re.M)
  if a.mutate_shop_list:
   needle='const bool retail = IsLocalServiceScript(script);';assert service.count(needle)==1;service=service.replace(needle,'const bool retail = true; // Mutation: bypass retail membership restriction.')
  (out/'service.inc').write_text(service)
  (out/'service_decl.h').write_text(re.sub(r'^#include[^\n]*\n|^#pragma once\n','',read('client/src/CGuestShopAccess.h'),flags=re.M))
  (out/'script_gate.inc').write_text(body(hooks,r'(?:static )?bool __declspec\(naked\) CPlayerPed__CanPlayerStartMission_ScriptHook\([^{}]*\{'))
  (out/'live_scope.inc').write_text(body(opcode,r'CRunningScript\* COpCodeSync::GetActiveScript\([^{}]*\{'))
 else:
  (out/'service_decl.h').write_text('class CGuestShopAccess {public:static bool CanStartFromScript(CPlayerPed* ped, CRunningScript*){return CPlayerPed__CanPlayerStartMission_Hook(ped,nullptr);} };')
  (out/'script_gate.inc').write_text('bool __declspec(naked) CPlayerPed__CanPlayerStartMission_ScriptHook(){__asm { jmp CPlayerPed__CanPlayerStartMission_Hook }}')
  (out/'live_scope.inc').write_text('CRunningScript* COpCodeSync::GetActiveScript(){return activeOpcodeScope&&!bProcessingNetworkOpcode?lastProcessedScript:nullptr;}')
 tasks=read('client/src/CTaskSequenceSync.cpp');hdr=read('client/src/COpCodeSync.h')
 (out/'policy_defs.inc').write_text(body(hdr,r'enum class eSyncedParamType\b[^{}]*\{')+';\n'+body(hdr,r'struct SSyncedOpCode\b[^{}]*\{')+';')
 (out/'perform_sequence.inc').write_text(body(tasks,r'void PerformSequence\([^{}]*\{'))
 (out/'sync_policy.inc').write_text(body(opcode,r'const SSyncedOpCode syncedOpcodes\[\][^{}]*\{')+';\n'+body(tasks,r'SSyncedOpCode m_syncedTasks\[\][^{}]*\{')+';\n'+body(tasks,r'bool CTaskSequenceSync::IsOpCodeTaskSynced\([^{}]*\{')+'\n'+body(tasks,r'bool CTaskSequenceSync::OnOpCodeExecuted\([^{}]*\{')+'\n'+body(opcode,r'bool COpCodeSync::IsOpcodeSyncable\([^{}]*\{'))
 (out/'send.inc').write_text(body(opcode,r'void BuildAndSendOpcode\([^{}]*\{'))
 (out/'audio.inc').write_text('static int aiLoadedAudioId[4];\n'+'\n'.join(body(audio,re.escape(sig)+r'[^{}]*\{') for sig in ('void __fastcall CAudioEngine__PreloadMissionAudio_Hook(','void __fastcall CAudioEngine__PlayLoadedMissionAudio_Hook(')))
 for n in ('shop_doubles.h','shop_tests.cpp'):shutil.copyfile(HERE/n,out/n)
 support={n:sha(HERE/n) for n in ('run_tests.py','shop_doubles.h','shop_tests.cpp')}
 (out/'compile.cmd').write_text('@echo off\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvarsall.bat" x64_x86 > environment.log 2>&1\nif errorlevel 1 exit /b 1\ncl /nologo /std:c++17 /EHsc /W4 /DNOMINMAX /DCOOP_CLIENT shop_tests.cpp /I source/shared /Fe:tests.exe /Fo:tests.obj\n')
 env=os.environ.copy();env.pop('GTA_SA_DIR',None)
 c=subprocess.run(['cmd.exe','/d','/c',str(out/'compile.cmd')],cwd=out,env=env,capture_output=True,text=True);(out/'compile.log').write_text(c.stdout+c.stderr)
 result={'SourceHashes':hashes,'SupportHashes':support,'NativeHashes':native,'CompileExitCode':c.returncode,'RuntimeValidated':False,'Mutation':a.mutate_shop_list}
 if not c.returncode:
  t=subprocess.run([str(out/'tests.exe')],cwd=out,env=env,capture_output=True,text=True);result.update(TestExitCode=t.returncode,Output=t.stdout+t.stderr);(out/'test.log').write_text(result['Output'])
 else:print(c.stdout+c.stderr)
 result['InputsStable']=hashes=={n:sha(ROOT/n) for n in inputs};result['SupportStable']=support=={n:sha(HERE/n) for n in support};result['NativeStable']=native=={n:sha(Path(n)) for n in native};result['FrozenMatch']=hashes=={n:sha(out/'source'/n) for n in inputs};result['ExtractionHashes']={n.name:sha(n) for n in out.glob('*.inc')}
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(result.get('Output','compile failure'))
 return 0 if result.get('TestExitCode')==0 and all(result[k] for k in ('InputsStable','SupportStable','NativeStable','FrozenMatch')) else 1
if __name__=='__main__':raise SystemExit(main())
