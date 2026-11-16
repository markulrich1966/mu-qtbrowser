PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= /usr/share/applications
PIXMAPDIR ?= /usr/share/pixmaps
ICONDIR ?= /usr/share/icons/hicolor/scalable/apps

# VERSION aus dem Makefile -> About-Dialog: wird als -DMU_VERSION
# an CMake weitergereicht (CMakeLists setzt daraus MU_VERSION).
VERSION ?= 0.1.2
ARCH ?= amd64
PET_ARCH ?= x86_64
PKG_NAME := mu-qtbrowser
DEB_DIR := build/pkg_deb

all:
	@echo "==> Building mu-browser, mu-browser-tabs & mu-browser-desktop (C++ / Qt6 WebEngine)..."
	mkdir -p build && cd build && cmake -DMU_VERSION=$(VERSION) .. && make
	@echo "==> Building mu-controller (Vala / GTK3)..."
	valac --pkg gtk+-3.0 --pkg gio-2.0 --pkg gio-unix-2.0 --pkg posix main.vala -o mu-controller
	@echo "==> Build completed successfully!"

install: all
	@echo "==> Installing binaries, wrapper script, icon, and desktop entries..."
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 build/mu-browser $(DESTDIR)$(BINDIR)/mu-browser
	install -m 755 build/mu-browser-tabs $(DESTDIR)$(BINDIR)/mu-browser-tabs
	install -m 755 build/mu-browser-desktop $(DESTDIR)$(BINDIR)/mu-browser-desktop
	install -m 755 mu-controller $(DESTDIR)$(BINDIR)/mu-controller
	install -m 755 launch-mu-browser.sh $(DESTDIR)$(BINDIR)/launch-mu-browser.sh

	install -d $(DESTDIR)$(DATADIR)
	install -m 644 desktop-files/mu-browser.desktop $(DESTDIR)$(DATADIR)/mu-browser.desktop
	install -m 644 desktop-files/mu-browser-standalone.desktop $(DESTDIR)$(DATADIR)/mu-browser-standalone.desktop
	install -m 644 desktop-files/mu-browser-tabs.desktop $(DESTDIR)$(DATADIR)/mu-browser-tabs.desktop
	install -m 644 desktop-files/mu-browser-desktop.desktop $(DESTDIR)$(DATADIR)/mu-browser-desktop.desktop

	install -d $(DESTDIR)$(PIXMAPDIR)
	install -m 644 mu-browser.svg $(DESTDIR)$(PIXMAPDIR)/mu-browser.svg
	install -d $(DESTDIR)$(ICONDIR)
	install -m 644 mu-browser.svg $(DESTDIR)$(ICONDIR)/mu-browser.svg

	@if command -v update-desktop-database >/dev/null 2>&1; then \
		update-desktop-database; \
	fi
	@echo "==> Installation complete."

source:
	@echo "==> Creating clean source tarball..."
	tar -czvf mu-qtbrowser-v$(VERSION).tar.gz \
		--exclude=build \
		--exclude=*.tar.gz \
		--exclude=*.deb \
		--exclude=*.pet \
		CMakeLists.txt main.cpp main_tabs.cpp main_desktop.cpp main.vala Makefile README.txt launch-mu-browser.sh \
		mu-browser.svg desktop-files
	@echo "==> Source package created: mu-qtbrowser-v$(VERSION).tar.gz"

deb: all
	@echo "==> Building Debian package ($(PKG_NAME)_$(VERSION)_$(ARCH).deb)..."
	rm -rf $(DEB_DIR)
	mkdir -p $(DEB_DIR)/DEBIAN
	mkdir -p $(DEB_DIR)/usr/bin
	mkdir -p $(DEB_DIR)/usr/share/applications
	mkdir -p $(DEB_DIR)/usr/share/pixmaps
	mkdir -p $(DEB_DIR)/usr/share/icons/hicolor/scalable/apps
	mkdir -p $(DEB_DIR)/usr/share/doc/$(PKG_NAME)

	install -m 755 build/mu-browser $(DEB_DIR)/usr/bin/mu-browser
	install -m 755 build/mu-browser-tabs $(DEB_DIR)/usr/bin/mu-browser-tabs
	install -m 755 build/mu-browser-desktop $(DEB_DIR)/usr/bin/mu-browser-desktop
	install -m 755 mu-controller $(DEB_DIR)/usr/bin/mu-controller
	install -m 755 launch-mu-browser.sh $(DEB_DIR)/usr/bin/launch-mu-browser.sh
	install -m 644 desktop-files/mu-browser.desktop $(DEB_DIR)/usr/share/applications/mu-browser.desktop
	install -m 644 desktop-files/mu-browser-standalone.desktop $(DEB_DIR)/usr/share/applications/mu-browser-standalone.desktop
	install -m 644 desktop-files/mu-browser-tabs.desktop $(DEB_DIR)/usr/share/applications/mu-browser-tabs.desktop
	install -m 644 desktop-files/mu-browser-desktop.desktop $(DEB_DIR)/usr/share/applications/mu-browser-desktop.desktop
	install -m 644 mu-browser.svg $(DEB_DIR)/usr/share/pixmaps/mu-browser.svg
	install -m 644 mu-browser.svg $(DEB_DIR)/usr/share/icons/hicolor/scalable/apps/mu-browser.svg
	install -m 644 README.txt $(DEB_DIR)/usr/share/doc/$(PKG_NAME)/README.txt

	@echo "Package: $(PKG_NAME)" > $(DEB_DIR)/DEBIAN/control
	@echo "Version: $(VERSION)" >> $(DEB_DIR)/DEBIAN/control
	@echo "Section: web" >> $(DEB_DIR)/DEBIAN/control
	@echo "Priority: optional" >> $(DEB_DIR)/DEBIAN/control
	@echo "Architecture: $(ARCH)" >> $(DEB_DIR)/DEBIAN/control
	@echo "Depends: libqt6webenginewidgets6, libgtk-3-0, libglib2.0-0, libx11-6" >> $(DEB_DIR)/DEBIAN/control
	@echo "Maintainer: Mark Ulrich <mark@schinkenpizza.de>" >> $(DEB_DIR)/DEBIAN/control
	@echo "Description: Modular IPC-coupled Qt6 WebEngine container with GTK3 controller and low-RAM tabbed variant." >> $(DEB_DIR)/DEBIAN/control

	dpkg-deb --build $(DEB_DIR) $(PKG_NAME)_$(VERSION)_$(ARCH).deb
	@echo "==> DEB Package created: $(PKG_NAME)_$(VERSION)_$(ARCH).deb"

pet: all
	@echo "==> Building Puppy Linux PET package ($(PKG_NAME)-$(VERSION)-$(PET_ARCH).pet)..."
	rm -rf build/pkg_pet
	mkdir -p build/pkg_pet/usr/bin
	mkdir -p build/pkg_pet/usr/share/applications
	mkdir -p build/pkg_pet/usr/share/pixmaps
	mkdir -p build/pkg_pet/usr/share/icons/hicolor/scalable/apps
	mkdir -p build/pkg_pet/usr/share/doc/$(PKG_NAME)

	install -m 755 build/mu-browser build/pkg_pet/usr/bin/mu-browser
	install -m 755 build/mu-browser-tabs build/pkg_pet/usr/bin/mu-browser-tabs
	install -m 755 build/mu-browser-desktop build/pkg_pet/usr/bin/mu-browser-desktop
	install -m 755 mu-controller build/pkg_pet/usr/bin/mu-controller
	install -m 755 launch-mu-browser.sh build/pkg_pet/usr/bin/launch-mu-browser.sh
	install -m 644 desktop-files/mu-browser.desktop build/pkg_pet/usr/share/applications/mu-browser.desktop
	install -m 644 desktop-files/mu-browser-standalone.desktop build/pkg_pet/usr/share/applications/mu-browser-standalone.desktop
	install -m 644 desktop-files/mu-browser-tabs.desktop build/pkg_pet/usr/share/applications/mu-browser-tabs.desktop
	install -m 644 desktop-files/mu-browser-desktop.desktop build/pkg_pet/usr/share/applications/mu-browser-desktop.desktop
	install -m 644 mu-browser.svg build/pkg_pet/usr/share/pixmaps/mu-browser.svg
	install -m 644 mu-browser.svg build/pkg_pet/usr/share/icons/hicolor/scalable/apps/mu-browser.svg
	install -m 644 README.txt build/pkg_pet/usr/share/doc/$(PKG_NAME)/README.txt

	@echo '#!/bin/sh' > build/pkg_pet/pinstall.sh
	@echo 'fixmenus' >> build/pkg_pet/pinstall.sh
	chmod +x build/pkg_pet/pinstall.sh

	cd build && tar -czvf ../$(PKG_NAME)-$(VERSION)-$(PET_ARCH).pet -C pkg_pet .
	@echo "==> PET Package created: $(PKG_NAME)-$(VERSION)-$(PET_ARCH).pet"

pet-desktopfiles:
	@echo "==> Building Puppy Linux PET package for desktop files only ($(PKG_NAME)-desktopfiles-$(VERSION).pet)..."
	rm -rf build/pkg_pet_desktopfiles
	mkdir -p build/pkg_pet_desktopfiles/usr/share/applications
	mkdir -p build/pkg_pet_desktopfiles/usr/share/pixmaps
	mkdir -p build/pkg_pet_desktopfiles/usr/share/icons/hicolor/scalable/apps
	install -m 644 desktop-files/mu-browser.desktop build/pkg_pet_desktopfiles/usr/share/applications/mu-browser.desktop
	install -m 644 desktop-files/mu-browser-standalone.desktop build/pkg_pet_desktopfiles/usr/share/applications/mu-browser-standalone.desktop
	install -m 644 desktop-files/mu-browser-tabs.desktop build/pkg_pet_desktopfiles/usr/share/applications/mu-browser-tabs.desktop
	install -m 644 desktop-files/mu-browser-desktop.desktop build/pkg_pet_desktopfiles/usr/share/applications/mu-browser-desktop.desktop
	install -m 644 mu-browser.svg build/pkg_pet_desktopfiles/usr/share/pixmaps/mu-browser.svg
	install -m 644 mu-browser.svg build/pkg_pet_desktopfiles/usr/share/icons/hicolor/scalable/apps/mu-browser.svg

	@echo '#!/bin/sh' > build/pkg_pet_desktopfiles/pinstall.sh
	@echo 'fixmenus' >> build/pkg_pet_desktopfiles/pinstall.sh
	chmod +x build/pkg_pet_desktopfiles/pinstall.sh

	cd build && tar -czvf ../$(PKG_NAME)-desktopfiles-$(VERSION).pet -C pkg_pet_desktopfiles .
	@echo "==> PET Desktopfiles Package created: $(PKG_NAME)-desktopfiles-$(VERSION).pet"

clean:
	@echo "==> Cleaning build artifacts..."
	rm -rf build mu-controller *.tar.gz *.deb *.pet
	@echo "==> Clean complete."

.PHONY: all install source deb pet pet-desktopfiles clean
