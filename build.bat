@echo off
rem Локальная сборка; Docker на компьютере не нужен для сборки в GitHub Actions.
chcp 65001 >nul
cd /d "%~dp0"

git submodule update --init --recursive || (echo [!] Не удалось получить NickelHook. & exit /b 1)
docker info >nul 2>&1 || (echo [!] Docker Desktop не запущен. & exit /b 1)
docker run --rm -v "%cd%:/work" -w /work --entrypoint bash ghcr.io/pgaskin/nickeltc:1 -euc "make -B all && make package"
if errorlevel 1 (echo [!] Ошибка сборки. & exit /b 1)
python scripts/check-package.py kofm_KoboRoot.tgz
if errorlevel 1 (echo [!] Ошибка проверки пакета. & exit /b 1)
echo Готово: %cd%\kofm_KoboRoot.tgz
