# SW_NSYS_Plugin

An NVIDIA Nsight Systems plugin specifically designed for **H3P PCIe switch boxes**. It monitors and records switch utilization, throughput, error counters, and device temperature onto the Nsight Systems timeline.

## Build Requirements

- **CUDA Toolkit** (for NVTX headers, usually located at `/usr/local/cuda/include`)
- **libh3ppci** / **libph3ppci**: This plugin interfaces with the H3P PCIe switch using the `libh3ppci.so` shared library.
- **g++** and Make

## Building the Plugin

1. Be sure to check the `Makefile` to ensure it points to the correct location of your `libh3ppci.so` / `libh3ppci.a` library.
2. Before compiling, you must export the NVTX header paths:
   ```bash
   export NVTX_PATH="/opt/nvidia/nsight-systems/2026.1.1/target-linux-x64/nvtx/include"
   export NVTXEXT_PATH="/opt/nvidia/nsight-systems/2026.1.1/target-linux-x64/nvtx/include"
   ```
3. Run `make` to compile:
   ```bash
   make
   ```
   This generates the `h3_sw_counters` executable.

## Installation and Usage with Nsight Systems

Nsight Systems loads the plugin via the configuration provided in the `h3_sw_counters.yaml` manifest.

1. Ensure the `h3_sw_counters` executable was built successfully.
2. Set the `NSYS_PLUGIN_SEARCH_DIRS` environment variable to point to the `$(pwd)` subdirectory inside this repository. You can use the following command in the project root:
   ```bash
   export NSYS_PLUGIN_SEARCH_DIRS=$(pwd)
   ```
   *(Note: The variable must point to the folder containing the `h3_sw_counters.yaml` file)*
3. Run `nsys profile` and pass the `--enable` flag with the name of the plugin (`h3_sw_counters`):
   ```bash
   nsys profile --enable h3_sw_counters <your_target_application>
   ```

## Passing Arguments to the Plugin

When using the `--enable` flag, you can pass custom arguments to the `h3_sw_counters` (such as the module type or sampling interval). 

The syntax requires the plugin name followed by its arguments, all separated by **commas** (no spaces) inside double quotes:

```bash
nsys profile --enable "h3_sw_counters,<arg1>,<val1>,<arg2>,<val2>" <your_target_application>
```

### Examples:

- **Monitor Error Counters instead of Throughput:**
  ```bash
  nsys profile --enable "h3_sw_counters,-m,error" ./my_app
  ```

- **Set Sampling Interval to 500ms:**
  ```bash
  nsys profile --enable "h3_sw_counters,-t,500" ./my_app
  ```

- **Combine both (Error module + 500ms interval):**
  ```bash
  nsys profile --enable "h3_sw_counters,-m,error,-t,500,-p,0,32" ./my_app
  ```

- **Monitor Multiple Modules Simultaneously:**
  ```bash
  # Monitor throughput and temperature
  nsys profile --enable "h3_sw_counters,-m,throughput,temperature,-i,0,-p,0" ./my_app
  
  # Monitor everything (throughput, error, temperature)
  nsys profile --enable "h3_sw_counters,-m,all,-i,0,-p,0" ./my_app
  ```

- **Filter by Device Index (e.g., Device 0 only):**
  ```bash
  nsys profile --enable "h3_sw_counters,-i,0" ./my_app
  ```

These arguments are passed directly to the `h3_sw_counters` executable by Nsight Systems during the profiling session.
