#!/usr/bin/env python3
"""
Convierte archivos .bin de LUTs de Rayleigh a arrays C embebidos.
"""

import sys
import struct

def bin_to_c_array(binfile, varname):
    """Convierte archivo binario a array const unsigned char[] en C."""
    with open(binfile, 'rb') as f:
        data = f.read()
    
    # Generar array C
    hex_bytes = ', '.join(f'0x{b:02x}' for b in data)
    
    # Dividir en líneas de ~80 caracteres
    lines = []
    current_line = "    "
    for item in hex_bytes.split(', '):
        if len(current_line) + len(item) + 2 > 80:
            lines.append(current_line)
            current_line = "    " + item + ","
        else:
            current_line += item + ","
    if current_line.strip():
        lines.append(current_line.rstrip(','))
    
    result = f"const unsigned char {varname}[] = {{\n"
    result += '\n'.join(lines)
    result += f"\n}};\nconst unsigned int {varname}_len = {len(data)};\n"
    
    return result

def main():
    luts = [
        ('rayleigh_lut_C01.bin', 'rayleigh_lut_c01_data'),
        ('rayleigh_lut_C02.bin', 'rayleigh_lut_c02_data'),
        ('rayleigh_lut_C03.bin', 'rayleigh_lut_c03_data'),
    ]
    
    # Generar archivo .c
    c_file = "../src/rayleigh_lut_embedded.c"
    with open(c_file, 'w') as f:
        f.write("/* Auto-generated file - DO NOT EDIT */\n")
        f.write("/* Generated from Rayleigh LUTs with secant-based interpolation */\n\n")
        f.write('#include "rayleigh_lut_embedded.h"\n\n')
        
        for binfile, varname in luts:
            print(f"Embedding {binfile}...")
            f.write(bin_to_c_array(binfile, varname))
            f.write('\n')
    
    # Generar archivo .h
    h_file = "../include/rayleigh_lut_embedded.h"
    with open(h_file, 'w') as f:
        f.write("/* Auto-generated file - DO NOT EDIT */\n")
        f.write("#ifndef RAYLEIGH_LUT_EMBEDDED_H\n")
        f.write("#define RAYLEIGH_LUT_EMBEDDED_H\n\n")
        
        for _, varname in luts:
            f.write(f"extern const unsigned char {varname}[];\n")
            f.write(f"extern const unsigned int {varname}_len;\n")
        
        f.write("\n#endif /* RAYLEIGH_LUT_EMBEDDED_H */\n")
    
    print(f"\n✓ Generated {c_file}")
    print(f"✓ Generated {h_file}")

if __name__ == '__main__':
    main()
