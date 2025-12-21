#!/usr/bin/env python3
# -*- coding: utf-8 -*-


import rasterio
import sys
import numpy as np

def compare_geotiffs(test_path, ref_path):
    print(f"--- Comparando archivos ---")
    print(f"TEST (Tu código): {test_path}")
    print(f"REF  (GDAL std):  {ref_path}")
    print("-" * 40)

    try:
        with rasterio.open(test_path) as ds_test, rasterio.open(ref_path) as ds_ref:
            # 1. Comparar proyecciones (CRS)
            crs_test = ds_test.crs.to_string() if ds_test.crs else "Sin CRS"
            crs_ref = ds_ref.crs.to_string() if ds_ref.crs else "Sin CRS"
            
            print(f"[CRS]")
            if crs_test == crs_ref:
                print(f"✅ Coinciden: {crs_test}")
            else:
                # A veces son equivalentes pero la cadena difiere ligeramente
                if ds_test.crs == ds_ref.crs:
                    print(f"✅ Coinciden (Lógicamente):")
                    print(f"   Test: {crs_test}")
                    print(f"   Ref:  {crs_ref}")
                else:
                    print(f"❌ DIFIEREN:")
                    print(f"   Test: {crs_test}")
                    print(f"   Ref:  {crs_ref}")

            # 2. Comparar Transformación (GeoTransform)
            gt_test = ds_test.transform
            gt_ref = ds_ref.transform
            
            # GeoTransform: [PixelWidth, RotX, OriginX, RotY, PixelHeight, OriginY]
            # rasterio transform: Affine(a, b, c, d, e, f) -> c=OriginX, f=OriginY
            
            # Diferencia en Origen (Metros)
            diff_x = gt_test.c - gt_ref.c
            diff_y = gt_test.f - gt_ref.f
            
            pixel_w = gt_ref.a
            pixel_h = gt_ref.e
            
            # Diferencia en Píxeles
            diff_px_x = diff_x / pixel_w
            diff_px_y = diff_y / pixel_h # pixel_h suele ser negativo

            print(f"\n[GeoTransform / Alineación]")
            print(f"Origen X (Test): {gt_test.c:12.4f} | Ref: {gt_ref.c:12.4f}")
            print(f"Origen Y (Test): {gt_test.f:12.4f} | Ref: {gt_ref.f:12.4f}")
            
            print(f"Res X    (Test): {gt_test.a:12.4f} | Ref: {gt_ref.a:12.4f}")
            print(f"Res Y    (Test): {gt_test.e:12.4f} | Ref: {gt_ref.e:12.4f}")

            # Umbral de tolerancia (ej. 1% de un píxel)
            tol_px = 0.01 
            
            if abs(diff_px_x) < tol_px and abs(diff_px_y) < tol_px:
                print(f"✅ Alineación Perfecta (Error < {tol_px} px)")
            else:
                print(f"⚠️  DESPLAZAMIENTO DETECTADO:")
                print(f"   Delta X: {diff_x:.4f} m ({diff_px_x:.4f} píxeles)")
                print(f"   Delta Y: {diff_y:.4f} m ({diff_px_y:.4f} píxeles)")
                
                if abs(diff_px_x - 0.5) < 0.1 or abs(diff_px_y - 0.5) < 0.1:
                    print("   -> PISTA: Parece un error de 'Pixel Center' vs 'Top-Left Corner'.")

            # 3. Comparar Dimensiones
            if ds_test.width == ds_ref.width and ds_test.height == ds_ref.height:
                 print(f"\n✅ Dimensiones Coinciden: {ds_test.width}x{ds_test.height}")
            else:
                 print(f"\n❌ Dimensiones DIFIEREN:")
                 print(f"   Test: {ds_test.width}x{ds_test.height}")
                 print(f"   Ref:  {ds_ref.width}x{ds_ref.height}")

    except Exception as e:
        print(f"Error fatal abriendo archivos: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Uso: python validate_geotiff.py <tu_archivo.tif> <referencia_gdal.tif>")
    else:
        compare_geotiffs(sys.argv[1], sys.argv[2])

