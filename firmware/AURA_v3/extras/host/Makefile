# AURA v3.1 capture shim: compiles the firmware renderer + screens for the PC.
FW ?= ../..
CXX ?= g++
CXXFLAGS := -std=gnu++17 -O2 -DAURA_HOST -Wall -Wextra -I$(FW)
FW_SRC := gfx.cpp assets.cpp ui_core.cpp boot_screen.cpp home_screen.cpp menu_screen.cpp buddy_face.cpp \
          apps_wx.cpp apps_data.cpp apps_tools.cpp keypad.cpp settings_screen.cpp pc_screen.cpp tz.cpp prof.cpp aurora_fx.cpp wifi_qr.cpp data_store.cpp src/qrcodegen/qrcodegen.c \
          $(EXTRA_FW)
SRC := $(addprefix $(FW)/,$(FW_SRC)) host_main.cpp host_hal.cpp host_captures.cpp host_prof.cpp host_bands.cpp $(EXTRA_HOST)
OUT ?= ../captures

all: aura_host
aura_host: $(SRC) $(wildcard $(FW)/*.h) host_*.cpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lm
captures: aura_host
	mkdir -p $(OUT)
	rm -f $(OUT)/*.ppm $(OUT)/*.png
	./aura_host $(OUT)
	python3 ppm2png.py $(OUT)
prof: aura_host
	./aura_host --prof 100 $(SCALE)
bandtest: aura_host
	./aura_host --bands nocache
	./aura_host --bands
clean:
	rm -f aura_host
.PHONY: all captures clean bandtest
