# nsz5 — lossless POD5 signal codec
#
# make test       build and run all tests with ASan/UBSan (development default)
# make test-opt   build and run all tests at -O3 without sanitizers
# make lib        build work/libnsz.so (release flags)
# make clean

CC       ?= gcc
WARN      = -Wall -Wextra -Wpedantic
CSTD      = -std=c99
OPT       = -O3 -march=x86-64-v3 -fopenmp
SAN       = -g -O1 -fopenmp -fsanitize=address,undefined -fno-omit-frame-pointer

SRC       = src/nsz.c src/predictor.c
HDRS      = $(wildcard src/*.h)
TESTS     = test_zigzag test_predictor test_range_coder test_magnitude test_context test_roundtrip

BUILD     = work

.PHONY: all test test-opt lib clean

all: test

$(BUILD):
	mkdir -p $(BUILD)

# --- sanitized test binaries ---
$(BUILD)/%_san: tests/%.c $(SRC) $(HDRS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(SAN) -o $@ $< $(SRC) -lm

# --- optimized test binaries ---
$(BUILD)/%_opt: tests/%.c $(SRC) $(HDRS) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(OPT) -o $@ $< $(SRC) -lm

test: $(addprefix $(BUILD)/, $(addsuffix _san, $(TESTS)))
	@for t in $^; do echo "== $$t"; ./$$t || exit 1; done

test-opt: $(addprefix $(BUILD)/, $(addsuffix _opt, $(TESTS)))
	@for t in $^; do echo "== $$t"; ./$$t || exit 1; done

lib: | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(OPT) -fPIC -shared -o $(BUILD)/libnsz.so $(SRC)

bench: | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(OPT) -o $(BUILD)/bench_file tools/bench_file.c $(SRC) -lm

train: | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(OPT) -o $(BUILD)/train_prior tools/train_prior.c src/predictor.c -lm

bench-batch: | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(OPT) -march=native -o $(BUILD)/bench_batch tools/bench_batch.c $(SRC) -lm

clean:
	rm -rf $(BUILD)
