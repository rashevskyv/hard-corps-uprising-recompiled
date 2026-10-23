# Lists the bounding box of the pixels each traced draw changed.
#   python3 -I scripts/draw-diff.py "<dir>/d<swap>-*.ppm"
import glob,sys
def rd(p):
    d=open(p,'rb').read()
    parts=d.split(b'\n',3)
    w,h=map(int,parts[1].split())
    return w,h,parts[3]
files=sorted(glob.glob(sys.argv[1]))
prev=None
for f in files:
    w,h,px=rd(f)
    if prev is not None and len(prev)==len(px):
        minx=miny=10**9; maxx=maxy=-1; cnt=0
        for y in range(0,min(h,720)):
            a=prev[y*w*3:(y+1)*w*3]; b=px[y*w*3:(y+1)*w*3]
            if a!=b:
                for x in range(w):
                    if a[x*3:x*3+3]!=b[x*3:x*3+3]:
                        cnt+=1; minx=min(minx,x); maxx=max(maxx,x); miny=min(miny,y); maxy=max(maxy,y)
        if cnt: print(f, 'changed', cnt, 'bbox', minx,miny,maxx,maxy)
    prev=px
