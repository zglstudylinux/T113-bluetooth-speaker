#
# T113 蓝牙音箱 — 交叉编译 Makefile（镜像 lvgl_demo_build/src/Makefile）
#
# 用法：
#   ./scripts/setup.sh        # 首次：从 SDK 复制 toolchain 到 ./toolchain/
#   make                      # 构建 build/bt_speaker
#   ./scripts/deploy.sh       # adb 部署到板子
#
CC      := $(shell [ -x "$(CURDIR)/toolchain/bin/arm-openwrt-linux-gcc" ] && echo "$(CURDIR)/toolchain/bin/arm-openwrt-linux-gcc" || echo "arm-openwrt-linux-gcc")

# OpenWrt wrapper 编译器要求 STAGING_DIR（app_sdk build.sh 同款做法）
export STAGING_DIR := $(dir $(firstword $(CC)))
LVGL_DIR_NAME ?= lvgl
LVGL_DIR ?= $(CURDIR)/third_party

CFLAGS  ?= -std=gnu99 -O2 -g \
	-march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard \
	-I$(CURDIR)/src \
	-I$(LVGL_DIR)/ \
	-I$(CURDIR)/third_party/lvgl/src/extra/libs/freetype \
	-I$(CURDIR)/third_party/freetype/include \
	-I$(CURDIR)/third_party/bt/include \
	-Wall -Wno-unused-function -Wno-unused-parameter -Wno-missing-prototypes \
	-Wno-sign-compare -Wno-format-nonliteral

# BT 库（btmanager 4.0.3）：链接用 vendor 副本，运行时用板上 /usr/lib + /lib
BT_LIB_DIR := $(CURDIR)/third_party/bt/lib
# 注意链接顺序：btmg 依赖 bluetooth-internal(静态) + glib 系；wirelesscom 依赖 json-c/dbus
LDFLAGS ?= -lm \
	-L$(CURDIR)/third_party/freetype/lib -lfreetype \
	-L$(BT_LIB_DIR) \
	-lbtmg -lshared-mainloop -lbluetooth-internal -lwirelesscom \
	-lgio-2.0 -lgobject-2.0 -lgmodule-2.0 -lglib-2.0 -lffi \
	-ldbus-1 -lasound -ljson-c -lsbc \
	-lreadline -lncursesw \
	-lz -lbz2 -lpthread -lrt -ldl \
	-Wl,-rpath,/usr/lib

BIN  = bt_speaker
BUILD = build

#Collect the files to compile
MAINSRC = ./src/main.c ./src/bt_speaker.c ./src/ui/ui_main.c

include $(LVGL_DIR)/lvgl/lvgl.mk
include $(LVGL_DIR)/lv_drivers/lv_drivers.mk

OBJEXT ?= .o

AOBJS = $(patsubst %,$(BUILD)/%,$(ASRCS:.S=$(OBJEXT)))
COBJS = $(patsubst %,$(BUILD)/%,$(CSRCS:.c=$(OBJEXT)))
MAINOBJ = $(patsubst %,$(BUILD)/%,$(MAINSRC:.c=$(OBJEXT)))

OBJS = $(AOBJS) $(COBJS)

all: $(BUILD)/$(BIN)

$(BUILD)/$(BIN): $(AOBJS) $(COBJS) $(MAINOBJ)
	$(CC) -o $@ $(MAINOBJ) $(AOBJS) $(COBJS) $(LDFLAGS)
	@echo "===> $@"

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@
	@echo "CC $<"

clean:
	rm -rf $(BUILD)

.PHONY: all clean
