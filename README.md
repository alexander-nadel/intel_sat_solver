# intel_sat_solver

This repository contains the code of Intel(R) SAT Solver (IntelSAT).

Compilation requires g++ version 10.1.0 or higher to compile. Use make as follows:

"make rs" for a statically linked release version.
"make d"  for a debug version (no optimizations).
"make"    for the standard version (optimized, but with debug information and assertions active)

When using IntelSAT, please refer to the following paper:

Alexander Nadel. Introducing Intel(R) SAT Solver. In SAT'22. To appear.

