#!/bin/bash
echo "=== ЛОГИ ЯДРА VTFS ==="
echo ""
echo "Последние 100 строк логов:"
echo "=========================="
dmesg | grep -E "\[vtfs\]" | tail -100
