TARGET = hagane
HAGANE = ./_hagane
CXX = clang++
CXXFLAGS = -O3 -Wall -Wconversion
HGN_FLAGS = -O3

HGN_SRC = for.hgn vector.hgn lexer.hgn main.hgn

HEADERS = $(wildcard *.h)
OBJECTS = $(patsubst %.cpp,%.o,$(wildcard *.cpp)) /usr/lib/libstdc++.a

$(TARGET): $(HGN_SRC) $(OBJECTS)
	$(HAGANE) $(HGN_FLAGS) $(HGN_SRC) $(addprefix -L,$(OBJECTS)) $(addprefix -o,$@)

%.o: %.cpp $(HEADERS)
	$(CXX) -c $(CXXFLAGS) $< -o $@
