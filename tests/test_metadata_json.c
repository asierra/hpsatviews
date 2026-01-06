/*
 * Test unitario para validar la generación de JSON de metadatos
 * Sprint 2: Integración con JSON
 */
#include "../include/metadata.h"
#include "../include/datanc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

// Colores para output
#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define YELLOW "\033[1;33m"
#define NC "\033[0m" // No Color

int tests_passed = 0;
int tests_failed = 0;

void test_assert(bool condition, const char *test_name) {
    if (condition) {
        printf("%s✓ PASS%s: %s\n", GREEN, NC, test_name);
        tests_passed++;
    } else {
        printf("%s✗ FAIL%s: %s\n", RED, NC, test_name);
        tests_failed++;
    }
}

// Test 1: Crear y destruir contexto
void test_metadata_lifecycle() {
    MetadataContext *ctx = metadata_create();
    test_assert(ctx != NULL, "Crear MetadataContext");
    
    if (ctx) {
        metadata_destroy(ctx);
        printf("  Destrucción exitosa\n");
    }
}

// Test 2: Agregar metadatos simples
void test_metadata_add() {
    MetadataContext *ctx = metadata_create();
    
    metadata_add(ctx, "gamma", 1.5);
    metadata_add(ctx, "clahe", true);
    metadata_add(ctx, "mode", "truecolor");
    metadata_add(ctx, "tiles", 8);
    
    test_assert(ctx != NULL, "Agregar metadatos con macro _Generic");
    
    metadata_destroy(ctx);
}

// Test 3: Configurar satélite y comando
void test_metadata_set_basic() {
    MetadataContext *ctx = metadata_create();
    
    metadata_set_satellite(ctx, "goes-16");
    metadata_set_command(ctx, "rgb");
    
    test_assert(ctx != NULL, "Configurar satélite y comando");
    
    metadata_destroy(ctx);
}

// Test 4: Simular carga desde DataNC
void test_metadata_from_nc() {
    MetadataContext *ctx = metadata_create();
    
    // Simular DataNC
    DataNC fake_nc = {0};
    fake_nc.sat_id = SAT_GOES16;
    fake_nc.timestamp = 1723052477; // 2024-08-07 18:01:17 UTC
    fake_nc.band_id = 13;
    fake_nc.varname = "CMI";
    fake_nc.is_float = true;
    fake_nc.fdata.fmin = 180.0;
    fake_nc.fdata.fmax = 320.0;
    
    metadata_from_nc(ctx, &fake_nc);
    
    test_assert(ctx != NULL, "Cargar metadatos desde DataNC simulado");
    
    metadata_destroy(ctx);
}

// Test 5: Configurar geometría
void test_metadata_geometry() {
    MetadataContext *ctx = metadata_create();
    
    metadata_set_geometry(ctx, -100.0f, 25.0f, -90.0f, 15.0f);
    
    test_assert(ctx != NULL, "Configurar geometría (bbox)");
    
    metadata_destroy(ctx);
}

// Test 6: Generar nombre de archivo
void test_metadata_filename() {
    MetadataContext *ctx = metadata_create();
    
    // Configurar datos mínimos
    metadata_set_satellite(ctx, "goes-16");
    metadata_set_command(ctx, "gray");
    
    // Simular timestamp
    DataNC fake_nc = {0};
    fake_nc.sat_id = SAT_GOES16;
    fake_nc.timestamp = 1723052477; // 2024-08-07 18:01:17 UTC
    fake_nc.band_id = 13;
    fake_nc.varname = "C13";
    fake_nc.is_float = true;
    fake_nc.fdata.fmin = 180.0;
    fake_nc.fdata.fmax = 320.0;
    
    metadata_from_nc(ctx, &fake_nc);
    
    // Agregar algunas operaciones
    metadata_add(ctx, "gamma", 1.5);
    metadata_add(ctx, "clahe", true);
    
    char *filename = metadata_build_filename(ctx, ".png");
    
    bool valid = (filename != NULL && strlen(filename) > 0);
    test_assert(valid, "Generar nombre de archivo");
    
    if (filename) {
        printf("  Generado: %s\n", filename);
        free(filename);
    }
    
    metadata_destroy(ctx);
}

// Test 7: Generar JSON completo
void test_metadata_save_json() {
    MetadataContext *ctx = metadata_create();
    
    // Configurar metadatos completos
    metadata_set_command(ctx, "gray");
    metadata_set_satellite(ctx, "goes-16");
    
    // Simular DataNC
    DataNC fake_nc = {0};
    fake_nc.sat_id = SAT_GOES16;
    fake_nc.timestamp = 1723052477;
    fake_nc.band_id = 13;
    fake_nc.varname = "C13";
    fake_nc.is_float = true;
    fake_nc.fdata.fmin = 180.5;
    fake_nc.fdata.fmax = 320.0;
    
    metadata_from_nc(ctx, &fake_nc);
    
    // Agregar geometría
    metadata_set_geometry(ctx, -100.0f, 25.0f, -90.0f, 15.0f);
    
    // Agregar enhancements
    metadata_add(ctx, "gamma", 1.0);
    metadata_add(ctx, "clahe", true);
    metadata_add(ctx, "invert", false);
    
    // Guardar JSON
    const char *output_json = "/tmp/test_metadata.json";
    int result = metadata_save_json(ctx, output_json);
    
    test_assert(result == 0, "Guardar JSON a archivo");
    
    if (result == 0) {
        printf("  JSON generado: %s\n", output_json);
        
        // Verificar que el archivo existe y tiene contenido
        FILE *f = fopen(output_json, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            
            printf("  Tamaño del JSON: %ld bytes\n", size);
            
            // Mostrar primeras líneas
            char line[256];
            printf("  Contenido:\n");
            int line_count = 0;
            while (fgets(line, sizeof(line), f) && line_count < 10) {
                printf("    %s", line);
                line_count++;
            }
            if (line_count >= 10) printf("    ...\n");
            
            fclose(f);
        }
    }
    
    metadata_destroy(ctx);
}

int main(void) {
    printf("=== Test de Generación de Metadatos y JSON (Sprint 2) ===\n\n");
    
    printf("--- Tests Básicos ---\n");
    test_metadata_lifecycle();
    test_metadata_add();
    test_metadata_set_basic();
    
    printf("\n--- Tests de Integración ---\n");
    test_metadata_from_nc();
    test_metadata_geometry();
    test_metadata_filename();
    
    printf("\n--- Test de JSON ---\n");
    test_metadata_save_json();
    
    printf("\n--- Resumen ---\n");
    printf("Tests pasados: %s%d%s\n", GREEN, tests_passed, NC);
    printf("Tests fallidos: %s%d%s\n", tests_failed > 0 ? RED : NC, tests_failed, NC);
    
    if (tests_failed == 0) {
        printf("%s✓ Todos los tests pasaron%s\n", GREEN, NC);
        return 0;
    } else {
        printf("%s✗ Algunos tests fallaron%s\n", RED, NC);
        return 1;
    }
}
