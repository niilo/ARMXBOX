# SPDX-License-Identifier: GPL-2.0-or-later
# Standalone host test using the generated headers and utility library from
# a configured native QEMU build. No firmware, game files or GPU is needed.
REPO_ROOT := $(abspath ../../..)
BUILD_DIR ?= $(REPO_ROOT)/build
HOST_ARCH := $(shell uname -m)
TCG_ARCH := $(if $(filter x86_64 i386 i686,$(HOST_ARCH)),i386,$(HOST_ARCH))
TEST_DIR := $(BUILD_DIR)/tests/tcg/optimizer
CPPFLAGS += -D_GNU_SOURCE -DCOMPILING_SYSTEM_VS_USER -DXBOX=1 \
  -I$(BUILD_DIR) -I$(REPO_ROOT) -I$(REPO_ROOT)/include \
  -I$(REPO_ROOT)/host/include/$(HOST_ARCH) \
  -I$(REPO_ROOT)/host/include/generic -I$(REPO_ROOT)/tcg/$(TCG_ARCH) \
  $(shell pkg-config --cflags glib-2.0)
CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -fno-strict-aliasing -fwrapv -ffunction-sections \
  -fdata-sections -UNDEBUG
LDLIBS += $(BUILD_DIR)/libqemuutil.a $(shell pkg-config --libs glib-2.0) \
  -pthread -lm
LDFLAGS += -Wl,--gc-sections

.PHONY: check clean
check: $(TEST_DIR)/test-setcond-on $(TEST_DIR)/test-setcond-off
	$(TEST_DIR)/test-setcond-on
	$(TEST_DIR)/test-setcond-off

$(TEST_DIR):
	mkdir -p $@

$(TEST_DIR)/test-setcond-on: test-setcond-branch.c $(REPO_ROOT)/tcg/optimize.c | $(TEST_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DXEMU_OPT_TCG_PEEPHOLE=1 $< $(LDFLAGS) $(LDLIBS) -o $@

$(TEST_DIR)/test-setcond-off: test-setcond-branch.c $(REPO_ROOT)/tcg/optimize.c | $(TEST_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DXEMU_OPT_TCG_PEEPHOLE=0 $< $(LDFLAGS) $(LDLIBS) -o $@

clean:
	rm -f $(TEST_DIR)/test-setcond-on $(TEST_DIR)/test-setcond-off
