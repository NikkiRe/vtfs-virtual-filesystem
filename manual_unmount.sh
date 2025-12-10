#!/bin/bash
# Скрипт для размонтирования VTFS

MODULE_NAME="vtfs"
MOUNT_POINT="/mnt/vtfs"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Требуются права root${NC}"
    echo "Запустите: sudo $0"
    exit 1
fi

echo "Размонтирование файловой системы..."

if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
    umount "$MOUNT_POINT"
    sleep 1
    echo -e "${GREEN}✅ Файловая система размонтирована${NC}"
else
    echo -e "${YELLOW}⚠️  Файловая система не была смонтирована${NC}"
fi

if lsmod | grep -q "^$MODULE_NAME "; then
    rmmod "$MODULE_NAME"
    echo -e "${GREEN}✅ Модуль выгружен${NC}"
else
    echo -e "${YELLOW}⚠️  Модуль не был загружен${NC}"
fi

echo ""
echo -e "${GREEN}Готово! Данные сохранены на сервере.${NC}"

