# Layout expected:
#   aero/main.cpp
#   aero/Makefile
#   aero/imgui/            <- git clone -b docking https://github.com/ocornut/imgui.git
#
# Linux deps (Debian/Ubuntu):
#   sudo apt install g++ pkg-config libglfw3-dev libglew-dev libassimp-dev libglm-dev
#
# Windows (MSYS2 MinGW64):
#   pacman -S mingw-w64-x86_64-{gcc,glfw,glew,assimp,glm,pkgconf}
#   then change -lGL below to -lopengl32

CXX      := g++
IMGUI    := imgui
SRC      := main.cpp \
            $(IMGUI)/imgui.cpp $(IMGUI)/imgui_draw.cpp $(IMGUI)/imgui_tables.cpp $(IMGUI)/imgui_widgets.cpp \
            $(IMGUI)/backends/imgui_impl_glfw.cpp $(IMGUI)/backends/imgui_impl_opengl3.cpp

CXXFLAGS := -std=c++17 -O3 -march=native -fopenmp -I$(IMGUI) -I$(IMGUI)/backends \
            $(shell pkg-config --cflags glfw3 glew assimp)
LDFLAGS  := $(shell pkg-config --libs glfw3 glew assimp) -lGL -fopenmp

aero: $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LDFLAGS)

clean:
	rm -f aero

.PHONY: clean
