/*
main file to structure all of the other files around
ECE 5367 Final Project: Pipelined Performance Analyzer
Spring 2026
Jonathan McChesney-Fleming
most of this information is sourced from:
https://student.cs.uwaterloo.ca/~isg/res/mips/opcodes
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "decode.h"
#include "CPUParameters.h"
#include "simulate.h"

#define MAX_LINE_LEN 256

int main(int argc, char **argv)
{
    FILE *fp;
    FILE *out;
    Program prog;
    Simulator single_sim;
    Simulator pipe_sim;

    char line[MAX_LINE_LEN];
    char *trimmed;
    char *comment;
    char mnemonic[32];
    char operands[MAX_LINE_LEN];
    uint32_t word;
    Instruction instr;

    const char *output_name;

    prog.instr_count = 0;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <TestFile.txt> [output.txt]\n", argv[0]);
        return 1;
    }

    output_name = (argc >= 3) ? argv[2] : "output.txt";

    fp = fopen(argv[1], "r");
    if (fp == NULL) {
        perror("fopen");
        return 1;
    }

    printf("Reading source file: %s\n\n", argv[1]);

    while (fgets(line, sizeof(line), fp) != NULL && prog.instr_count < MAX_INSTR) {
        line[strcspn(line, "\n")] = '\0';

        trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') {
            trimmed++;
        }

        if (*trimmed == '\0' || *trimmed == '#') {
            continue;
        }

        comment = strchr(trimmed, '#');
        if (comment != NULL) {
            *comment = '\0';
        }

        mnemonic[0] = '\0';
        operands[0] = '\0';
        sscanf(trimmed, "%31s %[^\n]", mnemonic, operands);

        word = assemble_instruction(mnemonic, operands);

        if (word == UNKNOWN_INSTRUCTION) {
            fprintf(stderr,
                    "Warning: unrecognized instruction '%s' -- line skipped\n",
                    mnemonic);
            continue;
        }

        if (!parse_instruction_fields(mnemonic, operands, &instr)) {
            fprintf(stderr,
                    "Warning: instruction '%s' assembled but not supported by simulator -- line skipped\n",
                    mnemonic);
            continue;
        }

        prog.instr_mem[prog.instr_count] = instr;

        printf("[%3d]  %-8s %-30s => 0x%08X\n",
               prog.instr_count, mnemonic, operands, word);

        prog.instr_count++;
    }

    fclose(fp);

    printf("\n%d instruction(s) loaded into program memory.\n\n", prog.instr_count);

    run_single_cycle(&single_sim, &prog);
    run_pipeline(&pipe_sim, &prog);

    if (pipe_sim.metrics.latency_sec > 0.0) {
        pipe_sim.metrics.speedup =
            single_sim.metrics.latency_sec / pipe_sim.metrics.latency_sec;
    }

    out = fopen(output_name, "w");
    if (out == NULL) {
        perror("fopen output");
        return 1;
    }

    write_report(out, &prog, &single_sim, &pipe_sim);
    fclose(out);

    printf("=== Single-Cycle Results ===\n");
    print_metrics(stdout, &single_sim.metrics);

    printf("=== Pipeline Results ===\n");
    print_metrics(stdout, &pipe_sim.metrics);

    printf("Output written to: %s\n", output_name);

    return 0;
}
