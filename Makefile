SHELL := /bin/sh

CC ?= cc
AR ?= ar
PYTHON ?= python3
BUILD_DIR ?= build
OPT_FLAGS ?= -O2
SANITIZER_FLAGS ?=
HARDEN_FLAGS ?= -fstack-protector-strong -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2 -fPIC
EXTRA_CFLAGS ?=
EXTRA_LDFLAGS ?=
PNG_CFLAGS := $(shell pkg-config --cflags libpng 2>/dev/null)
PNG_LIBS := $(shell pkg-config --libs libpng 2>/dev/null)
OPENSSL_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
OPENSSL_LIBS := $(shell pkg-config --libs openssl 2>/dev/null)
ifneq ($(strip $(OPENSSL_LIBS)),)
CPPFLAGS_HASH_ACCEL := -DODM_SHA256_OPENSSL_ACCEL=1
endif

GEN_DIR := $(BUILD_DIR)/generated
META_HEADER := $(GEN_DIR)/odm_build_meta.h
OBJ_DIR := $(BUILD_DIR)/obj

WARN_FLAGS := -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion \
	-Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror
BASE_FLAGS := -std=c11 $(WARN_FLAGS) $(OPT_FLAGS) $(HARDEN_FLAGS) \
	$(SANITIZER_FLAGS) $(EXTRA_CFLAGS) -ffile-prefix-map=$(CURDIR)=.
CPPFLAGS := -Iinclude -Isrc/cli -Isrc/spine -Isrc/media -Isrc/music_map -Isrc/music_inference -Isrc/ir -Isrc/package -Isrc/renderer -Isrc/visual -Isrc/visual_dynamics -Isrc/preview -Isrc/master -Isrc/studio -Isrc/delivery -Isrc/export -Isrc/compositor -Itests -I$(GEN_DIR) $(PNG_CFLAGS) $(OPENSSL_CFLAGS) $(CPPFLAGS_HASH_ACCEL)
LINK_FLAGS := -pthread $(SANITIZER_FLAGS) $(EXTRA_LDFLAGS) $(PNG_LIBS) $(OPENSSL_LIBS)
RELEASE_LINK_FLAGS := -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack \
	-Wl,--build-id=none

LIB_SOURCES := \
	src/core/status.c \
	src/core/version.c \
	src/core/hash.c \
	src/core/time.c \
	src/core/fixed.c \
	src/core/rng.c \
	src/core/wire.c \
	src/core/limits.c \
	src/core/memory_budget.c \
	src/core/arena.c \
	src/core/ownership.c \
	src/core/progress.c \
	src/core/job.c \
	src/core/executor.c \
	src/media/media_limits.c \
	src/media/media_facts.c \
	src/media/pcm.c \
	src/media/wav.c \
	src/media/png_crc.c \
	src/media/png.c \
	src/media/media_probe.c \
	src/media/media_dispatch.c src/media/decode_adapter.c \
	src/music_map/resample.c \
	src/music_map/analysis.c src/music_map/analysis_wire.c \
	src/music_inference/reaction_spine.c \
	src/score/score.c src/score/compiler.c \
	src/ir/ir.c src/ir/frame_state.c \
	src/package/deflate.c src/package/sign.c src/package/odparms.c \
	src/renderer/color.c src/renderer/sampler.c src/renderer/reference.c src/renderer/frame_root.c \
	src/visual/event_history.c src/visual/procedural.c \
	src/visual_dynamics/response.c src/visual_dynamics/scene.c \
	src/preview/preview.c src/preview/ffi_preview.c \
	src/master/master.c src/master/receipt.c src/master/controller_sign.c \
	src/studio/revision.c src/studio/approval.c src/studio/workflow.c \
	src/delivery/contract.c \
	src/export/export.c \
	src/compositor/math.c src/compositor/reaction.c src/compositor/style.c src/compositor/layout.c src/compositor/background.c \
	src/compositor/core.c src/compositor/field.c src/compositor/hud.c src/compositor/render.c \
	src/spine/spine_registry.c \
	src/spine/spine.c \
	src/spine/spine_impact.c \
	src/core/ffi.c

CLI_SOURCES := src/cli/main.c src/cli/selftest.c
BENCH_SOURCES := bench/benchmark_core.c
TEST_SOURCES := $(sort $(wildcard tests/unit/*.c) $(wildcard tests/properties/*.c) \
	$(wildcard tests/test_main.c))
LIB_OBJECTS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(LIB_SOURCES))
CLI_OBJECTS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(CLI_SOURCES))
TEST_OBJECTS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(TEST_SOURCES))
BENCH_OBJECTS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(BENCH_SOURCES))
META_INPUTS := $(shell find include src tests tools docs bench flutter -type f \
	\( -name '*.c' -o -name '*.h' -o -name '*.def' -o -name '*.py' \
	-o -name '*.md' -o -name '*.txt' -o -name '*.bad' -o -name '*.rgba16le' \
	-o -name '*.json' -o -name '*.dart' \) 2>/dev/null | LC_ALL=C sort) \
	Makefile GATES.md ODPAR_MUSIC_CONSTITUTION_v0.md START_IMPLEMENTATION.md \
	NEXT_CHAT_PROMPT.txt README.md

LIBRARY := $(BUILD_DIR)/libodpar_music.a
SHARED_LIBRARY := $(BUILD_DIR)/libodpar_music.so
CLI := $(BUILD_DIR)/odpar-music
TEST_RUNNER := $(BUILD_DIR)/odm-tests
CORE_BENCHMARK := $(BUILD_DIR)/odm-core-benchmark

.PHONY: visual-dynamics-oracle all clean ffi-shared test selftest guardians spine-check header-check determinism crypto-oracle sha-backend-oracle abi-check hardening media-oracle resampler-oracle music-analysis-oracle music-reaction-oracle contextual-salience-oracle reaction-projection-oracle strict-causal-oracle multi-axis-visual-oracle radial-provenance-oracle radial-timescale-geometry-oracle multiscale-radial-raster-oracle causal-trace-oracle render-ir-oracle frame-state-oracle odparms-oracle renderer-oracle renderer-property-oracle visual-oracle visual-dynamics-oracle composition-oracle director-oracle visual-policy-oracle layered-oracle style-oracle reaction-oracle layer-output-oracle layered-raster-oracle radial-geometry-oracle radial-hires-geometry-oracle particle-geometry-oracle strict-causal-field-oracle perspective-grid-oracle hud-geometry-oracle srgb-lut-check export-engine-oracle export-ffmpeg-smoke export-run-smoke preview-oracle preview-ffi flutter-preview-binding master-oracle studio-oracle delivery-oracle commercial-claims-check ffmpeg-delivery-smoke benchmark-core gate11-benchmark-observation evidence evidence-extension013 evidence-gate0 evidence-gate1 evidence-gate2 evidence-gate3 evidence-gate4 evidence-gate5 evidence-gate6 evidence-gate7 evidence-gate8 evidence-gate9 evidence-gate10 evidence-gate11 \
	repro analyze-gcc test-gcc test-clang asan-gcc asan-clang tsan-gcc \
	gate0-local gate0 gate1-local gate1 gate2 gate3 gate4 gate5 gate6 gate7 gate8-local gate8 gate9-local gate9 gate10-local gate10 gate11-local gate11

.PHONY: FORCE
FORCE:

all: $(LIBRARY) $(CLI)

$(META_HEADER): FORCE tools/gen_build_meta.py src/spine/modules.def $(META_INPUTS)
	@mkdir -p $(GEN_DIR)
	$(PYTHON) tools/gen_build_meta.py --output $(META_HEADER)

$(OBJ_DIR)/src/spine/spine_registry.o $(OBJ_DIR)/src/spine/spine.o $(OBJ_DIR)/src/spine/spine_impact.o: src/spine/modules.def

$(OBJ_DIR)/%.o: %.c $(META_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(BASE_FLAGS) -c $< -o $@

$(LIBRARY): $(LIB_OBJECTS)
	@mkdir -p $(dir $@)
	$(AR) rcsD $@ $(LIB_OBJECTS)

$(SHARED_LIBRARY): $(LIB_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) -shared $(BASE_FLAGS) $(LIB_OBJECTS) $(LINK_FLAGS) \
		$(RELEASE_LINK_FLAGS) -o $@

ffi-shared: $(SHARED_LIBRARY)

$(CLI): $(CLI_OBJECTS) $(LIBRARY)
	$(CC) $(BASE_FLAGS) $(CLI_OBJECTS) $(LIBRARY) $(LINK_FLAGS) \
		$(RELEASE_LINK_FLAGS) -pie -o $@

$(TEST_RUNNER): $(TEST_OBJECTS) $(LIBRARY)
	$(CC) $(BASE_FLAGS) $(TEST_OBJECTS) $(LIBRARY) $(LINK_FLAGS) -o $@

$(CORE_BENCHMARK): $(BENCH_OBJECTS) $(LIBRARY)
	$(CC) $(BASE_FLAGS) $(BENCH_OBJECTS) $(LIBRARY) $(LINK_FLAGS) -o $@

test: $(TEST_RUNNER)
	$(TEST_RUNNER)

selftest: $(CLI)
	$(CLI) selftest

guardians:
	$(PYTHON) tools/guardian.py --root .
	$(PYTHON) tools/guardian.py --root . --negative-fixtures

spine-check: $(CLI)
	$(PYTHON) tools/check_spine.py --root . --binary $(CLI) --library $(LIBRARY)

header-check: $(META_HEADER)
	$(PYTHON) tools/check_headers.py --root . --cc $(CC) --generated $(GEN_DIR)

determinism: $(CLI) $(TEST_RUNNER)
	$(PYTHON) tests/determinism/check_outputs.py --binary $(CLI) \
		--tests $(TEST_RUNNER)

crypto-oracle: $(LIBRARY)
	$(PYTHON) tools/check_sha256_oracle.py --root . --cc $(CC) \
		--library $(LIBRARY)

sha-backend-oracle:
	$(PYTHON) tools/check_sha256_backends.py --root . --cc $(CC)

abi-check: $(LIBRARY)
	$(PYTHON) tools/check_abi.py --root . --cc $(CC) \
		--library $(LIBRARY) --golden tests/golden/abi_v1.json

hardening: $(CLI)
	$(PYTHON) tools/check_hardening.py --root . --binary $(CLI)

media-oracle: $(LIBRARY)
	$(PYTHON) tools/check_media_oracle.py --root . --cc $(CC) --library $(LIBRARY)

resampler-oracle: $(LIBRARY)
	$(PYTHON) tools/check_resampler_oracle.py --root . --cc $(CC) --library $(LIBRARY)

music-analysis-oracle: $(LIBRARY)
	$(PYTHON) tools/check_music_analysis_oracle.py --root . --cc $(CC) --library $(LIBRARY)

music-reaction-oracle: $(LIBRARY)
	$(PYTHON) tools/check_music_reaction_oracle.py --root . --cc $(CC) --library $(LIBRARY)

contextual-salience-oracle: $(LIBRARY)
	$(PYTHON) tools/check_contextual_salience_oracle.py --root . --cc $(CC) --library $(LIBRARY)

multi-axis-visual-oracle: $(LIBRARY)
	$(PYTHON) tools/check_multi_axis_visual_oracle.py --root . --cc $(CC) --library $(LIBRARY)

radial-provenance-oracle: $(LIBRARY)
	$(PYTHON) tools/check_radial_provenance_oracle.py --root . --cc $(CC) --library $(LIBRARY)

radial-timescale-geometry-oracle: $(LIBRARY)
	$(PYTHON) tools/check_radial_timescale_geometry_oracle.py --root . --cc $(CC) --library $(LIBRARY)

multiscale-radial-raster-oracle: $(LIBRARY)
	$(PYTHON) tools/check_multiscale_radial_raster_oracle.py --root . --cc $(CC) --library $(LIBRARY)

causal-trace-oracle: $(LIBRARY)
	$(PYTHON) tools/check_causal_trace_oracle.py --root . --cc $(CC) --library $(LIBRARY)

reaction-projection-oracle: $(LIBRARY)
	$(PYTHON) tools/check_reaction_projection_oracle.py --root . --cc $(CC) --library $(LIBRARY)

strict-causal-oracle: $(LIBRARY)
	$(PYTHON) tools/check_strict_causal_oracle.py --root . --cc $(CC) --library $(LIBRARY)

render-ir-oracle: $(LIBRARY)
	$(PYTHON) tools/check_render_ir_oracle.py --root . --cc $(CC) --library $(LIBRARY)

frame-state-oracle: $(LIBRARY)
	$(PYTHON) tools/check_frame_state_oracle.py --root . --cc $(CC) --library $(LIBRARY)

odparms-oracle: $(LIBRARY)
	$(PYTHON) tools/check_odparms_oracle.py --root . --cc $(CC) --library $(LIBRARY)

renderer-oracle: $(LIBRARY)
	$(PYTHON) tools/check_renderer_oracle.py --root . --cc $(CC) --library $(LIBRARY)

renderer-property-oracle: $(LIBRARY)
	$(PYTHON) tools/check_renderer_property_oracle.py --root . --cc $(CC) --library $(LIBRARY)

visual-dynamics-oracle: $(LIBRARY)
	$(PYTHON) tools/check_visual_dynamics_oracle.py --root . --cc $(CC) --library $(LIBRARY)

visual-oracle: $(LIBRARY)
	$(PYTHON) tools/check_visual_oracle.py --root . --cc $(CC) --library $(LIBRARY)

composition-oracle: $(SHARED_LIBRARY)
	$(PYTHON) tools/check_composition_oracle.py --shared $(SHARED_LIBRARY)

director-oracle: $(SHARED_LIBRARY)
	$(PYTHON) tools/check_director_oracle.py --shared $(SHARED_LIBRARY)

visual-policy-oracle: $(SHARED_LIBRARY)
	$(PYTHON) tools/check_visual_policy_oracle.py --library $(SHARED_LIBRARY)

layered-oracle: $(LIBRARY)
	$(PYTHON) tools/check_layered_oracle.py --root . --cc $(CC) --library $(LIBRARY)

style-oracle: $(LIBRARY)
	$(PYTHON) tools/check_style_oracle.py --root . --cc $(CC) --library $(LIBRARY)

reaction-oracle: $(LIBRARY)
	$(PYTHON) tools/check_reaction_oracle.py --root . --cc $(CC) --library $(LIBRARY)

layer-output-oracle: $(LIBRARY)
	$(PYTHON) tools/check_layer_output_oracle.py --root . --cc $(CC) --library $(LIBRARY)

layered-raster-oracle: $(LIBRARY)
	$(PYTHON) tools/check_layered_raster_oracle.py --root . --cc $(CC) --library $(LIBRARY)

radial-geometry-oracle: $(LIBRARY)
	$(PYTHON) tools/check_radial_geometry_oracle.py --root . --cc $(CC) --library $(LIBRARY)

radial-hires-geometry-oracle: $(LIBRARY)
	$(PYTHON) tools/check_radial_hires_geometry_oracle.py --root . --cc $(CC) --library $(LIBRARY)

particle-geometry-oracle: $(LIBRARY)
	$(PYTHON) tools/check_particle_geometry_oracle.py --root . --cc $(CC) --library $(LIBRARY)

strict-causal-field-oracle: $(LIBRARY)
	$(PYTHON) tools/check_strict_causal_field_oracle.py --root . --cc $(CC) --library $(LIBRARY)

perspective-grid-oracle: $(LIBRARY)
	$(PYTHON) tools/check_perspective_grid_oracle.py --root . --cc $(CC) --library $(LIBRARY)

hud-geometry-oracle: $(LIBRARY)
	$(PYTHON) tools/check_hud_geometry_oracle.py --root . --cc $(CC) --library $(LIBRARY)

srgb-lut-check:
	$(PYTHON) tools/gen_layered_srgb_lut.py --check

export-engine-oracle: $(LIBRARY)
	$(PYTHON) tools/check_export_engine_oracle.py --root . --cc $(CC) --library $(LIBRARY)

export-ffmpeg-smoke: $(LIBRARY)
	$(PYTHON) tools/check_export_ffmpeg_smoke.py --root . --cc $(CC) --library $(LIBRARY)

export-run-smoke: $(LIBRARY)
	$(PYTHON) tools/check_export_run_smoke.py --root . --cc $(CC) --library $(LIBRARY)

preview-oracle: $(LIBRARY)
	$(PYTHON) tools/check_preview_oracle.py --root . --cc $(CC) --library $(LIBRARY)

preview-ffi: $(SHARED_LIBRARY)
	$(PYTHON) tools/check_preview_ffi.py --root . --cc $(CC) \
		--golden tests/golden/preview_ffi_v1.json --shared $(SHARED_LIBRARY)

flutter-preview-binding:
	$(PYTHON) tools/check_flutter_preview_ffi.py --root . \
		--golden tests/golden/preview_ffi_v1.json --binding flutter/odpar_music_ffi.dart

master-oracle: $(LIBRARY)
	$(PYTHON) tools/check_master_oracle.py --root . --cc $(CC) --library $(LIBRARY)

studio-oracle: $(LIBRARY)
	$(PYTHON) tools/check_studio_oracle.py --root . --cc $(CC) --library $(LIBRARY)

delivery-oracle: $(LIBRARY)
	$(PYTHON) tools/check_delivery_oracle.py --root . --cc $(CC) --library $(LIBRARY)

commercial-claims-check:
	$(PYTHON) tools/check_commercial_claims.py --root .

ffmpeg-delivery-smoke:
	$(PYTHON) tools/check_ffmpeg_delivery.py

benchmark-core: $(CORE_BENCHMARK)
	$(CORE_BENCHMARK)

gate11-benchmark-observation: $(CORE_BENCHMARK)
	$(PYTHON) tools/record_gate11_benchmark.py --root . --binary $(CORE_BENCHMARK) --output evidence/gate11_core_benchmark.json

repro:
	$(PYTHON) tests/determinism/check_reproducible.py --root . --cc $(CC)

analyze-gcc: $(META_HEADER)
	$(PYTHON) tools/check_gcc_analyzer.py --root . --cc gcc \
		--generated $(GEN_DIR)

test-gcc:
	@command -v gcc >/dev/null 2>&1 || { echo "ERROR: gcc is required"; exit 1; }
	@# Fast-safe lane: build authoritative artifacts once, then run read-only
	@# oracles in parallel. No oracle is allowed to race a library rebuild.
	$(MAKE) -j4 BUILD_DIR=build/gcc CC=gcc $(LIBRARY:build/%=build/gcc/%) $(SHARED_LIBRARY:build/%=build/gcc/%) $(CLI:build/%=build/gcc/%) $(TEST_RUNNER:build/%=build/gcc/%)
	$(MAKE) -j1 BUILD_DIR=build/gcc CC=gcc test selftest guardians spine-check header-check determinism hardening
	$(MAKE) -j4 BUILD_DIR=build/gcc CC=gcc crypto-oracle sha-backend-oracle media-oracle resampler-oracle music-analysis-oracle music-reaction-oracle contextual-salience-oracle reaction-projection-oracle strict-causal-oracle multi-axis-visual-oracle radial-provenance-oracle radial-timescale-geometry-oracle multiscale-radial-raster-oracle causal-trace-oracle render-ir-oracle frame-state-oracle odparms-oracle renderer-oracle renderer-property-oracle visual-oracle visual-dynamics-oracle composition-oracle director-oracle visual-policy-oracle layered-oracle style-oracle reaction-oracle layer-output-oracle layered-raster-oracle radial-geometry-oracle radial-hires-geometry-oracle particle-geometry-oracle strict-causal-field-oracle perspective-grid-oracle hud-geometry-oracle srgb-lut-check export-engine-oracle export-ffmpeg-smoke export-run-smoke preview-oracle preview-ffi flutter-preview-binding master-oracle studio-oracle delivery-oracle abi-check

test-clang:
	@command -v clang >/dev/null 2>&1 || { echo "ERROR: clang is required for the full Gate 0/1 matrix"; exit 1; }
	@# Same build-before-parallel-oracles contract as GCC.
	$(MAKE) -j4 BUILD_DIR=build/clang CC=clang $(LIBRARY:build/%=build/clang/%) $(SHARED_LIBRARY:build/%=build/clang/%) $(CLI:build/%=build/clang/%) $(TEST_RUNNER:build/%=build/clang/%)
	$(MAKE) -j1 BUILD_DIR=build/clang CC=clang test selftest guardians spine-check header-check determinism hardening
	$(MAKE) -j4 BUILD_DIR=build/clang CC=clang crypto-oracle sha-backend-oracle media-oracle resampler-oracle music-analysis-oracle music-reaction-oracle contextual-salience-oracle reaction-projection-oracle strict-causal-oracle multi-axis-visual-oracle radial-provenance-oracle radial-timescale-geometry-oracle multiscale-radial-raster-oracle causal-trace-oracle render-ir-oracle frame-state-oracle odparms-oracle renderer-oracle renderer-property-oracle visual-oracle visual-dynamics-oracle composition-oracle director-oracle visual-policy-oracle layered-oracle style-oracle reaction-oracle layer-output-oracle layered-raster-oracle radial-geometry-oracle radial-hires-geometry-oracle particle-geometry-oracle strict-causal-field-oracle perspective-grid-oracle hud-geometry-oracle srgb-lut-check export-engine-oracle export-ffmpeg-smoke export-run-smoke preview-oracle preview-ffi flutter-preview-binding master-oracle studio-oracle delivery-oracle abi-check

asan-gcc:
	$(MAKE) -j4 BUILD_DIR=build/asan-gcc CC=gcc OPT_FLAGS="-O1 -g1" \
		HARDEN_FLAGS= SANITIZER_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
		build/asan-gcc/odm-tests build/asan-gcc/odpar-music
	ASAN_OPTIONS="detect_leaks=0:halt_on_error=1" UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" build/asan-gcc/odm-tests
	ASAN_OPTIONS="detect_leaks=0:halt_on_error=1" UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" build/asan-gcc/odpar-music selftest

asan-clang:
	@command -v clang >/dev/null 2>&1 || { echo "ERROR: clang is required for the full Gate 0/1 matrix"; exit 1; }
	$(MAKE) -j4 BUILD_DIR=build/asan-clang CC=clang OPT_FLAGS="-O1 -g1" \
		HARDEN_FLAGS= SANITIZER_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
		build/asan-clang/odm-tests build/asan-clang/odpar-music
	ASAN_OPTIONS="detect_leaks=0:halt_on_error=1" UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" build/asan-clang/odm-tests
	ASAN_OPTIONS="detect_leaks=0:halt_on_error=1" UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" build/asan-clang/odpar-music selftest

tsan-gcc:
	$(MAKE) -j4 BUILD_DIR=build/tsan-gcc CC=gcc OPT_FLAGS="-O1 -g1" \
		HARDEN_FLAGS= SANITIZER_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
		build/tsan-gcc/odm-tests
	TSAN_OPTIONS="halt_on_error=1" build/tsan-gcc/odm-tests

gate0-local: test-gcc asan-gcc tsan-gcc analyze-gcc repro

gate0: gate0-local test-clang asan-clang

gate1-local: gate0-local
	$(MAKE) BUILD_DIR=build/bench-gcc CC=gcc benchmark-core

gate1: gate1-local test-clang asan-clang

evidence-extension013:
	$(PYTHON) tools/collect_extension013_evidence.py --root . \
		--lanes-dir evidence/extension013_lanes \
		--binary build/final013-gcc/odpar-music \
		--output evidence/extension013_evidence.json


evidence-gate0:
	$(PYTHON) tools/collect_gate0_evidence.py --root . \
		--output evidence/gate0_evidence.json

evidence-gate1:
	$(PYTHON) tools/collect_gate1_evidence.py --root . \
		--output evidence/gate1_evidence.json

evidence-gate2:
	$(PYTHON) tools/collect_gate2_evidence.py --root . \
		--output evidence/gate2_evidence.json

evidence-gate3:
	$(PYTHON) tools/collect_gate3_evidence.py --root . \
		--output evidence/gate3_evidence.json

evidence-gate4:
	$(PYTHON) tools/collect_gate4_evidence.py --root . \
		--output evidence/gate4_evidence.json

evidence-gate5:
	$(PYTHON) tools/collect_gate5_evidence.py --root . \
		--output evidence/gate5_evidence.json

evidence-gate6:
	$(PYTHON) tools/collect_gate6_evidence.py --root . \
		--output evidence/gate6_evidence.json

evidence-gate7:
	$(PYTHON) tools/collect_gate7_evidence.py --root . \
		--output evidence/gate7_evidence.json

evidence-gate8:
	$(PYTHON) tools/collect_gate8_evidence.py --root . \
		--output evidence/gate8_evidence.json

evidence-gate9:
	$(PYTHON) tools/collect_gate9_evidence.py --root . \
		--output evidence/gate9_evidence.json

evidence-gate10:
	$(PYTHON) tools/collect_gate10_evidence.py --root . \
		--output evidence/gate10_evidence.json

evidence-gate11:
	$(PYTHON) tools/collect_gate11_evidence.py --root . \
		--output evidence/gate11_evidence.json

gate2: evidence-gate2

gate3: evidence-gate3

gate4: evidence-gate4

gate5: evidence-gate5

gate6: evidence-gate6

gate7: evidence-gate7

gate8-local: evidence-gate8

gate8: evidence-gate8

gate9-local: evidence-gate9

gate9: evidence-gate9

gate10-local: evidence-gate10

gate10: evidence-gate10

evidence: evidence-gate11

clean:
	find "$(BUILD_DIR)" -mindepth 1 -delete 2>/dev/null || true
	rmdir "$(BUILD_DIR)" 2>/dev/null || true

# Gate 11 local closure keeps the FFmpeg adapter observation separate from
# compiler/sanitizer truth; it is an environment-specific evidence lane.
# commercial-claims-check is a RELEASE gate, not a unit lane: it is fail-closed
# against sealed gate2..gate10 evidence, and that evidence is produced by
# running test-gcc/test-clang. Keeping it inside those targets made the
# evidence unbootstrappable (it can only pass once it has already passed).
# It belongs here, after the lanes that seal the evidence it consumes.
gate11-local: test-gcc asan-gcc tsan-gcc analyze-gcc repro delivery-oracle commercial-claims-check gate11-benchmark-observation

gate11: gate11-local test-clang asan-clang ffmpeg-delivery-smoke

