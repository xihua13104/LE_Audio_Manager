# Copyright (c) 2026 Leon.

# SPDX-License-Identifier: MIT

########################################
# Targets
########################################

.PHONY: all
all: dbus-gen $(LIB_TARGET) $(APP_TARGET)

########################################
# Output directory
########################################

OUT_DIR := build

########################################
# Tools
########################################

CC	  ?= gcc
PKG_CONFIG  ?= pkg-config
DBUS_CODEGEN := gdbus-codegen

########################################
# D-Bus code generation
########################################

DBUS_XML   := src/bluez_iface.xml
DBUS_GEN_C := src/bluez_iface.c
DBUS_GEN_H := src/bluez_iface.h

$(DBUS_GEN_C) $(DBUS_GEN_H): $(DBUS_XML)
	@echo "Generating D-Bus code from $(DBUS_XML)..."
	$(DBUS_CODEGEN) \
		--interface-prefix org \
		--output $(DBUS_GEN_C) \
		--interface-info-body $(DBUS_XML)
	$(DBUS_CODEGEN) \
		--interface-prefix org \
		--output $(DBUS_GEN_H) \
		--interface-info-header $(DBUS_XML)
	@echo "==> D-Bus code generated"

.PHONY: dbus-gen
dbus-gen: $(DBUS_GEN_C) $(DBUS_GEN_H)

########################################
# Source files
########################################

APP_SOURCES := \
	example/main.c \
	example/speaker.c \
	example/broadcaster.c \
	example/tws.c \
	example/gatt_client.c \
	example/gatt_server.c

LIB_SOURCES := \
	$(DBUS_GEN_C) \
	src/lm_log.c \
	src/lm.c \
	src/lm_adapter.c \
	src/lm_device.c \
	src/lm_bearer.c \
	src/lm_adv.c \
	src/lm_agent.c \
	src/lm_player.c \
	src/lm_transport.c \
	src/lm_gatt_server.c \
	src/lm_gatt_client.c \
	src/lm_gatt_svc.c \
	src/lm_gatt_char.c \
	src/lm_gatt_desc.c \
	src/lm_parser.c \
	src/lm_profile.c \
	src/lm_utils.c

########################################
# Object files (flattened)
########################################

APP_OBJ := $(addprefix $(OUT_DIR)/,$(notdir $(APP_SOURCES:.c=.o)))
LIB_OBJ := $(addprefix $(OUT_DIR)/,$(notdir $(LIB_SOURCES:.c=.o)))

########################################
# pkg-config flags (Ubuntu)
########################################

GLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags glib-2.0 gio-2.0 gobject-2.0)
GLIB_LIBS   := $(shell $(PKG_CONFIG) --libs glib-2.0 gio-2.0 gobject-2.0)

CFLAGS  := -Wall -Wextra -fPIC $(GLIB_CFLAGS) \
	   -Isrc -Iinc -Iexample -I$(OUT_DIR)

LIBS    := $(GLIB_LIBS) -lpthread -lm

########################################
# Targets
########################################

APP_TARGET := $(OUT_DIR)/lea_manager
LIB_TARGET := $(OUT_DIR)/liblea_manager.so

.PHONY: all
all: dbus-gen $(LIB_TARGET) $(APP_TARGET)

########################################
# Link rules
########################################

$(LIB_TARGET): $(LIB_OBJ)
	$(CC) -shared -o $@ $^ $(LIBS)

$(APP_TARGET): $(APP_OBJ) $(LIB_TARGET)
	$(CC) -o $@ $(APP_OBJ) \
	    -L$(OUT_DIR) -l:$(notdir $(LIB_TARGET)) \
	    -Wl,-rpath,'$$ORIGIN' \
	    $(LIBS)

########################################
# Compile rules
########################################

$(OUT_DIR):
	mkdir -p $@

$(OUT_DIR)/%.o: %.c | $(OUT_DIR) dbus-gen
	$(CC) -c $(CFLAGS) $< -o $@

vpath %.c src example

########################################
# Clean
########################################

.PHONY: clean
clean:
	rm -rf $(OUT_DIR)/*.o $(DBUS_GEN_C) $(DBUS_GEN_H)