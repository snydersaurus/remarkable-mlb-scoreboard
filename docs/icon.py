import math
from PIL import Image, ImageDraw

S = 4; SIZE = 600*S; CX = CY = 300.0*S

def bez(p,t):
    p0,p1,p2,p3=p; mt=1-t
    return ((mt**3)*p0[0]+3*(mt**2)*t*p1[0]+3*mt*(t**2)*p2[0]+(t**3)*p3[0],
            (mt**3)*p0[1]+3*(mt**2)*t*p1[1]+3*mt*(t**2)*p2[1]+(t**3)*p3[1])

def tangent(p,t):
    p0,p1,p2,p3=p; mt=1-t
    x=3*(mt**2)*(p1[0]-p0[0])+6*mt*t*(p2[0]-p1[0])+3*(t**2)*(p3[0]-p2[0])
    y=3*(mt**2)*(p1[1]-p0[1])+6*mt*t*(p2[1]-p1[1])+3*(t**2)*(p3[1]-p2[1])
    n=math.hypot(x,y) or 1.0
    return x/n,y/n

def oc(r,deg):
    a=math.radians(deg); return CX+r*math.cos(a), CY+r*math.sin(a)

def seams(r, bulge):
    # endpoints near the top and bottom of the silhouette, curves bowing outward
    return [(oc(r,-68), (CX+bulge, CY-150*S*r/(230*S)), (CX+bulge, CY+150*S*r/(230*S)), oc(r,68)),
            (oc(r,-112),(CX-bulge, CY-150*S*r/(230*S)), (CX-bulge, CY+150*S*r/(230*S)), oc(r,112))]

def icon(seam_line=True, bulge_f=0.62, nst=10, inset=0.10):
    img=Image.new("RGB",(SIZE,SIZE),"white"); d=ImageDraw.Draw(img)
    r=232.0*S
    d.ellipse([CX-r,CY-r,CX+r,CY+r], outline="black", width=int(22*S))
    bulge = r*bulge_f
    for sm in seams(r, bulge):
        if seam_line:
            pts=[bez(sm,inset+(1-2*inset)*i/160.0) for i in range(161)]
            d.line(pts, fill="black", width=int(8*S), joint="curve")
        for i in range(nst):
            t = inset + (1-2*inset)*(i+0.5)/nst
            px,py=bez(sm,t); tx,ty=tangent(sm,t); nx,ny=-ty,tx
            a=math.radians(34)
            dx=nx*math.cos(a)+tx*math.sin(a); dy=ny*math.cos(a)+ty*math.sin(a)
            h=44*S/2
            d.line([(px-dx*h,py-dy*h),(px+dx*h,py+dy*h)], fill="black", width=int(12*S))
    return img.resize((600,600), Image.LANCZOS)

icon(True ).save("v1-seam-curved.png")
icon(False).save("v2-stitches-only.png")
icon(False, bulge_f=0.80, nst=9).save("v3-wide-curve.png")
print("ok")
