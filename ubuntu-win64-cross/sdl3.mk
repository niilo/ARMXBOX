# This file is part of MXE. See LICENSE.md for licensing information.

PKG             := sdl3
$(PKG)_WEBSITE  := https://www.libsdl.org/
$(PKG)_DESCR    := SDL3
$(PKG)_VERSION  := 3.4.10
$(PKG)_SUBDIR   := SDL3-$($(PKG)_VERSION)
$(PKG)_FILE     := SDL3-$($(PKG)_VERSION).tar.gz
$(PKG)_CHECKSUM := 12b34280415ec8418c864408b93d008a20a6530687ee613d60bfbd20411f2785
$(PKG)_URL      := https://github.com/libsdl-org/SDL/releases/download/release-$($(PKG)_VERSION)/$($(PKG)_FILE)
$(PKG)_GH_CONF  := libsdl-org/SDL/releases/tag,release-,,
$(PKG)_DEPS     := cc

define $(PKG)_BUILD
    cd '$(BUILD_DIR)' && $(TARGET)-cmake '$(SOURCE_DIR)' \
        -DSDL_SHARED=$(CMAKE_SHARED_BOOL) \
        -DSDL_STATIC=$(CMAKE_STATIC_BOOL) \
        -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
    $(MAKE) -C '$(BUILD_DIR)' -j '$(JOBS)'
    $(MAKE) -C '$(BUILD_DIR)' -j 1 install
endef
