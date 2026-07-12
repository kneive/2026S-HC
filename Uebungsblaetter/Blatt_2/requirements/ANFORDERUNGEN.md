# Anforderungen aus dem Aufgabenblatt HC02

Dieses Dokument fasst die Anforderungen zusammen, die aus dem Aufgabenblatt
abgeleitet werden koennen und die eine vollstaendige Loesung erfuellen muss.

## Aufgabe 1: SIMT-Ausfuehrungsmodell und Warp-Divergenz

### Ziel

- Sichtbar machen, dass eine GPU viele Threads im Gleichschritt nach dem
  SIMT-Modell ausfuehrt.
- Sichtbar machen, dass datenabhaengige Verzweigungen innerhalb eines Warps
  teuer sind.
- Den Unterschied zur CPU erklaeren, bei der dieselbe Art von Verzweigung
  deutlich weniger stoert.

### Kernel-Anforderungen

- Es muss ein rechenintensiver GPU-Kernel implementiert werden.
- Der Kernel soll minimalen Speicherbedarf haben.
- Jeder Thread muss genau ein Element bearbeiten.
- Die Arbeit pro Thread muss konfigurierbar sein.
- Die Last soll rein arithmetisch sein, zum Beispiel:
  - iterierte Polynom- oder Newton-Berechnung,
  - synthetisches ALU-Mischen ueber `k` Iterationen,
  - FMA- oder aehnliche Rechenketten.
- Der Kernel soll keine nennenswerten Speicherzugriffe enthalten.
- Das Ergebnis muss so verwendet oder geschrieben werden, dass der Compiler die
  Rechenschleife nicht wegoptimieren kann.

### Skalierungsanforderungen

- Die Problemgroesse `n` muss bis in den Millionenbereich skaliert werden.
- Der Durchsatz des Kernels muss gemessen werden, zum Beispiel:
  - Operationen pro Sekunde,
  - FLOP/s,
  - GFLOP/s.
- Die Skalierung ueber `n` muss dargestellt werden.
- Es muss festgehalten werden, ab welcher Problemgroesse die GPU "warmlaeuft".
- Es muss analysiert werden, wie viele Threads gestartet werden muessen, um das
  Geraet auszulasten.
- Es muss erklaert werden, warum dieser Auslastungsaufwand bei einer CPU in
  dieser Form nicht anfaellt.

### Warp-Divergenz-Anforderungen

- Es muss gezielt Warp-Divergenz eingefuehrt werden.
- Der Arbeitsaufwand pro Thread muss datenabhaengig variieren.
- Geeignete Varianten sind zum Beispiel:
  - Verzweigung ueber `threadIdx % k`,
  - Verzweigung ueber eine vom Element abhaengige Schleifenlaenge,
  - unterschiedliche Pfade mit unterschiedlicher Rechenlast.
- Der Divergenzgrad muss systematisch variiert werden.
- Die Variation muss den Bereich von "keine Divergenz" bis "jeder Thread im
  Warp nimmt einen anderen Pfad" abdecken.
- Fuer jeden Divergenzgrad muss die Gesamtperformanz gemessen werden.
- Der Performance-Einbruch durch Divergenz muss dargestellt und bewertet
  werden.

### Erklaerungsanforderungen

- Es muss erklaert werden, warum ein Warp divergente Pfade serialisiert.
- Es muss erklaert werden, dass ein Warp eine gemeinsame Instruktionsausfuehrung
  besitzt und Lanes je Pfad ueber Aktivitaetsmasken ein- oder ausgeschaltet
  werden.
- Es muss erklaert werden, warum die Laufzeiten der verschiedenen Pfade sich bei
  Divergenz addieren koennen.
- Es muss der Unterschied zur CPU erklaert werden:
  - Sprungvorhersage,
  - spekulative Ausfuehrung,
  - unabhaengige Kerne,
  - kein Warp mit gemeinsamem Kontrollfluss.
- Der SIMT-Unterschied gegenueber einer normalen CPU muss aus Messung und
  Erklaerung klar hervorgehen.

## Aufgabe 2: Speicherzugriff, Latenzverstecken und Bandbreite

### Ziel

- Sichtbar machen, dass eine GPU Speicherlatenz vor allem durch viele
  gleichzeitig lauffaehige Warps versteckt.
- Sichtbar machen, dass die GPU Speicherlatenz nicht primaer durch grosse
  Caches versteckt.
- Sichtbar machen, dass das Speicherzugriffsmuster ueber die tatsaechlich
  erreichte Bandbreite entscheidet.
- Den Unterschied zur CPU erklaeren, die Latenz stark ueber Cache-Hierarchie und
  Prefetching reduziert.

### Kernel-Anforderungen

- Es muss ein speicherintensiver Kernel implementiert werden.
- Der Kernel muss eine geringe arithmetische Intensitaet besitzen.
- Ein geeignetes Beispiel ist ein Streaming-Kernel, etwa:
  - `b[i] = a[i] * c`,
  - STREAM-Triad oder eine vergleichbare Operation.
- Die gemessene Laufzeit soll hauptsaechlich durch Speicherbandbreite und nicht
  durch Rechenleistung bestimmt werden.

### Zugriffsmuster-Anforderungen

Folgende drei Zugriffsmuster auf den Geraetespeicher muessen verglichen werden:

1. Zusammenhaengend/coalesced, also Stride 1.
2. Gestriped/strided, also Stride `k`.
3. Zufaellig, also Gather-Zugriffe.

Fuer jedes Zugriffsmuster muessen gemessen werden:

- erreichte effektive Bandbreite in GB/s,
- prozentuales Verhaeltnis zur maximal erreichbaren Bandbreite des Geraets.

Die maximale Bandbreite des Geraets muss ueber eine geeignete Geraeteabfrage
bestimmt oder nachvollziehbar angegeben werden, zum Beispiel ueber
`deviceQuery`, CUDA-Device-Properties oder eine entsprechende Hilfsfunktion.

### Occupancy-Anforderungen

- Es muss gezeigt werden, wie die GPU Speicherlatenz ueber Occupancy versteckt.
- Dazu muss die Blockgroesse oder die Anzahl gleichzeitig residenter Warps
  variiert werden.
- Fuer jede Variante muss der Durchsatz beziehungsweise die Bandbreite gemessen
  werden.
- Es muss gezeigt werden, dass genuegend lauffaehige Warps bereitstehen muessen,
  um Speicher-Stalls zu ueberbruecken.
- Die Occupancy muss angegeben werden.
- Occupancy ist gemaess Aufgabenblatt:

```text
Occupancy = aktive Warps / maximal moegliche aktive Warps
```

### Erklaerungsanforderungen

- Es muss erklaert werden, warum coalesced Zugriffe hohe Bandbreite erreichen.
- Es muss erklaert werden, warum strided Zugriffe schlechter werden.
- Es muss erklaert werden, warum zufaellige Gather-Zugriffe besonders teuer
  sind.
- Es muss erklaert werden, wie viele residente Warps Speicherlatenzen
  ueberdecken koennen.
- Es muss der Unterschied zwischen GPU- und CPU-Latenzstrategien erklaert
  werden:
  - GPU: Latenzverstecken durch massive Parallelitaet und Warp-Scheduling.
  - CPU: Latenzreduktion durch Cache-Hierarchie, Prefetching und starke
    Einzelkerne.
- Es muessen Konsequenzen fuer effiziente Datenanordnung in GPU-Anwendungen
  abgeleitet werden, zum Beispiel:
  - zusammenhaengende Speicherzugriffe bevorzugen,
  - Structure-of-Arrays statt Array-of-Structures,
  - Gather/Scatter vermeiden oder Daten vorher umordnen,
  - Alignment und Padding beachten,
  - ausreichend Parallelitaet bereitstellen.

## Allgemeine Abgabeanforderungen

### Bericht

- Im Repository muss ein kurzer Ergebnisbericht enthalten sein.
- Der Bericht muss die Messungen und Ergebnisse dokumentieren.
- Der Bericht muss die beobachteten Effekte erklaeren.
- Der Bericht muss die Reproduktion der Messungen ausreichend beschreiben.
