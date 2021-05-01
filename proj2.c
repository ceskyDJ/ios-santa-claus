#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>

// No valid input
#define BAD_INPUT -1

// Configurations from input arguments
struct {
    short elf_num;          // Number of elves
    short reindeer_num;     // Number of reindeer
    short elf_work;         // Maximum time of individual elf's work (in ms)
    short reindeer_holiday; // Maximum time of reindeer's holiday (in ms)
} configs;

/**
 * Parses provided input argument
 * @param inputArg Input argument to parse
 * @param min The smallest allowed number
 * @param max The largest allowed number
 * @return Parsed input argument or -1 if the argument is not valid
 */
int parseInputArg(char *inputArg, int min, int max);
/**
 * Loads configurations from input arguments
 * <strong>Side effects: modifies global variable configs</strong>
 * @param inputArgs Array of input arguments
 * @return true => success, false => problems with input arguments
 */
bool loadConfigurations(char **inputArgs);

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
    if (!loadConfigurations(argv)) {
        printf( "Invalid input argument(s)\n");

        return 1;
    }

    return 0;
}

/**
 * Parses provided input argument
 * @param inputArg Input argument to parse
 * @param min The smallest allowed number
 * @param max The largest allowed number
 * @return Parsed input argument or BAD_INPUT if the argument is not valid
 */
int parseInputArg(char *inputArg, int min, int max) {
    // Check if the argument is numeric-only
    for (int i = 0; i < (int)strlen(inputArg); i++) {
        if (!isdigit(inputArg[i])) {
            return BAD_INPUT;
        }
    }

    // Try to convert string to number
    int output;
    if ((output = (int)strtol(inputArg, NULL, 10)) == 0 && errno == EINVAL) {
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
 * <strong>Side effects: modifies global variable configs</strong>
 * @param inputArgs Array of input arguments
 * @return true => success, false => problems with input arguments
 */
bool loadConfigurations(char **inputArgs) {
    if ((configs.elf_num = (short)parseInputArg(inputArgs[1], 1, 1000)) == BAD_INPUT) {
        return false;
    }
    if ((configs.reindeer_num = (short)parseInputArg(inputArgs[2], 1, 19)) == BAD_INPUT) {
        return false;
    }
    if ((configs.elf_work = (short)parseInputArg(inputArgs[3], 0, 1000)) == BAD_INPUT) {
        return false;
    }
    if ((configs.reindeer_holiday = (short)parseInputArg(inputArgs[4], 0, 1000)) == BAD_INPUT) {
        return false;
    }

    return true;
}
