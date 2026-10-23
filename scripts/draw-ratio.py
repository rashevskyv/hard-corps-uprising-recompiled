# Per sprite draw of a traced swap: pixels changed by the draw vs non-zero texels of its k_8 index texture.
#   python3 -I scripts/draw-ratio.py <run dir> <swap>   (run with HCU_DK_TRACE_AT=<swap> HCU_DK_TRACE_DUMP=<run dir>/tr)
import glob,re,sys
def ppm(p):
    d=open(p,'rb').read(); parts=d.split(b'\n',3); w,h=map(int,parts[1].split()); return w,h,parts[3]
run=sys.argv[1]; swap=sys.argv[2]
log=open(run+'/stdout.txt',errors='replace').read().split('\n')
draws={}
for l in log:
    m=re.search(r'T draw (\d+) vs 54E0360FAF10C8B6 .*tf0=([0-9A-F]+)\(64x64\)',l)
    if m: draws[int(m.group(1))]=m.group(2)
tr=run+'/tr/'
for d in sorted(draws):
    try:
        w,h,px=ppm(tr+'d%s-%03d.ppm'%(swap,d)); pw,ph,pp=ppm(tr+'d%s-%03d.ppm'%(swap,d-1))
    except Exception: continue
    cnt=0; minx=miny=10**9; maxx=maxy=-1
    for y in range(min(h,720)):
        a=pp[y*w*3:(y+1)*w*3]; b=px[y*w*3:(y+1)*w*3]
        if a!=b:
            for x in range(w):
                if a[x*3:x*3+3]!=b[x*3:x*3+3]:
                    cnt+=1; minx=min(minx,x);maxx=max(maxx,x);miny=min(miny,y);maxy=max(maxy,y)
    raw=glob.glob(tr+'t%s-%03d-%s-64x64-k_8.raw'%(swap,d,draws[d]))
    idx=open(raw[0],'rb').read() if raw else b''
    nz=sum(1 for v in idx if v)
    print(d,draws[d],'changed',cnt,'bbox',(minx,miny,maxx,maxy) if cnt else '-','nonzero idx',nz)
