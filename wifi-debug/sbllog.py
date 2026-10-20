import mmap,os,struct,time,subprocess
for _ in range(100):
    if "Wait for device" in subprocess.run(["dmesg"],capture_output=True,text=True).stdout: break
    time.sleep(0.1)
time.sleep(1)
f=os.open("/sys/bus/pci/devices/0000:01:00.0/resource0",os.O_RDWR|os.O_SYNC)
m=mmap.mmap(f,0x100000,mmap.MAP_SHARED,mmap.PROT_READ|mmap.PROT_WRITE)
r=lambda o: struct.unpack("<I",m[o:o+4])[0]
def rd(a):
    if a < 0x80000: return r(a)
    m[0x310c:0x3110]=struct.pack("<I",0x40000000|((a>>19)&0x3f)); r(0x310c)
    return r(0x80000+(a&0x7ffff))
b=r(0x28); s,n=r(b+0x38),r(b+0x3c)
print("MHISTATUS %#x EE %d"%(r(0x48),r(b+0x28)))
for k,a in (("TCSR_PBL_LOGGING",0x01B000F8),("PBL_WLAN_BOOT_CFG",0x01E22B34),("PBL_BOOTSTRAP",0x01910008),("QFPROM_PWR_CTRL",0x1F8031C)):
    print(k,hex(rd(a)))
def dump(name,start,size):
    w=[rd(start+i) for i in range(0,size,4)]
    raw=b"".join(struct.pack("<I",x) for x in w)
    print(name,hex(start),[hex(x) for x in w]); print(" ascii:",raw.decode("ascii","replace"))
dump("PBL",0x01403DA0,40)
dump("SBL",s,min(n,48))
