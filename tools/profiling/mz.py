import struct, sys, re
for p in sys.argv[1:]:
    b = open(p,'rb').read()
    sig,last,pages,nrel,hdrpar,minal,maxal,ss,sp,cs_,ip_,ip,cs,relo,ovl = struct.unpack('<2sHHHHHHHHHHHHHH', b[:30])
    img = (pages-1)*512 + (last or 512) - hdrpar*16
    print(f"== {p}\n sig={sig} file={len(b)} load_image={img} relocs={nrel} hdr={hdrpar*16}")
    print(f" min_alloc={minal*16} ({minal:#x} para) max_alloc={maxal*16 if maxal!=0xffff else 'ALL (0xFFFF)'}")
    print(f" SS:SP={ss:04x}:{sp:04x} CS:IP={cs:04x}:{ip:04x}  overlay_data_after_image={len(b)-((pages-1)*512+(last or 512))}")
    print(f" initial memory need = image+min = {img+minal*16} bytes ({(img+minal*16)/1024:.0f} KB)")
    txt = b.decode('latin1')
    for k in ['Turbo Pascal','Borland','FOSSIL','fossil','DPMI','DOS4G','RTM','EMS','XMS','80286','286','386','Runtime error','OVR','DDPlus','OpenDoors','DOOR.SYS','DORINFO','SHARE','Overlay']:
        n = txt.count(k)
        if n: print(f"  '{k}': {n}")
