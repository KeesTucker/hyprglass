# HyprGlass Plugin

CXX ?= g++
# -MMD -MP: emit header dependency files so editing a .hpp rebuilds every
# object that includes it. Without this, a struct layout change in a header
# left stale .o files allocating the old object size while rebuilt TUs wrote
# the new layout - a heap overflow that took down the live compositor.
CXXFLAGS = -fPIC -g -O2 -std=c++23 -MMD -MP
LDFLAGS = -shared
INCLUDES = $(shell pkg-config --cflags hyprland pixman-1 libdrm)
LIBS = $(shell pkg-config --libs hyprland)

ifeq ($(basename $(CXX)),g++)
	CXXFLAGS += --no-gnu-unique
endif

TARGET = hyprglass.so
SOURCES = src/main.cpp src/GlassDecoration.cpp src/GlassCompositeDecoration.cpp src/GlassPassElement.cpp src/GlassCompositePassElement.cpp src/WindowGlassState.cpp src/GlassRenderer.cpp src/GlassLayerSurface.cpp src/GlassLayerPassElement.cpp src/GlassLayerCompositeElement.cpp src/PluginConfig.cpp src/ShaderManager.cpp
OBJ = $(SOURCES:.cpp=.o)

all: $(TARGET)

%.o : %.cpp
	@echo "[$(CXX)] $<"
	@$(CXX) -c $(CXXFLAGS) $(INCLUDES) $< -o $@

# Link to a temp file and rename into place: rename gives the output a new
# inode, so a currently-loaded (mmapped) copy of the old .so keeps its own
# intact backing file. Linking directly onto the loaded file truncates the
# inode Hyprland has mapped - that corrupted the live compositor once
# (SIGABRT in Hyprlang clearState on the next config reload).
$(TARGET): $(OBJ)
	@echo "Linking $(TARGET)..."
	@$(CXX) $(LDFLAGS) $(OBJ) -o $@.tmp $(LIBS)
	@mv -f $@.tmp $@
	@echo "Done!"

clean:
	rm -f $(OBJ) $(OBJ:.o=.d) $(TARGET)

-include $(OBJ:.o=.d)

.PHONY: all clean
