# Сборка libkofm.so и kofm_KoboRoot.tgz (внутри контейнера NickelTC).
# Имя архива отличается от финального KoboRoot.tgz, чтобы сборка kofm
# случайно не перезаписала готовый установочный файл из этой папки.
override KOFM_PKG := kofm_KoboRoot.tgz

# NickelHook.mk подключается дважды: сверху — настройки по умолчанию, снизу — правила сборки
include NickelHook/NickelHook.mk

override LIBRARY  := libkofm.so
override SOURCES  += src/kofm.cc
override PKGCONF  += Qt5Widgets
override CXXFLAGS += -Wall -Wextra -Wno-missing-field-initializers
# Пункты «Файлы», «OPDS», «OPDS адрес» в меню + скрипты OPDS
override KOBOROOT += res/nm_kofm:/mnt/onboard/.adds/nm/kofm
override KOBOROOT += res/opds_get.sh:/mnt/onboard/.adds/opds/opds_get.sh
override KOBOROOT += res/enter-url.sh:/mnt/onboard/.adds/opds/enter-url.sh
override KOBOROOT += res/history.default.txt:/mnt/onboard/.adds/opds/history.default.txt

include NickelHook/NickelHook.mk
