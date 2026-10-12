"""Check actual linker headroom, including DMA alignment padding."""
import re
import shutil
import subprocess
from pathlib import Path

Import("env")


def check_memory(source, target, env):
    nm = shutil.which("arm-none-eabi-nm", path=env["ENV"]["PATH"])
    if not nm:
        raise RuntimeError("Cannot verify RAM budget: arm-none-eabi-nm missing")
    output = subprocess.check_output([nm, "-n", str(target[0])], text=True)
    symbols = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2] in ("__end__", "__HeapLimit"):
            symbols[fields[2]] = int(fields[0], 16)
    heap_bytes = symbols["__HeapLimit"] - symbols["__end__"]
    # Core-1 stack: 8192; 172x44 RGB565 sprite: 15138; at least 17630
    # bytes remain for USB, EEPROM, allocator overhead and other runtime use.
    if heap_bytes < 40 * 1024:
        raise RuntimeError("Insufficient linker heap headroom: %d bytes (need 40960)" % heap_bytes)
    print("RAM guard: %d bytes linker heap headroom (minimum 40960)" % heap_bytes)
    # This guards individual authored stack frames, not total call-chain usage.
    # Both cores have 8 KiB available with core1_separate_stack enabled.
    build = Path(env.subst("$BUILD_DIR"))
    for name in ("main", "capture"):
        report = build / "src" / (name + ".cpp.su")
        if not report.is_file():
            raise RuntimeError("Missing compiler stack-usage report: %s" % report)
        for line in report.read_text().splitlines():
            match = re.search(r"\t(\d+)\t", line)
            if match and int(match.group(1)) > 2048:
                raise RuntimeError("Authored stack frame exceeds 2048 bytes: " + line)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check_memory)
