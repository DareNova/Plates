"""Portable installer reference / Linux entry point. Python 3.9+, standard library only."""
import argparse,hashlib,json,os,shutil,tempfile,uuid,zipfile
from pathlib import Path
from release_delta import apply_delta,sha

def safe(root,name):
 if not isinstance(name,str) or not name or '\\' in name or ':' in name or name.startswith('/') or any(p in ('','..','.') for p in name.split('/')):raise ValueError('Unsafe relative path')
 root=Path(root).resolve();path=root/name
 if not path.resolve().is_relative_to(root):raise ValueError('Path escapes install folder')
 for p in [path,*path.parents]:
  if p==root:break
  if p.is_symlink():raise ValueError('Symlink target refused')
 return path

def atomic(path,data):
 path.parent.mkdir(parents=True,exist_ok=True)
 tmp=path.with_name(path.name+'.tmp-'+uuid.uuid4().hex)
 try:
  tmp.write_bytes(data);os.replace(tmp,path)
 finally:tmp.unlink(missing_ok=True)

def save(path,state):atomic(path,json.dumps(state,indent=2).encode())
def filehash(path):
 with path.open('rb') as stream:
  h=hashlib.sha256()
  for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
 return h.hexdigest()

def active(game,filename):
 overlay=safe(game,'mediapc/Cars/'+filename)
 return overlay if overlay.exists() else safe(game,'media/Cars/'+filename)

def original(game,record):
 if record.get('backup'):
  p=safe(game,record['backup'])
  if filehash(p)!=record['original_hash']:raise ValueError('Backup changed or damaged')
  return p
 return safe(game,record['base_path'])

def install_file(game,state,statefile,relative,staged,base_path=None,patch_hash=None):
 target=safe(game,relative);old=state['files'].get(relative)
 if old and (not target.exists() or filehash(target)!=old['installed_hash']):raise ValueError('Installed file changed later; preserving '+relative)
 if old:record=dict(old)
 else:
  record={'backup':None,'original_hash':None}
  if target.exists():
   backup=safe(game,state['backup_root']+'/'+uuid.uuid4().hex+'.bak');backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(target,backup);record.update(backup=backup.relative_to(game).as_posix(),original_hash=filehash(backup))
 record.update(installed_hash=filehash(staged),base_path=base_path or (old or {}).get('base_path'))
 if patch_hash:record['patch_hash']=patch_hash
 # Journal the intended write first. A failure can be recovered on the next run.
 state['pending']={'relative':relative,'previous':old,'next':record};save(statefile,state)
 target.parent.mkdir(parents=True,exist_ok=True)
 # Stage on destination filesystem; keep previous target until verification completes.
 previous=target.read_bytes() if target.exists() else None
 try:
  atomic(target,staged.read_bytes())
  if filehash(target)!=record['installed_hash']:raise ValueError('Installed hash mismatch')
 except Exception:
  if previous is not None:atomic(target,previous)
  else:target.unlink(missing_ok=True)
  state.pop('pending',None);save(statefile,state);raise
 state['files'][relative]=record;state.pop('pending',None);save(statefile,state)

def recover(game,state,statefile):
 p=state.get('pending')
 if not p:return
 target=safe(game,p['relative']);new=p['next'];old=p['previous']
 current=filehash(target) if target.exists() else None
 if current==new['installed_hash']:state['files'][p['relative']]=new
 elif current==((old or {}).get('installed_hash') or new.get('original_hash')):pass
 elif current is not None:raise ValueError('Interrupted installation has an unexpected file; preserving it')
 state.pop('pending',None);save(statefile,state)

def check_base(base,manifest):
 expected=manifest.get('base_zip_sha256')
 if not isinstance(expected,str) or len(expected)!=64 or any(c not in '0123456789abcdef' for c in expected):raise ValueError('Missing or invalid base ZIP SHA-256; use an updated patch')
 if manifest.get('base_hash_policy','exact-zip')!='exact-zip':raise ValueError('Unsupported base hash policy')
 actual=filehash(base)
 if actual!=expected:raise ValueError('Base ZIP hash mismatch: expected '+expected+', found '+actual+'. Different mod/game version or repacked ZIP; skipped without changes.')

def reconstruct(game,base,manifest,patchfolder,dest):
 check_base(base,manifest)
 entries=manifest['base_entries']
 with zipfile.ZipFile(base) as z:
  names=z.namelist()
  if len({n.casefold() for n in names})!=len(names):raise ValueError('Duplicate ZIP entries')
  if set(names)!=set(entries):raise ValueError('Base ZIP file list mismatch')
  if sum(i.file_size for i in z.infolist())>512*1024*1024:raise ValueError('Base ZIP exceeds size limit')
  for n,h in entries.items():
   safe(Path('.'),n)
   if sha(z.read(n))!=h:raise ValueError('Base asset mismatch: '+n)
  replacements={}
  for record in manifest['outputs']:
   entry=record['entry'];safe(Path('.'),entry)
   if entry in replacements:raise ValueError('Duplicate patch output')
   reference=record['base'];data=b''
   if reference:
    loc=reference['location'];filename=reference['archive'];safe(Path('.'),filename)
    if loc=='active':source=base
    elif loc in ('media','mediapc'):source=safe(game,loc+'/Cars/'+filename)
    else:raise ValueError('Unsupported dependency location')
    with zipfile.ZipFile(source) as donor:data=donor.read(reference['entry'])
    if sha(data)!=reference['sha256']:raise ValueError('Donor asset version mismatch: '+filename)
   delta=safe(patchfolder,record['delta']).read_bytes()
   if sha(delta)!=record['delta_sha256']:raise ValueError('Patch payload damaged')
   output=apply_delta(data,delta)
   if sha(output)!=record['sha256'] or len(output)!=record['size']:raise ValueError('Reconstructed asset hash mismatch')
   replacements[entry]=output
  with zipfile.ZipFile(dest,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as out:
   for info in z.infolist():
    n=info.filename;out.writestr(n,replacements.pop(n,z.read(n)),compress_type=zipfile.ZIP_STORED if n.casefold() in ('buildnumber.txt','carbuildtime.txt') else zipfile.ZIP_DEFLATED)
   for n,data in replacements.items():out.writestr(n,data)
 with zipfile.ZipFile(dest) as check:
  if check.testzip():raise ValueError('Built ZIP CRC failure')
  for record in manifest['outputs']:
   if sha(check.read(record['entry']))!=record['sha256']:raise ValueError('Built ZIP asset verification failure')

def run(package,game,command,workers=2):
 workers=int(workers)
 if not 1<=workers<=4:raise ValueError("Choose 1–4 install workers")
 package=Path(package).resolve();game=Path(game).resolve()
 if not (game/'forzahorizon6.exe').is_file():raise ValueError('Select the folder containing forzahorizon6.exe')
 data=safe(package,'mod') if (package/'mod').is_dir() else package
 runtime_dir=safe(data,'runtime') if (data/'runtime').is_dir() else data
 statefile=safe(package,'install-state/state.json');runtime=json.loads((data/'runtime.json').read_text(encoding='utf-8-sig'))
 state=json.loads(statefile.read_text()) if statefile.exists() else {'schema':1,'game':str(game),'backup_root':package.relative_to(game).as_posix()+'/install-state/backups' if package.is_relative_to(game) else None,'files':{}}
 # Backups stay beside state, not in a replaceable patches folder.
 if not state['backup_root']:raise ValueError('Extract FH6FrontPlates inside the game folder first')
 if Path(state['game']).resolve()!=game:raise ValueError('This install record belongs to a different game folder')
 statefile.parent.mkdir(parents=True,exist_ok=True);recover(game,state,statefile);report={'installed':[],'unchanged':[],'skipped':[],'restored':[]}
 if command=='status':
  for n,r in state['files'].items():report['installed' if safe(game,n).exists() and filehash(safe(game,n))==r['installed_hash'] else 'skipped'].append(n)
 elif command=='disable':
  ordered=sorted(state['files'],key=lambda n:not n.startswith('mediapc/Cars/'))
  for n in ordered:
   r=state['files'][n]
   if not n.startswith('mediapc/Cars/') and any(k.startswith('mediapc/Cars/') for k in state['files']):report['skipped'].append(n+': retained because a car could not be restored');continue
   target=safe(game,n)
   if not target.exists() or filehash(target)!=r['installed_hash']:report['skipped'].append(n+': changed after installation');continue
   if r.get('backup'):atomic(target,original(game,r).read_bytes())
   else:target.unlink()
   del state['files'][n];save(statefile,state);report['restored'].append(n)
 else:
  if runtime.get('game_sha256') and filehash(game/'forzahorizon6.exe')!=runtime['game_sha256']:raise ValueError('Unsupported game version; nothing installed')
  loader=game/'version.dll';helper=game/'platesync-version.dll';expected=runtime['files']['version.dll']
  standalone=loader.exists() and filehash(loader)==expected
  companion=helper.exists() and filehash(helper)==expected
  if runtime.get('require_loader') and standalone:raise ValueError('PlateSync requires Multiverse; rename the included DLL platesync-version.dll and keep the loader version.dll')
  if standalone and helper.exists() and filehash(helper) in runtime['compatible_helpers']:raise ValueError('Two PlateSync DLL copies detected; keep standalone OR Multiverse companion')
  if not standalone and not companion:raise ValueError('Copy the included PlateSync version.dll beside the game executable; use platesync-version.dll with Multiverse')
  if not standalone and (not loader.exists() or filehash(loader) not in runtime['compatible_loaders']):raise ValueError('Install a compatible Multiverse loader first; existing version.dll preserved')
  from concurrent.futures import ThreadPoolExecutor
  from collections import Counter
  jobs=[]
  for folder in sorted((data/'patches').iterdir()):
   if not folder.is_dir():continue
   try:
    path=folder/'manifest.json';manifest=json.loads(path.read_text(encoding='utf-8-sig'))
    if manifest['schema']!=1:raise ValueError('Unsupported patch schema')
    filename=manifest['filename']
    if '/' in filename or not filename.lower().endswith('.zip'):raise ValueError('Invalid car filename')
    relative='mediapc/Cars/'+filename;safe(game,relative)
    jobs.append({'folder':folder,'manifest':manifest,'filename':filename,'relative':relative,'patch_hash':filehash(path)})
   except Exception as e:report['skipped'].append(folder.name+': '+str(e))
  counts=Counter(j['relative'].casefold() for j in jobs)
  workers=min(workers,max(1,os.cpu_count() or 1));report['workers']=workers
  with ThreadPoolExecutor(max_workers=workers) as pool:
   for start in range(0,len(jobs),workers):
    pending=[]
    with tempfile.TemporaryDirectory(prefix='building-',dir=statefile.parent) as temp:
     for index,job in enumerate(jobs[start:start+workers]):
      try:
       if counts[job['relative'].casefold()]>1:raise ValueError('Multiple patches target the same car ZIP; no variant was selected')
       manifest=job['manifest'];relative=job['relative'];target=safe(game,relative);previous=state['files'].get(relative)
       job['target_hash']=filehash(target) if target.exists() else None
       if previous:
        if job['target_hash']!=previous['installed_hash']:raise ValueError('Installed ZIP changed later')
        base=original(game,previous)
       else:base=active(game,job['filename'])
       location=manifest.get('base_location','either')
       actual='mediapc' if (previous and previous.get('backup')) or (not previous and base.parent.parent.name.casefold()=='mediapc') else 'media'
       if location not in ('either',actual):raise ValueError('Patch expects '+location+' base')
       if previous and previous.get('patch_hash')==job['patch_hash']:
        check_base(base,manifest)
        for output in manifest['outputs']:
         if filehash(safe(job['folder'],output['delta']))!=output['delta_sha256']:raise ValueError('Patch payload damaged')
        report['unchanged'].append(job['filename']);continue
       job['base_relative']=base.relative_to(game).as_posix();job['dest']=Path(temp)/(str(index)+'.zip')
       pending.append((job,pool.submit(reconstruct,game,base,manifest,job['folder'],job['dest'])))
      except Exception as e:report['skipped'].append(job['folder'].name+': '+str(e))
     # Close all source/donor readers in this batch before replacing any live ZIP.
     prepared=[]
     for job,future in pending:
      try:future.result();prepared.append(job)
      except Exception as e:report['skipped'].append(job['folder'].name+': '+str(e))
     for job in prepared:
      try:
       target=safe(game,job['relative']);current=filehash(target) if target.exists() else None
       if current!=job['target_hash']:raise ValueError('Car ZIP changed while preparing; preserving the later change')
       install_file(game,state,statefile,job['relative'],job['dest'],job['base_relative'],job['patch_hash'])
       report['installed'].append(job['filename'])
      except Exception as e:report['skipped'].append(job['folder'].name+': '+str(e))
  if report['installed'] or report['unchanged']:
   config=safe(game,'PlateSync/diagnostic.ini')
   text=config.read_text(encoding='utf-8-sig') if config.exists() else ''
   import re
   lines=text.splitlines();out=[];inside=False;found=False;keys=set()
   def finish():
    for key in ('Enabled','AutoAttach'):
     if key.casefold() not in keys:out.append(key+'=1')
   for line in lines:
    section=re.match(r'^\s*\[([^]]+)\]',line)
    if section:
     if inside:finish()
     inside=section[1].casefold()=='profiles';found=found or inside
    key=re.match(r'^\s*(Enabled|AutoAttach)\s*=',line,re.I) if inside else None
    if key:keys.add(key[1].casefold());line=key[1]+'=1'
    out.append(line)
   if inside:finish()
   if not found:out+=['[profiles]','Enabled=1','AutoAttach=1']
   desired=('\r\n'.join(out)+'\r\n').encode()
   if not config.exists() or config.read_bytes()!=desired:
    temporary=package/'install-state/default.ini';temporary.write_bytes(desired);install_file(game,state,statefile,'PlateSync/diagnostic.ini',temporary)
 statefile.parent.mkdir(parents=True,exist_ok=True);save(package/'last-install-report.json',report)
 return report

def main():
 package=Path(__file__).resolve().parent
 if package.name=='mod':package=package.parent
 parser=argparse.ArgumentParser();parser.add_argument('command',choices=['enable','disable','status']);parser.add_argument('--game',default=str(package.parent));parser.add_argument('--workers',type=int,default=2,choices=range(1,5));args=parser.parse_args()
 # File installer only; refuse when a matching game process is running on Linux.
 if args.command!='status' and Path('/proc').exists():
  for p in Path('/proc').glob('[0-9]*/comm'):
   try:
    if 'forzahorizon6' in p.read_text().casefold():raise RuntimeError('Close Forza first')
   except (OSError,UnicodeError):pass
 print(json.dumps(run(package,args.game,args.command,args.workers),indent=2))
if __name__=='__main__':main()
