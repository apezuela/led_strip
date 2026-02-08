#!/bin/bash

# Script de commit rápido para Git
# Uso: ./commit.sh "mensaje del commit"

# Colores para output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # Sin color

# Verificar si se proporcionó un mensaje
if [ -z "$1" ]; then
    echo -e "${RED}Error: Debes proporcionar un mensaje de commit${NC}"
    echo "Uso: ./commit.sh \"tu mensaje aquí\""
    exit 1
fi

# Mostrar archivos modificados
echo -e "${BLUE}📋 Archivos modificados:${NC}"
git status --short

echo ""
read -p "¿Continuar con el commit? (s/n): " -n 1 -r
echo

if [[ $REPLY =~ ^[Ss]$ ]]; then
    # Añadir todos los archivos
    echo -e "${BLUE}➕ Añadiendo archivos...${NC}"
    git add .
    
    # Hacer commit
    echo -e "${BLUE}💾 Haciendo commit...${NC}"
    git commit -m "$1"
    
    # Push
    echo -e "${BLUE}🚀 Subiendo a GitHub...${NC}"
    git push
    
    echo -e "${GREEN}✅ ¡Listo! Cambios subidos exitosamente${NC}"
else
    echo -e "${RED}❌ Operación cancelada${NC}"
    exit 0
fi
