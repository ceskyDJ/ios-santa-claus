#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/shm.h>

// No valid input
#define BAD_INPUT -1

// Configurations from input arguments
typedef struct configs {
    int elf_num;          // Number of elves
    int reindeer_num;     // Number of reindeer
    int elf_work;         // Maximum time of individual elf's work (in ms)
    int reindeer_holiday; // Maximum time of reindeer's holiday (in ms)
} configs_t;

// Shared data between all processes
typedef struct shared_data {
    int process_num;    // Number of created (child) processes
} shared_data_t;

// ID for elves and reindeer
int id;

/**
 * Parses provided input argument
 * @param input_arg Input argument to parse
 * @param min The smallest allowed number
 * @param max The largest allowed number
 * @return Parsed input argument or -1 if the argument is not valid
 */
int parse_input_arg(char *input_arg, int min, int max);
/**
 * Loads configurations from input arguments
 * @param configs Pointer to the structure to fill with loaded configurations
 * @param input_args Array of input arguments
 * @return true => success, false => problems with input arguments
 */
bool load_configurations(configs_t *configs, char **input_args);
/**
 * Creates Santa process
 * @param log_file Log file where every action is logged to
 * @return true => success, false => problems with process creating
 */
bool spawn_santa(FILE *log_file);
/**
 * Creates elf processes
 * @param elf_num Number of elves to create
 * @param elf_work Maximum time of elf's individual work
 * @param log_file Log file where every action is logged to
 * @return true => success, false => problems with process creating
 */
bool spawn_elves(int elf_num, int elf_work, FILE *log_file);
/**
 * Creates reindeer processes
 * @param reindeer_num Number of reindeer to create
 * @param reindeer_holiday Maximum reindeer's holiday time
 * @param log_file Log file where every action is logged to
 * @return true => success, false => problems with process creating
 */
bool spawn_reindeer(int reindeer_num, int reindeer_holiday, FILE *log_file);

/**
 * Program for simulating Santa Claus live
 * @param argc Number of input arguments (5 required)
 * @param argv Input arguments
 * @return Exit code (0 => success, 1 => error)
 */
int main(int argc, char *argv[]) {
    // Program need 4 explicit arguments (+ 1 implicit): NE NR TE TR
    if (argc < 5) {
        printf("Too few input arguments\n");

        return 1;
    }

    // Load configurations from input arguments
    configs_t configs;
    if (!load_configurations(&configs, argv)) {
        printf( "Invalid input argument(s)\n");

        return 1;
    }

    // Open file for logging actions
    FILE *log_file;
    if ((log_file = fopen("proj2.out", "w")) == NULL) {
        printf("Cannot open log file\n");

        return 1;
    }

    // Set unbuffered mode to the log file (every output will be written immediately)
    if (setvbuf(log_file, NULL, _IONBF, 0) != 0) {
        printf("Cannot set log file to unbuffered mode\n");

        return 1;
    }

    // Prepare shared memory
    int shared_mem_id;
    if ((shared_mem_id = shmget(IPC_PRIVATE, sizeof(shared_data_t), 0)) == -1) {
        printf("Cannot get shared memory\n");

        fclose(log_file);
        return 1;
    }

    // Create needed processes
    if (!spawn_santa(log_file)) {
        printf("Cannot create process for Santa\n");

        shmctl(shared_mem_id, IPC_RMID, 0);
        fclose(log_file);
        return 1;
    }
    if (!spawn_elves(configs.elf_num, configs.elf_work, log_file)) {
        printf("Cannot create process for elf\n");

        // Terminate Santa process
        kill(-1, SIGKILL);

        shmctl(shared_mem_id, IPC_RMID, 0);
        fclose(log_file);
        return 1;
    }
    if (!spawn_reindeer(configs.reindeer_num, configs.reindeer_holiday, log_file)) {
        printf("Cannot create process for reindeer\n");

        // Terminate already created processes
        kill(-1, SIGKILL);

        shmctl(shared_mem_id, IPC_RMID, 0);
        fclose(log_file);
        return 1;
    }

    shmctl(shared_mem_id, IPC_RMID, 0);
    fclose(log_file);
    return 0;
}

/**
 * Parses provided input argument
 * @param input_arg Input argument to parse
 * @param min The smallest allowed number
 * @param max The largest allowed number
 * @return Parsed input argument or BAD_INPUT if the argument is not valid
 */
int parse_input_arg(char *input_arg, int min, int max) {
    // Check if the argument is numeric-only
    for (int i = 0; i < (int)strlen(input_arg); i++) {
        if (!isdigit(input_arg[i])) {
            return BAD_INPUT;
        }
    }

    // Try to convert string to number
    int output;
    if ((output = (int)strtol(input_arg, NULL, 10)) == 0 && errno == EINVAL) {
        return BAD_INPUT;
    }

    // Check limits
    if (output < min || output > max) {
        return BAD_INPUT;
    }

    return output;
}

/**
 * Loads configurations from input arguments
 * @param configs Pointer to the structure to fill with loaded configurations
 * @param input_args Array of input arguments
 * @return true => success, false => problems with input arguments
 */
bool load_configurations(configs_t *configs, char **input_args) {
    if ((configs->elf_num = parse_input_arg(input_args[1], 1, 1000)) == BAD_INPUT) {
        return false;
    }
    if ((configs->reindeer_num = parse_input_arg(input_args[2], 1, 19)) == BAD_INPUT) {
        return false;
    }
    if ((configs->elf_work = parse_input_arg(input_args[3], 0, 1000)) == BAD_INPUT) {
        return false;
    }
    if ((configs->reindeer_holiday = parse_input_arg(input_args[4], 0, 1000)) == BAD_INPUT) {
        return false;
    }

    return true;
}

/**
 * Creates Santa process
 * @param log_file Log file where every action is logged to
 * @return true => success, false => problems with process creating
 */
bool spawn_santa(FILE *log_file) {
    // Create a new (child) process by dividing the main process into two processes
    pid_t ppid = fork();
    if (ppid == -1) {
        // Error while creating the process (child process hasn't been created)
        return false;
    } else if (ppid == 0) {
        // Process has been successfully created --> this is code for the new (child) process
        fprintf(log_file, "A: Santa: going to sleep\n"); // TODO: Replace "A" with action number

        // Child process is done
        exit(0);
    } else {
        // Process has been successfully created --> this is code for original (main) process
    }

    return true;
}

/**
 * Creates elf processes
 * @param elf_num Number of elves to create
 * @param elf_work Maximum time of elf's individual work
 * @param log_file Log file where every action is logged to
 * @return true => success, false => problems with process creating
 */
bool spawn_elves(int elf_num, int elf_work, FILE *log_file) {
    (void)elf_work; // TODO: remove, only for simulating some action with the parameter

    for (int i = 0; i < elf_num; i++) {
        // Create a new (child) process by dividing the main process into two processes
        pid_t ppid = fork();
        if (ppid == -1) {
            // Error while creating the process (child process hasn't been created)
            return false;
        } else if (ppid == 0) {
            // Process has been successfully created --> this is code for the new (child) process

            // Set identifier
            id = i + 1;

            // Notify about start working action
            fprintf(log_file, "A: Elf %d: started\n", id); // TODO: Replace "A" with action number

            // Child process is done
            exit(0);
        } else {
            // Process has been successfully created --> this is code for original (main) process
        }
    }

    return true;
}

/**
 * Creates reindeer processes
 * @param reindeer_num Number of reindeer to create
 * @param reindeer_holiday Maximum reindeer's holiday time
 * @param log_file Log file where every action is logged to
 * @return true => success, false => problems with process creating
 */
bool spawn_reindeer(int reindeer_num, int reindeer_holiday, FILE *log_file) {
    (void)reindeer_holiday; // TODO: remove, only for simulating some action with the parameter

    for (int i = 0; i < reindeer_num; i++) {
        // Create a new (child) process by dividing the main process into two processes
        pid_t ppid = fork();
        if (ppid == -1) {
            // Error while creating the process (child process hasn't been created)
            return false;
        } else if (ppid == 0) {
            // Process has been successfully created --> this is code for the new (child) process

            // Set identifier
            id = i + 1;

            // Notify about start working action
            fprintf(log_file, "A: RD %d: rstarted\n", id); // TODO: Replace "A" with action number

            // Child process is done
            exit(0);
        } else {
            // Process has been successfully created --> this is code for original (main) process
        }
    }

    return true;
}
