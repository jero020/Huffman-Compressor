#!/bin/bash

# Compilar el proyecto con make
echo "Compilando el editor de texto..."
make

# Verificar si la compilación fue exitosa (código de salida 0)
if [ $? -eq 0 ]; then
    echo "Compilación exitosa. Iniciando el editor..."
    sleep 1
    # Ejecutar el editor pasándole todos los argumentos proporcionados al script
    ./editor "$@"
else
    echo "Error en la compilación. No se iniciará el editor."
    exit 1
fi
