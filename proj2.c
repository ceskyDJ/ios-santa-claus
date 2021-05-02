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
#include <sys/time.h>

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
    // Semaphore for creating barrier for main process - it must wait for every child process to complete
    sem_t main_barrier_sem;
    // Semaphore for process process counting critical section (manipulating with ended_processes in shared_data)
    sem_t end_process_counting_sem;
    // Semaphore for process numbering critical section (manipulating with process_num in shared_data)
    sem_t numbering_sem;
    // Semaphore for counting reindeer critical section (manipulating with reindeer_home_num in shared_data)
    sem_t reindeer_counting_sem;
    // Semaphore for creating barrier for Christmas start - Santa must wait for all reindeer are at home
    sem_t xmas_barrier_sem;

    // Number of created (child) processes
    int process_num;
    // Number of ended (done) child processes
    int ended_processes;
    // Number of reindeer at home (back from holiday)
    int reindeer_home_num;
} shared_data_t;

// ID for elves and reindeer
int id;

// Help functions
/**
 * Parses provided input argument
 * @param input_arg Input argument to parse
 * @param min The smallest allowed number
 * @param max The largest allowed number
 * @return Parsed input argument or -1 if the argument is not valid
 */
int parse_input_arg(char *input_arg, int min, int max);

// Initialization functions
/**
 * Loads configurations from input arguments
 * @param configs Pointer to the structure to fill with loaded configurations
 * @param input_args Array of input arguments
 * @return true => success, false => problems with input arguments
 */
bool load_configurations(configs_t *configs, char **input_args);
/**
 * Prepares all required semaphores
 * Created semaphores can be safely destroyed by terminate_semaphores() function
 * <strong>Caution: After modifying this function the terminate_semaphores() function must be updated</strong>
 * @param shared_data Pointer to the shared data where to store semaphores
 * @return 0 => success, 1 => error while creating one of the semaphores
 */
bool prepare_semaphores(shared_data_t *shared_data);
/**
 * Destroys all semaphores created by prepare_semaphores() function
 * <strong>Caution: It needs to be updated after adding a new semaphore into prepare_semaphore()</strong>
 * @param shared_data Pointer to the shared data where to store semaphores
 */
void terminate_semaphores(shared_data_t *shared_data);

// Work with child processes
/**
 * Creates Santa process
 * @param configs Process configurations
 * @param log_file Log file where every action is logged to
 * @param shared_mem_id Identification of shared memory block
 * @return true => success, false => problems with process creating
 */
bool spawn_santa(configs_t *configs, FILE *log_file, int shared_mem_id);
/**
 * Creates elf processes
 * @param configs Process configurations
 * @param log_file Log file where every action is logged to
 * @param shared_mem_id Identification of shared memory block
 * @return true => success, false => problems with process creating
 */
bool spawn_elves(configs_t *configs, FILE *log_file, int shared_mem_id);
/**
 * Creates reindeer processes
 * @param configs Process configurations
 * @param log_file Log file where every action is logged to
 * @param shared_mem_id Identification of shared memory block
 * @return true => success, false => problems with process creating
 */
bool spawn_reindeer(configs_t *configs, FILE *log_file, int shared_mem_id);

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
    if ((shared_mem_id = shmget(IPC_PRIVATE, sizeof(shared_data_t), 0644 | IPC_CREAT)) == -1) {
        printf("Cannot get shared memory\n");

        fclose(log_file);
        return 1;
    }

    // Attach shared memory
    shared_data_t *shared_data;
    if ((shared_data = shmat(shared_mem_id, NULL, 0)) == (void *)-1) {
        printf("Cannot attach shared memory\n");

        shmctl(shared_mem_id, IPC_RMID, 0);
        fclose(log_file);
        return 1;
    }

    // Prepare semaphores
    if (!prepare_semaphores(shared_data)) {
        printf("Cannot create one of the semaphores\n");

        shmdt(shared_data);
        shmctl(shared_mem_id, IPC_RMID, 0);
        fclose(log_file);
        return 1;
    }

    // Create needed processes
    if (!spawn_santa(&configs, log_file, shared_mem_id)) {
        printf("Cannot create process for Santa\n");

        terminate_semaphores(shared_data);
        shmdt(shared_data);
        shmctl(shared_mem_id, IPC_RMID, 0);
        fclose(log_file);
        return 1;
    }
    if (!spawn_elves(&configs, log_file, shared_mem_id)) {
        printf("Cannot create process for elf\n");

        // Terminate Santa process
        kill(-1, SIGKILL);

        terminate_semaphores(shared_data);
        shmdt(shared_data);
        shmctl(shared_mem_id, IPC_RMID, 0);
        fclose(log_file);
        return 1;
    }
    if (!spawn_reindeer(&configs, log_file, shared_mem_id)) {
        printf("Cannot create process for reindeer\n");

        // Terminate already created processes
        kill(-1, SIGKILL);

        terminate_semaphores(shared_data);
        shmdt(shared_data);
        shmctl(shared_mem_id, IPC_RMID, 0);
        fclose(log_file);
        return 1;
    }

    // Main process can end only if all child processes have ended
    sem_wait(&shared_data->main_barrier_sem);

    terminate_semaphores(shared_data);
    shmdt(shared_data);
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
 * Prepares all required semaphores
 * Created semaphores can be safely destroyed by terminate_semaphores() function
 * <strong>Caution: After modifying this function the terminate_semaphores() function must be updated</strong>
 * @param shared_data Pointer to the shared data where to store semaphores
 * @return 0 => success, 1 => error while creating one of the semaphores
 */
bool prepare_semaphores(shared_data_t *shared_data) {
    // Init semaphore for process numbering
    if ((sem_init(&shared_data->numbering_sem, 1, 1)) == -1) {
        return false;
    }

    // Init semaphore for blocking main process until all child processes are done
    if ((sem_init(&shared_data->main_barrier_sem, 1, 0)) == -1) {
        // Numbering semaphore is already created, it needs to be destroyed
        sem_destroy(&shared_data->numbering_sem);
        return false;
    }

    // Init semaphore for counting ended processes
    if ((sem_init(&shared_data->end_process_counting_sem, 1, 0)) == -1) {
        // Previous semaphores are already created, they need to be destroyed
        sem_destroy(&shared_data->main_barrier_sem);
        sem_destroy(&shared_data->numbering_sem);
        return false;
    }

    // Init semaphore for counting reindeer at home
    if ((sem_init(&shared_data->reindeer_counting_sem, 1, 1)) == -1) {
        // Previous semaphores are already created, they need to be destroyed
        sem_destroy(&shared_data->end_process_counting_sem);
        sem_destroy(&shared_data->main_barrier_sem);
        sem_destroy(&shared_data->numbering_sem);
        return false;
    }

    // Init semaphore for blocking Santa to start X-mas
    if ((sem_init(&shared_data->xmas_barrier_sem, 1, 0)) == -1) {
        // Previous semaphores are already created, they need to be destroyed
        sem_destroy(&shared_data->reindeer_counting_sem);
        sem_destroy(&shared_data->end_process_counting_sem);
        sem_destroy(&shared_data->main_barrier_sem);
        sem_destroy(&shared_data->numbering_sem);
        return false;
    }

    return true;
}

/**
 * Destroys all semaphores created by prepare_semaphores() function
 * <strong>Caution: It needs to be updated after adding a new semaphore into prepare_semaphore()</strong>
 * @param shared_data Pointer to the shared data where to store semaphores
 */
void terminate_semaphores(shared_data_t *shared_data) {
    sem_destroy(&shared_data->numbering_sem);
    sem_destroy(&shared_data->main_barrier_sem);
    sem_destroy(&shared_data->end_process_counting_sem);
    sem_destroy(&shared_data->reindeer_counting_sem);
    sem_destroy(&shared_data->xmas_barrier_sem);
}

/**
 * Creates Santa process
 * @param configs Process configurations
 * @param log_file Log file where every action is logged to
 * @param shared_mem_id Identification of shared memory block
 * @return true => success, false => problems with process creating
 */
bool spawn_santa(configs_t *configs, FILE *log_file, int shared_mem_id) {
    // Create a new (child) process by dividing the main process into two processes
    pid_t ppid = fork();
    if (ppid == -1) {
        // Error while creating the process (child process hasn't been created)
        return false;
    } else if (ppid == 0) {
        // Process has been successfully created --> this is code for the new (child) process

        // Attach shared memory
        shared_data_t *shared_data;
        if ((shared_data = shmat(shared_mem_id, NULL, 0)) == (void *)-1) {
            return false;
        }

        // Critical section - getting action number
        sem_wait(&shared_data->numbering_sem);
        int action_num = ++shared_data->process_num;
        sem_post(&shared_data->numbering_sem);
        // END of critical section

        fprintf(log_file, "%d: Santa: going to sleep\n", action_num);

        // Waiting for all reindeer are at home to start X-mas
        sem_wait(&shared_data->xmas_barrier_sem);

        // Critical section - getting action number
        sem_wait(&shared_data->numbering_sem);
        action_num = ++shared_data->process_num;
        sem_post(&shared_data->numbering_sem);
        // END of critical section

        fprintf(log_file, "%d: Santa: Christmas started\n", action_num);

        // Child process is done
        // Critical section - incrementing end processes number
        sem_wait(&shared_data->end_process_counting_sem);
        shared_data->ended_processes++;
        shmdt(shared_data);
        sem_post(&shared_data->end_process_counting_sem);
        // END of critical section

        // Allow main process to exit
        if (shared_data->ended_processes == (1 + configs->elf_num + configs->reindeer_num)) {
            sem_post(&shared_data->main_barrier_sem);
        }

        exit(0);
    } else {
        // Process has been successfully created --> this is code for original (main) process
    }

    return true;
}

/**
 * Creates elf processes
 * @param configs Process configurations
 * @param log_file Log file where every action is logged to
 * @param shared_mem_id Identification of shared memory block
 * @return true => success, false => problems with process creating
 */
bool spawn_elves(configs_t *configs, FILE *log_file, int shared_mem_id) {
    for (int i = 0; i < configs->elf_num; i++) {
        // Create a new (child) process by dividing the main process into two processes
        pid_t ppid = fork();
        if (ppid == -1) {
            // Error while creating the process (child process hasn't been created)
            return false;
        } else if (ppid == 0) {
            // Process has been successfully created --> this is code for the new (child) process

            // Attach shared memory
            shared_data_t *shared_data;
            if ((shared_data = shmat(shared_mem_id, NULL, 0)) == (void *)-1) {
                return false;
            }

            // Critical section - getting action number
            sem_wait(&shared_data->numbering_sem);
            int action_num = ++shared_data->process_num;
            sem_post(&shared_data->numbering_sem);
            // END of critical section

            // Set identifier
            id = i + 1;

            // Notify about start working action
            fprintf(log_file, "%d: Elf %d: started\n", action_num, id);

            // Prepare for randomization - construct seed
            // Seed is constructed from PID of the child process and microseconds of the current time
            struct timeval current_time;
            gettimeofday(&current_time, NULL);
            srand(getpid() + current_time.tv_usec);

            // Simulate individual working for a pseudorandom time
            int work_time = rand() % (configs->elf_work + 1);
            usleep(work_time * 1000); // * 1000 => convert milliseconds to microseconds

            // Critical section - getting action number
            sem_wait(&shared_data->numbering_sem);
            action_num = ++shared_data->process_num;
            sem_post(&shared_data->numbering_sem);
            // END of critical section

            // Let know individual work is completed and elf need Santa's help
            fprintf(log_file, "%d: Elf %d: need help\n", action_num, id);

            // Child process is done
            // Critical section - incrementing end processes number
            sem_wait(&shared_data->end_process_counting_sem);
            shared_data->ended_processes++;
            shmdt(shared_data);
            sem_post(&shared_data->end_process_counting_sem);
            // END of critical section

            // Allow main process to exit
            if (shared_data->ended_processes == (1 + configs->elf_num + configs->reindeer_num)) {
                sem_post(&shared_data->main_barrier_sem);
            }

            exit(0);
        } else {
            // Process has been successfully created --> this is code for original (main) process
        }
    }

    return true;
}

/**
 * Creates reindeer processes
 * @param configs Process configurations
 * @param log_file Log file where every action is logged to
 * @param shared_mem_id Identification of shared memory block
 * @return true => success, false => problems with process creating
 */
bool spawn_reindeer(configs_t *configs, FILE *log_file, int shared_mem_id) {
    for (int i = 0; i < configs->reindeer_num; i++) {
        // Create a new (child) process by dividing the main process into two processes
        pid_t ppid = fork();
        if (ppid == -1) {
            // Error while creating the process (child process hasn't been created)
            return false;
        } else if (ppid == 0) {
            // Process has been successfully created --> this is code for the new (child) process

            // Attach shared memory
            shared_data_t *shared_data;
            if ((shared_data = shmat(shared_mem_id, NULL, 0)) == (void *)-1) {
                printf("%d\n", shared_mem_id);
                return false;
            }

            // Critical section - getting action number
            sem_wait(&shared_data->numbering_sem);
            int action_num = ++shared_data->process_num;
            sem_post(&shared_data->numbering_sem);
            // END of critical section

            // Set identifier
            id = i + 1;

            // Notify about go to holiday action
            fprintf(log_file, "%d: RD %d: rstarted\n", action_num, id);

            // Prepare for randomization - construct seed
            // Seed is constructed from PID of the child process and microseconds of the current time
            struct timeval current_time;
            gettimeofday(&current_time, NULL);
            srand(getpid() + current_time.tv_usec);

            // Simulate holiday for a pseudorandom time
            int holiday_time = (rand() + (configs->reindeer_holiday / 2)) % (configs->reindeer_holiday + 1);
            usleep(holiday_time * 1000); // * 1000 => convert milliseconds to microseconds

            // Critical section - getting action number
            sem_wait(&shared_data->numbering_sem);
            action_num = ++shared_data->process_num;
            sem_post(&shared_data->numbering_sem);
            // END of critical section

            // Let know reindeer is back at home
            fprintf(log_file, "%d: RD %d: return home\n", action_num, id);

            // Increment number of returned reindeer
            sem_wait(&shared_data->reindeer_counting_sem);
            shared_data->reindeer_home_num++;
            sem_post(&shared_data->reindeer_counting_sem);

            // Waiting for all reindeer are at home to start Christmas
            // The last-returned reindeer unlocks X-mas barrier and Santa will start Christmas
            if (shared_data->reindeer_home_num == configs->reindeer_num) {
                sem_post(&shared_data->xmas_barrier_sem);
                printf("Reindeer: %d\n", id);
            }

            // Child process is done
            // Critical section - incrementing end processes number
            sem_wait(&shared_data->end_process_counting_sem);
            shared_data->ended_processes++;
            shmdt(shared_data);
            sem_post(&shared_data->end_process_counting_sem);
            // END of critical section

            // Allow main process to exit
            if (shared_data->ended_processes == (1 + configs->elf_num + configs->reindeer_num)) {
                sem_post(&shared_data->main_barrier_sem);
            }

            exit(0);
        } else {
            // Process has been successfully created --> this is code for original (main) process
        }
    }

    return true;
}
