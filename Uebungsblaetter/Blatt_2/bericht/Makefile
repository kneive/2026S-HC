BUILD_DIR := ../build/bericht
REPORT_TEX := bericht.tex
REPORT_PDF := bericht.pdf

.PHONY: all aufgaben report clean

all: aufgaben report

$(BUILD_DIR):
	mkdir -p $@

aufgaben:
	$(MAKE) -C ../aufgabe_1 all
	$(MAKE) -C ../aufgabe_2 all

report: $(REPORT_TEX) | $(BUILD_DIR)
	latexmk -g -pdf -interaction=nonstopmode -output-directory=$(BUILD_DIR) $(REPORT_TEX)
	cp $(BUILD_DIR)/$(REPORT_PDF) $(REPORT_PDF)

clean:
	latexmk -C -output-directory=$(BUILD_DIR) $(REPORT_TEX)
	rm -f $(REPORT_PDF)
