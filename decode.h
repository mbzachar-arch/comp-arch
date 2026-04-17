#ifndef DECODE_H
#define DECODE_H

#include <stdint.h>
#include "CPUParameters.h"

#define MAX_INSTRUCTIONS 1024
#define MAX_LINE_LEN 256
#define MAX_LABELS 128
#define UNKNOWN_INSTRUCTION 0xFFFFFFFFu

typedef struct {
    char name[32];
    int address;
} Label;

typedef struct {
    Label labels[MAX_LABELS];
    int count;
} LabelTable;

uint32_t encode_R(uint32_t funct, uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shamt);
uint32_t encode_I(uint32_t op, uint32_t rs, uint32_t rt, int16_t imm);
uint32_t encode_J(uint32_t op, uint32_t target);

int reg_num(const char *name);
uint32_t assemble_instruction(const char *mnemonic, char *operands);

void init_label_table(LabelTable *table);
int add_label(LabelTable *table, const char *name, int address);
int find_label_address(const LabelTable *table, const char *name);
int first_pass_collect_labels(const char *filename, LabelTable *table);

int parse_instruction_fields_with_labels(const char *mnemonic,
                                         char *operands,
                                         Instruction *instr,
                                         const LabelTable *table,
                                         int current_pc);

int disassemble_word(uint32_t word, Instruction *instr);

#endif
//done