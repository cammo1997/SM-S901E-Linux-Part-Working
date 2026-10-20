import mmap,os,struct,time,subprocess
p="/sys/bus/pci/devices/0000:01:00.0/resource0"
for _ in range(100):
    if "Wait for device" in subprocess.run(["dmesg"],capture_output=True,text=True).stdout: break
    time.sleep(0.1)
time.sleep(1)
if open("/sys/bus/pci/devices/0000:01:00.0/config","rb").read(2)==b"\xff\xff":
    print("link down, skip BAR read"); raise SystemExit
if "Wait for device" not in subprocess.run(["dmesg"],capture_output=True,text=True).stdout:
    print("no SBL wait seen, skip BAR read"); raise SystemExit
f=os.open(p,os.O_RDONLY|os.O_SYNC); m=mmap.mmap(f,0x1000,mmap.MAP_SHARED,mmap.PROT_READ)
r=lambda o: struct.unpack("<I",m[o:o+4])[0]
b=r(0x28); st=r(0x48)
print("MHISTATUS %#x state %d syserr %d BHIOFF %#x"%(st,(st>>8)&0xff,(st>>2)&1,b))
for n,o in (("EXECENV",0x28),("STATUS",0x2c),("ERRCODE",0x30),("ERRDBG1",0x34),("ERRDBG2",0x38),("ERRDBG3",0x3c)):
    print(n,hex(r(b+o)))
