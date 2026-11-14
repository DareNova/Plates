"""Copy/literal delta: never embeds an unchanged base file. No external libraries."""
import hashlib,struct
MAGIC=b'PWDELTA1'
def sha(data):return hashlib.sha256(data).hexdigest()
def make_delta(base,target):
 block=32;index={}
 for i in range(0,len(base)-block+1,block):index.setdefault(base[i:i+block],i)
 ops=[];literal=bytearray();i=0
 def flush():
  if literal:ops.append((1,bytes(literal)));literal.clear()
 while i<len(target):
  off=index.get(target[i:i+block]) if i+block<=len(target) else None
  if off is None:literal.append(target[i]);i+=1;continue
  n=block
  while i+n+block<=len(target) and off+n+block<=len(base) and target[i+n:i+n+block]==base[off+n:off+n+block]:n+=block
  while i+n<len(target) and off+n<len(base) and target[i+n]==base[off+n]:n+=1
  flush();ops.append((0,off,n));i+=n
 flush();out=bytearray(MAGIC+struct.pack('<I',len(target)))
 for op in ops:
  if op[0]==0:out+=struct.pack('<BII',0,op[1],op[2])
  else:out+=struct.pack('<BI',1,len(op[1]))+op[1]
 return bytes(out)
def apply_delta(base,patch):
 if patch[:8]!=MAGIC or len(patch)<12:raise ValueError('Invalid delta header')
 size=struct.unpack_from('<I',patch,8)[0]
 if size>256*1024*1024:raise ValueError('Delta too large')
 out=bytearray();p=12
 while p<len(patch):
  tag=patch[p];p+=1
  if tag==0:
   off,n=struct.unpack_from('<II',patch,p);p+=8
   if off+n>len(base):raise ValueError('Delta copy out of bounds')
   out+=base[off:off+n]
  elif tag==1:
   n=struct.unpack_from('<I',patch,p)[0];p+=4
   if p+n>len(patch):raise ValueError('Truncated delta literal')
   out+=patch[p:p+n];p+=n
  else:raise ValueError('Unknown delta operation')
  if len(out)>size:raise ValueError('Delta output overflow')
 if len(out)!=size:raise ValueError('Delta output size mismatch')
 return bytes(out)
