# 1. Estampar la letra (a, b, c...) en la esquina superior izquierda de cada imagen
for img in a_otis.jpg b_ash.jpg c_airmass.jpg d_ctp.jpg e_clahe.jpg f_severestorm.jpg; do
    # Extraer la primera letra del nombre del archivo
    letra=$(echo $img | cut -c1)
    
    convert "$img" \
        -gravity NorthWest \
        -pointsize 36 \
        -font "Helvetica-Bold" \
        -fill black \
        -undercolor white \
        -annotate +15+15 " ($letra) " \
        "lbl_$img"
done

# 2. Ensamblar el panel final usando las imágenes ya etiquetadas
# (Ajusta el orden de los archivos lbl_*.jpg aquí si intercambiaste c, d o e)
montage \
  lbl_a_otis.jpg \
  lbl_b_ash.jpg \
  lbl_c_airmass.jpg \
  lbl_d_ctp.jpg \
  lbl_e_clahe.jpg \
  lbl_f_severestorm.jpg \
  -tile 2x3 \
  -geometry +15+15 \
  -background white \
  ../fig2.jpg

