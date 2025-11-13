-include config.mk

ALLFLAGS += -Wall -Wextra -Wpedantic
CFLAGS += -MD -std=c17 -g $(ALLFLAGS)
CXXFLAGS += -MD -std=c++17 -g $(ALLFLAGS)

DEPS += freetype2 harfbuzz libpng sdl3
CPPFLAGS += $(shell pkgconf --cflags $(PKGFLAGS) $(DEPS))
LDLIBS += $(shell pkgconf --libs $(PKGFLAGS) $(DEPS))

################################################################################
### Main targets ###############################################################
################################################################################

all: bin/game.exe

clean:
	rm -rf bin/ doc/

doc:
	doxygen
docs: doc

.PHONY: all clean doc docs

################################################################################
### Compilation rules ##########################################################
################################################################################

bin/obj:
	mkdir -p bin/obj
bin/obj/%.c.o: src/%.c | bin/obj
	$(COMPILE.c) $(OUTPUT_OPTION) $<
bin/obj/%.cpp.o: src/%.cpp | bin/obj
	$(COMPILE.cpp) $(OUTPUT_OPTION) $<

SOURCES = $(wildcard src/*.c src/*.cpp)
OBJECTS = $(SOURCES:src/%=bin/obj/%.o)
-include $(OBJECTS:.o=.d)

bin/game.exe: $(OBJECTS)
	$(LINK.cpp) $^ $(LOADLIBES) $(LDLIBS) -o $@
