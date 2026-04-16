#ifndef DECODE_H
#define DECODE_H

#include <stdint.h>
#include "CPUParameters.h"

#define MAX_INSTRUCTIONS 1024
#define MAX_LINE_LEN 256
#define UNKNOWN_INSTRUCTION 0xFFFFFFFFu

uint32_t encode_R(uint32_t funct, uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shamt);
uint32_t encode_I(uint32_t op, uint32_t rs, uint32_t rt, int16_t imm);
uint32_t encode_J(uint32_t op, uint32_t target);

int reg_num(const char *name);
uint32_t assemble_instruction(const char *mnemonic, char *operands);
int parse_instruction_fields(const char *mnemonic, char *operands, Instruction *instr);

#endif
