# DriftNARS — Makefile
#
# Targets:
#   all              Build driftnars binary and libdriftnars.a (default)
#   driftnars        CLI binary only  (bin/driftnars)
#   libdriftnars.a   Static library   (bin/libdriftnars.a)
#   test             Run built-in unit and system tests
#   clean            Remove all build artifacts including generated RuleTable.c
#
# Options (pass on command line):
#   OPENMP=1    Enable OpenMP threading  (make OPENMP=1)

CC      := gcc
AR      := ar
ARFLAGS := rcs

CFLAGS := \
    -std=c99 \
    -pedantic \
    -O3 \
    -g3 \
    -flto \
    -fPIC \
    -pthread \
    -D_POSIX_C_SOURCE=199506L

WFLAGS := \
    -Wall \
    -Wextra \
    -Wformat-security \
    -Wno-unknown-pragmas \
    -Wno-tautological-compare \
    -Wno-dollar-in-identifier-extension \
    -Wno-unused-parameter \
    -Wno-unused-variable \
    -Wno-strict-prototypes

LDFLAGS := -lm -lpthread

ifdef OPENMP
CFLAGS += -fopenmp
endif

# SSE2 floating-point on x86_64 only; harmlessly empty on ARM/other
ifeq ($(shell uname -m),x86_64)
SSE_FLAGS := -mfpmath=sse -msse2
endif

# ── Shared library extension / flags ───────────────────────────────────────────
ifeq ($(shell uname -s),Darwin)
SHLIB_EXT    := dylib
SHLIB_LFLAGS := -dynamiclib
else
SHLIB_EXT    := so
SHLIB_LFLAGS := -shared
endif

BUILDDIR := build
BINDIR   := bin

# ── Sources ───────────────────────────────────────────────────────────────────
# Exclude generated RuleTable.c from the static wildcard expansion
SRCS_CORE := $(filter-out src/engine/RuleTable.c, $(wildcard src/engine/*.c))

# ── Stage-2 object files ──────────────────────────────────────────────────────
OBJS_CORE := $(patsubst src/engine/%.c,     $(BUILDDIR)/%.o,            $(SRCS_CORE))
OBJS_GEN  := $(BUILDDIR)/RuleTable.o
OBJS_DS   := $(BUILDDIR)/driftscript.o
OBJS_ALL  := $(OBJS_CORE) $(OBJS_GEN) $(OBJS_DS)
OBJS_LIB  := $(filter-out $(BUILDDIR)/main.o, $(OBJS_ALL))

# Dependency files for automatic header change tracking
DEPFILES  := $(OBJS_CORE:.o=.d)

# ── Targets ───────────────────────────────────────────────────────────────────
.PHONY: all clean test httpd
all: $(BINDIR)/driftnars $(BINDIR)/libdriftnars.a $(BINDIR)/libdriftnars.$(SHLIB_EXT) $(BINDIR)/driftscript

# ── Stage 1: bootstrap binary — only purpose is generating RuleTable.c ────────
$(BUILDDIR)/bootstrap: $(SRCS_CORE)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WFLAGS) -DSTAGE=1 $^ $(LDFLAGS) -o $@

# ── Generated rule table ───────────────────────────────────────────────────────
src/engine/RuleTable.c: $(BUILDDIR)/bootstrap
	$(BUILDDIR)/bootstrap NAL_GenerateRuleTable > $@

# ── Stage-2 object compilation ─────────────────────────────────────────────────
# Generated file gets no WFLAGS — the output is machine-written
$(BUILDDIR)/RuleTable.o: src/engine/RuleTable.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SSE_FLAGS) -DSTAGE=2 -c $< -o $@

# DriftScript compiler object (library mode — no main())
# Uses its own CFLAGS without _POSIX_C_SOURCE (which suppresses snprintf on some platforms)
$(BUILDDIR)/driftscript.o: src/compiler/driftscript.c src/compiler/driftscript.h
	@mkdir -p $(dir $@)
	$(CC) -std=c99 -pedantic -O3 -g3 -flto -fPIC $(WFLAGS) $(SSE_FLAGS) -DDS_LIBRARY -DSTAGE=2 -MMD -MP -c $< -o $@

$(BUILDDIR)/%.o: src/engine/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WFLAGS) $(SSE_FLAGS) -Isrc/compiler -DSTAGE=2 -MMD -MP -c $< -o $@

# ── Final binary ───────────────────────────────────────────────────────────────
$(BINDIR)/driftnars: $(OBJS_ALL)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SSE_FLAGS) $^ $(LDFLAGS) -o $@

# ── Static library ─────────────────────────────────────────────────────────────
$(BINDIR)/libdriftnars.a: $(OBJS_LIB)
	@mkdir -p $(dir $@)
	$(AR) $(ARFLAGS) $@ $^

# ── Shared library ────────────────────────────────────────────────────────────
$(BINDIR)/libdriftnars.$(SHLIB_EXT): $(OBJS_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(SHLIB_LFLAGS) $(CFLAGS) $(SSE_FLAGS) $^ $(LDFLAGS) -o $@

# ── DriftScript compiler (standalone, no library dependency) ──────────────────
$(BINDIR)/driftscript: src/compiler/driftscript.c
	@mkdir -p $(dir $@)
	$(CC) -std=c99 -pedantic -O2 -g -Wall -Wextra -o $@ $<

# ── HTTP server ───────────────────────────────────────────────────────────────
HTTPD_SRC := src/server/httpd.c
HTTPD_OBJ := $(BUILDDIR)/httpd.o

$(BUILDDIR)/httpd.o: $(HTTPD_SRC) $(BINDIR)/libdriftnars.a
	@mkdir -p $(dir $@)
	$(CC) -std=c99 $(WFLAGS) -O2 -g -D_GNU_SOURCE -DSTAGE=2 -Isrc/engine -Isrc/compiler -c $< -o $@

$(BINDIR)/driftnars-httpd: $(HTTPD_OBJ) $(BINDIR)/libdriftnars.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SSE_FLAGS) $^ $(LDFLAGS) -o $@

httpd: $(BINDIR)/driftnars-httpd

# ── Tests ──────────────────────────────────────────────────────────────────────
test: $(BINDIR)/driftnars
	$(BINDIR)/driftnars test

# ── Clean ──────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILDDIR) $(BINDIR) src/engine/RuleTable.c

# Pull in generated header dependencies (silently absent on first build)
-include $(DEPFILES)
