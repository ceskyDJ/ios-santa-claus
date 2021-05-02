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
#include <stdarg.h>

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
    // Semaphore for blocking Santa from waking up
    // Santa is woken up when all of reindeer are at home or at least 3 elves need help
    sem_t wake_santa_sem;
    // Semaphore for blocking reindeer before they are hitched
    sem_t reindeer_hitched_sem;
    // Semaphore for blocking Santa from starting Christmas until all of reindeer are hitched
    sem_t all_reindeer_hitched_sem;
    // Semaphore for counting hitched reindeer (manipulating with reindeer_hitched_num in shared_data)
    sem_t hitched_counting_sem;
    // Semaphore for counting elves which need to help (manipulating with elf_need_help_num in shared_data)
    sem_t elf_counting_sem;
    // Semaphore for blocking elf until it get help from Santa
    sem_t elf_got_help_sem;
    // Semaphore for blocking elves from entering workshop, when its not empty
    sem_t workshop_empty_sem;

    // Number of created (child) processes
    int process_num;
    // Number of ended (done) child processes
    int ended_processes;
    // Number of reindeer at home (back from holiday)
    int reindeer_home_num;
    // Number of hitched reindeer
    int reindeer_hitched_num;
    // Number of elves which ask for help
    int elf_need_help_num;
    // Is Santa's workshop opened?
    bool workshop_open;
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
/**
 * Logs an action
 * @param log_file Log file where to write the action to
 * @param shared_data Shared data (access to shared memory)
 * @param action Action name with tags (for ex. %d). Action number will be added automatically
 * @param ... Replacements for tags
 */
void log_action(FILE *log_file, shared_data_t *shared_data, const char *action, ...);

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
    if ((shared_mem_id = shmget(IPC_PRIVATE, sizeof(shared_data_t), 0600 | IPC_CREAT)) == -1) {
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
 * Logs an action
 * @param log_file Log file where to write the action to
 * @param shared_data Shared data (access to shared memory)
 * @param action Action name with tags (for ex. %d). Action number will be added automatically
 * @param ... Replacements for tags
 */
void log_action(FILE *log_file, shared_data_t *shared_data, const char *action, ...) {
    va_list tag_replacements;

    va_start(tag_replacements, action);

    // Critical section - getting number and write alert into log file
    sem_wait(&shared_data->numbering_sem);
    int action_num = ++shared_data->process_num;

    fprintf(log_file, "%d: ", action_num);
    vfprintf(log_file, action, tag_replacements);
    fprintf(log_file, "\n");
    sem_post(&shared_data->numbering_sem);
    // END of critical section

    va_end(tag_replacements);
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
        // Previous semaphores are already created, they need to be destroyed
        terminate_semaphores(shared_data);
        return false;
    }

    // Init semaphore for counting ended processes
    if ((sem_init(&shared_data->end_process_counting_sem, 1, 1)) == -1) {
        // Previous semaphores are already created, they need to be destroyed
        terminate_semaphores(shared_data);
        return false;
    }

    // Init semaphore for counting reindeer at home
    if ((sem_init(&shared_data->reindeer_counting_sem, 1, 1)) == -1) {
        // Previous semaphores are already created, they need to be destroyed
        terminate_semaphores(shared_data);
        return false;
    }

    // Init semaphore for blocking Santa from waking up
    if ((sem_init(&shared_data->wake_santa_sem, 1, 0)) == -1) {
        // Previous semaphores are already created, they need to be destroyed
        terminate_semaphores(shared_data);
        return false;
    }

    // Init semaphore for blocking reindeer until its hitched 
    if ((sem_init(&shared_data->reindeer_hitched_sem, 1, 0)) == -1) {
        // Previous semaphores are already created, they need to be destroyed
        terminate_semaphores(shared_data);
        return false;
    }

    // Init semaphore for blocking Santa to start Christmas
    if ((sem_init(&shared_data->all_reindeer_hitched_sem, 1, 0)) == -1) {
        // Previous semaphores are already created, they need to be destroyed
        terminate_semaphores(shared_data);
        return false;
    }

    // Init semaphore for counting hitched reindeer
    if ((sem_init(&shared_data->hitched_counting_sem, 1, 1)) == -1) {
        // Previous semaphores are already created, they need to be destroyed
        terminate_semaphores(shared_data);
        return false;
    }

    // Init semaphore for counting elves waiting for help
    if ((sem_init(&shared_data->elf_counting_sem, 1, 1)) == -1) {
        // Previous semaphores are already created, they need to be destroyed
        terminate_semaphores(shared_data);
        return false;
    }

    if ((sem_init(&shared_data->elf_got_help_sem, 1, 0)) == -1) {
        // Previous semaphores are already created, they need to be destroyed
        terminate_semaphores(shared_data);
        return false;
    }

    if ((sem_init(&shared_data->workshop_empty_sem, 1, 1)) == -1) {
        // Previous semaphores are already created, they need to be destroyed
        terminate_semaphores(shared_data);
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
    sem_destroy(&shared_data->wake_santa_sem);
    sem_destroy(&shared_data->reindeer_hitched_sem);
    sem_destroy(&shared_data->all_reindeer_hitched_sem);
    sem_destroy(&shared_data->hitched_counting_sem);
    sem_destroy(&shared_data->elf_counting_sem);
    sem_destroy(&shared_data->elf_got_help_sem);
    sem_destroy(&shared_data->workshop_empty_sem);
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

        // Workshop is opened, so elves can get help there
        shared_data->workshop_open = true;

        // Santa sleeps until interrupt (see code in next block)
        do {
            log_action(log_file, shared_data, "Santa: going to sleep");

            // Sleep until at least 3 elves need help or the last reindeer come home
            sem_wait(&shared_data->wake_santa_sem);
            if (shared_data->reindeer_home_num == configs->reindeer_num) {
                // All reindeer are at home --> let's hitch them
                // After that Christmas will be started, so elves are without Santa's help from now
                break;
            } else {
                // Elves need help

                log_action(log_file, shared_data, "Santa: helping elves");

                // Help elves
                for (int i = 0; i < 3; i++) {
                    sem_post(&shared_data->elf_got_help_sem);
                }

                // Critical section - decrease number of elves waiting for help by 3 (Santa has helped them yet)
                sem_wait(&shared_data->elf_counting_sem);
                shared_data->elf_need_help_num -= 3;
                sem_post(&shared_data->elf_counting_sem);
                // END of critical section

                // Workshop is empty now
                sem_post(&shared_data->workshop_empty_sem);
            }
        } while (1);

        // Workshop is closed now, so elves can't get help and should go to holiday
        log_action(log_file, shared_data, "Santa: closing workshop");
        shared_data->workshop_open = false;

        // Send waiting elves to holiday
        // Some of elves aren't at holiday right now and didn't see the info sign at the workshop says "closed"
        for (int i = 0; i < shared_data->elf_need_help_num; i++) {
            sem_post(&shared_data->elf_got_help_sem);
        }

        // Hitch reindeer
        for (int i = 0; i < configs->reindeer_num; i++) {
            sem_post(&shared_data->reindeer_hitched_sem);
        }

        // Wait for all reindeer are hitched
        sem_wait(&shared_data->all_reindeer_hitched_sem);

        log_action(log_file, shared_data, "Santa: Christmas started");

        // Child process is done
        // Critical section - incrementing end processes number
        sem_wait(&shared_data->end_process_counting_sem);
        shared_data->ended_processes++;
        sem_post(&shared_data->end_process_counting_sem);
        // END of critical section

        // Allow main process to exit
        if (shared_data->ended_processes == (1 + configs->elf_num + configs->reindeer_num)) {
            sem_post(&shared_data->main_barrier_sem);
        }

        shmdt(shared_data);
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

            // Set identifier
            id = i + 1;

            // Notify about start working action
            log_action(log_file, shared_data, "Elf %d: started", id);

            // Elf's working
            do {
                // Prepare for randomization - construct seed
                // Seed is constructed from PID of the child process and microseconds of the current time
                struct timeval current_time;
                gettimeofday(&current_time, NULL);
                srand(getpid() + current_time.tv_usec);

                // Simulate individual working for a pseudorandom time
                int work_time = rand() % (configs->elf_work + 1);
                usleep(work_time * 1000); // * 1000 => convert milliseconds to microseconds

                log_action(log_file, shared_data, "Elf %d: need help", id);

                if (!shared_data->workshop_open) {
                    // Santa has already started Christmas, so the elf goes to holiday

                    log_action(log_file, shared_data, "Elf %d: taking holidays", id);

                    break;
                } else {
                    // Critical section - counting elves waiting for help
                    sem_wait(&shared_data->elf_counting_sem);
                    shared_data->elf_need_help_num++;
                    sem_post(&shared_data->elf_counting_sem);
                    // END of critical section

                    // Wake up Santa if elf is the 3rd in the queue
                    if (shared_data->elf_need_help_num >= 3) {
                        // Waiting for open workshop
                        sem_wait(&shared_data->workshop_empty_sem);

                        // Wake up Santa
                        sem_post(&shared_data->wake_santa_sem);
                    }

                    // Wait for Santa's help
                    sem_wait(&shared_data->elf_got_help_sem);

                    if (shared_data->workshop_open) {
                        // Elf got help from Santa

                        log_action(log_file, shared_data, "Elf %d: get help", id);
                    } else {
                        // Christmas has started yet, so elf won't get help and must go to holiday

                        log_action(log_file, shared_data, "Elf %d: taking holidays", id);
                    }

                    // Start next individual work...
                }
            } while (1);

            // Child process is done
            // Critical section - incrementing end processes number
            sem_wait(&shared_data->end_process_counting_sem);
            shared_data->ended_processes++;
            sem_post(&shared_data->end_process_counting_sem);
            // END of critical section

            // Allow main process to exit
            if (shared_data->ended_processes == (1 + configs->elf_num + configs->reindeer_num)) {
                sem_post(&shared_data->main_barrier_sem);
            }

            shmdt(shared_data);
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
            // Set identifier
            id = i + 1;

            // Notify about go to holiday action
            log_action(log_file, shared_data, "RD %d: rstarted", id);

            // Prepare for randomization - construct seed
            // Seed is constructed from PID of the child process and microseconds of the current time
            struct timeval current_time;
            gettimeofday(&current_time, NULL);
            srand(getpid() + current_time.tv_usec);

            // Simulate holiday for a pseudorandom time
            int holiday_time = (rand() + (configs->reindeer_holiday / 2)) % (configs->reindeer_holiday + 1);
            usleep(holiday_time * 1000); // * 1000 => convert milliseconds to microseconds

            // Let know reindeer is back at home
            log_action(log_file, shared_data, "RD %d: return home", id);

            // Increment number of returned reindeer
            sem_wait(&shared_data->reindeer_counting_sem);
            shared_data->reindeer_home_num++;
            sem_post(&shared_data->reindeer_counting_sem);

            // Waiting for all reindeer are at home to start Christmas
            // The last-returned reindeer wakes Santa up and he can start hitching reindeer
            if (shared_data->reindeer_home_num == configs->reindeer_num) {
                sem_post(&shared_data->wake_santa_sem);
            }

            // Wait for the time the reindeer is hitched
            sem_wait(&shared_data->reindeer_hitched_sem);

            log_action(log_file, shared_data, "RD %d: get hitched", id);

            // Critical section - counting hitched reindeer
            sem_wait(&shared_data->hitched_counting_sem);
            shared_data->reindeer_hitched_num++;
            sem_post(&shared_data->hitched_counting_sem);
            // END of critical section

            // All reindeer are hitched --> Santa can start Christmas
            if (shared_data->reindeer_hitched_num == configs->reindeer_num) {
                sem_post(&shared_data->all_reindeer_hitched_sem);
            }

            // Child process is done
            // Critical section - incrementing end processes number
            sem_wait(&shared_data->end_process_counting_sem);
            shared_data->ended_processes++;
            sem_post(&shared_data->end_process_counting_sem);
            // END of critical section

            // Allow main process to exit
            if (shared_data->ended_processes == (1 + configs->elf_num + configs->reindeer_num)) {
                sem_post(&shared_data->main_barrier_sem);
            }

            shmdt(shared_data);
            exit(0);
        } else {
            // Process has been successfully created --> this is code for original (main) process
        }
    }

    return true;
}
