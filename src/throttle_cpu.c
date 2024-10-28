#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FREQUENCY_DECREMENT 500000
#define WAIT_TIME_AFTER_THROTTLE 15
#define WAIT_TIME_AFTER_TEMP_DROP 30
#define MAX_ATTEMPTS 5

volatile sig_atomic_t keep_running = 1;

float get_current_cpu_temp(void) {
  FILE *temp_file = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
  if (temp_file == NULL) {
    perror("Error: Couldn't Read CPU Temperature");
    return -1;
  }
  float cpu_temp;
  fscanf(temp_file, "%f", &cpu_temp);
  fclose(temp_file);
  return cpu_temp / 1000.00;
}

int get_current_cpu_frequency(int cpu) {
  char frequency_file_path[256];
  snprintf(frequency_file_path, sizeof(frequency_file_path),
           "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);

  FILE *frequency_file = fopen(frequency_file_path, "r");
  if (frequency_file == NULL) {
    perror("Error: Couldn't Read CPU Frequency");
    return -1;
  }

  int current_max_frequency;
  if (fscanf(frequency_file, "%d", &current_max_frequency) != 1) {
    perror("Error: reading current max frequency");
    fclose(frequency_file);
    return -1;
  }

  fclose(frequency_file);
  return current_max_frequency;
}

int set_cpu_frequency(int cpu, int frequency) {
  char frequency_file_path[256];
  snprintf(frequency_file_path, sizeof(frequency_file_path),
           "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);

  FILE *frequency_file = fopen(frequency_file_path, "w");
  if (frequency_file == NULL) {
    perror("Error: Couldn't Set Frequency");
    return -1;
  }

  fprintf(frequency_file, "%d", frequency);
  fclose(frequency_file);
  return 0;
}

int get_original_cpu_frequency(int cpu) {
  char original_frequency_file_path[256];
  snprintf(original_frequency_file_path, sizeof(original_frequency_file_path),
           "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
  FILE *frequency_file = fopen(original_frequency_file_path, "r");

  if (frequency_file == NULL) {
    perror("Error: Couldn't read `cpuinfo_max_freq`");
    return -1;
  }
  int original_cpu_freq;
  if (fscanf(frequency_file, "%d", &original_cpu_freq) != 1) {
    perror("Error: Couldn't read original frequency");
    fclose(frequency_file);
    return -1;
  }

  fclose(frequency_file);
  return original_cpu_freq;
}

int throttle(int temperature_limit) {
  int max_cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);
  float current_cpu_temp = get_current_cpu_temp();
  int throttle_attempts = 0;

  while (current_cpu_temp > temperature_limit &&
         throttle_attempts < MAX_ATTEMPTS && keep_running) {
    printf("[*] Throttle attempt %d/%d\n", throttle_attempts + 1, MAX_ATTEMPTS);

    for (int cpu = 0; cpu < max_cpu_cores; cpu++) {
      int current_max_frequency = get_current_cpu_frequency(cpu);
      if (current_max_frequency < 0) {
        continue;
      }

      int new_frequency = current_max_frequency - FREQUENCY_DECREMENT;
      if (new_frequency < 0) {
        new_frequency = 0;
      }

      printf("CPU%d: Throttling frequency from %d Hz to %d Hz\n", cpu,
             current_max_frequency, new_frequency);
      if (set_cpu_frequency(cpu, new_frequency) < 0) {
        continue;
      }
    }

    printf("[*] Waiting for %d seconds, Current temperature: %f°C...\n",
           WAIT_TIME_AFTER_THROTTLE, current_cpu_temp);
    sleep(WAIT_TIME_AFTER_THROTTLE);

    current_cpu_temp = get_current_cpu_temp();
    if (current_cpu_temp < 0) {
      printf("Error: reading CPU temperature.\n");
      return -1;
    }

    throttle_attempts++;
  }

  if (!keep_running) {
    return -1; // If Ctrl + C is pressed, exit the throttle loop
  }

  if (current_cpu_temp <= temperature_limit) {
    printf("[*] Temperature is below the limit. Waiting for %d seconds to "
           "confirm...\n",
           WAIT_TIME_AFTER_TEMP_DROP);
    sleep(WAIT_TIME_AFTER_TEMP_DROP);
    current_cpu_temp = get_current_cpu_temp();

    if (current_cpu_temp <= temperature_limit) {
      printf("[*] Temperature is stable. Unthrottling...\n");
      return 1;
    } else {
      printf("[*] Temperature's still above limit. Continuing throttling...\n");
      return throttle(temperature_limit);
    }
  } else {
    printf("[!] Maximum attempts reached. Unthrottling and exiting...\n");
    return 0;
  }
}

void unthrottle() {
  int max_cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);

  for (int cpu = 0; cpu < max_cpu_cores; cpu++) {
    int original_frequency = get_original_cpu_frequency(cpu);
    if (original_frequency < 0) {
      continue;
    }
    printf("[*] CPU%d: Restoring frequency to original %d Hz\n", cpu,
           original_frequency);

    if (set_cpu_frequency(cpu, original_frequency) < 0) {
      continue;
    }
  }
}

void handle_sigint(int sig) {
  printf("\n[*] Caught signal %d (Ctrl + C). Unthrottling and exiting...\n",
         sig);
  unthrottle();
  exit(0);
}

int main(int argc, char *argv[]) {
  signal(SIGINT, handle_sigint);

  if (argc != 2) {
    printf("Usage: sudo %s <Temperature_Limit>\n", argv[0]);
    exit(1);
  }

  int temperature_limit = atoi(argv[1]);
  if (temperature_limit == 0 && argv[1][0] != '0') {
    printf("Invalid input. Please provide a valid integer.\n");
    exit(1);
  }

  float current_cpu_temp = get_current_cpu_temp();
  if (current_cpu_temp < 0) {
    printf("Error: reading CPU temperature.\n");
    return 1;
  }

  if (current_cpu_temp > temperature_limit) {
    printf("[*] Starting Throttle...\n");
    int success = throttle(temperature_limit);
    if (!success && keep_running) {
      printf("[!] Throttling failed after max attempts. Unthrottling...\n");
      unthrottle();
    }
    unthrottle();
  } else {
    printf("[*] CPU Temperature is under set limit: %.2f°C\n",
           current_cpu_temp);
  }

  return 0;
}
