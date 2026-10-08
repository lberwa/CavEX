#!/bin/bash

# Basisverzeichnis = dort, wo das Skript liegt
BASE_DIR="$(cd "$(dirname "$0")" && pwd)"

cd "$BASE_DIR" || exit 1

# Alle MP3-Dateien rekursiv finden
find . -type f -iname "*.mp3" | while read -r file; do
    # Verzeichnis und Dateiname trennen
    dir="$(dirname "$file")"
    name="$(basename "$file")"

    # Zielname
    out="${name%.mp3}.pcm"

    echo "Konvertiere: $file"

    # In den Zielordner wechseln
    cd "$BASE_DIR/$dir" || exit 1

    # Konvertieren (nur mit Dateinamen!)
    ffmpeg -y \
        -i "$name" \
        -f s16le \
        -acodec pcm_s16le \
        -ac 2 \
        -ar 44100 \
        "$out"

    # Zurück ins Basisverzeichnis
    cd "$BASE_DIR" || exit 1
done

echo "Fertig."

