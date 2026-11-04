# CLATTER 12 - build and test (host + Cortex-M33 compile check). See BUILDING.md.
#
#   make            generate the panel, build and run every host test
#   make panel      build/clatter_12.cpp (paste or flash via the Plinky 12 IDE) + variants
#   make test       resampler, engine, C/C++ equivalence, simulated panel session
#   make m33        compile the generated panel for the RP2350's Cortex-M33 (arm-none-eabi-g++),
#                   report section sizes. Needs ARM_GXX (see BUILDING.md)
#   make bench      host timing (not the RP2350)
#   make pocket-test   CLATTER Pocket's own host tests, to show the submodule still passes
#   make clean
#
# Nothing here talks to a Plinky 12. Nothing here flashes anything.

POCKET := third_party/clatter-pocket
BUILD := build
CC ?= cc
CXX ?= c++
PY ?= python3
ARM_GXX ?= $(shell command -v arm-none-eabi-g++ 2>/dev/null || ls $$(brew --prefix arm-none-eabi-gcc@8 2>/dev/null)/bin/arm-none-eabi-g++ 2>/dev/null)
ARM_SIZE ?= $(shell command -v arm-none-eabi-size 2>/dev/null)

CORE := core/c12_engine.c core/c12_engine.h core/c12_resampler.c core/c12_resampler.h core/c12_exports.c
GEN_DEPS := tools/amalgamate.py tools/plinky_macros.txt $(CORE) stubs/sysinc/*.h $(POCKET)/lxr/CLATTER_REVISION

# The native C reference: the generated engine text itself, compiled as C with the platform's C
# library (packed tables, single precision, FX bus off, exactly the panel's configuration).
UNITY_CFLAGS := -std=gnu11 -O2 -ffp-contract=off -w \
	-include stdint.h -include stddef.h -include string.h -include stdlib.h -include stdio.h \
	-include math.h -include assert.h -DC12_LXR_CODE= -DC12_LXR_DATA=
TEST_CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function -Icore -Itests -I$(POCKET)/engine -I$(POCKET)/seq
CXXFLAGS_HOST := -std=gnu++17 -O2 -ffp-contract=off -Wall -Wno-unused-function -Wno-unused-variable

M33_FLAGS := -std=gnu++17 -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard -O2 \
	-ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wno-psabi

.PHONY: all panel test m33 bench pocket-test clean
all: panel test

$(BUILD)/c12_engine.inc $(BUILD)/c12_engine_c.c: $(GEN_DEPS)
	@mkdir -p $(BUILD)/test $(BUILD)/sim
	$(PY) tools/amalgamate.py --tables packed -o $(BUILD)/c12_engine.inc --c-out $(BUILD)/c12_engine_c.c

$(BUILD)/clatter_12.cpp: $(BUILD)/c12_engine.inc panel/clatter12.cpp tools/make_panel.py VERSION
	$(PY) tools/make_panel.py --compact -o $@
$(BUILD)/clatter_12_full.cpp: $(BUILD)/c12_engine.inc panel/clatter12.cpp tools/make_panel.py VERSION
	$(PY) tools/make_panel.py -o $@
$(BUILD)/clatter_12_sram.cpp: $(BUILD)/c12_engine.inc panel/clatter12.cpp tools/make_panel.py VERSION
	$(PY) tools/make_panel.py --compact --in-ram 1 -o $@
$(BUILD)/clatter_12_psram.cpp: $(BUILD)/c12_engine.inc panel/clatter12.cpp tools/make_panel.py VERSION
	$(PY) tools/make_panel.py --compact --extra "C12_TABLES_IN_PSRAM 1" -o $@
# Variants (see BUILDING.md): clatter_12 (tables in SRAM, LXR code in flash), _sram (LXR code in
# SRAM too), _psram (tables in PSRAM), _full (not compacted; for reading and the simulator).
panel: $(BUILD)/clatter_12.cpp $(BUILD)/clatter_12_full.cpp $(BUILD)/clatter_12_sram.cpp $(BUILD)/clatter_12_psram.cpp

$(BUILD)/c12_unity.o: $(BUILD)/c12_engine_c.c
	$(CC) $(UNITY_CFLAGS) -c $< -o $@

$(BUILD)/test_resampler: tests/test_resampler.c core/c12_resampler.c core/c12_resampler.h
	$(CC) $(TEST_CFLAGS) -DCLATTER_STRING_H -DCLATTER_STDLIB_H -DCLATTER_MATH_H -I$(POCKET)/lxr/shim -I$(POCKET)/lxr/upstream/Core/Inc $(if $(filter Darwin,$(shell uname -s)),-Dsection=annotate) \
		tests/test_resampler.c core/c12_resampler.c -lm -o $@
$(BUILD)/test_engine: tests/test_engine.c tests/check.h $(BUILD)/c12_unity.o
	$(CC) $(TEST_CFLAGS) tests/test_engine.c $(BUILD)/c12_unity.o -lm -o $@
$(BUILD)/equiv_c: tests/equiv_c.c tests/scenario.inc $(BUILD)/c12_unity.o
	$(CC) $(TEST_CFLAGS) -ffp-contract=off tests/equiv_c.c $(BUILD)/c12_unity.o -lm -o $@
$(BUILD)/equiv_cxx: tests/equiv_cxx.cpp tests/scenario.inc $(BUILD)/c12_engine.inc
	$(CXX) $(CXXFLAGS_HOST) -w -I$(BUILD) -Itests tests/equiv_cxx.cpp -lm -o $@
$(BUILD)/test_panel_sim: tests/test_panel_sim.cpp sim/plinky_sim.cpp sim/plinky_sim.h $(BUILD)/clatter_12_full.cpp
	$(CXX) $(CXXFLAGS_HOST) -Wno-unused-parameter -Isim -I$(BUILD) -Itests tests/test_panel_sim.cpp sim/plinky_sim.cpp -lm -o $@
# CLATTER Pocket's own host library (its Makefile, its flags), built into our build directory, as
# the independent reference for the synthesis. Relative path: make cannot take one with spaces.
$(BUILD)/pocket/liblxr_nofx.a:
	$(MAKE) -C $(POCKET) BUILD=../../$(BUILD)/pocket ../../$(BUILD)/pocket/liblxr_nofx.a
$(BUILD)/pocket/seq.o: $(POCKET)/seq/pocket_kit.c $(POCKET)/seq/pocket_gen.c
	@mkdir -p $(BUILD)/pocket
	$(CC) -std=gnu11 -O2 -w -I$(POCKET)/engine -I$(POCKET)/seq -c $(POCKET)/seq/pocket_kit.c -o $(BUILD)/pocket/kit.o
	$(CC) -std=gnu11 -O2 -w -I$(POCKET)/engine -I$(POCKET)/seq -c $(POCKET)/seq/pocket_gen.c -o $(BUILD)/pocket/gen.o
	ld -r $(BUILD)/pocket/kit.o $(BUILD)/pocket/gen.o -o $@ 2>/dev/null || $(CC) -r -nostdlib $(BUILD)/pocket/kit.o $(BUILD)/pocket/gen.o -o $@
$(BUILD)/test_authentic: tests/test_authentic.cpp tests/check.h $(BUILD)/c12_engine.inc $(BUILD)/pocket/liblxr_nofx.a $(BUILD)/pocket/seq.o
	$(CXX) $(CXXFLAGS_HOST) -w -I$(BUILD) -Itests -I$(POCKET)/engine -I$(POCKET)/seq tests/test_authentic.cpp \
		$(BUILD)/pocket/seq.o $(BUILD)/pocket/liblxr_nofx.a -lm -o $@

$(BUILD)/bench: tests/bench.c $(BUILD)/c12_unity.o
	$(CC) $(TEST_CFLAGS) tests/bench.c $(BUILD)/c12_unity.o -lm -o $@

test: $(BUILD)/test_resampler $(BUILD)/test_engine $(BUILD)/equiv_c $(BUILD)/equiv_cxx $(BUILD)/test_authentic $(BUILD)/test_panel_sim
	$(BUILD)/test_resampler
	$(BUILD)/test_engine
	$(BUILD)/test_authentic
	$(BUILD)/equiv_c $(BUILD)/test/equiv_c.raw
	$(BUILD)/equiv_cxx $(BUILD)/test/equiv_cxx.raw
	@# C++ resolves <math.h> calls on floats to the float overloads, C to the double ones: a few LSB
	$(PY) tools/compare_pcm.py $(BUILD)/test/equiv_c.raw $(BUILD)/test/equiv_cxx.raw 80
	$(BUILD)/test_panel_sim
	$(PY) tools/check_api.py panel/clatter12.cpp sim/plinky_sim.h

# The generated panel for the RP2350's core, with the simulator's declarations of the Plinky API
# in front (the real IDE headers are not published as a compilable set). Proves the engine and
# glue compile for Cortex-M33 + single-precision FPU with GCC, and gives section sizes. It does
# not link against Plinky firmware.
M33_VARIANTS := clatter_12 clatter_12_sram clatter_12_psram
m33: $(foreach v,$(M33_VARIANTS),$(BUILD)/$(v).cpp)
	@test -n "$(ARM_GXX)" || (echo "arm-none-eabi-g++ not found (set ARM_GXX)"; exit 1)
	@for v in $(M33_VARIANTS); do \
		printf '#include "plinky_sim.h"\n#include "%s.cpp"\n' $$v > $(BUILD)/m33_$$v.cpp; \
		echo "$(ARM_GXX) ... $$v"; \
		$(ARM_GXX) $(M33_FLAGS) -Werror=return-type -Isim -I$(BUILD) -c $(BUILD)/m33_$$v.cpp -o $(BUILD)/m33_$$v.o || exit 1; \
	done
	$(PY) tools/m33_sizes.py $(foreach v,$(M33_VARIANTS),$(BUILD)/m33_$(v).o)
	$(PY) tools/check_link.py $(BUILD)/c12_engine.inc $(foreach v,$(M33_VARIANTS),$(BUILD)/m33_$(v).o)

bench: $(BUILD)/bench
	$(BUILD)/bench

pocket-test:
	$(MAKE) -C $(POCKET) test

clean:
	rm -rf $(BUILD)
