# 8D Music (C++) — build
#
# Needs the PipeWire, cairo and X11 development files:
#     sudo apt install build-essential pkg-config \
#                      libpipewire-0.3-dev libcairo2-dev libx11-dev \
#                      libpango1.0-dev libsystemd-dev
#
# If they are installed somewhere else, point EIGHTD_SDK at a prefix holding
# usr/include and usr/lib (the layout `dpkg -x` produces) and the build will
# use that instead of pkg-config.

CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O3 -ffast-math -fno-math-errno -Wall -Wextra \
            -Wno-unused-parameter -Wno-missing-field-initializers
LDFLAGS  ?=

BIN      := 8dmusic
BUILD    := build
SRC      := src/main.cpp src/ui/App.cpp src/ui/Window.cpp \
            src/audio/Engine.cpp src/audio/Graph.cpp src/audio/NowPlaying.cpp \
            src/dsp/Processor.cpp
OBJ      := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SRC))
DEPS     := $(OBJ:.o=.d)

ifdef EIGHTD_SDK
  ARCH    := $(shell $(CXX) -dumpmachine)
  INCS    := -I$(EIGHTD_SDK)/usr/include/pipewire-0.3 \
             -I$(EIGHTD_SDK)/usr/include/spa-0.2 \
             -I$(EIGHTD_SDK)/usr/include/cairo \
             -I$(EIGHTD_SDK)/usr/include/freetype2 \
             -I$(EIGHTD_SDK)/usr/include/pango-1.0 \
             -I$(EIGHTD_SDK)/usr/include/harfbuzz \
             -I$(EIGHTD_SDK)/usr/include/glib-2.0 \
             -I$(EIGHTD_SDK)/usr/lib/$(ARCH)/glib-2.0/include \
             -I$(EIGHTD_SDK)/usr/include
  LIBS    := -L$(EIGHTD_SDK)/usr/lib/$(ARCH) -lpipewire-0.3 -lcairo -lX11 \
             -lpangocairo-1.0 -lpango-1.0 -lgobject-2.0 -lglib-2.0 \
             -lsystemd -lpthread -lm
else
  PKGS    := libpipewire-0.3 cairo x11 pangocairo libsystemd
  INCS    := $(shell pkg-config --cflags $(PKGS))
  LIBS    := $(shell pkg-config --libs $(PKGS)) -lpthread -lm
endif

.PHONY: all clean run check deps
all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)

$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCS) -MMD -MP -c $< -o $@

run: $(BIN)
	./$(BIN)

check: $(BIN)
	./$(BIN) --check

deps:
	sudo apt install build-essential pkg-config libpipewire-0.3-dev \
	                 libcairo2-dev libx11-dev libpango1.0-dev libsystemd-dev

clean:
	rm -rf $(BUILD) $(BIN)

-include $(DEPS)
