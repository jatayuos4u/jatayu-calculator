CXX ?= g++
PKG_CONFIG ?= pkg-config
QT_CFLAGS = $(shell $(PKG_CONFIG) --cflags Qt6Widgets)
QT_LIBS = $(shell $(PKG_CONFIG) --libs Qt6Widgets)
CXXFLAGS ?= -O2 -Wall -Wextra -Wpedantic
HARDEN_CPPFLAGS = -D_FORTIFY_SOURCE=3
HARDEN_CXXFLAGS = -fstack-protector-strong
HARDEN_LDFLAGS = -Wl,-z,relro,-z,now,-z,noexecstack
PREFIX ?= /usr/local
VERSION := 1.1.1

.PHONY: all test install deb clean
all: build/jatayu-calculator

build/jatayu-calculator: src/main.cpp src/calculator.cpp src/calculator.h src/converter.cpp src/converter.h
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(HARDEN_CPPFLAGS) $(QT_CFLAGS) $(CXXFLAGS) $(HARDEN_CXXFLAGS) -std=c++17 -fPIE src/main.cpp src/calculator.cpp src/converter.cpp $(LDFLAGS) $(HARDEN_LDFLAGS) -pie $(QT_LIBS) -o $@

build/calculator-tests: tests/calculator-tests.cpp src/calculator.cpp src/calculator.h src/converter.cpp src/converter.h
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 -Isrc tests/calculator-tests.cpp src/calculator.cpp src/converter.cpp $(LDFLAGS) -o $@

test: build/calculator-tests
	./build/calculator-tests

install: all
	install -Dm755 build/jatayu-calculator "$(DESTDIR)$(PREFIX)/bin/jatayu-calculator"
	install -Dm644 packaging/in.jatayuos.Calculator.desktop "$(DESTDIR)$(PREFIX)/share/applications/in.jatayuos.Calculator.desktop"
	install -Dm644 assets/jatayu-calculator.png "$(DESTDIR)$(PREFIX)/share/icons/hicolor/128x128/apps/jatayu-calculator.png"
	install -Dm644 README.md "$(DESTDIR)$(PREFIX)/share/doc/jatayu-calculator/README.md"
	install -Dm644 packaging/BOOST-LICENSE.txt "$(DESTDIR)$(PREFIX)/share/doc/jatayu-calculator/BOOST-LICENSE.txt"

deb: all
	./packaging/build-deb.sh "$(VERSION)"

clean:
	rm -rf build
