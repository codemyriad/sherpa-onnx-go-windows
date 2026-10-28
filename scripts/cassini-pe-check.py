"""Check a Windows DLL's machine type and print its imported DLLs.

Usage: python cassini-pe-check.py DLL MACHINE
MACHINE is the expected IMAGE_FILE_HEADER.Machine (0x8664 x64, 0x14c x86,
0xaa64 ARM64). Prints "machine: ..." and "imports: A.dll B.dll" (sorted).
"""
import sys

import pefile

path, want = sys.argv[1], int(sys.argv[2], 16)
pe = pefile.PE(path, fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
machine = pe.FILE_HEADER.Machine
print(f"machine: {machine:#x} ({pefile.MACHINE_TYPE.get(machine, '?')})")
if machine != want:
    sys.exit(f"expected machine {want:#x}")
imports = sorted(e.dll.decode() for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []))
print("imports: " + " ".join(imports))
