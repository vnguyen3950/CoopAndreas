"""Actual entrance sampler/Receive and Win32 sink, private x86 only; never calls GTA."""
import argparse, hashlib, json, os, re, shutil, struct, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
INPUTS = [
    'client/src/CEntryExitDiagnostics.cpp', 'client/src/CEntryExitDiagnostics.h',
    'client/src/CEntryExitMarkerSync.cpp', 'shared/runtime_diagnostics.h',
    'shared/config.h', 'shared/network/packets/scripts.h', 'shared/network/packet.h',
    'client/src/CPacketBuffer.h',
    'third_party/plugin-sdk/plugin_sa/game_sa/CEntryExit.h',
    'third_party/plugin-sdk/plugin_sa/game_sa/CPad.h',
    'client/src/Debug/CImGui.cpp',
]

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest().upper()

def extract(text, pattern):
    match = re.search(pattern, text)
    assert match, pattern
    start = text.index('{', match.start())
    mask = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',
                  lambda m: ' ' * len(m[0]), text, flags=re.S)
    depth = 1
    for end in range(start + 1, len(text)):
        depth += (mask[end] == '{') - (mask[end] == '}')
        if not depth:
            return text[match.start():end + 1]
    raise ValueError(pattern)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--mutate-safe-mask', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve()
    assert ROOT / '.cache' in out.parents and not out.exists()
    out.mkdir(parents=True)
    hashes = {p: sha(ROOT / p) for p in INPUTS}
    for path in INPUTS:
        dest = out / 'source' / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / path, dest)
    read = lambda p: (out / 'source' / p).read_text(encoding='utf-8-sig')
    process = extract(read(INPUTS[0]), r'void CEntryExitDiagnostics::Process\(')
    if args.mutate_safe_mask:
        assert 'controls & 0x20u' in process
        process = process.replace('controls & 0x20u', 'controls & 0x10u')
    receive = extract(read(INPUTS[2]), r'void CEntryExitMarkerSync::Receive\(')
    (out / 'functions.inc').write_text(process + '\n' + receive + '\n')
    flags = extract(read(INPUTS[8]), r'struct SEntryExitFlags\b') + ';\n'
    packets = read(INPUTS[5])
    payload = extract(packets, r'struct _EnExPayload\b') + ';\n'
    packet = extract(packets, r'class EnExSync\b') + ';\n'
    (out / 'native_fields.inc').write_text(flags)
    (out / 'packet_fields.inc').write_text('namespace Packets { namespace Scripts {\n' + payload + packet + '\n}}\n')
    workspace = ROOT.parent.parent if ROOT.parent.name == 'worktrees' else ROOT.parent
    exe = workspace / 'game-lab' / 'gta_sa.exe'
    disk = exe.read_bytes()
    assert sha(exe) == 'A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26'
    pe = struct.unpack_from('<I', disk, 0x3c)[0]
    sections, optional = struct.unpack_from('<H', disk, pe + 6)[0], struct.unpack_from('<H', disk, pe + 20)[0]
    image_base = struct.unpack_from('<I', disk, pe + 52)[0]
    def native_bytes(address, size):
        for i in range(sections):
            _, vsize, rva, rawsize, raw = struct.unpack_from('<8sIIII', disk, pe + 24 + optional + 40*i)
            if rva <= address-image_base < rva+max(vsize, rawsize):
                return disk[raw + address-image_base-rva:raw + address-image_base-rva+size]
        raise ValueError(hex(address))
    proof = {hex(a): native_bytes(a,n).hex() for a,n in [(0x440D40,7),(0x440D52,7),(0x440D5B,5),
             (0x440E12,4),(0x156CF30,26)]}
    assert proof['0x440d40'] == '6639980e010000'  # WORD at pad+10E.
    assert proof['0x440d52'] == '803d8830a40001'  # Replay mode is exactly 1.
    assert proof['0x440d5b'] == 'a0c8a79600'      # Disabled BYTE.
    assert proof['0x440e12'] == 'f6463140'        # Access WORD bit4000.
    assert 'a198cdb700' in proof['0x156cf30'] and 'a128cfb700' in proof['0x156cf30']
    player_info = workspace / 'gta-reversed/source/game_sa/PlayerInfo.cpp'
    native_resources = {str(exe): sha(exe), str(player_info): sha(player_info)}
    assert 'bPlayerSafe = enable;' in player_info.read_text(encoding='utf-8-sig')
    assert 'DisablePlayerControls |= 0x200' in read(INPUTS[10])
    (out / 'native-proof.json').write_text(json.dumps({'Hashes':native_resources, 'Bytes':proof},indent=2))
    for path in ['fixture.cpp', 'native_doubles.h']:
        shutil.copyfile(HERE / path, out / path)
    (out / 'compile.cmd').write_text(
        '@echo off\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvarsall.bat" x64_x86 > environment.log 2>&1\n'
        'if errorlevel 1 exit /b 1\n'
        'cl /nologo /std:c++17 /EHsc /W4 /DNOMINMAX /DCOOP_CLIENT fixture.cpp /I source/shared /Fe:fixture.exe /Fo:fixture.obj\n')
    env = os.environ.copy()
    env.pop('GTA_SA_DIR', None)
    compile_run = subprocess.run(['cmd.exe', '/d', '/c', str(out / 'compile.cmd')],
                                 cwd=out, env=env, text=True, capture_output=True)
    (out / 'compile.log').write_text(compile_run.stdout + compile_run.stderr)
    result = {'SourceCommit': subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD']).decode().strip(),
              'ProductionHashes': hashes, 'SupportHashes': {p: sha(HERE / p) for p in ['run_tests.py', 'fixture.cpp', 'native_doubles.h']},
              'CompileExitCode': compile_run.returncode, 'GTA_SA_DIR_Cleared': True,
              'Mutation': args.mutate_safe_mask, 'NativeResources': native_resources,
              'ProductionChanged': False, 'GameLaunched': False, 'NativeAddressesInvoked': False,
              'Cases': []}
    cases = ['state', 'sampling', 'wrap', 'unready', 'guards', 'receive', 'flags',
             'cap', 'module-fail', 'module-truncated', 'module-noslash', 'module-long',
             'directory-fail', 'file-fail', 'write-fail', 'partial-write', 'reject-fields']
    if compile_run.returncode == 0:
        for case in cases:
            run = subprocess.run([str(out / 'fixture.exe'), case], cwd=out, env=env,
                                 capture_output=True, text=True)
            (out / (case + '.log')).write_text(run.stdout + run.stderr)
            counts = re.search(r'(\d+) assertions, (\d+) failures', run.stdout)
            row = {'Case': case, 'ExitCode': run.returncode, 'Output': run.stdout + run.stderr,
                   'Count': int(counts[1]) if counts else 0, 'Failures': int(counts[2]) if counts else -1}
            result['Cases'].append(row)
    result['Count'] = sum(c['Count'] for c in result['Cases'])
    result['InputsStable'] = hashes == {p: sha(ROOT / p) for p in INPUTS}
    result['NativeResourcesStable'] = all(sha(Path(p)) == value for p,value in native_resources.items())
    result['ExtractionHashes'] = {p.name: sha(p) for p in out.glob('*.inc')}
    result['BinaryInputs'] = [{'Path': str(out / 'fixture.exe'), 'Bytes': (out / 'fixture.exe').stat().st_size,
                              'SHA256': sha(out / 'fixture.exe')}] if (out / 'fixture.exe').exists() else []
    # Only success-path files are expected to contain complete valid JSON lines.
    logs = sorted((out / 'CoopAndreas_diagnostics').glob('*.log'))
    result['SavedLogs'] = [{'Path': str(p), 'Bytes': p.stat().st_size, 'SHA256': sha(p)} for p in logs]
    validated = 0
    for case in result['Cases']:
        match = re.search(r'VALID_JSON_PATH=(.*)', case['Output'])
        if not match:
            continue
        path = Path(match[1].strip())
        for line in path.read_text(encoding='utf-8').splitlines():
            assert out in path.parents, 'Sink must stay in private executable cache'
            event = json.loads(line)
            assert isinstance(event['elapsed_ms'], int) and isinstance(event['scope'], str)
            validated += 1
    result['SavedJSONLinesParsed'] = validated
    result['Passed'] = compile_run.returncode == 0 and len(result['Cases']) == len(cases) and all(
        c['ExitCode'] == 0 and c['Failures'] == 0 and c['Count'] > 0 for c in result['Cases']) and result['InputsStable'] and result['NativeResourcesStable']
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'Passed': result['Passed'], 'Count': result['Count'], 'SavedJSONLinesParsed': validated,
                      'CompileExitCode': compile_run.returncode}))
    if compile_run.returncode:
        print(compile_run.stdout + compile_run.stderr)
    for case in result['Cases']:
        if case['ExitCode']:
            print(case['Output'])
    return 0 if result['Passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
