# Copyright (C) 2017 Fredrik Öhrström
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

all: release

help:
	@echo "Usage: make (release|debug|clean|clean-all)"
	@echo "       if you have both linux64, winapi64 and arm32 configured builds,"
	@echo "       then add linux64, winapi64 or arm32 to build only for that particular host."
	@echo "E.g.:  make debug winapi64"
	@echo "       make release linux64"

BUILDDIRS:=$(dir $(realpath $(wildcard build/*/spec.gmk)))

ifeq (,$(BUILDDIRS))
    $(error Run configure first!)
endif

VERBOSE?=@

ifeq (winapi64,$(findstring winapi64,$(MAKECMDGOALS)))
BUILDDIRS:=$(filter %x86_64-w64-mingw32/,$(BUILDDIRS))
endif

ifeq (linux64,$(findstring linux64,$(MAKECMDGOALS)))
BUILDDIRS:=$(filter %x86_64-pc-linux-gnu/,$(BUILDDIRS))
endif

ifeq (arm32,$(findstring arm32,$(MAKECMDGOALS)))
BUILDDIRS:=$(filter %arm-unknown-linux-gnueabihf/,$(BUILDDIRS))
endif

release:
	@echo Building release for $(words $(BUILDDIRS)) host\(s\).
	@for x in $(BUILDDIRS); do echo; echo Bulding $$(basename $$x) ; $(MAKE) --no-print-directory -C $$x release ; done

debug:
	@echo Building debug for $(words $(BUILDDIRS)) host\(s\).
	@for x in $(BUILDDIRS); do echo; echo Bulding $$(basename $$x) ; $(MAKE) --no-print-directory -C $$x debug ; done

test: test_release

test_release:
	@echo Running tests on release
	@for x in $(BUILDDIRS); do echo; ./test.sh $$x/release/beak ; done

test_debug:
	@echo Running tests
	@for x in $(BUILDDIRS); do echo; ./test.sh $$x/debug/beak ; done

clean:
	@echo Removing release and debug builds
	@for x in $(BUILDDIRS); do echo; rm -rf $$x/release $$x/debug $$x/generated_autocomplete.h; done

clean-all:
	@echo Removing configuration and artifacts
	$(VERBOSE)rm -rf $(BUILDDIRS)

DESTDIR?=/usr/local
install:
	install -Dm 755 -s build/x86_64-pc-linux-gnu/release/beak $(DESTDIR)/bin/beak
	install -Dm 644 doc/beak.1 $(DESTDIR)/man/man1/beak.1

uninstall:
	rm -f $(DESTDIR)/bin/beak
	rm -f $(DESTDIR)/man/man1/beak.1

linux64:

arm32:

winapi64:

.PHONY: all release debug test test_release test_debug clean clean-all help linux64 winapi64 arm32
