"""Compile frozen actual pickup registry/codecs. Root owns final enum allocation."""
import argparse,hashlib,json,subprocess,shutil,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
INPUTS=('shared/network/pickup_lifecycle.h','shared/network/npc_sync.h','shared/network/packets/pickups.h','shared/network/packet.h','shared/network/packet_types.h','third_party/serialize.h')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);parser.add_argument('--mutation',action='store_true');parser.add_argument('--ordinal-mutation',action='store_true');parser.add_argument('--diagnostic-stage-mutation',action='store_true');parser.add_argument('--terminal-mutation',action='store_true');parser.add_argument('--held-view-mutation',action='store_true');parser.add_argument('--test-file',type=Path);parser.add_argument('--probe-file',type=Path);parser.add_argument('--native',action='store_true');parser.add_argument('--server',action='store_true');args=parser.parse_args();out=args.output.resolve()
    if args.diagnostic_stage_mutation and not args.native:parser.error('Diagnostic stage mutation requires --native')
    if args.ordinal_mutation and not args.server:parser.error('Ordinal mutation requires --server')
    if args.held_view_mutation and not args.native:parser.error('Held-view mutation requires --native')
    if out.exists()or ROOT/'.cache'not in out.parents:parser.error('Choose a new worktree .cache directory')
    inputs=INPUTS+('shared/runtime_diagnostics.h',)+('client/src/CPickupSync.h','client/src/CPickupSync.cpp','client/src/CPickupSyncNative.cpp','client/src/PickupNativeOutcome.h',
                  'third_party/plugin-sdk/plugin_sa/game_sa/eWeaponType.h','third_party/plugin-sdk/plugin_sa/game_sa/CPickup.h',
                  'third_party/plugin-sdk/plugin_sa/game_sa/CWeaponInfo.h','third_party/plugin-sdk/plugin_sa/game_sa/ePedType.h')if args.native else INPUTS
    if args.server:inputs+=('server/src/CPickupSync.h','server/src/CPickupSync.cpp')
    if args.native or args.server:inputs+=('shared/network/player_animation_sync.h',)
    out.mkdir(parents=True);before={name:sha(ROOT/name)for name in inputs}
    for name in inputs:
        target=out/'source'/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,target)
    enum=out/'source/shared/network/packet_types.h';text=enum.read_text(encoding='utf-8-sig')
    enumOverlayApplied='PICKUP_HELLO'not in text
    if enumOverlayApplied:
        text=text.replace('    PACKET_ID_MAX','    PICKUP_HELLO,\n    PICKUP_STATE,\n    PICKUP_ACTION,\n    PACKET_ID_MAX')
        text=text.replace('"FIRE_BIND"};','"FIRE_BIND", "PICKUP_HELLO", "PICKUP_STATE", "PICKUP_ACTION"};')
        enum.write_text(text,encoding='utf-8')
    if args.mutation and not args.native:
        path=out/'source/shared/network/pickup_lifecycle.h';text=path.read_text();needle='row->stage!=Stage::Active||'
        assert text.count(needle)==1;path.write_text(text.replace(needle,'',1),encoding='utf-8')
    if args.terminal_mutation:
        path=out/'source/shared/network/pickup_lifecycle.h';text=path.read_text();needle='(row->stage!=Stage::Reserved&&row->stage!=Stage::Removed)'
        assert text.count(needle)==1;path.write_text(text.replace(needle,'(row->stage!=Stage::Reserved&&!(row->stage==Stage::Removed&&row->reason==Reason::Ambiguous))',1),encoding='utf-8')
    animationHash=None;supportHash=None;nativeReferenceHash=None;nativeMergeHash=None
    if args.native or args.server:
        actorPath='shared/network/player_animation_sync.h';animationHash=sha(out/'source'/actorPath)
    if args.server:
        text=(out/'source/server/src/CPickupSync.cpp').read_text(encoding='utf-8-sig')
        if args.ordinal_mutation:
            assert text.count('if(output.used){')==1;text=text.replace('if(output.used){','if(false&&output.used){',1)
        (out/'pickup_server.inc').write_text(re.sub(r'^#include[^\n]*\n?','',text,flags=re.M),encoding='utf-8')
        support=ROOT/'tests/pickup_server_doubles.h';supportHash=sha(support);shutil.copyfile(support,out/support.name)
    if args.native:
        text=(out/'source/client/src/CPickupSync.cpp').read_text(encoding='utf-8-sig');(out/'pickup_client.inc').write_text(re.sub(r'^#include[^\n]*\n?','',text,flags=re.M),encoding='utf-8')
        if args.diagnostic_stage_mutation:
            path=out/'pickup_client.inc';value=path.read_text();start=value.index('    static constexpr const char*reasons[]=');end=value.index('    RuntimeDiagnostics::Write',start)
            original='    static std::array<uint8_t,size_t(TraceStage::Count)> counts{};\n    auto&count=counts[size_t(stage)];if(count>=8)return;++count;\n'
            path.write_text(value[:start]+original+value[end:],encoding='utf-8')
        if args.held_view_mutation:
            path=out/'pickup_client.inc';text=path.read_text();needle='if(!row||row->grant!=grant.grant||row->collector!=CNetworkPlayerManager::m_nMyId){SendResult(receipt);hasGrant=false;return;}'
            assert text.count(needle)==1;path.write_text(text.replace(needle,needle.replace('SendResult(receipt);',''),1),encoding='utf-8')
        text=(out/'source/client/src/PickupNativeOutcome.h').read_text(encoding='utf-8-sig');text=re.sub(r'^#include <[^\n]*\n?','',text,flags=re.M);(out/'pickup_native_outcome.inc').write_text(text,encoding='utf-8')
        native=(out/'source/client/src/CPickupSyncNative.cpp').read_text(encoding='utf-8-sig')
        hookParts=[]
        for signature in ('int __cdecl GenerateHook','void __fastcall WeaponDropsHook','bool __cdecl MergeHook','void __fastcall MoneyDropsHook'):
            match=re.search(re.escape(signature)+r'\([^{}]+\)\{.*?\n\}',native,flags=re.S)
            if not match:raise ValueError('Native hook changed: '+signature)
            hookParts.append(match[0])
        (out/'pickup_native_hooks.inc').write_text('#define PICKUP_MERGE_HOOK 1\nusing MergeFn=bool(__cdecl*)(CVector,eWeaponType,uint8_t,uint32_t,bool);\nvoid*mergeOriginal=nullptr;\nvoid*moneyDropsOriginal=nullptr;\nusing GenerateFn=int(__cdecl*)(CVector,uint32_t,uint8_t,uint32_t,uint32_t,bool,char*);\nusing WeaponDropsFn=void(__thiscall*)(CPed*);\nvoid*generateOriginal=nullptr;void*weaponDropsOriginal=nullptr;\n'+'\n'.join(hookParts),encoding='utf-8')
        if args.mutation:
            path=out/'pickup_native_outcome.inc';text=path.read_text();needle='||pickup->m_nAmmo!=item.ammo';assert text.count(needle)==1;path.write_text(text.replace(needle,''),encoding='utf-8')
        nativePath=Path(r'C:\Users\Vu\work\gta-coop\gta-reversed\source\game_sa\Pickups.cpp');reference=nativePath.read_text(encoding='utf-8-sig')
        match=re.search(r'bool CPickups::TryToMerge_WeaponType\([^{}]+\{.*?\n\}',reference,flags=re.S)
        if not match:raise ValueError('Native merge reference changed')
        # Only the unused parameter name is omitted for /W4; native body unchanged.
        (out/'pickup_native_merge.inc').write_text(match[0].replace('bool arg4','bool'),encoding='utf-8')
        nativeReferenceHash=sha(nativePath);nativeMergeHash=sha(out/'pickup_native_merge.inc')
        pickupPath=nativePath.with_name('Pickup.cpp');pickupReference=pickupPath.read_text(encoding='utf-8-sig')
        start=pickupReference.index('bool nonMissionPickup =');end=pickupReference.index('\n                }',start)
        (out/'pickup_native_touch.inc').write_text('inline bool ActualNativeTouch(int mi,CPlayerPed*player,CVehicle*vehicle,float diffZ,float distXY){bool isPicked=false;\n'+pickupReference[start:end]+'\nreturn isPicked;}',encoding='utf-8')

        enums=[]
        for file,name in (('eWeaponType.h','eWeaponType'),('CPickup.h','ePickupType'),('CWeaponInfo.h','eWeaponSkill'),('ePedType.h','ePedType')):
            text=(out/'source/third_party/plugin-sdk/plugin_sa/game_sa'/file).read_text(encoding='utf-8-sig')
            match=re.search(r'enum\s+(?:PLUGIN_API\s+)?'+name+r'\s*(?::[^{}]+)?\{[^}]+\};',text)
            if not match:raise ValueError('SDK enum changed: '+name)
            enums.append(match[0])
        (out/'pickup_sdk_enums.inc').write_text('\n'.join(enums),encoding='utf-8')
        support=ROOT/'tests/pickup_native_doubles.h';supportHash=sha(support);shutil.copyfile(support,out/support.name)
    test=args.test_file.resolve()if args.test_file else ROOT/('tests/pickup_native_tests.cpp'if args.native else 'tests/pickup_server_tests.cpp'if args.server else 'tests/pickup_lifecycle_tests.cpp');testHash=sha(test);shutil.copyfile(test,out/'tests.cpp')
    probe=args.probe_file.resolve()if args.probe_file else None;probeHash=sha(probe)if probe else None
    if probe:shutil.copyfile(probe,out/'probe.cpp')
    cmd=['cl.exe','/nologo','/std:c++17','/EHsc','/W4','/WX','/O2','/I'+str(out/'source'),'/I'+str(out),'/I'+str(out/'source/shared'),'/I'+str(out/'source/third_party'),str(out/('probe.cpp'if probe else 'tests.cpp')),'/Fe:'+str(out/'tests.exe'),'/Fo:'+str(out/'tests.obj')]
    env=r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
    (out/'compile.cmd').write_text('@echo off\ncall "'+env+'" -arch=x86 -host_arch=x64 > environment.log 2>&1\nif errorlevel 1 exit /b 1\n'+subprocess.list2cmdline(cmd)+'\n')
    c=subprocess.run(['cmd.exe','/d','/c',str(out/'compile.cmd')],cwd=out,capture_output=True,text=True);(out/'compile.log').write_text(c.stdout+c.stderr,encoding='utf-8')
    record={'ProductionHashes':before,'TestHash':testHash,'TestSource':str(test),'CompilerCommand':cmd,'CompileExitCode':c.returncode,'RuntimeValidated':False,'EnumOverlayApplied':enumOverlayApplied,'Mutation':args.mutation or args.terminal_mutation or args.held_view_mutation or args.ordinal_mutation or args.diagnostic_stage_mutation,'DiagnosticStageMutation':args.diagnostic_stage_mutation,'OrdinalMutation':args.ordinal_mutation,'TerminalMutation':args.terminal_mutation,'HeldViewMutation':args.held_view_mutation,'Native':args.native,'AnimationHeaderSource':'Current checkout frozen with other inputs','AnimationHeaderHash':animationHash,'DoubleHash':supportHash}
    record.update(NativeReferenceHash=nativeReferenceHash,ExtractedNativeMergeHash=nativeMergeHash,NativeMergeEdits='Only unused parameter name omitted; native body unchanged'if args.native else None,NativeTouchReferenceHash=sha(pickupPath)if args.native else None,ExtractedNativeTouchHash=sha(out/'pickup_native_touch.inc')if args.native else None)
    record.update(ProbeSource=str(probe)if probe else None,ProbeHash=probeHash)
    if not c.returncode:
        run=subprocess.run([str(out/'tests.exe')],cwd=out,capture_output=True,text=True);record.update(TestExitCode=run.returncode,Output=run.stdout+run.stderr);
        for line in run.stdout.splitlines():
            if line.startswith('JSON '):json.loads(line[5:])
        (out/'test.log').write_text(record['Output'],encoding='utf-8')
    record['InputsStable']=before=={name:sha(ROOT/name)for name in inputs}and sha(test)==testHash
    if supportHash:record['InputsStable']&=sha(support)==supportHash
    if probeHash:record['InputsStable']&=sha(probe)==probeHash
    record['Pass']=record['InputsStable']and c.returncode==0 and record.get('TestExitCode')==(1 if args.mutation or args.terminal_mutation or args.held_view_mutation or args.ordinal_mutation or args.diagnostic_stage_mutation else 0)
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8');print(json.dumps({'Pass':record['Pass'],'Output':record.get('Output',c.stdout+c.stderr),'Evidence':str(out/'result.json')},indent=2))
    return 0 if record['Pass']else 1
if __name__=='__main__':raise SystemExit(main())
