import mmap,os,struct,time,sys
fd=os.open("/sys/bus/pci/devices/0000:01:00.0/resource0",os.O_RDONLY|os.O_SYNC)
m=mmap.mmap(fd,0x1000,mmap.MAP_SHARED,mmap.PROT_READ)
r=lambda o: struct.unpack("<I",m[o:o+4])[0]
t0=time.time(); last=None
while time.time()-t0<float(sys.argv[1]):
    try:
        b=r(0x28)
        s=(r(0x48),r(0x38),b,r(b+0x28),r(b+0x2c),r(b+0x30),r(b+0x34),r(b+0x38),r(b+0x3c))
    except Exception as e:
        s=("ERR",str(e))
    if s!=last:
        print("%6.2fs MHISTATUS=%s CTRL=%s BHIOFF=%s EE=%s BHISTATUS=%s ERRCODE=%s DBG1=%s DBG2=%s DBG3=%s"%((time.time()-t0,)+tuple(("%08x"%x if isinstance(x,int) else x) for x in (s if len(s)==9 else (s[0],s[1])+("",)*7))),flush=True)
        last=s
    time.sleep(0.05)
