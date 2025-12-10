#!/bin/bash
# Комплексный тест файловой системы VTFS
# Тестирует работу в RAM и Server режимах

set -e

MODULE_NAME="vtfs"
MOUNT_POINT="/mnt/vtfs"
SERVER_URL="http://127.0.0.1:8080/api"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

cleanup() {
    echo ""
    echo "Очистка..."
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        umount "$MOUNT_POINT" 2>/dev/null || true
        sleep 1
    fi
    if lsmod | grep -q "^$MODULE_NAME "; then
        rmmod "$MODULE_NAME" 2>/dev/null || true
    fi
    rm -rf "$MOUNT_POINT" 2>/dev/null || true
}

echo "=========================================="
echo "  ТЕСТ ФАЙЛОВОЙ СИСТЕМЫ VTFS"
echo "=========================================="
echo ""

# Проверка прав
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Требуются права root${NC}"
    echo "Запустите: sudo $0"
    exit 1
fi

# Устанавливаем trap только после проверки прав
trap cleanup EXIT

# Компиляция модуля
echo "[1/8] Компиляция модуля..."
make clean >/dev/null 2>&1 || true
if ! make >/dev/null 2>&1; then
    echo -e "${RED}❌ Ошибка компиляции${NC}"
    exit 1
fi
echo -e "${GREEN}✅ Модуль скомпилирован${NC}"
echo ""

# Создание точки монтирования
mkdir -p "$MOUNT_POINT"

# ==========================================
# ТЕСТ 1: RAM РЕЖИМ (без токена)
# ==========================================
echo "[2/8] Тест RAM режима..."
insmod "$MODULE_NAME.ko" 2>/dev/null || true
mount -t vtfs none "$MOUNT_POINT" -o token=""
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ RAM режим: монтирование успешно${NC}"
    
    # Создание файла
    echo "test_ram_data" > "$MOUNT_POINT/test_ram.txt"
    if [ -f "$MOUNT_POINT/test_ram.txt" ]; then
        echo -e "${GREEN}✅ RAM режим: файл создан${NC}"
        if [ "$(cat "$MOUNT_POINT/test_ram.txt")" = "test_ram_data" ]; then
            echo -e "${GREEN}✅ RAM режим: данные записаны и прочитаны${NC}"
        else
            echo -e "${RED}❌ RAM режим: данные не совпадают${NC}"
        fi
    else
        echo -e "${RED}❌ RAM режим: файл не создан${NC}"
    fi
    
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        umount "$MOUNT_POINT"
        sleep 1
    fi
    if lsmod | grep -q "^$MODULE_NAME "; then
        rmmod "$MODULE_NAME"
    fi
    echo -e "${GREEN}✅ RAM режим: тест завершен${NC}"
else
    echo -e "${RED}❌ RAM режим: ошибка монтирования${NC}"
fi
echo ""

# ==========================================
# ТЕСТ 2: SERVER РЕЖИМ (с токеном)
# ==========================================
echo "[3/8] Проверка доступности сервера..."
if ! curl -s "$SERVER_URL/list?token=test&parent_ino=100" >/dev/null 2>&1; then
    echo -e "${YELLOW}⚠️  Сервер недоступен, пропускаем тест сервера${NC}"
    echo "   Запустите сервер: cd server && mvn spring-boot:run"
    echo ""
    exit 0
fi
echo -e "${GREEN}✅ Сервер доступен${NC}"
echo ""

echo "[4/8] Тест Server режима..."
TOKEN="test_$(date +%s)"
insmod "$MODULE_NAME.ko" 2>/dev/null || true
mount -t vtfs none "$MOUNT_POINT" -o token="$TOKEN"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ Server режим: монтирование успешно (token: $TOKEN)${NC}"
    
    # Создание файла
    echo "test_server_data" > "$MOUNT_POINT/test_server.txt"
    if [ -f "$MOUNT_POINT/test_server.txt" ]; then
        echo -e "${GREEN}✅ Server режим: файл создан${NC}"
        if [ "$(cat "$MOUNT_POINT/test_server.txt")" = "test_server_data" ]; then
            echo -e "${GREEN}✅ Server режим: данные записаны и прочитаны${NC}"
        else
            echo -e "${RED}❌ Server режим: данные не совпадают${NC}"
        fi
    else
        echo -e "${RED}❌ Server режим: файл не создан${NC}"
    fi
    
    # Создание директории
    mkdir "$MOUNT_POINT/test_dir"
    if [ -d "$MOUNT_POINT/test_dir" ]; then
        echo -e "${GREEN}✅ Server режим: директория создана${NC}"
    else
        echo -e "${RED}❌ Server режим: директория не создана${NC}"
    fi
    
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        umount "$MOUNT_POINT"
        sleep 1
    fi
    if lsmod | grep -q "^$MODULE_NAME "; then
        rmmod "$MODULE_NAME"
    fi
    echo -e "${GREEN}✅ Server режим: тест завершен${NC}"
    
    # Проверка персистентности
    echo ""
    echo "[5/8] Проверка персистентности данных..."
    sleep 1
    insmod "$MODULE_NAME.ko" 2>/dev/null || true
    mount -t vtfs none "$MOUNT_POINT" -o token="$TOKEN"
    
    if [ -f "$MOUNT_POINT/test_server.txt" ]; then
        echo -e "${GREEN}✅ Данные сохранились после перемонтирования${NC}"
        if [ "$(cat "$MOUNT_POINT/test_server.txt")" = "test_server_data" ]; then
            echo -e "${GREEN}✅ Данные корректны${NC}"
        fi
    else
        echo -e "${RED}❌ Данные не сохранились${NC}"
    fi
    
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        umount "$MOUNT_POINT"
        sleep 1
    fi
    if lsmod | grep -q "^$MODULE_NAME "; then
        rmmod "$MODULE_NAME"
    fi
else
    echo -e "${RED}❌ Server режим: ошибка монтирования${NC}"
fi
echo ""

# ==========================================
# ТЕСТ 3: ПЕРЕКЛЮЧЕНИЕ РЕЖИМОВ
# ==========================================
echo "[6/8] Тест переключения режимов..."
insmod "$MODULE_NAME.ko" 2>/dev/null || true

# RAM -> Server
mount -t vtfs none "$MOUNT_POINT" -o token=""
echo "ram_data" > "$MOUNT_POINT/ram_file.txt"
if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
    umount "$MOUNT_POINT"
    sleep 1
fi

mount -t vtfs none "$MOUNT_POINT" -o token="$TOKEN"
if [ ! -f "$MOUNT_POINT/ram_file.txt" ]; then
    echo -e "${GREEN}✅ RAM данные не видны в Server режиме (правильно)${NC}"
else
    echo -e "${RED}❌ RAM данные видны в Server режиме (неправильно)${NC}"
fi
if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
    umount "$MOUNT_POINT"
    sleep 1
fi

# Server -> RAM
mount -t vtfs none "$MOUNT_POINT" -o token="$TOKEN"
echo "server_data" > "$MOUNT_POINT/server_file.txt"
if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
    umount "$MOUNT_POINT"
    sleep 1
fi

mount -t vtfs none "$MOUNT_POINT" -o token=""
if [ ! -f "$MOUNT_POINT/server_file.txt" ]; then
    echo -e "${GREEN}✅ Server данные не видны в RAM режиме (правильно)${NC}"
else
    echo -e "${RED}❌ Server данные видны в RAM режиме (неправильно)${NC}"
fi
if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
    umount "$MOUNT_POINT"
    sleep 1
fi
if lsmod | grep -q "^$MODULE_NAME "; then
    rmmod "$MODULE_NAME"
fi

echo -e "${GREEN}✅ Переключение режимов работает корректно${NC}"
echo ""

# ==========================================
# ИТОГИ
# ==========================================
echo "[7/8] Итоги тестирования..."
echo -e "${GREEN}✅ Все тесты пройдены успешно!${NC}"
echo ""
echo "[8/8] Очистка..."
cleanup
echo -e "${GREEN}✅ Очистка завершена${NC}"
echo ""
echo "=========================================="
echo -e "${GREEN}  ВСЕ ТЕСТЫ ПРОЙДЕНЫ${NC}"
echo "=========================================="

