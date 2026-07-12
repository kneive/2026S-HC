# HC 2026 Übung 02

## Kompilieren

Aus dem `root` Ordner:

```bash
make              # baut Aufgabe 1, Aufgabe 2 und den Gesamtbericht
make bericht      # wie make
make aufgabe_1    # nur Aufgabe 1 bauen
make aufgabe_2    # nur Aufgabe 2 bauen
```

Der Gesamtbericht liegt danach in:

```text
bericht/bericht.pdf
```

Build-Artefakte werden zentral unter `build/` abgelegt.

## Ordnerstruktur

```text
/
├── Makefile                 zentraler Einstiegspunkt
├── bericht/                 Gesamtbericht aus Aufgabe 1 und Aufgabe 2
├── build/                   zentrale Build-Ausgaben
├── aufgabe_1/               Code, Daten, Plots und LaTeX fuer Aufgabe 1
├── aufgabe_2/               Code, Daten, Plots und LaTeX fuer Aufgabe 2
├── requirements/            abgeleitete Anforderungen
└── additional_kernels/      nicht zentrale Zusatzkernel
```

In den Aufgabenordnern gilt jeweils:

```text
aufgabe_?
├──src/       CUDA-Quellcode
├──include/   Header
├──scripts/   Plot-Skripte
├──outputs/   CSV, PNG und PDF-Ausgaben
├──tex/       LaTeX-Wrapper und Body-Dateien
└──docs/      kurze Markdown-Berichte
```

## Implementierte Kernel

Aufgabe 1:

- `kernel_fma`: compute-bound FMA-Kernel zur Skalierung ueber die Threadzahl.
- `kernel_fma_div`: FMA-Kernel mit steuerbarer Warp-Divergenz ueber `stride`.
- `kernel_mandelbrot`: natuerliche Divergenz durch datenabhaengige Iterationen.
- `kernel_mc_pi`: Monte-Carlo-Pi mit zufaelliger Divergenz und Reduktion.

Aufgabe 2:

- `kernel_copy_coalesced`: zusammenhaengende Speicherzugriffe.
- `kernel_copy_strided`: strided Speicherzugriffe mit schlechterem Coalescing.
- `kernel_copy_gather`: zufaellige Gather-Zugriffe.
- `kernel_stream_triad`: STREAM-Triad als Bandbreiten-Referenzfall.

## Ergebnisdateien

```text
bericht/bericht.pdf
aufgabe_1/outputs/aufgabe_1.pdf
aufgabe_2/outputs/aufgabe_2.pdf
```
