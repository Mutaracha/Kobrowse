# NickelHook is a pinned upstream submodule. Keep project packaging separate
# from its stock koboroot rule, which writes KoboRoot.tgz in the checkout.
include NickelHook/NickelHook.mk

override LIBRARY  := libkobrowse.so
override SOURCES  += src/kobrowse.cc
override PKGCONF  += Qt5Widgets
override CXXFLAGS += -Wall -Wextra -Wno-missing-field-initializers
override SKIPCONFIGURE += package
override KOBOROOT += res/nm_kobrowse:/mnt/onboard/.adds/nm/kobrowse

include NickelHook/NickelHook.mk

# Run after a fresh build. User configuration is not included.
package:
	tar cvzf kobrowse_KoboRoot.tgz --show-transformed --owner=0 --group=0 --numeric-owner --mode=0644 $(foreach file,$(KOBOROOT),--transform="s,$(word 1,$(subst :, ,$(file))),.$(word 2,$(subst :, ,$(file))),") $(foreach file,$(KOBOROOT),$(word 1,$(subst :, ,$(file))))

.PHONY: package
