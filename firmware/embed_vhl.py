import os

vhl_path = r"D:\TYBSC\TYBSC Arduino Project\Esp32 Digital Musical Workstation\Instruments\harmonium new\001_first try-1.vhl"

destinations = [
    (
        r"d:\TYBSC\TYBSC Arduino Project\Esp32 Digital Musical Workstation\firmware\components\vhl_storage\include\vhl_harmonium_package.h",
        r"d:\TYBSC\TYBSC Arduino Project\Esp32 Digital Musical Workstation\firmware\components\vhl_storage\vhl_harmonium_package.c"
    ),
    (
        r"d:\TYBSC\TYBSC Arduino Project\Esp32 Digital Musical Workstation\firmware\src\vhl_storage\include\vhl_harmonium_package.h",
        r"d:\TYBSC\TYBSC Arduino Project\Esp32 Digital Musical Workstation\firmware\src\vhl_storage\vhl_harmonium_package.c"
    )
]

with open(vhl_path, "rb") as f:
    data = f.read()

size = len(data)
print(f"VHL Binary File Read: {size} bytes ({vhl_path})")

h_content = f"""#ifndef VHL_HARMONIUM_PACKAGE_H
#define VHL_HARMONIUM_PACKAGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {{
#endif

extern const uint8_t g_vhl_harmonium_package[{size}];
extern const size_t g_vhl_harmonium_package_size;

#ifdef __cplusplus
}}
#endif

#endif /* VHL_HARMONIUM_PACKAGE_H */
"""

lines = [
    '#include "vhl_harmonium_package.h"\n\n',
    f"const size_t g_vhl_harmonium_package_size = {size};\n\n",
    f"const uint8_t g_vhl_harmonium_package[{size}] = {{\n"
]
for i in range(0, size, 16):
    chunk = data[i:i+16]
    hex_vals = ", ".join([f"0x{b:02x}" for b in chunk])
    comma = "," if i + 16 < size else ""
    lines.append(f"    {hex_vals}{comma}\n")
lines.append("};\n")
c_content = "".join(lines)

for h_path, c_path in destinations:
    os.makedirs(os.path.dirname(h_path), exist_ok=True)
    os.makedirs(os.path.dirname(c_path), exist_ok=True)
    with open(h_path, "w", encoding="utf-8") as f:
        f.write(h_content)
    with open(c_path, "w", encoding="utf-8") as f:
        f.write(c_content)
    print(f"Generated: {h_path}")
    print(f"Generated: {c_path}")

print("vhl_harmonium_package generated successfully for both components and src.")
