#include "string.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define FREQUENCY_DECREMENT 500000
#define WAIT_TIME_AFTER_TEMP_DROP 3000
#define MAX_CORES 256
static int min_cpu_freq[MAX_CORES];
static int min_freq_initialized = 0;

volatile sig_atomic_t keep_running = 1;

#define RESET "\033[0m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN "\033[36m"
#define BOLD "\033[1m"

float get_current_cpu_temp(void) {
  FILE *temp_file = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
  if (temp_file == NULL) {
    perror(RED "[Error] Couldn't Read CPU Temperature" RESET);
    return -1;
  }
  float cpu_temp;
  fscanf(temp_file, "%f", &cpu_temp);
  fclose(temp_file);
  return cpu_temp / 1000.0;
}

int get_current_cpu_frequency(int cpu) {
  char frequency_file_path[256];
  snprintf(frequency_file_path, sizeof(frequency_file_path),
           "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);

  FILE *frequency_file = fopen(frequency_file_path, "r");
  if (frequency_file == NULL) {
    perror(RED "[Error] Couldn't Read CPU Frequency" RESET);
    return -1;
  }

  int current_max_frequency;
  if (fscanf(frequency_file, "%d", &current_max_frequency) != 1) {
    perror(RED "[Error] reading current max frequency" RESET);
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
    perror(RED "[Error] Couldn't Set Frequency" RESET);
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
    perror(RED "[Error] Couldn't read `cpuinfo_max_freq`" RESET);
    return -1;
  }
  int original_cpu_freq;
  if (fscanf(frequency_file, "%d", &original_cpu_freq) != 1) {
    perror(RED "[Error] Couldn't read original frequency" RESET);
    fclose(frequency_file);
    return -1;
  }

  fclose(frequency_file);
  return original_cpu_freq;
}

int get_min_cpu_frequency(int cpu) {
  char path[256];
  snprintf(path, sizeof(path),
           "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_min_freq", cpu);

  FILE *f = fopen(path, "r");
  if (!f)
    return -1;

  int min;
  if (fscanf(f, "%d", &min) != 1) {
    fclose(f);
    return -1;
  }
  fclose(f);
  return min;
}

void init_min_frequencies(int max_cores) {
  for (int cpu = 0; cpu < max_cores; cpu++) {
    min_cpu_freq[cpu] = get_min_cpu_frequency(cpu);
    if (min_cpu_freq[cpu] < 0) {
      min_cpu_freq[cpu] = 400000; // 400 MHz btw
    }
  }
  min_freq_initialized = 1;
}

int throttle(int temperature_limit, int max_attempts,
             int wait_time_after_throttle) {
  int max_cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);
  float current_cpu_temp = get_current_cpu_temp();
  int throttle_attempts = 0;

  if (!min_freq_initialized)
    init_min_frequencies(max_cpu_cores);

  while (current_cpu_temp > temperature_limit &&
         throttle_attempts < max_attempts && keep_running) {
    printf(CYAN "[INFO]" RESET " Throttle attempt %d/%d\n",
           throttle_attempts + 1, max_attempts);

    int last_from = -1, last_to = -1, start = 0;

    int hit_min_count = 0;

    for (int cpu = 0; cpu < max_cpu_cores; cpu++) {
      int cur = get_current_cpu_frequency(cpu);
      int newf = cur - FREQUENCY_DECREMENT;
      if (newf < min_cpu_freq[cpu]) {
        newf = min_cpu_freq[cpu];
        hit_min_count++;
      }

      if (cpu == 0) {
        last_from = cur;
        last_to = newf;
        start = 0;
      } else if (cur != last_from || newf != last_to) {
        printf(CYAN "[INFO]" RESET " CPUs %d–%d: Throttling from " BOLD
                    "%d" RESET " to " BOLD "%d Hz" RESET "\n",
               start, cpu - 1, last_from, last_to);
        start = cpu;
        last_from = cur;
        last_to = newf;
      }

      set_cpu_frequency(cpu, newf);
    }

    printf(CYAN "[INFO]" RESET " CPUs %d–%d: Throttling from " BOLD "%d" RESET
                " to " BOLD "%d Hz" RESET "\n",
           start, max_cpu_cores - 1, last_from, last_to);

    if (hit_min_count == max_cpu_cores) {
      printf(YELLOW "[WARN]" RESET
                    " All CPU cores reached MIN frequency limit. "
                    "Cannot throttle further safely.\n");
    }

    printf("\n");
    printf(CYAN "[INFO]" RESET
                " Waiting for %d seconds, Current temperature: " MAGENTA
                "%.2f°C" RESET "\n",
           wait_time_after_throttle, current_cpu_temp);
    sleep(wait_time_after_throttle);

    current_cpu_temp = get_current_cpu_temp();
    if (current_cpu_temp < 0) {
      printf(RED "[Error] reading CPU temperature.\n" RESET);
      return -1;
    }

    throttle_attempts++;
  }

  if (!keep_running)
    return -1;

  while (1) {
    current_cpu_temp = get_current_cpu_temp();
    if (current_cpu_temp > temperature_limit)
      break;

    printf(GREEN
           "[DONE]" RESET
           " Temperature is safe (%.2f°C). Monitoring... Ctrl+C to exit\n",
           current_cpu_temp);

    for (int i = 0; i < WAIT_TIME_AFTER_TEMP_DROP; i++) {
      if (!keep_running)
        return -1;

      current_cpu_temp = get_current_cpu_temp();
      if (current_cpu_temp > temperature_limit)
        break;

      sleep(5);
    }

    if (current_cpu_temp > temperature_limit)
      break;
  }

  current_cpu_temp = get_current_cpu_temp();
  if (current_cpu_temp >= temperature_limit &&
      max_attempts != throttle_attempts) {
    printf(CYAN "[INFO]" RESET
                " Temperature's still above limit. Continuing throttling\n");
    return throttle(temperature_limit, max_attempts, wait_time_after_throttle);
  }

  if (max_attempts == throttle_attempts) {
    printf(YELLOW "[WARN]" RESET
                  " Maximum attempts reached. Unthrottling and exiting\n");
    return 0;
  }

  return 0;
}

void unthrottle() {
  int max_cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);

  int last_freq = -1;
  int start = 0;

  for (int cpu = 0; cpu < max_cpu_cores; cpu++) {
    int orig = get_original_cpu_frequency(cpu);
    if (orig < 0)
      continue;

    if (cpu == 0) {
      last_freq = orig;
      start = 0;
    } else if (orig != last_freq) {
      printf(CYAN "[INFO]" RESET " CPUs %d–%d: Restoring to " BOLD "%d Hz" RESET
                  "\n",
             start, cpu - 1, last_freq);

      start = cpu;
      last_freq = orig;
    }

    set_cpu_frequency(cpu, orig);
  }

  printf(CYAN "[INFO]" RESET " CPUs %d–%d: Restoring to " BOLD "%d Hz" RESET
              "\n",
         start, max_cpu_cores - 1, last_freq);
}

void handle_sigint(int sig) {
  printf("\n" CYAN "[INFO]" RESET
         " Caught signal %d (Ctrl + C). Unthrottling and exiting\n",
         sig);
  unthrottle();
  exit(0);
}

void print_help() {
  printf("Arguments:\n");
  printf("  Temperature_Limit            : Maximum CPU temperature before "
         "throttling (1–110°C)\n");
  printf("  Max_Attempts                 : Number of times the program will "
         "attempt to throttle (default: 5)\n");
  printf("  Wait_Time_After_Throttle     : Wait time between throttle "
         "attempts, in seconds (default: 15)\n");
}

int main(int argc, char *argv[]) {
  signal(SIGINT, handle_sigint);

  if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
    printf("Usage: sudo %s <Temperature_Limit> <Max_Attempts> "
           "<Wait_Time_After_Throttle>\n",
           argv[0]);
    print_help();
    exit(0);
  }

  if (argc < 2) {
    printf(RED "[Error] Invalid arguments\n" RESET);
    printf("\nUsage: sudo %s <Temperature_Limit> <Max_Attempts> "
           "<Wait_Time_After_Throttle>\n",
           argv[0]);
    print_help();
    exit(1);
  }

  char *val;
  long temperature_limit = strtol(argv[1], &val, 10);

  if (*val != '\0' || temperature_limit <= 0 || temperature_limit >= 110) {
    printf(
        RED
        "[Error] Temperature limit must be between 1 and 109 degrees.\n" RESET);
    exit(1);
  }

  int wait_time_after_throttle = 15;
  int max_attempts = 5;
  if (argc >= 4) {
    char *end;

    long value = strtol(argv[2], &end, 10);
    if (*end != '\0' || value < 0) {
      fprintf(stderr, RED "[Error] Invalid max attempts: %s\n" RESET, argv[2]);
      exit(1);
    }
    max_attempts = (int)value;

    long wait_val = strtol(argv[3], &end, 10);
    if (*end != '\0' || wait_val <= 0) {
      fprintf(stderr, RED "[Error] Invalid wait time: %s\n" RESET, argv[3]);
      exit(1);
    }
    wait_time_after_throttle = (int)wait_val;
  }

  float current_cpu_temp = get_current_cpu_temp();
  if (current_cpu_temp < 0) {
    printf(RED "[Error] reading CPU temperature.\n" RESET);
    return 1;
  }

  if (current_cpu_temp > temperature_limit) {
    printf(CYAN "[INFO]" RESET " Starting Throttle\n");
    int success =
        throttle(temperature_limit, max_attempts, wait_time_after_throttle);
    if (!success && keep_running) {
      unthrottle();
    }
  } else {
    printf(CYAN "[INFO]" RESET " CPU Temperature is under set limit: " MAGENTA
                "%.2f°C" RESET "\n",
           current_cpu_temp);
  }

  return 0;
}
