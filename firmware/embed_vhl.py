import os

vhl_path = r"d:\TYBSC\TYBSC Arduino Project\Esp32 Digital Musical Workstation\Instruments\Tabla\000_80bpm_oWW3QEmxL.vhl"
h_path = r"d:\TYBSC\TYBSC Arduino Project\Esp32 Digital Musical Workstation\firmware\components\vhl_storage\include\vhl_harmonium_package.h"
c_path = r"d:\TYBSC\TYBSC Arduino Project\Esp32 Digital Musical Workstation\firmware\components\vhl_storage\vhl_harmonium_package.c"

with open(vhl_path, "rb") as f:
    data = f.read()

size = len(data)
print(f"VHL Binary File Read: {size} bytes")

with open(h_path, "w", encoding="utf-8") as f:
    f.write("#ifndef VHL_HARMONIUM_PACKAGE_H\n")
    f.write("#define VHL_HARMONIUM_PACKAGE_H\n\n")
    f.write("#include <stdint.h>\n#include <stddef.h>\n\n")
    f.write("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n")
    f.write(f"extern const uint8_t g_vhl_harmonium_package[{size}];\n")
    f.write("extern const size_t g_vhl_harmonium_package_size;\n\n")
    f.write("#ifdef __cplusplus\n}\n#endif\n\n")
    f.write("#endif /* VHL_HARMONIUM_PACKAGE_H */\n")

with open(c_path, "w", encoding="utf-8") as f:
    f.write("#include \"vhl_harmonium_package.h\"\n\n")
    f.write(f"const size_t g_vhl_harmonium_package_size = {size};\n\n")
    f.write(f"const uint8_t g_vhl_harmonium_package[{size}] = {{\n")
    for i in range(0, size, 16):
        chunk = data[i:i+16]
        hex_vals = ", ".join([f"0x{b:02x}" for b in chunk])
        comma = "," if i + 16 < size else ""
        f.write(f"    {hex_vals}{comma}\n")
    f.write("};\n")

print("vhl_harmonium_package.c generated successfully.")
