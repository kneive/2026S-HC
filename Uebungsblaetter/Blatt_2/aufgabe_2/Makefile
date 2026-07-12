# Makefile fuer Aufgabe 2 auf RTX 4060 (Ada, sm_89)
NVCC    ?= nvcc
ARCH    ?= sm_89
NVFLAGS ?= -O3 -arch=$(ARCH) -lineinfo

SRC_DIR := src
INC_DIR := include
OUT_DIR := outputs
BUILD_DIR := ../build/aufgabe_2
SCRIPT_DIR := scripts
TEX_DIR := tex

BENCH := $(BUILD_DIR)/bench
REPORT_TEX := $(TEX_DIR)/aufgabe_2.tex
REPORT_PDF := $(OUT_DIR)/aufgabe_2.pdf
PATTERNS_CSV := $(OUT_DIR)/patterns.csv
PATTERNS_PNG := $(OUT_DIR)/patterns.png
OCCUPANCY_CSV := $(OUT_DIR)/occupancy.csv
OCCUPANCY_PNG := $(OUT_DIR)/occupancy.png

SRC := $(SRC_DIR)/bench.cu $(SRC_DIR)/memory.cu $(SRC_DIR)/stream.cu
HDR := $(INC_DIR)/common.cuh $(INC_DIR)/memory.cuh $(INC_DIR)/stream.cuh

all: bench memory report

$(BUILD_DIR) $(OUT_DIR):
	mkdir -p $@

$(BENCH): $(SRC) $(HDR) | $(BUILD_DIR)
	$(NVCC) $(NVFLAGS) -I$(INC_DIR) $(SRC) -o $@

bench: $(BENCH)

run: bench
	./$(BENCH)

stream: bench
	./$(BENCH) stream 67108864 20

$(PATTERNS_CSV): | $(BENCH) $(OUT_DIR)
	./$(BENCH) sweeppat $@

$(PATTERNS_PNG): $(PATTERNS_CSV)
	python3 $(SCRIPT_DIR)/plot_patterns.py $(PATTERNS_CSV) $@

patterns: $(PATTERNS_PNG)

$(OCCUPANCY_CSV): | $(BENCH) $(OUT_DIR)
	./$(BENCH) sweepocc $@

$(OCCUPANCY_PNG): $(OCCUPANCY_CSV)
	python3 $(SCRIPT_DIR)/plot_occupancy.py $(OCCUPANCY_CSV) $@

occupancy: $(OCCUPANCY_PNG)

memory: patterns occupancy

report: $(REPORT_TEX) $(PATTERNS_PNG) $(OCCUPANCY_PNG) | $(BUILD_DIR) $(OUT_DIR)
	latexmk -g -pdf -interaction=nonstopmode -output-directory=$(BUILD_DIR) $(REPORT_TEX)
	cp $(BUILD_DIR)/aufgabe_2.pdf $(REPORT_PDF)

clean:
	rm -f $(BENCH) $(OUT_DIR)/patterns.csv $(OUT_DIR)/patterns.png \
	      $(OUT_DIR)/occupancy.csv $(OUT_DIR)/occupancy.png \
	      $(REPORT_PDF) \
	      $(BUILD_DIR)/aufgabe_2.aux $(BUILD_DIR)/aufgabe_2.fdb_latexmk \
	      $(BUILD_DIR)/aufgabe_2.fls $(BUILD_DIR)/aufgabe_2.log \
	      $(BUILD_DIR)/aufgabe_2.out $(BUILD_DIR)/aufgabe_2.pdf

.PHONY: all bench run stream patterns occupancy memory report clean
