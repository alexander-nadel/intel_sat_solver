## IntelSAT Testing and Debugging Scripts

This directory contains scripts for testing, fuzzing, and delta-debugging IntelSAT.

### Usage  
Run any script without parameters to see its usage instructions.

### Available Scripts  

- **`run_and_verify_intel_sat.csh`**  
  Runs and verifies IntelSAT on a given instance (including incremental instances).  
  Every generated clause and the solution (if any) are verified.  

- **`run_and_verify_intel_sat_on_regression.csh`**  
  Runs and verifies IntelSAT on the regression instances located in `regression_instances`.  

- **`delta_debug_intel_sat.csh`**  
  Performs delta debugging on IntelSAT in case of a failure (i.e., finds a small instance where the failure still occurs).  

- **`delta_debug_intel_sat_till_fixed_point.csh`**  
  Runs `delta_debug_intel_sat.csh` iteratively until a fixed point is reached.  

- **`fuzz_and_verify.csh`**  
  Fuzzes incremental instances and verifies IntelSAT.  

- **`fuzz_and_verify_parallel.csh`**  
  Fuzzes incremental instances and verifies IntelSAT in parallel.  
