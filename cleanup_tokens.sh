#!/bin/bash
# Скрипт для очистки старых токенов из базы данных

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo "=========================================="
echo "  ОЧИСТКА ТОКЕНОВ ИЗ БАЗЫ ДАННЫХ"
echo "=========================================="
echo ""

# Что такое токен
echo -e "${BLUE}Что такое токен?${NC}"
echo "Токен - это уникальный идентификатор файловой системы в Server режиме."
echo "Каждая смонтированная ФС с токеном имеет свои файлы в базе данных."
echo "Пример: mount -t vtfs none /mnt/vtfs -o token=\"my_token\""
echo ""

# Показываем все токены
echo -e "${BLUE}Текущие токены в базе данных:${NC}"
TOKENS=$(PGPASSWORD=vtfs_password psql -h localhost -U vtfs_user -d vtfs_db -t -c "SELECT DISTINCT token FROM vtfs_files ORDER BY token;" 2>/dev/null | grep -v '^$' | tr -d ' ')

if [ -z "$TOKENS" ]; then
    echo -e "${YELLOW}В базе данных нет токенов${NC}"
    exit 0
fi

echo "$TOKENS" | nl
echo ""

# Статистика по токенам
echo -e "${BLUE}Статистика по токенам:${NC}"
PGPASSWORD=vtfs_password psql -h localhost -U vtfs_user -d vtfs_db <<SQL
SELECT 
    token,
    COUNT(*) as files_count,
    SUM(CASE WHEN (mode::integer & 0040000) != 0 THEN 1 ELSE 0 END) as dirs_count,
    SUM(CASE WHEN (mode::integer & 0100000) != 0 THEN 1 ELSE 0 END) as files_count_only
FROM vtfs_files 
GROUP BY token 
ORDER BY token;
SQL

echo ""

# Выбор действия
echo "Что вы хотите сделать?"
echo "  1) Удалить все токены (ОПАСНО!)"
echo "  2) Удалить токены с префиксом 'test_' (старые тесты)"
echo "  3) Удалить токены с префиксом 'test_persistence_' (старые тесты персистентности)"
echo "  4) Удалить токены с префиксом 'token=' (старые токены с ошибкой парсинга)"
echo "  5) Удалить конкретный токен"
echo "  6) Отмена"
echo ""
read -p "Выберите действие (1-6): " ACTION

case $ACTION in
    1)
        echo -e "${RED}⚠️  ВНИМАНИЕ: Вы удалите ВСЕ данные из базы!${NC}"
        read -p "Вы уверены? (yes/no): " CONFIRM
        if [ "$CONFIRM" = "yes" ]; then
            PGPASSWORD=vtfs_password psql -h localhost -U vtfs_user -d vtfs_db <<SQL
DELETE FROM vtfs_file_data;
DELETE FROM vtfs_files;
SQL
            echo -e "${GREEN}✅ Все токены удалены${NC}"
        else
            echo "Отменено"
        fi
        ;;
    2)
        COUNT=$(PGPASSWORD=vtfs_password psql -h localhost -U vtfs_user -d vtfs_db -t -c "SELECT COUNT(*) FROM vtfs_files WHERE token LIKE 'test_%';" 2>/dev/null | tr -d ' ')
        echo "Будет удалено записей: $COUNT"
        read -p "Продолжить? (yes/no): " CONFIRM
        if [ "$CONFIRM" = "yes" ]; then
            PGPASSWORD=vtfs_password psql -h localhost -U vtfs_user -d vtfs_db <<SQL
DELETE FROM vtfs_file_data WHERE token IN (SELECT DISTINCT token FROM vtfs_files WHERE token LIKE 'test_%');
DELETE FROM vtfs_files WHERE token LIKE 'test_%';
SQL
            echo -e "${GREEN}✅ Токены с префиксом 'test_' удалены${NC}"
        else
            echo "Отменено"
        fi
        ;;
    3)
        COUNT=$(PGPASSWORD=vtfs_password psql -h localhost -U vtfs_user -d vtfs_db -t -c "SELECT COUNT(*) FROM vtfs_files WHERE token LIKE 'test_persistence_%';" 2>/dev/null | tr -d ' ')
        echo "Будет удалено записей: $COUNT"
        read -p "Продолжить? (yes/no): " CONFIRM
        if [ "$CONFIRM" = "yes" ]; then
            PGPASSWORD=vtfs_password psql -h localhost -U vtfs_user -d vtfs_db <<SQL
DELETE FROM vtfs_file_data WHERE token IN (SELECT DISTINCT token FROM vtfs_files WHERE token LIKE 'test_persistence_%');
DELETE FROM vtfs_files WHERE token LIKE 'test_persistence_%';
SQL
            echo -e "${GREEN}✅ Токены с префиксом 'test_persistence_' удалены${NC}"
        else
            echo "Отменено"
        fi
        ;;
    4)
        COUNT=$(PGPASSWORD=vtfs_password psql -h localhost -U vtfs_user -d vtfs_db -t -c "SELECT COUNT(*) FROM vtfs_files WHERE token LIKE 'token=%';" 2>/dev/null | tr -d ' ')
        echo "Будет удалено записей: $COUNT"
        echo -e "${YELLOW}Эти токены были созданы из-за старой ошибки парсинга mount options${NC}"
        read -p "Продолжить? (yes/no): " CONFIRM
        if [ "$CONFIRM" = "yes" ]; then
            PGPASSWORD=vtfs_password psql -h localhost -U vtfs_user -d vtfs_db <<SQL
DELETE FROM vtfs_file_data WHERE token IN (SELECT DISTINCT token FROM vtfs_files WHERE token LIKE 'token=%');
DELETE FROM vtfs_files WHERE token LIKE 'token=%';
SQL
            echo -e "${GREEN}✅ Токены с префиксом 'token=' удалены${NC}"
        else
            echo "Отменено"
        fi
        ;;
    5)
        read -p "Введите токен для удаления: " TOKEN_TO_DELETE
        if [ -z "$TOKEN_TO_DELETE" ]; then
            echo -e "${RED}Токен не указан${NC}"
            exit 1
        fi
        
        COUNT=$(PGPASSWORD=vtfs_password psql -h localhost -U vtfs_user -d vtfs_db -t -c "SELECT COUNT(*) FROM vtfs_files WHERE token = '$TOKEN_TO_DELETE';" 2>/dev/null | tr -d ' ')
        if [ "$COUNT" = "0" ]; then
            echo -e "${YELLOW}Токен '$TOKEN_TO_DELETE' не найден${NC}"
            exit 1
        fi
        
        echo "Будет удалено записей: $COUNT"
        read -p "Продолжить? (yes/no): " CONFIRM
        if [ "$CONFIRM" = "yes" ]; then
            PGPASSWORD=vtfs_password psql -h localhost -U vtfs_user -d vtfs_db <<SQL
DELETE FROM vtfs_file_data WHERE token = '$TOKEN_TO_DELETE';
DELETE FROM vtfs_files WHERE token = '$TOKEN_TO_DELETE';
SQL
            echo -e "${GREEN}✅ Токен '$TOKEN_TO_DELETE' удален${NC}"
        else
            echo "Отменено"
        fi
        ;;
    6)
        echo "Отменено"
        exit 0
        ;;
    *)
        echo -e "${RED}Неверный выбор${NC}"
        exit 1
        ;;
esac

echo ""
echo -e "${BLUE}Оставшиеся токены:${NC}"
REMAINING=$(PGPASSWORD=vtfs_password psql -h localhost -U vtfs_user -d vtfs_db -t -c "SELECT DISTINCT token FROM vtfs_files ORDER BY token;" 2>/dev/null | grep -v '^$' | tr -d ' ')
if [ -z "$REMAINING" ]; then
    echo -e "${YELLOW}Токенов не осталось${NC}"
else
    echo "$REMAINING" | nl
fi

