
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clip_loader.h"
#include "logger.h"

#define MAX_LINE_LENGTH 256

GeoClip buscar_clip_por_clave(const char *ruta_archivo, const char *clave_buscada) {
    GeoClip resultado = {0}; 
    resultado.encontrado = 0;

    FILE *fp = fopen(ruta_archivo, "r");
    if (fp == NULL) {
        LOG_ERROR("No se pudo abrir el archivo de recortes: %s", ruta_archivo);
        return resultado;
    }

    char linea[MAX_LINE_LENGTH];
    
    // Saltamos la cabecera si existe
    if (fgets(linea, sizeof(linea), fp)) {
        if (strstr(linea, "clave") == NULL) {
            rewind(fp); 
        }
    }

    while (fgets(linea, sizeof(linea), fp)) {
        char linea_copia[MAX_LINE_LENGTH];
        strcpy(linea_copia, linea);

        char *token = strtok(linea_copia, ",");
        if (token == NULL) continue;

        // Limpieza de saltos de línea
        token[strcspn(token, "\r\n")] = 0; 

        if (strcmp(token, clave_buscada) == 0) {
            strncpy(resultado.clave, token, sizeof(resultado.clave) - 1);
            resultado.encontrado = 1;

            token = strtok(NULL, ",");
            if (token) strncpy(resultado.region, token, sizeof(resultado.region) - 1);

            token = strtok(NULL, ","); if(token) resultado.ul_x = atof(token);
            token = strtok(NULL, ","); if(token) resultado.ul_y = atof(token);
            token = strtok(NULL, ","); if(token) resultado.lr_x = atof(token);
            token = strtok(NULL, ","); if(token) resultado.lr_y = atof(token);

            break; 
        }
    }

    fclose(fp);
    return resultado;
}

void listar_clips_disponibles(const char *ruta_archivo) {
    FILE *fp = fopen(ruta_archivo, "r");
    if (fp == NULL) {
        LOG_WARN("No se puede leer el archivo en %s", ruta_archivo);
        return;
    }

    char linea[MAX_LINE_LENGTH];
    
    printf("\nRecortes (Clips) Disponibles:\n");
    printf("===================================================================================\n");
    printf("%-15s | %-30s | %s\n", "CLAVE", "DESCRIPCIÓN", "COORDENADAS (lon_min,lat_max,lon_max,lat_min)");
    printf("===================================================================================\n");

    int es_encabezado = 1;

    while (fgets(linea, sizeof(linea), fp)) {
        if (strlen(linea) < 5) continue;

        char linea_copia[MAX_LINE_LENGTH];
        strcpy(linea_copia, linea);

        char *clave = strtok(linea_copia, ",");
        if (!clave) continue;

        if (es_encabezado && strcmp(clave, "clave") == 0) {
            es_encabezado = 0;
            continue;
        }

        char *region = strtok(NULL, ",");
        if (!region) region = "---";
        region[strcspn(region, "\r\n")] = 0;

        char *ul_x_str = strtok(NULL, ",");
        char *ul_y_str = strtok(NULL, ",");
        char *lr_x_str = strtok(NULL, ",");
        char *lr_y_str = strtok(NULL, ",");

        if (ul_x_str && ul_y_str && lr_x_str && lr_y_str) {
            // Limpiar saltos de línea del último valor
            lr_y_str[strcspn(lr_y_str, "\r\n")] = 0;
            // Convertir a double y formatear con 4 decimales
            double ul_x = atof(ul_x_str);
            double ul_y = atof(ul_y_str);
            double lr_x = atof(lr_x_str);
            double lr_y = atof(lr_y_str);
            printf("%-15s | %-30s | %.6f,%.6f,%.6f,%.6f\n", clave, region, ul_x, ul_y, lr_x, lr_y);
        } else {
            printf("%-15s | %-30s | (coordenadas incompletas)\n", clave, region);
        }
    }
    printf("===================================================================================\n\n");
    fclose(fp);
}