# NickelHook is a pinned upstream submodule. Project packaging uses build/.
include NickelHook/NickelHook.mk

override LIBRARY  := libkobrowse.so
override SOURCES  += src/kobrowse.cc src/zip_writer.cc src/zip_validator.cc src/zip_extract.cc
override PKGCONF  += Qt5Widgets
override LDFLAGS  += -lz
override CXXFLAGS += -Wall -Wextra -Wno-missing-field-initializers
override SKIPCONFIGURE += package
override KOBOROOT += res/nm_kobrowse:/mnt/onboard/.adds/nm/kobrowse
override KOBOROOT += res/uninstall_marker:/mnt/onboard/.adds/kobrowse/uninstall
override KOBOROOT += res/KOBALT-LICENSE.txt:/mnt/onboard/.adds/kobrowse/KOBALT-LICENSE.txt

include NickelHook/NickelHook.mk

# Run after a fresh build. User configuration is not included.
package:
	mkdir -p build
	tar cvzf build/KoboRoot.tgz --show-transformed --owner=0 --group=0 --numeric-owner --mode=0644 $(foreach file,$(KOBOROOT),--transform="s,$(word 1,$(subst :, ,$(file))),.$(word 2,$(subst :, ,$(file))),") $(foreach file,$(KOBOROOT),$(word 1,$(subst :, ,$(file))))

.PHONY: package
