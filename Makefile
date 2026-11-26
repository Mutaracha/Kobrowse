# NickelHook is a pinned upstream submodule. Keep project packaging separate
# from its stock koboroot rule, which writes KoboRoot.tgz in the checkout.
include NickelHook/NickelHook.mk

override LIBRARY  := libkofm.so
override SOURCES  += src/kofm.cc
override PKGCONF  += Qt5Widgets
override CXXFLAGS += -Wall -Wextra -Wno-missing-field-initializers
override SKIPCONFIGURE += package
override KOBOROOT += res/nm_kofm:/mnt/onboard/.adds/nm/kofm

include NickelHook/NickelHook.mk

# Run after a fresh build. The archive contains neither user config nor Kobalt.
package:
	tar cvzf kofm_KoboRoot.tgz --show-transformed --owner=0 --group=0 --numeric-owner --mode=0644 $(foreach file,$(KOBOROOT),--transform="s,$(word 1,$(subst :, ,$(file))),.$(word 2,$(subst :, ,$(file))),") $(foreach file,$(KOBOROOT),$(word 1,$(subst :, ,$(file))))

.PHONY: package
