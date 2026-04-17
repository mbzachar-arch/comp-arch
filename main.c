#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "decode.h"
#include "CPUParameters.h"
#include "simulate.h"

static int is_machine_code_file(const char *filename) {
    FILE *fp;
    char line[MAX_LINE_LEN];
    char *trimmed;

    fp = fopen(filename, "r");
    if (fp == NULL) return 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        if (*trimmed == '\0' || *trimmed == '\n' || *trimmed == '#') continue;

        fclose(fp);
        return (strncmp(trimmed, "0x", 2) == 0 || strncmp(trimmed, "0X", 2) == 0);
    }

    fclose(fp);
    return 0;
}

static int load_machine_program(const char *filename, Program *prog) {
    FILE *fp;
    char line[MAX_LINE_LEN];
    char *trimmed;
    uint32_t word;
    Instruction instr;

    prog->instr_count = 0;

    fp = fopen(filename, "r");
    if (fp == NULL) return 0;

    while (fgets(line, sizeof(line), fp) != NULL && prog->instr_count < MAX_INSTR) {
        line[strcspn(line, "\n")] = '\0';

        trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        if (*trimmed == '\0' || *trimmed == '#') continue;

        word = (uint32_t)strtoul(trimmed, NULL, 0);

        if (!disassemble_word(word, &instr)) continue;

        prog->instr_mem[prog->instr_count++] = instr;
    }

    fclose(fp);
    return 1;
}

static int load_assembly_program(const char *filename, Program *prog) {
    FILE *fp;
    char line[MAX_LINE_LEN];
    char *trimmed;
    char *comment;
    char *colon;
    char mnemonic[32];
    char operands[MAX_LINE_LEN];
    Instruction instr;
    LabelTable labels;
    int pc = 0;

    prog->instr_count = 0;

    if (!first_pass_collect_labels(filename, &labels)) return 0;

    fp = fopen(filename, "r");
    if (fp == NULL) return 0;

    while (fgets(line, sizeof(line), fp) != NULL && prog->instr_count < MAX_INSTR) {
        line[strcspn(line, "\n")] = '\0';

        trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        if (*trimmed == '\0' || *trimmed == '#') continue;

        comment = strchr(trimmed, '#');
        if (comment != NULL) *comment = '\0';

        colon = strchr(trimmed, ':');
        if (colon != NULL) {
            trimmed = colon + 1;
            while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
            if (*trimmed == '\0') continue;
        }

        mnemonic[0] = '\0';
        operands[0] = '\0';
        sscanf(trimmed, "%31s %[^\n]", mnemonic, operands);

        if (!parse_instruction_fields_with_labels(mnemonic, operands, &instr, &labels, pc)) {
            continue;
        }

        prog->instr_mem[prog->instr_count++] = instr;
        pc++;
    }

    fclose(fp);
    return 1;
}

static int load_program(const char *filename, Program *prog) {
    if (is_machine_code_file(filename)) {
        return load_machine_program(filename, prog);
    }
    return load_assembly_program(filename, prog);
}

static void run_one_file(const char *input_name, const char *output_name) {
    Program prog;
    Simulator single_sim;
    Simulator pipe_sim;
    FILE *out;
    FILE *trace;
    char trace_name[256];

    if (!load_program(input_name, &prog)) {
        fprintf(stderr, "Failed to load %s\n", input_name);
        return;
    }

    out = fopen(output_name, "w");
    if (out == NULL) {
        fprintf(stderr, "Could not open output file %s\n", output_name);
        return;
    }

    snprintf(trace_name, sizeof(trace_name), "%s_cycle_trace.txt", input_name);
    trace = fopen(trace_name, "w");
    if (trace == NULL) {
        trace = NULL;
    }

    run_single_cycle(&single_sim, &prog, trace);
    run_pipeline(&pipe_sim, &prog, trace);

    if (pipe_sim.metrics.latency_sec > 0.0) {
        pipe_sim.metrics.speedup =
            single_sim.metrics.latency_sec / pipe_sim.metrics.latency_sec;
    }

    write_professor_style_report(out, input_name, &prog, &single_sim, &pipe_sim);

    fclose(out);
    if (trace != NULL) fclose(trace);

    printf("Finished %s -> %s\n", input_name, output_name);
}

int main(int argc, char **argv) {
    const char *all_files[] = {
        "alu_heavy1.asm",
        "balanced_mix1.asm",
        "branchy_loop1.asm",
        "alu_heavy1_machine.asm",
        "balanced_mix1_machine.asm",
        "branchy_loop1_machine.asm"
    };
    int i;
    char output_name[256];

    if (argc < 2) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  %s <inputfile> [outputfile]\n", argv[0]);
        fprintf(stderr, "  %s all\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "all") == 0) {
        for (i = 0; i < 6; i++) {
            snprintf(output_name, sizeof(output_name), "%s_output.txt", all_files[i]);
            run_one_file(all_files[i], output_name);
        }
        return 0;
    }

    if (argc >= 3) {
        run_one_file(argv[1], argv[2]);
    } else {
        run_one_file(argv[1], "output.txt");
    }

    return 0;
}
//done