#==============================================================================
# MACROS
#==============================================================================

CC := gcc
CSTD := -std=c11
DEV_CFLAGS := -Wall -Wextra -Wpedantic -Wformat=2 -Wno-unused-parameter \
             -Wshadow -Wwrite-strings -Wstrict-prototypes \
             -Wold-style-definition -Wredundant-decls -Wnested-externs \
             -Wmissing-include-dirs -Og
LINK_FLAGS := -lm -lfdb_c -lpthread

FDB_VERSION := 710
PARAMS := -DFDB_API_VERSION=$(FDB_VERSION)

BIN_DIR := bin/
DEP_DIR := dep/
OBJ_DIR := obj/
SRC_DIR := src/

TEST_DEP_DIR := dep/test/
TEST_OBJ_DIR := obj/test/
TEST_SRC_DIR := src/test/

BENCH_DEP_DIR := dep/benchmark/
BENCH_OBJ_DIR := obj/benchmark/
BENCH_SRC_DIR := src/benchmark/

SOURCES := $(shell ls $(SRC_DIR)*.c)
OBJECTS := $(subst $(SRC_DIR),$(OBJ_DIR),$(subst .c,.o,$(SOURCES)))
DEPFILES := $(subst $(SRC_DIR),$(DEP_DIR),$(subst .c,.d,$(SOURCES)))

TEST_SOURCES := $(shell ls $(TEST_SRC_DIR)*.c)
TEST_OBJECTS := $(subst $(TEST_SRC_DIR),$(TEST_OBJ_DIR),$(subst .c,.o,$(TEST_SOURCES)))
TEST_DEPFILES := $(subst $(TEST_SRC_DIR),$(TEST_DEP_DIR),$(subst .c,.d,$(TEST_SOURCES)))

BENCH_SOURCES := $(shell ls $(BENCH_SRC_DIR)*.c)
BENCH_OBJECTS := $(subst $(BENCH_SRC_DIR),$(BENCH_OBJ_DIR),$(subst .c,.o,$(BENCH_SOURCES)))
BENCH_DEPFILES := $(subst $(BENCH_SRC_DIR),$(BENCH_DEP_DIR),$(subst .c,.d,$(BENCH_SOURCES)))

TEST_UNIT_CMD := $(addprefix $(BIN_DIR),seguro-test-unit)
TEST_INTEG_CMD := $(addprefix $(BIN_DIR),seguro-test-integ)

BENCHMARK_WRITE_CMD := $(addprefix $(BIN_DIR),seguro-benchmark-write)

#==============================================================================
# RULES
#==============================================================================

# Default target. Compile & link all source files, then print usage instructions.
#
default : $(BENCHMARK_CMD) help

# Helpful rule which lists all other rules and encourages documentation
#
# target: help - Display all targets in makefile
#
help :
	@egrep "^# target:" makefile

# Run Seguro tests
#
# target: test - Run all Seguro tests
#
test : test-unit test-integ

# Run Seguro unit tests
#
# target: test-unit - Run Seguro unit tests
#
test-unit : $(TEST_UNIT_CMD)
	@$(TEST_UNIT_CMD)

# Link unit tests into an executable binary
#
$(TEST_UNIT_CMD) : $(OBJECTS) $(addprefix $(TEST_OBJ_DIR),unit.o)
	@mkdir -p $(BIN_DIR)
	$(CC) $(addprefix $(TEST_OBJ_DIR),unit.o) $(OBJECTS) $(LINK_FLAGS) -o $@

# Run Seguro integration tests
#
# target: test-unit - Run Seguro integration tests
#
test-integ : $(TEST_INTEG_CMD)
	@$(TEST_INTEG_CMD)

# Link integration tests into an executable binary
#
$(TEST_INTEG_CMD) : $(OBJECTS) $(addprefix $(TEST_OBJ_DIR),integ.o)
	@mkdir -p $(BIN_DIR)
	$(CC) $(addprefix $(TEST_OBJ_DIR),integ.o) $(OBJECTS) $(LINK_FLAGS) -o $@

# Run Seguro benchmarks
#
# target: benchmark - Run all Seguro benchmarks
#
benchmark : benchmark-write

# Run Seguro write benchmarks
#
# target: benchmark-write - Run Seguro write benchmarks
#
benchmark-write : $(BENCHMARK_WRITE_CMD)
	@$(BENCHMARK_WRITE_CMD)

# Link benchmark suite into an executable binary
#
$(BENCHMARK_WRITE_CMD) : $(OBJECTS) $(addprefix $(BENCH_OBJ_DIR),write.o)
	@mkdir -p $(BIN_DIR)
	$(CC) $(addprefix $(BENCH_OBJ_DIR),write.o) $(OBJECTS) $(LINK_FLAGS) -o $@

# Compile all source files, but do not link. As a side effect, compile a dependency file for each source file.
#
# Dependency files are a common makefile feature used to speed up builds by auto-generating granular makefile targets.
# These files minimize the number of targets that need to be recomputed when source files are modified and can lead to
# massive build-time improvements.

# For more information, see the "-M" option documentation in the GCC man page, as well as this paper:
# https://web.archive.org/web/20150319074420/http://aegis.sourceforge.net/auug97.pdf
#
$(addprefix $(DEP_DIR),%.d): $(addprefix $(SRC_DIR),%.c)
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(DEP_DIR)
	$(CC) -MD -MP -MF $@ -MT '$@ $(subst $(DEP_DIR),$(OBJ_DIR),$(@:.d=.o))' \
		$< -c -o $(subst $(DEP_DIR),$(OBJ_DIR),$(@:.d=.o)) $(CSTD) $(PARAMS) $(DEV_CFLAGS)

# Same as above, but specifically for test files
#
$(addprefix $(TEST_DEP_DIR),%.d): $(addprefix $(TEST_SRC_DIR),%.c)
	@mkdir -p $(TEST_OBJ_DIR)
	@mkdir -p $(TEST_DEP_DIR)
	$(CC) -MD -MP -MF $@ -MT '$@ $(subst $(DEP_DIR),$(OBJ_DIR),$(@:.d=.o))' \
		$< -c -o $(subst $(DEP_DIR),$(OBJ_DIR),$(@:.d=.o)) $(CSTD) $(PARAMS) $(DEV_CFLAGS)

# Same as above, but specifically for benchmarking files
#
$(addprefix $(BENCH_DEP_DIR),%.d): $(addprefix $(BENCH_SRC_DIR),%.c)
	@mkdir -p $(BENCH_OBJ_DIR)
	@mkdir -p $(BENCH_DEP_DIR)
	$(CC) -MD -MP -MF $@ -MT '$@ $(subst $(DEP_DIR),$(OBJ_DIR),$(@:.d=.o))' \
		$< -c -o $(subst $(DEP_DIR),$(OBJ_DIR),$(@:.d=.o)) $(CSTD) $(PARAMS) $(DEV_CFLAGS)

# Force build of dependency and object files to import additional makefile targets
#
-include $(DEPFILES) $(TEST_DEPFILES) $(BENCH_DEPFILES)

# Clean up files produced by the makefile. Any invocation should execute, regardless of file modification date, hence
# dependency on FRC.
#
# target: clean - Remove all files produced by this makefile
clean : FRC
	@rm -rf $(BIN_DIR) $(DEP_DIR) $(OBJ_DIR)

# Special pseudo target which always needs to be recomputed. Forces full rebuild of target every time when used as a
# component.
FRC :

