# CPU Temperature Monitor

A C program that monitors CPU temperature and automatically throttles CPU frequency to prevent overheating on Linux systems. This tool provides a simple yet effective way to manage CPU temperatures through frequency control.

## Description

This program continuously monitors the CPU temperature and automatically reduces the CPU frequency when the temperature exceeds a user-defined limit. It will attempt to throttle the CPU multiple times if necessary and restore original frequencies when temperatures return to safe levels.

## Features

- Real-time CPU temperature monitoring
- Automatic CPU frequency throttling
- Multiple throttling attempts with configurable parameters
- Graceful restoration of original CPU frequencies
- Support for multi-core processors
- Signal handling for clean program termination

## Prerequisites

- Linux operating system
- GCC compiler
- Root privileges (required for CPU frequency control)
- Access to system files:
  - `/sys/class/thermal/thermal_zone0/temp`
  - `/sys/devices/system/cpu/cpu*/cpufreq/`

## Installation

1. Clone the repository or download the source code
2. Compile the program:
   ```bash
   gcc -o cpu_throttle cpu_throttle.c
   ```

## Usage

Run the program with root privileges and specify the temperature limit in Celsius:

```bash
sudo ./cpu_throttle <Temperature_Limit>
```

Example:
```bash
sudo ./cpu_throttle 80    # Sets temperature limit to 80°C
```

## Configuration Constants

The program includes several configurable parameters:

```c
#define FREQUENCY_DECREMENT 500000      // Frequency reduction step (Hz)
#define WAIT_TIME_AFTER_THROTTLE 15     // Wait time after throttling (seconds)
#define WAIT_TIME_AFTER_TEMP_DROP 3000  // Wait time after temperature drops (seconds)
#define MAX_ATTEMPTS 5                  // Maximum throttling attempts
```

## How It Works

1. The program reads the current CPU temperature from the system
2. If the temperature exceeds the specified limit:
   - Reduces the maximum frequency for all CPU cores
   - Waits for a specified time period
   - Checks the temperature again
3. If the temperature is still high after maximum attempts:
   - Restores original frequencies
   - Exits the program
4. Handles Ctrl+C (SIGINT) gracefully by restoring original frequencies

## Functions

- `get_current_cpu_temp()`: Reads current CPU temperature
- `get_current_cpu_frequency()`: Gets current maximum frequency for a CPU core
- `set_cpu_frequency()`: Sets new maximum frequency for a CPU core
- `get_original_cpu_frequency()`: Retrieves the original maximum frequency
- `throttle()`: Implements the main throttling logic
- `unthrottle()`: Restores original CPU frequencies
- `handle_sigint()`: Handles interrupt signals

## Error Handling

The program includes comprehensive error handling for:
- File operations
- Temperature and frequency reading/writing
- Invalid user input
- System calls

## Notes

- Requires root privileges due to system file access
- Temperature readings are in Celsius
- Frequency values are in Hertz
- The program continuously monitors temperature until interrupted
- Use Ctrl+C to safely exit and restore original frequencies

## Safety Features

- Maximum attempt limit to prevent excessive throttling
- Graceful shutdown with signal handling
- Automatic restoration of original frequencies
- Temperature verification after throttling

## License

This project is open source and available under the MIT License.


---
Note: Always monitor your system when testing new temperature management tools.
