TARGET = hagane
HAGANE = ./_hagane
CXX = clang++
CXXFLAGS = -O3 -Wall -Wconversion

HGN_SRC = for.hgn vector.hgn main.hgn

HEADERS = $(wildcard *.h)
OBJECTS = $(patsubst %.cpp,%.o,$(wildcard *.cpp)) /usr/lib/libstdc++.a

$(TARGET): $(HGN_SRC) $(OBJECTS)
	$(HAGANE) $(HGN_SRC) $(addprefix -L,$(OBJECTS)) $(addprefix -o,$@)

%.o: %.c $(HEADERS)
	$(CXX) -c $(CXXFLAGS) $< -o $@
