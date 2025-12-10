#!/bin/bash
echo "=== ПРОВЕРКА БАЗЫ ДАННЫХ ==="
echo ""
echo "Подключение к PostgreSQL и проверка данных:"
echo ""

PGPASSWORD=postgres psql -h localhost -U postgres -d vtfs_db << SQL
\echo '1. Все файлы для токена test_persistence_1765325022:'
SELECT id, ino, name, parent_ino, mode, nlink FROM vtfs_files 
WHERE token = 'test_persistence_1765325022' 
ORDER BY parent_ino, ino;

\echo ''
\echo '2. Статистика по токену:'
SELECT parent_ino, COUNT(*) as files_count 
FROM vtfs_files 
WHERE token = 'test_persistence_1765325022'
GROUP BY parent_ino;

\echo ''
\echo '3. Данные файлов:'
SELECT ino, LENGTH(data) as data_length, "offset" 
FROM vtfs_file_data 
WHERE token = 'test_persistence_1765325022'
LIMIT 10;
SQL
