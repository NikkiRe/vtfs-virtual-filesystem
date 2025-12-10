#!/bin/bash
# Тест интеграции модуля с сервером
# Проверяет сохранение данных после размонтирования/перемонтирования

set -e

MODULE_NAME="vtfs"
MOUNT_POINT="/mnt/vtfs"
SERVER_URL="http://127.0.0.1:8080/api"
TOKEN="test_persistence_$(date +%s)"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

cleanup() {
    umount "$MOUNT_POINT" 2>/dev/null || true
    rmmod "$MODULE_NAME" 2>/dev/null || true
    rm -rf "$MOUNT_POINT" 2>/dev/null || true
}

echo "=========================================="
echo "  ТЕСТ ИНТЕГРАЦИИ С СЕРВЕРОМ"
echo "=========================================="
echo "Токен: $TOKEN"
echo ""

if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Требуются права root${NC}"
    echo "Запустите: sudo $0"
    exit 1
fi

trap cleanup EXIT

# Проверка доступности сервера
echo "[1/10] Проверка доступности сервера..."
if ! curl -s "$SERVER_URL/list?token=$TOKEN&parent_ino=100" > /dev/null 2>&1; then
    echo -e "${YELLOW}⚠️  Сервер недоступен${NC}"
    echo "Запустите сервер: cd server && mvn spring-boot:run"
    echo "Или запустите в фоне: cd server && nohup mvn spring-boot:run > /dev/null 2>&1 &"
    exit 1
fi
echo -e "${GREEN}✅ Сервер доступен${NC}"
echo ""

# Компиляция модуля
echo "[2/10] Компиляция модуля..."
make clean >/dev/null 2>&1 || true
if ! make >/dev/null 2>&1; then
    echo -e "${RED}❌ Ошибка компиляции${NC}"
    exit 1
fi
echo -e "${GREEN}✅ Модуль скомпилирован${NC}"
echo ""

# Создание точки монтирования
mkdir -p "$MOUNT_POINT"

# Монтирование в Server режиме
echo "[3/10] Монтирование в Server режиме..."
insmod "$MODULE_NAME.ko" 2>/dev/null || true
if ! mount -t vtfs none "$MOUNT_POINT" -o token="$TOKEN"; then
    echo -e "${RED}❌ Ошибка монтирования${NC}"
    exit 1
fi
echo -e "${GREEN}✅ Монтирование успешно${NC}"
echo ""

# Создание файлов и директорий
echo "[4/10] Создание файлов и директорий..."
echo "test_data_1" > "$MOUNT_POINT/file1.txt"
echo "test_data_2" > "$MOUNT_POINT/file2.txt"
mkdir "$MOUNT_POINT/test_dir"
echo "test_data_3" > "$MOUNT_POINT/test_dir/file3.txt"
echo -e "${GREEN}✅ Файлы созданы${NC}"
echo ""

# Проверка что файлы созданы
echo "[5/10] Проверка созданных файлов..."
if [ ! -f "$MOUNT_POINT/file1.txt" ] || [ ! -f "$MOUNT_POINT/file2.txt" ] || [ ! -f "$MOUNT_POINT/test_dir/file3.txt" ]; then
    echo -e "${RED}❌ Файлы не найдены${NC}"
    exit 1
fi

DATA1=$(cat "$MOUNT_POINT/file1.txt")
DATA2=$(cat "$MOUNT_POINT/file2.txt")
DATA3=$(cat "$MOUNT_POINT/test_dir/file3.txt")

if [ "$DATA1" != "test_data_1" ] || [ "$DATA2" != "test_data_2" ] || [ "$DATA3" != "test_data_3" ]; then
    echo -e "${RED}❌ Данные не совпадают${NC}"
    exit 1
fi
echo -e "${GREEN}✅ Данные корректны${NC}"
echo ""

# Размонтирование
echo "[6/10] Размонтирование..."
umount "$MOUNT_POINT" || true
sleep 1
rmmod "$MODULE_NAME" 2>/dev/null || true
sleep 1
echo -e "${GREEN}✅ Размонтирование успешно${NC}"
echo ""

# Повторное монтирование
echo "[7/10] Повторное монтирование..."
insmod "$MODULE_NAME.ko" 2>/dev/null || true
if ! mount -t vtfs none "$MOUNT_POINT" -o token="$TOKEN"; then
    echo -e "${RED}❌ Ошибка повторного монтирования${NC}"
    exit 1
fi
echo -e "${GREEN}✅ Повторное монтирование успешно${NC}"
echo ""

# Проверка сохраненных данных
echo "[8/10] Проверка сохраненных данных..."
if [ ! -f "$MOUNT_POINT/file1.txt" ]; then
    echo -e "${RED}❌ file1.txt не найден после перемонтирования${NC}"
    exit 1
fi

if [ ! -f "$MOUNT_POINT/file2.txt" ]; then
    echo -e "${RED}❌ file2.txt не найден после перемонтирования${NC}"
    exit 1
fi

if [ ! -d "$MOUNT_POINT/test_dir" ]; then
    echo -e "${RED}❌ test_dir не найден после перемонтирования${NC}"
    exit 1
fi

if [ ! -f "$MOUNT_POINT/test_dir/file3.txt" ]; then
    echo -e "${RED}❌ test_dir/file3.txt не найден после перемонтирования${NC}"
    exit 1
fi
echo -e "${GREEN}✅ Все файлы найдены${NC}"
echo ""

# Проверка содержимого файлов
echo "[9/10] Проверка содержимого файлов..."
RESTORED_DATA1=$(cat "$MOUNT_POINT/file1.txt")
RESTORED_DATA2=$(cat "$MOUNT_POINT/file2.txt")
RESTORED_DATA3=$(cat "$MOUNT_POINT/test_dir/file3.txt")

if [ "$RESTORED_DATA1" != "test_data_1" ]; then
    echo -e "${RED}❌ Данные file1.txt не совпадают: '$RESTORED_DATA1' (ожидалось 'test_data_1')${NC}"
    exit 1
fi

if [ "$RESTORED_DATA2" != "test_data_2" ]; then
    echo -e "${RED}❌ Данные file2.txt не совпадают: '$RESTORED_DATA2' (ожидалось 'test_data_2')${NC}"
    exit 1
fi

if [ "$RESTORED_DATA3" != "test_data_3" ]; then
    echo -e "${RED}❌ Данные file3.txt не совпадают: '$RESTORED_DATA3' (ожидалось 'test_data_3')${NC}"
    exit 1
fi
echo -e "${GREEN}✅ Все данные восстановлены корректно${NC}"
echo ""

# Итоги
echo "[10/10] Итоги тестирования..."
echo -e "${GREEN}✅ Все тесты пройдены успешно!${NC}"
echo ""
echo "=========================================="
echo -e "${GREEN}  ИНТЕГРАЦИЯ РАБОТАЕТ КОРРЕКТНО${NC}"
echo "=========================================="
echo ""
echo "Результаты:"
echo "  ✅ Файлы создаются на сервере"
echo "  ✅ Данные сохраняются на сервере"
echo "  ✅ Данные загружаются при монтировании"
echo "  ✅ Данные переживают размонтирование"
echo ""
echo "Для проверки после перезагрузки системы:"
echo "  1. Перезагрузите Ubuntu"
echo "  2. Запустите сервер: cd server && mvn spring-boot:run"
echo "  3. Запустите: sudo mount -t vtfs none /mnt/vtfs -o token=\"$TOKEN\""
echo "  4. Проверьте файлы: ls -la /mnt/vtfs"


