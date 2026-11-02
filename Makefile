# AMEN-FM1: desktop build, tests and demos. The FM-1 plugin blobs: `make fm1` (needs the JieLi toolchain).
CC ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -ffp-contract=off
B := build

.PHONY: all test sim demo breaks plugins fm1 os clean

all: test

$(B)/fe_tables.h: tools/gen_tables.py
	python3 tools/gen_tables.py $@

$(B)/fe_shared.h: tools/gen_tables.py
	python3 tools/gen_tables.py --shared $@

engines/jungle_data.h: tools/gen_breaks.py
	python3 tools/gen_breaks.py

$(B)/amen_host: host/amen_host.c engines/eng_jungle.c engines/eng_reese.c engines/jungle_data.h $(B)/fe_tables.h sdk/src/dsp.c
	$(CC) $(CFLAGS) -I$(B) -Isdk/src -Iengines -o $@ host/amen_host.c

test: $(B)/amen_host
	$(B)/amen_host test

demo: $(B)/amen_host
	$(B)/amen_host demo $(B)/demo.wav

breaks: $(B)/amen_host
	for n in 0 1 2 3 4 5 6 7; do $(B)/amen_host break $$n $(B)/break$$n.wav; done

# AMEN OS on the desktop: the scripted session (screens as PPM, the audio as WAV, checks)
$(B)/amen_sim: os/host/sim.c os/amen.h os/amen.c os/core.c os/ui.c os/font.h engines/eng_jungle.c engines/eng_reese.c engines/jungle_data.h $(B)/fe_tables.h
	$(CC) $(CFLAGS) -I$(B) -Isdk/src -Iengines -Ios -o $@ os/host/sim.c

os/font.h: tools/gen_font.py
	python3 tools/gen_font.py

sim: $(B)/amen_sim
	@mkdir -p $(B)/sim
	$(B)/amen_sim $(B)/sim

# AMEN OS for the FM-1 (needs JIELI_TOOLCHAIN and AC79_SDK)
os:
	sh tools/build_os.sh

# desktop plugin libraries (Felucca's SDL2 / libretro builds load them with --plugins DIR)
plugins: $(B)/plugins/jungle.so $(B)/plugins/reese.so

$(B)/plugins/%.so: plugins/%.c engines/eng_jungle.c engines/eng_reese.c engines/jungle_data.h $(B)/fe_tables.h
	@mkdir -p $(B)/plugins
	$(CC) $(CFLAGS) -fPIC -fvisibility=hidden -shared -I$(B) -Isdk/src -Iengines -o $@ $<

# the FM-1 blobs: JUNGLE in slot 0 (8 KiB), REESE in slot 1 (7.5 KiB)
fm1:
	sh tools/build_fm1.sh

clean:
	rm -rf $(B)
