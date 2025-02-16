# intel_sat_solver

This repository contains the code of Intel(R) SAT Solver (IntelSAT).

Compilation requires g++ version 10.1.0 or higher. 

To compile IntelSAT executable in Release mode, run make as follows: "make rs". To compile IntelSAT library in Release mode, run make as follows: "make libr". For other compilation options, please refer to Makefile.

When using IntelSAT, please refer to the following paper:

Alexander Nadel. "Introducing Intel(R) SAT Solver", SAT'22.

The "scripts" directory contains useful scripts for verifying, fuzzying and delta-debugging IntelSAT. Please refer to "scripts/README" for further details.
The "third_party" directory contains third-party tools applied by the scripts in "scripts" directory for verifying, fuzzying and delta-debugging IntelSAT. Please refer to "third-party/README" and "third-party/LICENSE" for further details.

