"""Compile real gang-war contract/codecs from an immutable hashed snapshot."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
INPUTS=("shared/network/gang_wars.h","shared/network/packets/gang_wars.h",
        "shared/network/packet.h","shared/network/packet_types.h","third_party/serialize.h")
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest().upper()
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--msvc-env",type=Path,default=Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"))
    args=parser.parse_args();out=args.output.resolve()
    if out.exists() or ROOT/".cache" not in out.parents:parser.error("Choose a new ignored .cache evidence directory")
    out.mkdir(parents=True);before={name:digest(ROOT/name)for name in INPUTS}
    for name in INPUTS:
        p=out/"source"/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,p)
    test=ROOT/"tests/gang_wars_tests.cpp";testHash=digest(test);shutil.copyfile(test,out/"tests.cpp")
    frozen={name:digest(out/"source"/name)for name in INPUTS}
    command=["cl.exe","/nologo","/std:c++17","/EHsc","/W4","/WX","/O2","/I"+str(out/"source/shared"),
             "/I"+str(out/"source/third_party"),str(out/"tests.cpp"),"/Fe:"+str(out/"tests.exe"),"/Fo:"+str(out/"tests.obj")]
    batch='@echo off\ncall "'+str(args.msvc_env.resolve())+'" -arch=x86 -host_arch=x64 > environment.log 2>&1\n'
    batch+='if errorlevel 1 exit /b 1\ncl.exe /Bv > compiler-version.log 2>&1\n'+subprocess.list2cmdline(command)+'\n'
    (out/"compile.cmd").write_text(batch,encoding="utf-8")
    report={"ProductionHashes":frozen,"TestHash":testHash,"CompilerCommand":command,"RuntimeValidated":False}
    if before==frozen:
        build=subprocess.run(["cmd.exe","/d","/c",str(out/"compile.cmd")],cwd=out,capture_output=True,text=True)
        (out/"compile.log").write_text(build.stdout+build.stderr,encoding="utf-8");report["CompileExitCode"]=build.returncode
        if build.returncode==0:
            run=subprocess.run([str(out/"tests.exe")],capture_output=True,text=True)
            (out/"test.log").write_text(run.stdout+run.stderr,encoding="utf-8");report["TestExitCode"]=run.returncode
    after={name:digest(ROOT/name)for name in INPUTS};report["InputsStable"]=before==frozen==after and digest(test)==testHash
    report["Pass"]=report["InputsStable"]and report.get("CompileExitCode")==0 and report.get("TestExitCode")==0
    (out/"result.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"Pass":report["Pass"],"Evidence":str(out/"result.json")},indent=2));return 0 if report["Pass"]else 1
if __name__=="__main__":raise SystemExit(main())
