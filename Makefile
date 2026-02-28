# OpenNARS for Applications — Makefile
#
# Targets:
#   all         Build NAR binary and libnar.a static library (default)
#   NAR         CLI binary only
#   libnar.a    Static library (excludes main.c, links against your own program)
#   test        Run built-in unit and system tests
#   clean       Remove all build artifacts including generated RuleTable.c
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

BUILDDIR := build

# ── Sources ───────────────────────────────────────────────────────────────────
# Exclude generated RuleTable.c from the static wildcard expansion
SRCS_CORE := $(filter-out src/RuleTable.c, $(wildcard src/*.c))
SRCS_NET  := $(wildcard src/NetworkNAR/*.c)

# ── Stage-2 object files ──────────────────────────────────────────────────────
OBJS_CORE := $(patsubst src/%.c,            $(BUILDDIR)/%.o,            $(SRCS_CORE))
OBJS_NET  := $(patsubst src/NetworkNAR/%.c, $(BUILDDIR)/NetworkNAR/%.o, $(SRCS_NET))
OBJS_GEN  := $(BUILDDIR)/RuleTable.o
OBJS_ALL  := $(OBJS_CORE) $(OBJS_GEN) $(OBJS_NET)
OBJS_LIB  := $(filter-out $(BUILDDIR)/main.o, $(OBJS_ALL))

# Dependency files for automatic header change tracking
DEPFILES  := $(OBJS_CORE:.o=.d) $(OBJS_NET:.o=.d)

# ── Targets ───────────────────────────────────────────────────────────────────
.PHONY: all clean test
all: NAR libnar.a

# ── Stage 1: bootstrap binary — only purpose is generating RuleTable.c ────────
NAR_bootstrap: $(SRCS_CORE) $(SRCS_NET)
	$(CC) $(CFLAGS) $(WFLAGS) -DSTAGE=1 $^ $(LDFLAGS) -o $@

# ── Generated rule table ───────────────────────────────────────────────────────
src/RuleTable.c: NAR_bootstrap
	./NAR_bootstrap NAL_GenerateRuleTable > $@

# ── Stage-2 object compilation ─────────────────────────────────────────────────
# Generated file gets no WFLAGS — the output is machine-written
$(BUILDDIR)/RuleTable.o: src/RuleTable.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SSE_FLAGS) -DSTAGE=2 -c $< -o $@

$(BUILDDIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WFLAGS) $(SSE_FLAGS) -DSTAGE=2 -MMD -MP -c $< -o $@

$(BUILDDIR)/NetworkNAR/%.o: src/NetworkNAR/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WFLAGS) $(SSE_FLAGS) -DSTAGE=2 -MMD -MP -c $< -o $@

# ── Final binary ───────────────────────────────────────────────────────────────
NAR: $(OBJS_ALL)
	$(CC) $(CFLAGS) $(SSE_FLAGS) $^ $(LDFLAGS) -o $@

# ── Static library ─────────────────────────────────────────────────────────────
libnar.a: $(OBJS_LIB)
	$(AR) $(ARFLAGS) $@ $^

# ── Tests ──────────────────────────────────────────────────────────────────────
test: NAR
	./NAR test

# ── Clean ──────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILDDIR) NAR NAR_bootstrap libnar.a src/RuleTable.c

# Pull in generated header dependencies (silently absent on first build)
-include $(DEPFILES)
