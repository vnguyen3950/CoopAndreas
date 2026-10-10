"""Isolated actual-source SDK build. Never installs or selects packages."""
import hashlib,json,os,re,subprocess,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT.parent.parent
OUT=ROOT/'.cache/pickup-runtime/x86-final-002'
XMAKE=BASE/'tools/xmake/xmake/xmake.exe'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest().upper()
def main():
    env=dict(os.environ);env.pop('GTA_SA_DIR',None)
    names=subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode().split('\0')
    before={n:sha(ROOT/n)for n in names if n and (ROOT/n).is_file()}
    identity=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip()
    config=ROOT/'.xmake/windows/x64/xmake.conf'
    record={'SourceCommit':identity,'SourceRoot':str(ROOT),'GTA_SA_DIR_Cleared':True,'NativeRuntime':False,'ConfigPath':str(config),'ConfigSHA256':sha(config),'SourceHashes':before,'Targets':{}}
    for target,filename in [('client','CoopAndreasSA.dll'),('server','server.exe')]:
        info=subprocess.check_output([str(XMAKE),'show','-t',target],cwd=ROOT,env=env).decode(errors='replace');info=re.sub(r'\x1b\[[0-9;]*m','',info)
        OUT.mkdir(parents=True,exist_ok=True);(OUT/(target+'-effective.txt')).write_text(info)
        destination=(ROOT/re.search(r'targetfile: ([^\n]+)',info).group(1).strip()).resolve()
        assert destination==OUT/'windows/x86/release'/filename
        pch=re.search(r'-Fp([^\s]+)',info).group(1)
        pch=(ROOT/pch).resolve();assert pch.is_relative_to(OUT.resolve())
        command=[str(XMAKE),'build','-r','-j','4',target]
        print('Building '+target+' at '+str(destination),flush=True)
        result=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,text=True)
        log=OUT/(target+'-build.log');log.write_text(result.stdout+result.stderr,encoding='utf-8')
        item={'Command':command,'ExitCode':result.returncode,'TargetPath':str(destination),'PCHPath':str(pch),'ObjectDir':str(OUT/'.objs'/target),'Log':str(log),'LogSHA256':sha(log)}
        if result.returncode==0:
            raw=destination.read_bytes();pe=struct.unpack_from('<I',raw,0x3c)[0];assert raw[:2]==b'MZ' and raw[pe:pe+4]==b'PE\0\0' and struct.unpack_from('<H',raw,pe+4)[0]==0x14c
            item.update(Bytes=len(raw),SHA256=sha(destination))
        else:print((result.stdout+result.stderr)[-8000:],flush=True)
        record['Targets'][target]=item
        if result.returncode:break
    record['SourceInputsUnchanged']=before=={n:sha(ROOT/n)for n in before}
    record['Passed']=record['SourceInputsUnchanged']and len(record['Targets'])==2 and all(x['ExitCode']==0 for x in record['Targets'].values())
    (OUT/'build-result.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8');print(json.dumps({'Passed':record['Passed'],'SourceCommit':identity,'Report':str(OUT/'build-result.json')},indent=2))
    return 0 if record['Passed']else 1
if __name__=='__main__':raise SystemExit(main())
