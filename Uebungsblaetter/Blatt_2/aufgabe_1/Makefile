# Makefile fuer Aufgabe 1 auf RTX 4060 (Ada, sm_89)
NVCC    ?= nvcc
ARCH    ?= sm_89
NVFLAGS ?= -O3 -arch=$(ARCH) -lineinfo
LIBS    ?= -lcurand

SRC_DIR := src
INC_DIR := include
OUT_DIR := outputs
BUILD_DIR := ../build/aufgabe_1
SCRIPT_DIR := scripts
TEX_DIR := tex

BENCH := $(BUILD_DIR)/bench
REPORT_TEX := $(TEX_DIR)/aufgabe_1.tex
REPORT_PDF := $(OUT_DIR)/aufgabe_1.pdf
SCALING_CSV := $(OUT_DIR)/scaling.csv
SCALING_PNG := $(OUT_DIR)/scaling.png
DIVERGENCE_CSV := $(OUT_DIR)/divergence.csv
DIVERGENCE_PNG := $(OUT_DIR)/divergence.png
MANDELBROT_CSV := $(OUT_DIR)/mandelbrot.csv
MANDELBROT_PGM := $(OUT_DIR)/mandelbrot.pgm
MANDELBROT_GRAPH := $(OUT_DIR)/mandelbrot_scaling.png
MANDELBROT_IMAGE := $(OUT_DIR)/mandelbrot_result.png
MONTECARLO_CSV := $(OUT_DIR)/montecarlo.csv
MONTECARLO_POINTS := $(OUT_DIR)/montecarlo_points.csv
MONTECARLO_GRAPH := $(OUT_DIR)/montecarlo_scaling.png
MONTECARLO_IMAGE := $(OUT_DIR)/montecarlo_result.png

SRC := $(SRC_DIR)/bench.cu $(SRC_DIR)/fma.cu $(SRC_DIR)/mandelbrot.cu $(SRC_DIR)/montecarlo.cu
HDR := $(INC_DIR)/common.cuh $(INC_DIR)/fma.cuh $(INC_DIR)/mandelbrot.cuh $(INC_DIR)/montecarlo.cuh

all: bench assignment report

$(BUILD_DIR) $(OUT_DIR):
	mkdir -p $@

$(BENCH): $(SRC) $(HDR) | $(BUILD_DIR)
	$(NVCC) $(NVFLAGS) -I$(INC_DIR) $(SRC) -o $@ $(LIBS)

bench: $(BENCH)

run: bench
	./$(BENCH)

$(SCALING_CSV): | $(BENCH) $(OUT_DIR)
	./$(BENCH) sweepn $@

$(SCALING_PNG): $(SCALING_CSV)
	python3 $(SCRIPT_DIR)/plot_scaling.py $(SCALING_CSV) $@

scaling: $(SCALING_PNG)

$(DIVERGENCE_CSV): | $(BENCH) $(OUT_DIR)
	./$(BENCH) sweepdiv $@

$(DIVERGENCE_PNG): $(DIVERGENCE_CSV)
	python3 $(SCRIPT_DIR)/plot_divergence.py $(DIVERGENCE_CSV) $@

divergence: $(DIVERGENCE_PNG)

$(MANDELBROT_CSV): | $(BENCH) $(OUT_DIR)
	./$(BENCH) sweepmand $@

$(MANDELBROT_PGM): | $(BENCH) $(OUT_DIR)
	./$(BENCH) mandimg $@ 1600 1200 1000

$(MANDELBROT_GRAPH): $(MANDELBROT_CSV)
	python3 $(SCRIPT_DIR)/plot_mandelbrot.py $(MANDELBROT_CSV) $@

$(MANDELBROT_IMAGE): $(MANDELBROT_PGM)
	python3 $(SCRIPT_DIR)/plot_mandelbrot_image.py $(MANDELBROT_PGM) $@

mandelbrot: $(MANDELBROT_GRAPH) $(MANDELBROT_IMAGE)

$(MONTECARLO_CSV): | $(BENCH) $(OUT_DIR)
	./$(BENCH) sweepmc $@

$(MONTECARLO_POINTS): | $(BENCH) $(OUT_DIR)
	./$(BENCH) mcpoints $@ 20000

$(MONTECARLO_GRAPH): $(MONTECARLO_CSV)
	python3 $(SCRIPT_DIR)/plot_montecarlo.py $(MONTECARLO_CSV) $@

$(MONTECARLO_IMAGE): $(MONTECARLO_POINTS)
	python3 $(SCRIPT_DIR)/plot_montecarlo_points.py $(MONTECARLO_POINTS) $@

montecarlo: $(MONTECARLO_GRAPH) $(MONTECARLO_IMAGE)

assignment: scaling divergence mandelbrot montecarlo

report: $(REPORT_TEX) $(SCALING_PNG) $(DIVERGENCE_PNG) \
        $(MANDELBROT_GRAPH) $(MANDELBROT_IMAGE) \
        $(MONTECARLO_GRAPH) $(MONTECARLO_IMAGE) | $(BUILD_DIR) $(OUT_DIR)
	latexmk -g -pdf -interaction=nonstopmode -output-directory=$(BUILD_DIR) $(REPORT_TEX)
	cp $(BUILD_DIR)/aufgabe_1.pdf $(REPORT_PDF)

clean:
	rm -f $(BENCH) $(OUT_DIR)/scaling.csv $(OUT_DIR)/scaling.png \
	      $(OUT_DIR)/divergence.csv $(OUT_DIR)/divergence.png \
	      $(OUT_DIR)/mandelbrot.csv $(OUT_DIR)/mandelbrot.pgm \
	      $(OUT_DIR)/mandelbrot_scaling.png $(OUT_DIR)/mandelbrot_result.png \
	      $(OUT_DIR)/montecarlo.csv $(OUT_DIR)/montecarlo_points.csv \
	      $(OUT_DIR)/montecarlo_scaling.png $(OUT_DIR)/montecarlo_result.png \
	      $(REPORT_PDF) \
	      $(BUILD_DIR)/aufgabe_1.aux $(BUILD_DIR)/aufgabe_1.fdb_latexmk \
	      $(BUILD_DIR)/aufgabe_1.fls $(BUILD_DIR)/aufgabe_1.log \
	      $(BUILD_DIR)/aufgabe_1.out $(BUILD_DIR)/aufgabe_1.pdf

.PHONY: all bench run scaling divergence mandelbrot montecarlo assignment report clean
