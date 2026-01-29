#!/usr/bin/env python3
"""
Extrae y simplifica las LUTs de Rayleigh de pyspectral para uso en hpsatviews.

Convierte el formato 4D (wavelength, sun_zen_sec, azimuth, sat_zen_sec) 
a 3D (sun_zen_angle, sat_zen_angle, azimuth_angle) para cada banda GOES ABI.

Autor: Alejandro Aguilar Sierra (asierra@unam.mx)
Fecha: Noviembre 2025
"""

import sys
import struct
import numpy as np

try:
    import netCDF4 as nc
except ImportError:
    print("ERROR: netCDF4 no está instalado.")
    print("Instalar con: pip install netCDF4")
    sys.exit(1)

# Longitudes de onda centrales GOES-19 ABI (nm)
GOES19_WAVELENGTHS = {
    'C01': 470.0,  # Blue
    'C02': 640.0,  # Red  
    'C03': 865.0,  # Veggie (fuera de rango en la LUT, usar 800 nm)
}

def secant_to_angle(secant):
    """Convierte secante a ángulo cenital en grados."""
    return np.degrees(np.arccos(1.0 / secant))

def interpolate_wavelength(wavelengths, reflectance, target_wl):
    """
    Interpola la reflectancia para una longitud de onda específica.
    
    Args:
        wavelengths: Array 1D de longitudes de onda (nm)
        reflectance: Array 4D [wavelength, sun_zen, azimuth, sat_zen]
        target_wl: Longitud de onda objetivo (nm)
    
    Returns:
        Array 3D [sun_zen, azimuth, sat_zen] interpolado
    """
    # Buscar índice más cercano
    idx = np.searchsorted(wavelengths, target_wl)
    
    # Si coincide exactamente
    if idx < len(wavelengths) and wavelengths[idx] == target_wl:
        return reflectance[idx, :, :, :]
    
    # Si está fuera de rango
    if idx == 0:
        print(f"    ⚠️  Wavelength {target_wl} nm menor que mínimo {wavelengths[0]} nm")
        return reflectance[0, :, :, :]
    if idx >= len(wavelengths):
        print(f"    ⚠️  Wavelength {target_wl} nm mayor que máximo {wavelengths[-1]} nm")
        return reflectance[-1, :, :, :]
    
    # Interpolación lineal entre idx-1 e idx
    w1, w2 = wavelengths[idx-1], wavelengths[idx]
    r1, r2 = reflectance[idx-1, :, :, :], reflectance[idx, :, :, :]
    alpha = (target_wl - w1) / (w2 - w1)
    
    print(f"    Interpolando entre {w1} nm y {w2} nm (alpha={alpha:.3f})")
    return r1 * (1 - alpha) + r2 * alpha

def export_binary_lut(output_file, sza_secants, vza_secants, az_angles, data):
    """
    Exporta LUT en formato binario optimizado para C.
    
    Formato del archivo:
    - Header (48 bytes):
      * 9 floats: min, max, step para cada eje (sza_sec, vza_sec, az)
      * 3 ints: número de puntos en cada eje
    - Data: array 3D de floats [sza_sec][vza_sec][az] en orden C
    
    Args:
        output_file: Nombre del archivo de salida
        sza_secants: Array 1D de secantes cenitales solares
        vza_secants: Array 1D de secantes cenitales de visión
        az_angles: Array 1D de ángulos azimutales (grados)
        data: Array 3D [sun_zen_sec, sat_zen_sec, azimuth] de reflectancias
    """
    with open(output_file, 'wb') as f:
        # Calcular steps
        sz_step = (sza_secants[-1] - sza_secants[0]) / (len(sza_secants) - 1) if len(sza_secants) > 1 else 0
        vz_step = (vza_secants[-1] - vza_secants[0]) / (len(vza_secants) - 1) if len(vza_secants) > 1 else 0
        az_step = (az_angles[-1] - az_angles[0]) / (len(az_angles) - 1) if len(az_angles) > 1 else 0
        
        # Header (9 floats + 3 ints = 48 bytes)
        header = struct.pack('fff', float(sza_secants[0]), float(sza_secants[-1]), sz_step)
        header += struct.pack('fff', float(vza_secants[0]), float(vza_secants[-1]), vz_step)
        header += struct.pack('fff', float(az_angles[0]), float(az_angles[-1]), az_step)
        header += struct.pack('iii', len(sza_secants), len(vza_secants), len(az_angles))
        f.write(header)
        
        # Data (orden C: [sza_sec][vza_sec][az])
        # Convertir a float32 para ahorrar espacio
        data_flat = data.astype('float32').tobytes()
        f.write(data_flat)
    
    file_size = 48 + len(sza_secants) * len(vza_secants) * len(az_angles) * 4
    print(f"    ✓ Guardado: {output_file}")
    print(f"      Tamaño: {file_size / 1024:.1f} KB")

def main():
    input_file = '/home/aguilars/cspp/geo2grid_v_1_2/pyspectral_data/rayleigh_only/rayleigh_lut_us-standard.h5'
    
    print("="*70)
    print("Extracción de LUTs de Rayleigh para hpsatviews")
    print("="*70)
    print(f"\nArchivo de entrada: {input_file}")
    
    print("\nCargando LUT de Rayleigh...")
    try:
        with nc.Dataset(input_file, 'r') as ds:
            wavelengths = ds.variables['wavelengths'][:]
            sun_zen_sec = ds.variables['sun_zenith_secant'][:]
            sat_zen_sec = ds.variables['satellite_zenith_secant'][:]
            azimuth_diff = ds.variables['azimuth_difference'][:]
            reflectance = ds.variables['reflectance'][:]
    except Exception as e:
        print(f"ERROR al leer archivo: {e}")
        sys.exit(1)
    
    print(f"  ✓ Shape original: {reflectance.shape}")
    print(f"    [wavelength={len(wavelengths)}, sun_zen_sec={len(sun_zen_sec)}, " +
          f"azimuth={len(azimuth_diff)}, sat_zen_sec={len(sat_zen_sec)}]")
    print(f"  ✓ Wavelengths: {wavelengths[0]:.0f}-{wavelengths[-1]:.0f} nm")
    
    # NO convertir secantes a ángulos - mantenerlos como secantes para interpolación correcta
    print(f"\nEjes de la LUT (en secantes):") 
    print(f"  Solar Zenith Secant: {sun_zen_sec[0]:.2f} - {sun_zen_sec[-1]:.2f} ({len(sun_zen_sec)} puntos)")
    print(f"  Satellite Zenith Secant: {sat_zen_sec[0]:.2f} - {sat_zen_sec[-1]:.2f} ({len(sat_zen_sec)} puntos)")
    print(f"  Azimuth: {azimuth_diff[0]:.0f}° - {azimuth_diff[-1]:.0f}° ({len(azimuth_diff)} puntos)")
    
    # Procesar cada banda
    print("\n" + "="*70)
    print("Procesando bandas GOES-19 ABI")
    print("="*70)
    
    for band, wl in GOES19_WAVELENGTHS.items():
        print(f"\n[{band}] Wavelength objetivo: {wl} nm")
        
        # Ajustar wavelength si está fuera de rango
        wl_adjusted = wl
        if wl > wavelengths[-1]:
            print(f"    ⚠️  Fuera de rango. Usando {wavelengths[-1]} nm (extrapolación)")
            wl_adjusted = wavelengths[-1]
        
        # Extraer/interpolar para esta wavelength
        # reflectance shape: [wavelength, sun_zen, azimuth, sat_zen]
        refl_3d = interpolate_wavelength(wavelengths, reflectance, wl_adjusted)
        
        # Reordenar de [sun_zen_sec, azimuth, sat_zen_sec] → [sun_zen_sec, sat_zen_sec, azimuth]
        # para que sea compatible con nuestra función de interpolación trilineal
        refl_3d = np.transpose(refl_3d, (0, 2, 1))
        
        print(f"    Shape final: {refl_3d.shape} [sun_zen_sec={refl_3d.shape[0]}, " +
              f"sat_zen_sec={refl_3d.shape[1]}, azimuth={refl_3d.shape[2]}]")
        print(f"    Rango valores: {refl_3d.min():.6f} - {refl_3d.max():.6f}")
        
        # Exportar con ejes en SECANTES (no ángulos)
        output_file = f'rayleigh_lut_{band}.bin'
        export_binary_lut(output_file, sun_zen_sec, sat_zen_sec, 
                         azimuth_diff, refl_3d)
    
    print("\n" + "="*70)
    print("✓ Extracción completada exitosamente")
    print("="*70)
    print("\nArchivos generados:")
    for band in GOES19_WAVELENGTHS.keys():
        print(f"  - rayleigh_lut_{band}.bin")
    print("\nEstos archivos pueden ser cargados en C con rayleigh_lut_load()")

if __name__ == '__main__':
    main()
