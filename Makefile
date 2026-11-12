# Heavy Gear port - build
#   make              tools (hgprj, test_data) and the viewer (hgview; needs SDL3 3.2+, found with pkg-config)
#   make check        unit tests; with GAME=<game folder> also parses every model and texture of your copy
#   make WINDOWS=1 CC=x86_64-w64-mingw32-gcc SDL3_DIR=<SDL3-devel-mingw>/x86_64-w64-mingw32   Windows cross-build
#   The cutscenes (Indeo 5 AVIs) need FFmpeg's libavformat / libavcodec: used when pkg-config finds them (MOVIES=0
#   leaves them out, MOVIES=1 insists).

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra -D_DEFAULT_SOURCE
LDLIBS  += -lm

ifeq ($(WINDOWS),1)
EXE      := .exe
SDL_CFLAGS := -I$(SDL3_DIR)/include
SDL_LIBS   := -L$(SDL3_DIR)/lib -lSDL3
else
EXE      :=
SDL_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl3 2>/dev/null)
endif

ifndef MOVIES
MOVIES := $(shell pkg-config --exists libavformat libavcodec libavutil 2>/dev/null && echo 1 || echo 0)
endif
ifeq ($(MOVIES),1)
AV_CFLAGS := -DHG_MOVIES $(shell pkg-config --cflags libavformat libavcodec libavutil 2>/dev/null)
AV_LIBS   := $(shell pkg-config --libs libavformat libavcodec libavutil 2>/dev/null)
endif

CORE := src/hgprj.c src/unpack.c src/fsutil.c src/wtb.c src/tex.c src/bwd.c src/material.c src/mission.c
VIEW := $(CORE) src/raster.c src/world.c src/skygnd.c src/shp.c src/hud.c src/gear.c src/sim.c src/gearmodel.c src/units.c src/combat.c src/structures.c src/objectives.c src/radio.c src/effects.c
AUDIO := src/audio.c src/sfx.c src/codecs.c
SHELL_SRC := src/pcx.c src/smk.c src/shell.c src/brf.c src/career.c src/zlite.c src/front.c src/squad.c src/garage.c src/modelview.c src/design.c src/vdf.c

all: hgprj$(EXE) test_data$(EXE) hgview$(EXE) heavygear$(EXE)

hgprj$(EXE): tools/hgprj.c $(CORE) src/*.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tools/hgprj.c $(CORE) $(LDLIBS)

test_data$(EXE): tests/test_data.c $(CORE) src/shp.c src/gear.c src/brf.c src/design.c src/vdf.c src/*.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_data.c $(CORE) src/shp.c src/gear.c src/brf.c src/design.c src/vdf.c $(LDLIBS)

test_unpack$(EXE): tests/test_unpack.c src/unpack.c src/career.c src/zlite.c src/*.h
	$(CC) $(CFLAGS) -o $@ tests/test_unpack.c src/unpack.c src/career.c src/zlite.c $(LDLIBS)

hgview$(EXE): tools/hgview.c $(VIEW) $(AUDIO) src/*.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(LDFLAGS) -o $@ tools/hgview.c $(VIEW) $(AUDIO) $(SDL_LIBS) $(LDLIBS)

heavygear$(EXE): tools/heavygear.c $(VIEW) $(SHELL_SRC) $(AUDIO) src/movie.c src/*.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(AV_CFLAGS) $(LDFLAGS) -o $@ tools/heavygear.c $(VIEW) $(SHELL_SRC) $(AUDIO) src/movie.c $(SDL_LIBS) $(AV_LIBS) $(LDLIBS)

test_sim$(EXE): tests/test_sim.c src/sim.c src/sim.h
	$(CC) $(CFLAGS) -o $@ tests/test_sim.c src/sim.c $(LDLIBS)

test_radio$(EXE): tests/test_radio.c src/radio.c src/radio.h
	$(CC) $(CFLAGS) -o $@ tests/test_radio.c src/radio.c $(LDLIBS)

test_squad$(EXE): tests/test_squad.c src/squad.c src/squad.h
	$(CC) $(CFLAGS) -o $@ tests/test_squad.c src/squad.c $(LDLIBS)

test_garage$(EXE): tests/test_garage.c src/garage.c src/garage.h src/shell_tables.h
	$(CC) $(CFLAGS) -o $@ tests/test_garage.c src/garage.c $(LDLIBS)

test_effects$(EXE): tests/test_effects.c src/effects.c src/effects.h src/gear_tables.h
	$(CC) $(CFLAGS) -o $@ tests/test_effects.c src/effects.c $(LDLIBS)

test_objectives$(EXE): tests/test_objectives.c src/objectives.c src/objectives.h src/mission.h
	$(CC) $(CFLAGS) -o $@ tests/test_objectives.c src/objectives.c $(LDLIBS)

test_combat$(EXE): tests/test_combat.c src/combat.c src/gear.c $(CORE) src/*.h
	$(CC) $(CFLAGS) -o $@ tests/test_combat.c src/combat.c src/gear.c $(CORE) $(LDLIBS)

check: test_unpack$(EXE) test_data$(EXE) test_sim$(EXE) test_combat$(EXE) test_objectives$(EXE) test_radio$(EXE) test_effects$(EXE) test_squad$(EXE) test_garage$(EXE)
	./test_unpack$(EXE)
	./test_sim$(EXE)
	./test_combat$(EXE)
	./test_objectives$(EXE)
	./test_radio$(EXE)
	./test_effects$(EXE)
	./test_squad$(EXE)
	./test_garage$(EXE)
	./test_data$(EXE) $(GAME)

clean:
	rm -f hgprj hgview heavygear test_data test_unpack test_sim test_combat test_objectives test_radio test_effects test_squad test_garage *.exe

.PHONY: all check clean
