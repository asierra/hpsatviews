#ifndef NOCTURNAL_PSEUDOCOLOR_H
#define NOCTURNAL_PSEUDOCOLOR_H

#include "image.h"
#include "datanc.h"

/**
 * @brief Genera una imagen de pseudocolor nocturno a partir de datos de temperatura de brillo.
 *
 * Esta función toma datos de temperatura de brillo en Kelvin (banda 13 de GOES),
 * los mapea a una paleta de colores meteorológica y opcionalmente los mezcla
 * con una imagen de fondo (luces de ciudad).
 *
 * @param temp_data Puntero a la estructura DataF con los datos de temperatura.
 * @param fondo Puntero opcional a una imagen de fondo (ImageData). Si es NULL, no se usa fondo.
 * @return Una estructura ImageData con la imagen RGB resultante.
 */
ImageData create_nocturnal_pseudocolor(const DataF* temp_data, const ImageData* fondo);

#endif
