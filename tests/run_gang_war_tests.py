"""Compile real gang-war contract/codecs from an immutable hashed snapshot."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
INPUTS=("shared/network/gang_wars.h","shared/network/packets/gang_wars.h",
        "shared/network/packet.h","shared/network/packet_types.h","third_party/serialize.h")
SERVICE_INPUTS=("client/src/CGangWarSync.h","client/src/CGangWarSync.cpp","client/src/GangWarsNative.h",
               "server/src/CGangWarSync.h","server/src/CGangWarSync.cpp",
               "client/src/PacketHandlers/gang_wars.cpp","server/src/PacketHandlers/gang_wars.cpp",
               "client/src/Main.cpp","client/src/CCore.cpp","server/src/CNetwork.cpp","server/src/CNetworkPlayerManager.cpp",
               "third_party/plugin-sdk/plugin_sa/game_sa/CRadar.h")
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest().upper()
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--suite",choices=["codec","service"],default="codec")
    parser.add_argument("--mutation",action="store_true",help="Prove the native field-preservation regression detects a targeted mutation")
    parser.add_argument("--msvc-env",type=Path,default=Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"))
    args=parser.parse_args();out=args.output.resolve()
    if out.exists() or ROOT/".cache" not in out.parents:parser.error("Choose a new ignored .cache evidence directory")
    inputs=INPUTS+(SERVICE_INPUTS if args.suite=="service" else ())
    out.mkdir(parents=True);before={name:digest(ROOT/name)for name in inputs}
    for name in inputs:
        p=out/"source"/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,p)
    test=ROOT/("tests/gang_war_service_tests.cpp" if args.suite=="service" else "tests/gang_wars_tests.cpp")
    testHash=digest(test);shutil.copyfile(test,out/"tests.cpp")
    frozen={name:digest(out/"source"/name)for name in inputs}
    supportHash=None;extractions={}
    if args.suite=="service":
        support=ROOT/"tests/gang_war_doubles.h";supportHash=digest(support);shutil.copyfile(support,out/support.name)
        sdk=(out/"source/third_party/plugin-sdk/plugin_sa/game_sa/CRadar.h").read_text(encoding="utf-8-sig")
        enums=[]
        for name in ('eBlipType','eBlipDisplay'):
            match=re.search(r'enum\s+'+name+r'\s*\{[^}]+\};',sdk)
            if not match:raise ValueError("SDK radar enum shape changed")
            enums.append(match[0])
        (out/"gang_radar_enums.inc").write_text('\n'.join(enums)+'\n',encoding="utf-8")
        for role in ("client","server"):
            name=role+"/src/CGangWarSync.cpp";source=(out/"source"/name).read_text(encoding="utf-8-sig")
            expected=['#include "stdafx.h"','#include "CGangWarSync.h"']+(['#include "GangWarsNative.h"']if role=="client"else [])
            includes=re.findall(r'^#include[^\n]*',source,flags=re.M)
            if includes!=expected:raise ValueError("Service include contract changed: "+name)
            target=out/("gang_"+role+".inc");target.write_text(re.sub(r'^#include[^\n]*\n?','',source,flags=re.M),encoding="utf-8")
            extractions[name]={"IncludesOnlyRemoved":includes,"Hash":digest(target)}
        name="client/src/GangWarsNative.h";source=(out/"source"/name).read_text(encoding="utf-8-sig")
        source=source.replace('#include <CZoneInfo.h>\n','').replace('#include <CGangWars.h>\n','').replace('#include <CRadar.h>\n','')
        if source.count('inline View Live()')!=1:raise ValueError("Native binding contract changed")
        source=source.replace('inline View Live()','inline View DiskLive()')
        source=source.replace('inline void ClearNativeBlip()','inline void DiskClearNativeBlip()')
        if args.mutation:
            if source.count('&uint8_t(~0x60)')!=1:raise ValueError("Mutation target changed")
            source=source.replace('&uint8_t(~0x60)','&uint8_t(0)')
        target=out/"gang_native.inc";target.write_text(source,encoding="utf-8")
        extractions[name]={"NativeBindingDouble":"Fixed Live addresses replaced by recorded view; field algorithms unchanged",
                          "Mutation":args.mutation,"Hash":digest(target)}
    command=["cl.exe","/nologo","/std:c++17","/EHsc","/W4","/WX","/O2","/I"+str(out),"/I"+str(out/"source/shared"),
             "/I"+str(out/"source/third_party"),str(out/"tests.cpp"),"/Fe:"+str(out/"tests.exe"),"/Fo:"+str(out/"tests.obj")]
    batch='@echo off\ncall "'+str(args.msvc_env.resolve())+'" -arch=x86 -host_arch=x64 > environment.log 2>&1\n'
    batch+='if errorlevel 1 exit /b 1\ncl.exe /Bv > compiler-version.log 2>&1\n'+subprocess.list2cmdline(command)+'\n'
    (out/"compile.cmd").write_text(batch,encoding="utf-8")
    report={"ProductionHashes":frozen,"TestHash":testHash,"CompilerCommand":command,"RuntimeValidated":False,
            "Suite":args.suite,"Mutation":args.mutation,"Extractions":extractions,"DoubleHeaderHash":supportHash}
    if before==frozen:
        build=subprocess.run(["cmd.exe","/d","/c",str(out/"compile.cmd")],cwd=out,capture_output=True,text=True)
        (out/"compile.log").write_text(build.stdout+build.stderr,encoding="utf-8");report["CompileExitCode"]=build.returncode
        if build.returncode==0:
            cases=["client","native","server","shutdown"]if args.suite=="service"else [None]
            runs=[]
            for case in cases:
                run=subprocess.run([str(out/"tests.exe")]+([case]if case else []),capture_output=True,text=True)
                runs.append({"Case":case,"ExitCode":run.returncode,"Output":run.stdout+run.stderr})
            (out/"test.log").write_text('\n'.join(run["Output"]for run in runs),encoding="utf-8")
            report["Runs"]=runs;report["TestExitCode"]=0 if all(run["ExitCode"]==0 for run in runs)else 1
    after={name:digest(ROOT/name)for name in inputs};report["InputsStable"]=before==frozen==after and digest(test)==testHash
    if supportHash:report["InputsStable"]&=digest(support)==supportHash
    expected=1 if args.mutation else 0
    report["Pass"]=report["InputsStable"]and report.get("CompileExitCode")==0 and report.get("TestExitCode")==expected
    if args.mutation:
        report["MutationDetected"]=args.suite=="service"and all(
            run["ExitCode"]==(1 if run["Case"]=="native"else 0)for run in report.get("Runs",[]))and any(
            'FAIL: Territory apply preserves population, no-cops, dealer and race fields' in run["Output"]for run in report.get("Runs",[]))
        report["Pass"]&=report["MutationDetected"]
    (out/"result.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"Pass":report["Pass"],"Evidence":str(out/"result.json")},indent=2));return 0 if report["Pass"]else 1
if __name__=="__main__":raise SystemExit(main())
