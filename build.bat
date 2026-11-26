@echo off
rem Сборка kofm через Docker (NickelTC). Результат: KoboRoot.tgz в этой папке.
chcp 65001 >nul
cd /d "%~dp0"

rem 1. Docker запущен?
docker info >nul 2>&1 || (echo [!] Docker Desktop не запущен. & pause & exit /b 1)

rem 2. Скачать NickelHook один раз (через контейнер с git, свой git не нужен)
if not exist NickelHook\NickelHook.mk (
  echo Загрузка NickelHook...
  docker run --rm -v "%cd%:/git" alpine/git clone --depth 1 https://github.com/pgaskin/NickelHook.git NickelHook || (pause & exit /b 1)
)

rem 3. Сборка в NickelTC
docker run --rm -v "%cd%:/work" -w /work --entrypoint make ghcr.io/pgaskin/nickeltc:1 clean all koboroot
if errorlevel 1 (echo [!] Ошибка сборки. & pause & exit /b 1)

echo.
rem Копия под понятным именем; на ридер класть как .kobo\KoboRoot.tgz
copy /y KoboRoot.tgz kofm.tgz >nul
echo Готово: %cd%\kofm.tgz  (на ридере файл должен называться KoboRoot.tgz)
pause
