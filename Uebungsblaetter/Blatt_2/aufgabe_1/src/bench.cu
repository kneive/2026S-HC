// bench.cu - Treiber fuer Aufgabe 1:
// rechenintensiver FMA-Kernel, n-Skalierung und Warp-Divergenz.

#include "common.cuh"
#include "fma.cuh"
#include "mandelbrot.cuh"
#include "montecarlo.cuh"
#include <cstdlib>
#include <cstring>

enum class Cmd {
    Default, Fma, Div, Mand, Mc, SweepN, SweepDiv,
    SweepMand, MandImg, SweepMc, McPoints, Unknown
};

static Cmd parse_cmd(const char* s) {
    if (!strcmp(s, "fma")) return Cmd::Fma;
    if (!strcmp(s, "div")) return Cmd::Div;
    if (!strcmp(s, "mand")) return Cmd::Mand;
    if (!strcmp(s, "mc")) return Cmd::Mc;
    if (!strcmp(s, "sweepn")) return Cmd::SweepN;
    if (!strcmp(s, "sweepdiv")) return Cmd::SweepDiv;
    if (!strcmp(s, "sweepmand")) return Cmd::SweepMand;
    if (!strcmp(s, "mandimg")) return Cmd::MandImg;
    if (!strcmp(s, "sweepmc")) return Cmd::SweepMc;
    if (!strcmp(s, "mcpoints")) return Cmd::McPoints;
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
            fma_run(1 << 26, 1024, 20);
            fma_run_div(1 << 24, 4096, 1, 20);
            mandelbrot_run(4096, 4096, 1000, 10);
            montecarlo_run(1 << 20, 4096, 10);
            break;

        case Cmd::Fma:
            fma_run(arg(argc, argv, 2, 1 << 26),
                    arg(argc, argv, 3, 1024),
                    arg(argc, argv, 4, 20));
            break;

        case Cmd::Div:
            fma_run_div(arg(argc, argv, 2, 1 << 24),
                        arg(argc, argv, 3, 4096),
                        arg(argc, argv, 4, 1),
                        arg(argc, argv, 5, 20));
            break;

        case Cmd::Mand:
            mandelbrot_run(arg(argc, argv, 2, 4096),
                           arg(argc, argv, 3, 4096),
                           arg(argc, argv, 4, 1000),
                           arg(argc, argv, 5, 10));
            break;

        case Cmd::Mc:
            montecarlo_run(arg(argc, argv, 2, 1 << 20),
                           arg(argc, argv, 3, 4096),
                           arg(argc, argv, 4, 10));
            break;

        case Cmd::SweepN:
            fma_run_sweepn(argc > 2 ? argv[2] : "scaling.csv");
            break;

        case Cmd::SweepDiv:
            fma_run_sweepdiv(argc > 2 ? argv[2] : "divergence.csv");
            break;

        case Cmd::SweepMand:
            mandelbrot_run_sweep(argc > 2 ? argv[2] : "mandelbrot.csv");
            break;

        case Cmd::MandImg:
            mandelbrot_write_image(argc > 2 ? argv[2] : "mandelbrot.pgm",
                                   arg(argc, argv, 3, 1600),
                                   arg(argc, argv, 4, 1200),
                                   arg(argc, argv, 5, 1000));
            break;

        case Cmd::SweepMc:
            montecarlo_run_sweep(argc > 2 ? argv[2] : "montecarlo.csv");
            break;

        case Cmd::McPoints:
            montecarlo_write_points(argc > 2 ? argv[2] : "montecarlo_points.csv",
                                    arg(argc, argv, 3, 20000));
            break;

        case Cmd::Unknown:
        default:
            fprintf(stderr,
                    "Usage: %s [fma n iters reps | div n iters stride reps | "
                    "mand w h maxIter reps | mc n samples reps | "
                    "sweepn file.csv | sweepdiv file.csv | "
                    "sweepmand file.csv | mandimg file.pgm w h maxIter | "
                    "sweepmc file.csv | mcpoints file.csv n]\n", argv[0]);
            return 1;
    }

    return 0;
}
