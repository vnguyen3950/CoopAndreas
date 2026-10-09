"""Freeze real services/codecs; recorded native collaborators, never GTA execution."""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
SDK='third_party/plugin-sdk/plugin_sa/game_sa/'
INPUTS=['shared/config.h','shared/network/packet.h','shared/network/packet_types.h','third_party/serialize.h',
 'shared/network/player_animation_sync.h','shared/network/packets/player_animation.h','shared/network/player_vitals.h',
 'shared/network/packets/players.h','shared/network/serializable_types.h',
 'client/src/CPlayerAnimationSync.h','client/src/CPlayerAnimationSync.cpp','server/src/CPlayerAnimationSync.h',
 'server/src/CPlayerAnimationSync.cpp','client/src/CPlayerVitalsSync.cpp',SDK+'CPed.h',SDK+'ePedState.h',
 SDK+'CAnimBlendAssociation.h',SDK+'eAnimBlendCallbackType.h',SDK+'CAnimBlendAssociation.cpp',SDK+'CAnimBlendHierarchy.h',SDK+'CAnimBlock.h',SDK+'CAnimManager.h',SDK+'CAnimManager.cpp',
 'client/src/Hooks/PlayerHooks.cpp','client/src/CCore.cpp','client/src/Main.cpp','client/src/CNetwork.cpp',
 'client/src/PacketHandlers/system.cpp','client/src/PacketHandlers/players.cpp','server/src/PacketHandlers/players.cpp','server/src/CNetwork.cpp']
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def block(s,pattern):
 m=re.search(pattern,s,re.S)
 if not m:raise ValueError(pattern)
 start=s.index('{',m.start());depth=1
 masked=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),s,flags=re.S)
 for end in range(start+1,len(s)):
  depth+=(masked[end]=='{')-(masked[end]=='}')
  if not depth:return s[m.start():end+1]
 raise ValueError('Unbalanced extraction')
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',required=True);ap.add_argument('--suite',choices=['service','codec','contract'],default='service');ap.add_argument('--mutate-birth',action='store_true');a=ap.parse_args()
 out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
 before={n:sha(ROOT/n)for n in INPUTS};support={p.name:sha(p)for p in HERE.glob('*')if p.is_file()}
 for name in INPUTS:
  dest=out/'source'/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,dest)
 def read(n):return (out/'source'/n).read_text(encoding='utf-8-sig')
 def noincludes(s):return re.sub(r'^\s*#include[^\n]*\n','',s,flags=re.M)
 life=out/'source/shared/network/player_animation_sync.h'
 if a.mutate_birth:life.write_text(read('shared/network/player_animation_sync.h').replace('next.birth < life.birth','false /* mutation: ignore actor birth */'))
 sender=block(read('shared/network/serializable_types.h'),r'struct SenderPlayerId\b[^{}]*\{')+';'
 respawn=block(read('shared/network/packets/players.h'),r'class RespawnPlayer\b[^{}]*\{')+';'
 (out/'respawn.inc').write_text(sender+'\nnamespace Packets::Players {\n'+respawn+'\n}\n')
 enums=block(read(SDK+'ePedState.h'),r'enum\s+(?:PLUGIN_API\s+)?ePedState\b[^{}]*\{')+';\n'
 enums+=block(read(SDK+'eAnimBlendCallbackType.h'),r'enum\s+(?:PLUGIN_API\s+)?eAnimBlendCallbackType\b[^{}]*\{')+';\n'
 enums+=block(read(SDK+'CPed.h'),r'enum\s+(?:PLUGIN_API\s+)?eMoveState\b[^{}]*\{')+';\n'
 (out/'native_enums.inc').write_text(enums)
 (out/'client.inc').write_text(noincludes(read('client/src/CPlayerAnimationSync.cpp')))
 (out/'server.inc').write_text(noincludes(read('server/src/CPlayerAnimationSync.cpp')))
 (out/'vitals.inc').write_text(block(read('client/src/CPlayerVitalsSync.cpp'),r'bool CPlayerVitalsSync::HasBoundPed\([^{}]*\{'))
 shutil.copyfile(HERE/'doubles.h',out/'doubles.h');shutil.copyfile(HERE/(a.suite+'.cpp'),out/'tests.cpp')
 cmd=['cl.exe','/nologo','/std:c++17','/EHsc','/W4','/DNOMINMAX','/DCOOP_SERVER','tests.cpp','/I'+str(out/'source'),'/I'+str(out/'source/shared'),'/I'+str(out/'source/third_party'),'/Fe:tests.exe','/Fo:tests.obj']
 env=r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
 (out/'compile.cmd').write_text('@echo off\ncall "'+env+'" x64_x86 > environment.log 2>&1\nif errorlevel 1 exit /b 1\n'+subprocess.list2cmdline(cmd)+'\n')
 processenv=os.environ.copy();processenv.pop('GTA_SA_DIR',None)
 c=subprocess.run(['cmd.exe','/d','/c',str(out/'compile.cmd')],cwd=out,env=processenv,capture_output=True,text=True)
 (out/'compile.log').write_text(c.stdout+c.stderr)
 result={'ProductionHashes':before,'SupportHashes':support,'CompileExitCode':c.returncode,'Suite':a.suite,'Mutation':a.mutate_birth,'NativeRuntimeValidated':False}
 if not c.returncode:
  t=subprocess.run([str(out/'tests.exe')],cwd=out,env=processenv,capture_output=True,text=True);result.update(TestExitCode=t.returncode,Output=t.stdout+t.stderr);(out/'test.log').write_text(result['Output'])
 else:print(c.stdout+c.stderr)
 if not c.returncode and a.suite=='codec':
  clientcmd=[word.replace('/DCOOP_SERVER','/DCOOP_CLIENT').replace('/Fe:tests.exe','/Fe:client.exe').replace('/Fo:tests.obj','/Fo:client.obj') for word in cmd]
  (out/'compile-client.cmd').write_text('@echo off\ncall "'+env+'" x64_x86 > environment-client.log 2>&1\nif errorlevel 1 exit /b 1\n'+subprocess.list2cmdline(clientcmd)+'\n')
  cc=subprocess.run(['cmd.exe','/d','/c',str(out/'compile-client.cmd')],cwd=out,env=processenv,capture_output=True,text=True)
  (out/'compile-client.log').write_text(cc.stdout+cc.stderr);result['ClientCompileExitCode']=cc.returncode
  if not cc.returncode:
   role=[]
   for executable in ['client.exe','tests.exe']:
    test=subprocess.run([str(out/executable),'--receive'],cwd=out,env=processenv,capture_output=True,text=True)
    role.append({'Role':executable,'ExitCode':test.returncode,'Output':test.stdout+test.stderr})
    print(test.stdout+test.stderr)
   result['OppositeRoles']=role
   if any(t['ExitCode'] for t in role):result['TestExitCode']=1
  else:result['TestExitCode']=1
 result['InputsStable']=before=={n:sha(ROOT/n)for n in INPUTS}
 result['FrozenInputsMatch']=all(sha(out/'source'/n)==value for n,value in before.items() if not(a.mutate_birth and n=='shared/network/player_animation_sync.h'))
 result['SupportStable']=support=={p.name:sha(p)for p in HERE.glob('*')if p.is_file()}
 result['ExtractionHashes']={p.name:sha(p)for p in out.glob('*.inc')}
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(result.get('Output','Compile failed'))
 return 0 if result['InputsStable']and result['SupportStable']and result['FrozenInputsMatch']and result.get('TestExitCode')==0 else 1
if __name__=='__main__':raise SystemExit(main())
