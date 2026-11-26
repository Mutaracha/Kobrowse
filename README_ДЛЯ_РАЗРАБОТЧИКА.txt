ИСХОДНИКИ kobr — файловый менеджер для Kobo (с поиском)
=========================================================

Назначение
----------
kobr — плагин Nickel (NickelHook), который даёт файловый менеджер,
поиск книг, экранную клавиатуру и запуск книги через Kobalt.
В архиве — исходники той сборки, которая проверена на ридере.

СОСТАВ
------
src/kofm.cc        — весь код плагина (основной файл, ~168 КБ)
Makefile           — сборка внутри Docker/NickelTC
build.bat          — то же для Windows
NickelHook/*       — исходники и правила плагина NickelHook
res/nm_kofm        — пункты NickelMenu (Файлы / Обновить библиотеку)
examples/kofm.ini  — пример конфига с пояснениями
build_final.py     — собрать итоговый KoboRoot.tgz из готового архива
libkofm.so         — готовая библиотека (для сравнения/отладки)

КАК СОБРАТЬ
-----------
1) Нужны Docker Desktop и образ ghcr.io/pgaskin/nickeltc:1.
2) В этой папке:
   docker run --rm -v "<путь к этой папке>:/work" -w /work \
     --entrypoint make ghcr.io/pgaskin/nickeltc:1 clean all koboroot
   Получите libkofm.so и kofm_KoboRoot.tgz.
3) build_final.py собирает установочный KoboRoot.tgz.

ГДЕ ЧТО В kofm.cc
-----------------
строка  66 — путь к конфигу: /mnt/onboard/.adds/kofm/kofm.ini
строка  70 — путь к логу:   /mnt/onboard/.adds/kofm/kofm.log
строка 112 — ключ show_hidden в списке настроек (по умолчанию 1)
строка 205 — чтение show_hidden из kofm.ini
строка 209 — функция klog(): всё пишется в kofm.log
строка 2201 — пункт «Скрытые файлы» в экране «Настройки»
строка 2244 — инициализация m_showHidden из конфига
строка 2790 — фильтр каталога: `if (m_showHidden) flt |= QDir::Hidden;`
строка 3092 — поле m_showHidden
строки ~1565, ~3240 — подбор гарнитуры и замер заголовка поиска

ПРОБЛЕМА: СКРЫТЫЕ ПАПКИ (.adds И ДР.)
------------------------------------
Скрытые папки (чьё имя начинается с точки) показывает ключ
`show_hidden` в `/mnt/onboard/.adds/kofm/kofm.ini`:

    [kofm]
    show_hidden=1

То же можно переключить в интерфейсе kofm:
    Настройки → Скрытые файлы → показывать.

Если нужно, чтобы такие папки отображались ВСЕГДА, независимо от
настройки, уберите условие в строке 2790:

    QDir::Filters flt = QDir::AllEntries | QDir::NoDotAndDotDot
                      | QDir::Hidden | QDir::System;

Важно: kofm.ini — пользовательский файл, установка пакета его НЕ
перезаписывает. После сборки проверяйте именно его, а не дефолт.

ПРИМЕЧАНИЕ: OPDS из этой версии убран. Код OPDS остался в kofm.cc
(строки ~585–1126), но пункт меню и скрипты из пакета удалены.

КАК ВЫТАСКИВАТЬ ЛОГИ И ДИАГНОСТИКУ
----------------------------------
1) Обычный лог всегда виден через USB:
       .adds/kofm/kofm.log
   В нём есть «button font: ...» и «search title: ...».

2) Расширенная диагностика шрифтов:
   - создайте файл-сигнал /tmp/kofm.diag (например, через NickelMenu);
   - через 25 секунд kofm.cc вызовет kofm_font_diag() и создаст
     /tmp/kofm_diag/fonts_diag.txt и PNG-снимки;
   - скопируйте /tmp/kofm_diag на диск (в .adds/kofm/diag).

   Готовые пункты для .adds/nm/kofm:

   menu_item:main:Диагностика шрифтов:cmd_spawn:quiet:touch /tmp/kofm.diag
   menu_item:main:Диагностика сохранить:cmd_spawn:quiet:sh -c "cp -r /tmp/kofm_diag /mnt/onboard/.adds/kofm/diag"

   Сначала нажмите первый пункт, подождите ~30 секунд, затем второй.

3) Файлы .adds/kofm/kofm.ini и kofm.log создаются при первом запуске
   kofm, поэтому после заводского сброса их может не быть.

ЧТО НЕ СТОИТ МЕНЯТЬ ВСЛЕПУЮ
---------------------------
- Пути /mnt/onboard/.adds/... и /usr/local/Kobo — они от Kobo.
- Пределы btn_pt (3..24) и алгоритм fitButtonFont уже исправлены.
- Подбор гарнитуры заголовка (tightTitleFamily) подобран по замерам:
  «Sans Serif» давал 233 px, Avenir Next — 133 px.

Все правки вносите в копию и обязательно делайте бэкап kofm.ini.
