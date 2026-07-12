// bench.cu - Treiber fuer Aufgabe 2:
// Speicherzugriffsmuster, effektive Bandbreite und Occupancy.

#include "common.cuh"
#include "memory.cuh"
#include "stream.cuh"
#include <cstdlib>
#include <cstring>

enum class Cmd { Default, Mem, Stream, SweepPat, SweepOcc, Unknown };

static Cmd parse_cmd(const char* s) {
    if (!strcmp(s, "mem")) return Cmd::Mem;
    if (!strcmp(s, "stream")) return Cmd::Stream;
    if (!strcmp(s, "sweeppat")) return Cmd::SweepPat;
    if (!strcmp(s, "sweepocc")) return Cmd::SweepOcc;
    return Cmd::Unknown;
}

static int arg(int argc, char** argv, int i, int def) {
    return argc > i ? atoi(argv[i]) : def;
}

int main(int argc, char** argv) {
    print_gpu_info();

    Cmd cmd = (argc == 1) ? Cmd::Default : parse_cmd(argv[1]);

    switch (cmd) {
        case Cmd::Default:
            memory_run(1 << 25, 32, 30);
            break;

        case Cmd::Mem:
            memory_run(arg(argc, argv, 2, 1 << 25),
                       arg(argc, argv, 3, 32),
                       arg(argc, argv, 4, 30));
            break;

        case Cmd::Stream:
            stream_run(arg(argc, argv, 2, 1 << 26),
                       arg(argc, argv, 3, 20));
            break;

        case Cmd::SweepPat:
            memory_run_patterns_csv(argc > 2 ? argv[2] : "patterns.csv");
            break;

        case Cmd::SweepOcc:
            memory_run_occupancy_csv(argc > 2 ? argv[2] : "occupancy.csv");
            break;

        case Cmd::Unknown:
        default:
            fprintf(stderr,
                    "Usage: %s [mem n stride reps | stream n reps | "
                    "sweeppat file.csv | sweepocc file.csv]\n", argv[0]);
            return 1;
    }

    return 0;
}
