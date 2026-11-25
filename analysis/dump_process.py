#!/usr/bin/env python3
"""Read-only snapshot of a running process image (no suspension, no writes).

Used to recover the unpacked image of the packed (".bns0/.bns1") AccountInventoryDaemon.exe.
Only OpenProcess(QUERY_INFORMATION|VM_READ) + ReadProcessMemory are used, so the
target keeps running undisturbed.

Outputs (in OUT dir):
  AccountInventoryDaemon.mem.exe  image rebuilt as a PE: section raw data == memory page
  image.raw                       flat image bytes (base .. base+SizeOfImage)
  modules.txt                     loaded module list
  regions.txt                     virtual memory region map
"""
import ctypes
import ctypes.wintypes as wt
import os
import struct
import sys

EXE = "AccountInventoryDaemon.exe"
PID = int(sys.argv[1]) if len(sys.argv) > 1 else None
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "mem")
os.makedirs(OUT, exist_ok=True)

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
TH32CS_SNAPMODULE = 0x00000008
TH32CS_SNAPMODULE32 = 0x00000010


class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("th32ModuleID", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("GlbcntUsage", wt.DWORD), ("proccntUsage", wt.DWORD),
                ("modBaseAddr", ctypes.POINTER(ctypes.c_byte)), ("modBaseSize", wt.DWORD),
                ("hModule", wt.HMODULE), ("szModule", ctypes.c_wchar * 256),
                ("szExePath", ctypes.c_wchar * 260)]


class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                ("AllocationProtect", wt.DWORD), ("PartitionId", wt.WORD),
                ("RegionSize", ctypes.c_size_t), ("State", wt.DWORD), ("Protect", wt.DWORD),
                ("Type", wt.DWORD), ("AllocationBase2", ctypes.c_void_p)]


k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
k32.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
k32.Module32FirstW.argtypes = [wt.HANDLE, ctypes.POINTER(MODULEENTRY32W)]
k32.Module32NextW.argtypes = [wt.HANDLE, ctypes.POINTER(MODULEENTRY32W)]
k32.OpenProcess.restype = wt.HANDLE
k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.VirtualQueryEx.restype = ctypes.c_size_t
k32.VirtualQueryEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.POINTER(MEMORY_BASIC_INFORMATION), ctypes.c_size_t]
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.CloseHandle.argtypes = [wt.HANDLE]


def list_modules(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    mods = []
    me = MODULEENTRY32W()
    me.dwSize = ctypes.sizeof(MODULEENTRY32W)
    ok = k32.Module32FirstW(snap, ctypes.byref(me))
    while ok:
        base = ctypes.cast(me.modBaseAddr, ctypes.c_void_p).value
        mods.append((base, me.modBaseSize, me.szModule, me.szExePath))
        ok = k32.Module32NextW(snap, ctypes.byref(me))
    k32.CloseHandle(snap)
    return mods


def rpm(h, addr, size):
    buf = ctypes.create_string_buffer(size)
    got = ctypes.c_size_t(0)
    if not k32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, size, ctypes.byref(got)):
        return None
    return buf.raw[:got.value]


def find_pid(name):
    import subprocess
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {name}", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        if name.lower() in line.lower():
            return int(line.split('","')[1].strip('"'))
    return None


if PID is None:
    PID = find_pid(EXE)
    if PID is None:
        sys.exit(f"{EXE} is not running")
print("pid", PID)
mods = list_modules(PID)
with open(os.path.join(OUT, "modules.txt"), "w", encoding="utf-8") as f:
    for base, size, name, path in mods:
        f.write(f"{base:#018x} size={size:#x} {name} {path}\n")
main = [m for m in mods if m[2].lower() == EXE.lower()]
if not main:
    sys.exit("main module not found")
base, size, _, _ = main[0]
print(f"image base {base:#x} size {size:#x}")

h = k32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, PID)
if not h:
    sys.exit(f"OpenProcess failed: {ctypes.get_last_error()}")

regions = []
addr = base
mbi = MEMORY_BASIC_INFORMATION()
image = bytearray(size)
covered = bytearray(size)
while addr < base + size:
    r = k32.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi))
    if not r:
        break
    b = mbi.BaseAddress or 0
    n = mbi.RegionSize
    regions.append((b, n, mbi.State, mbi.Protect, mbi.Type, mbi.AllocationBase or 0))
    if mbi.State == 0x1000:  # MEM_COMMIT
        data = rpm(h, b, n)
        if data is not None:
            off = b - base
            image[off:off + len(data)] = data
            covered[off:off + len(data)] = b"\x01" * len(data)
    addr = b + n

with open(os.path.join(OUT, "regions.txt"), "w") as f:
    for b, n, st, pr, ty, ab in regions:
        f.write(f"{b:#018x} {n:#010x} state={st:#x} protect={pr:#06x} type={ty:#x} allocbase={ab:#x}\n")
k32.CloseHandle(h)
print("covered bytes", sum(covered), "of", size)

raw = os.path.join(OUT, "image.raw")
with open(raw, "wb") as f:
    f.write(image)

e_lfanew = struct.unpack_from("<I", image, 0x3C)[0]
nsec = struct.unpack_from("<H", image, e_lfanew + 6)[0]
opt_size = struct.unpack_from("<H", image, e_lfanew + 20)[0]
opt_off = e_lfanew + 24
sec_off = opt_off + opt_size
file_align = 0x1000
struct.pack_into("<I", image, opt_off + 36, file_align)
out = bytearray(image[:sec_off + nsec * 40])
for i in range(nsec):
    s = sec_off + i * 40
    name = bytes(image[s:s + 8]).rstrip(b"\x00").decode("latin1")
    vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", image, s + 8)
    rva = vaddr
    n = (max(vsize, 1) + file_align - 1) // file_align * file_align
    need = len(out)
    if rva < need:
        rva = (need + file_align - 1) // file_align * file_align
    out += bytearray(rva - len(out))
    chunk = image[vaddr:vaddr + n] if vaddr + n <= len(image) else bytes(n)
    out += chunk
    struct.pack_into("<II", out, s + 16, n, rva)
    print(f"  {name:8s} va={vaddr:#08x} vsize={vsize:#08x} raw@{rva:#08x} n={n:#x}")

pe = os.path.join(OUT, EXE + ".mem.exe")
with open(pe, "wb") as f:
    f.write(out)
print("wrote", pe, len(out))
