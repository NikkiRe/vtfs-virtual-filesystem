#!/bin/bash
# Скрипт для проверки данных после перезагрузки
# Использование: sudo ./check_after_reboot.sh [TOKEN]

set -e

MODULE_NAME="vtfs"
MOUNT_POINT="/mnt/vtfs"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Требуются права root${NC}"
    echo "Запустите: sudo $0 [TOKEN]"
    exit 1
fi

# Если токен не указан, выбираем автоматически
if [ -z "$1" ]; then
    echo -e "${BLUE}Поиск токенов в базе данных...${NC}"
    
    # Проверяем доступность PostgreSQL
    if ! command -v psql >/dev/null 2>&1; then
        echo -e "${RED}❌ psql не найден. Установите PostgreSQL client${NC}"
        exit 1
    fi
    
    # Получаем список токенов с префиксом test_persistence_ отсортированных по времени (последний = самый новый)
    TOKENS=$(PGPASSWORD=vtfs_password psql -h localhost -U vtfs_user -d vtfs_db -t -c "SELECT DISTINCT token FROM vtfs_files WHERE token LIKE 'test_persistence_%' ORDER BY token DESC;" 2>/dev/null | grep -v '^$' | tr -d ' ')
    
    if [ -z "$TOKENS" ]; then
        echo -e "${YELLOW}⚠️  Токены с префиксом 'test_persistence_' не найдены${NC}"
        echo ""
        echo "Попытка найти любые токены..."
        TOKENS=$(PGPASSWORD=vtfs_password psql -h localhost -U vtfs_user -d vtfs_db -t -c "SELECT DISTINCT token FROM vtfs_files ORDER BY token DESC LIMIT 10;" 2>/dev/null | grep -v '^$' | tr -d ' ')
        
        if [ -z "$TOKENS" ]; then
            echo -e "${RED}❌ В базе данных нет токенов${NC}"
            echo "Возможно, вы еще не запускали тесты или данные были удалены."
            exit 1
        fi
    fi
    
    TOKEN_COUNT=$(echo "$TOKENS" | wc -l)
    
    if [ "$TOKEN_COUNT" -eq 1 ]; then
        TOKEN=$(echo "$TOKENS" | head -1)
        echo -e "${GREEN}Найден токен: $TOKEN${NC}"
    else
        echo -e "${GREEN}Найдено токенов: $TOKEN_COUNT${NC}"
        echo ""
        echo "Доступные токены (отсортированы по времени создания, последний = самый новый):"
        echo "$TOKENS" | nl -w2 -s'. '
        echo ""
        # Берем последний (самый новый) токен
        TOKEN=$(echo "$TOKENS" | head -1)
        echo -e "${GREEN}Используется последний токен: $TOKEN${NC}"
    fi
    echo ""
else
    TOKEN="$1"
fi

cleanup() {
    umount "$MOUNT_POINT" 2>/dev/null || true
    rmmod "$MODULE_NAME" 2>/dev/null || true
}

# Не размонтируем автоматически при успешном завершении
# trap cleanup EXIT

echo "=========================================="
echo "  ПРОВЕРКА ДАННЫХ ПОСЛЕ ПЕРЕЗАГРУЗКИ"
echo "=========================================="
echo "Токен: $TOKEN"
echo ""

# Компиляция модуля
echo "[1/5] Компиляция модуля..."
make clean >/dev/null 2>&1 || true
if ! make >/dev/null 2>&1; then
    echo -e "${RED}❌ Ошибка компиляции${NC}"
    exit 1
fi
echo -e "${GREEN}✅ Модуль скомпилирован${NC}"
echo ""

# Монтирование
echo "[2/5] Монтирование..."
mkdir -p "$MOUNT_POINT"

# Проверяем и размонтируем если уже смонтировано
if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
    echo "Размонтирование существующей точки монтирования..."
    umount "$MOUNT_POINT" 2>/dev/null || true
    sleep 1
fi

# Проверяем и выгружаем модуль если уже загружен
if lsmod | grep -q "^$MODULE_NAME "; then
    echo "Выгрузка существующего модуля..."
    rmmod "$MODULE_NAME" 2>/dev/null || true
    sleep 1
fi

# Загружаем модуль
if ! insmod "$MODULE_NAME.ko" 2>/dev/null; then
    echo -e "${YELLOW}⚠️  Модуль уже загружен или ошибка загрузки${NC}"
    # Пытаемся выгрузить и загрузить заново
    rmmod "$MODULE_NAME" 2>/dev/null || true
    sleep 1
    if ! insmod "$MODULE_NAME.ko" 2>/dev/null; then
        echo -e "${RED}❌ Ошибка загрузки модуля${NC}"
        exit 1
    fi
fi

# Монтируем файловую систему
if ! mount -t vtfs none "$MOUNT_POINT" -o token="$TOKEN"; then
    echo -e "${RED}❌ Ошибка монтирования${NC}"
    exit 1
fi
echo -e "${GREEN}✅ Монтирование успешно${NC}"
echo ""

# Проверка файлов
echo "[3/5] Проверка файлов..."
FILES_FOUND=0

if [ -f "$MOUNT_POINT/file1.txt" ]; then
    DATA1=$(cat "$MOUNT_POINT/file1.txt")
    echo -e "${GREEN}✅ file1.txt найден: '$DATA1'${NC}"
    FILES_FOUND=$((FILES_FOUND + 1))
else
    echo -e "${YELLOW}⚠️  file1.txt не найден${NC}"
fi

if [ -f "$MOUNT_POINT/file2.txt" ]; then
    DATA2=$(cat "$MOUNT_POINT/file2.txt")
    echo -e "${GREEN}✅ file2.txt найден: '$DATA2'${NC}"
    FILES_FOUND=$((FILES_FOUND + 1))
else
    echo -e "${YELLOW}⚠️  file2.txt не найден${NC}"
fi

if [ -d "$MOUNT_POINT/test_dir" ]; then
    echo -e "${GREEN}✅ test_dir найден${NC}"
    if [ -f "$MOUNT_POINT/test_dir/file3.txt" ]; then
        DATA3=$(cat "$MOUNT_POINT/test_dir/file3.txt")
        echo -e "${GREEN}✅ test_dir/file3.txt найден: '$DATA3'${NC}"
        FILES_FOUND=$((FILES_FOUND + 1))
    else
        echo -e "${YELLOW}⚠️  test_dir/file3.txt не найден${NC}"
    fi
else
    echo -e "${YELLOW}⚠️  test_dir не найден${NC}"
fi

echo ""

# Список всех файлов
echo "[4/5] Список всех файлов в корне:"
timeout 5 ls -la "$MOUNT_POINT" 2>&1 || {
    echo -e "${YELLOW}⚠️  ls завис или занял слишком много времени${NC}"
    echo "Попытка принудительного завершения..."
}
echo ""

# Итоги
echo "[5/5] Итоги проверки..."
if [ $FILES_FOUND -eq 3 ]; then
    echo -e "${GREEN}✅ Все файлы найдены и данные сохранены!${NC}"
    echo ""
    echo "=========================================="
    echo -e "${GREEN}  ПЕРСИСТЕНТНОСТЬ РАБОТАЕТ${NC}"
    echo "=========================================="
    echo ""
    echo -e "${BLUE}Файловая система остается смонтированной в $MOUNT_POINT${NC}"
    echo "Для размонтирования выполните:"
    echo "  sudo umount $MOUNT_POINT"
    echo "  sudo rmmod $MODULE_NAME"
    exit 0
else
    echo -e "${YELLOW}⚠️  Найдено файлов: $FILES_FOUND из 3${NC}"
    echo ""
    echo "Возможные причины:"
    echo "  - Сервер не запущен"
    echo "  - Неправильный токен"
    echo "  - Данные не были сохранены на сервере"
    # Размонтируем при ошибке
    cleanup
    exit 1
fi



