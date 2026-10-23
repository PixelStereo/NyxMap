#!/bin/bash

# Créer un sous-dossier pour les fichiers convertis
mkdir -p mp4_convertis

for f in *.mov *.MOV; do
  # Vérifie si le fichier existe pour éviter les erreurs si aucun .mov n'est trouvé
  [ -e "$file" ] || continue
  
  filename=$(basename -- "$f")
  name="${filename%.*}"

  echo "Conversion de : $filename..."

  ffmpeg -i "$f" \
    -c:v libx264 -crf 18 \
    -preset slow \
    -pix_fmt yuv420p \
    -color_primaries bt709 \
    -color_trc bt709 \
    -colorspace bt709 \
    -c:a aac -b:a 320k \
    -movflags +faststart \
    "mp4_convertis/${name}.mp4"
done

echo "Toutes les conversions sont terminées !"
