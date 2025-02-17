# Intel(R) SAT Solver

This repository contains the source code for the Intel(R) SAT Solver (IntelSAT).

## Compilation

Compilation requires g++ version 10.1.0 or higher.

- To compile the IntelSAT executable in Release mode, run:

 make rs

- To compile the IntelSAT library in Release mode, run:

 make libr

- For additional compilation options, please refer to the Makefile.

## Scripts

The `scripts` directory contains useful scripts for verification, fuzzing, and delta-debugging of IntelSAT. Please refer to `scripts/README` for further details.

## Third-Party Tools

The `third_party` directory includes third-party tools used by the validation scripts mentioned above. For more details, see:
- `third_party/README`
- `third_party/LICENSE`

## Citation

If you use IntelSAT in your work, please cite the following paper:

Alexander Nadel. "Introducing Intel(R) SAT Solver", SAT'22.
