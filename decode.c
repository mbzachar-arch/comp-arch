#include "decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define OPCODE(x) (((x) >> 26) & 0x3F)
#define RS(x)     (((x) >> 21) & 0x1F)
#define RT(x)     (((x) >> 16) & 0x1F)
#define RD(x)     (((x) >> 11) & 0x1F)
#define FUNCT(x)  ((x) & 0x3F)
#define IMM(x)    ((int16_t)((x) & 0xFFFF))
#define TARGET(x) ((x) & 0x03FFFFFF)

struct rtype_entry {
    const char *name;
    uint32_t funct;
};

struct itype_entry {
    const char *name;
    uint32_t op;
};

static struct rtype_entry rtype_table[] = {
    { "add", 0x20 },
    { "sub", 0x22 },
    { "and", 0x24 },
    { "or",  0x25 },
    { NULL,  0    }
};

static struct itype_entry itype_table[] = {
    { "addi", 0x08 },
    { NULL,   0    }
};

static struct itype_entry loadstore_table[] = {
    { "lw", 0x23 },
    { "sw", 0x2B },
    { NULL, 0    }
};

uint32_t encode_R(uint32_t funct, uint32_t rs, uint32_t rt, uint32_t rd, uint32_t shamt) {
    uint32_t word = 0;
    word |= (rs << 21);
    word |= (rt << 16);
    word |= (rd << 11);
    word |= (shamt << 6);
    word |= (funct & 0x3F);
    return word;
}

uint32_t encode_I(uint32_t op, uint32_t rs, uint32_t rt, int16_t imm) {
    uint32_t word = 0;
    word |= (op << 26);
    word |= (rs << 21);
    word |= (rt << 16);
    word |= (uint16_t)imm;
    return word;
}

uint32_t encode_J(uint32_t op, uint32_t target) {
    uint32_t word = 0;
    word |= (op << 26);
    word |= (target & 0x03FFFFFFu);
    return word;
}

int reg_num(const char *name) {
    const char *r = (name[0] == '$') ? name + 1 : name;

    if (r[0] >= '0' && r[0] <= '9') {
        return atoi(r);
    }

    if (strcmp(r, "zero") == 0) return 0;
    if (strcmp(r, "at")   == 0) return 1;
    if (strcmp(r, "v0")   == 0) return 2;
    if (strcmp(r, "v1")   == 0) return 3;
    if (strcmp(r, "a0")   == 0) return 4;
    if (strcmp(r, "a1")   == 0) return 5;
    if (strcmp(r, "a2")   == 0) return 6;
    if (strcmp(r, "a3")   == 0) return 7;
    if (strcmp(r, "t0")   == 0) return 8;
    if (strcmp(r, "t1")   == 0) return 9;
    if (strcmp(r, "t2")   == 0) return 10;
    if (strcmp(r, "t3")   == 0) return 11;
    if (strcmp(r, "t4")   == 0) return 12;
    if (strcmp(r, "t5")   == 0) return 13;
    if (strcmp(r, "t6")   == 0) return 14;
    if (strcmp(r, "t7")   == 0) return 15;
    if (strcmp(r, "s0")   == 0) return 16;
    if (strcmp(r, "s1")   == 0) return 17;
    if (strcmp(r, "s2")   == 0) return 18;
    if (strcmp(r, "s3")   == 0) return 19;
    if (strcmp(r, "s4")   == 0) return 20;
    if (strcmp(r, "s5")   == 0) return 21;
    if (strcmp(r, "s6")   == 0) return 22;
    if (strcmp(r, "s7")   == 0) return 23;
    if (strcmp(r, "t8")   == 0) return 24;
    if (strcmp(r, "t9")   == 0) return 25;
    if (strcmp(r, "k0")   == 0) return 26;
    if (strcmp(r, "k1")   == 0) return 27;
    if (strcmp(r, "gp")   == 0) return 28;
    if (strcmp(r, "sp")   == 0) return 29;
    if (strcmp(r, "fp")   == 0) return 30;
    if (strcmp(r, "ra")   == 0) return 31;

    return -1;
}

uint32_t assemble_instruction(const char *mnemonic, char *operands) {
    char a[32], b[32], c[32], base[32];
    int16_t offset;
    int i;

    a[0] = b[0] = c[0] = base[0] = '\0';
    offset = 0;

    for (i = 0; rtype_table[i].name != NULL; i++) {
        if (strcmp(mnemonic, rtype_table[i].name) == 0) {
            sscanf(operands, "%[^,], %[^,], %s", a, b, c);
            return encode_R(rtype_table[i].funct, reg_num(b), reg_num(c), reg_num(a), 0);
        }
    }

    for (i = 0; itype_table[i].name != NULL; i++) {
        if (strcmp(mnemonic, itype_table[i].name) == 0) {
            sscanf(operands, "%[^,], %[^,], %s", a, b, c);
            return encode_I(itype_table[i].op, reg_num(b), reg_num(a), (int16_t)strtol(c, NULL, 0));
        }
    }

    for (i = 0; loadstore_table[i].name != NULL; i++) {
        if (strcmp(mnemonic, loadstore_table[i].name) == 0) {
            sscanf(operands, "%[^,], %hd(%[^)])", a, &offset, base);
            return encode_I(loadstore_table[i].op, reg_num(base), reg_num(a), offset);
        }
    }

    if (strcmp(mnemonic, "beq") == 0) {
        sscanf(operands, "%[^,], %[^,], %s", a, b, c);
        return encode_I(0x04, reg_num(a), reg_num(b), (int16_t)strtol(c, NULL, 0));
    }

    if (strcmp(mnemonic, "j") == 0) {
        sscanf(operands, "%s", a);
        return encode_J(0x02, (uint32_t)strtol(a, NULL, 0));
    }

    if (strcmp(mnemonic, "nop") == 0) {
        return 0x00000000u;
    }

    return UNKNOWN_INSTRUCTION;
}

void init_label_table(LabelTable *table) {
    table->count = 0;
}

int add_label(LabelTable *table, const char *name, int address) {
    if (table->count >= MAX_LABELS) return 0;

    strncpy(table->labels[table->count].name, name, 31);
    table->labels[table->count].name[31] = '\0';
    table->labels[table->count].address = address;
    table->count++;

    return 1;
}

int find_label_address(const LabelTable *table, const char *name) {
    int i;

    for (i = 0; i < table->count; i++) {
        if (strcmp(table->labels[i].name, name) == 0) {
            return table->labels[i].address;
        }
    }

    return -1;
}

int first_pass_collect_labels(const char *filename, LabelTable *table) {
    FILE *fp;
    char line[MAX_LINE_LEN];
    char *trimmed;
    char *comment;
    char *colon;
    int pc = 0;

    init_label_table(table);

    fp = fopen(filename, "r");
    if (fp == NULL) return 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\n")] = '\0';

        trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        if (*trimmed == '\0' || *trimmed == '#') continue;

        comment = strchr(trimmed, '#');
        if (comment != NULL) *comment = '\0';

        colon = strchr(trimmed, ':');
        if (colon != NULL) {
            *colon = '\0';
            add_label(table, trimmed, pc);

            trimmed = colon + 1;
            while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

            if (*trimmed == '\0') continue;
        }

        pc++;
    }

    fclose(fp);
    return 1;
}

int parse_instruction_fields_with_labels(const char *mnemonic,
                                         char *operands,
                                         Instruction *instr,
                                         const LabelTable *table,
                                         int current_pc) {
    char a[32], b[32], c[32];
    int n;
    int target;

    a[0] = b[0] = c[0] = '\0';

    instr->op = OP_INVALID;
    instr->rs = 0;
    instr->rt = 0;
    instr->rd = 0;
    instr->imm = 0;
    instr->label[0] = '\0';

    if (strcmp(mnemonic, "add") == 0) {
        n = sscanf(operands, "%[^,], %[^,], %s", a, b, c);
        if (n == 3) {
            instr->op = OP_ADD;
            instr->rd = reg_num(a);
            instr->rs = reg_num(b);
            instr->rt = reg_num(c);
            return 1;
        }
    }

    if (strcmp(mnemonic, "sub") == 0) {
        n = sscanf(operands, "%[^,], %[^,], %s", a, b, c);
        if (n == 3) {
            instr->op = OP_SUB;
            instr->rd = reg_num(a);
            instr->rs = reg_num(b);
            instr->rt = reg_num(c);
            return 1;
        }
    }

    if (strcmp(mnemonic, "and") == 0) {
        n = sscanf(operands, "%[^,], %[^,], %s", a, b, c);
        if (n == 3) {
            instr->op = OP_AND;
            instr->rd = reg_num(a);
            instr->rs = reg_num(b);
            instr->rt = reg_num(c);
            return 1;
        }
    }

    if (strcmp(mnemonic, "or") == 0) {
        n = sscanf(operands, "%[^,], %[^,], %s", a, b, c);
        if (n == 3) {
            instr->op = OP_OR;
            instr->rd = reg_num(a);
            instr->rs = reg_num(b);
            instr->rt = reg_num(c);
            return 1;
        }
    }

    if (strcmp(mnemonic, "addi") == 0) {
        n = sscanf(operands, "%[^,], %[^,], %s", a, b, c);
        if (n == 3) {
            instr->op = OP_ADDI;
            instr->rt = reg_num(a);
            instr->rs = reg_num(b);
            instr->imm = (int)strtol(c, NULL, 0);
            return 1;
        }
    }

    if (strcmp(mnemonic, "lw") == 0) {
        n = sscanf(operands, "%[^,], %d(%[^)])", a, &instr->imm, b);
        if (n == 3) {
            instr->op = OP_LW;
            instr->rt = reg_num(a);
            instr->rs = reg_num(b);
            return 1;
        }
    }

    if (strcmp(mnemonic, "sw") == 0) {
        n = sscanf(operands, "%[^,], %d(%[^)])", a, &instr->imm, b);
        if (n == 3) {
            instr->op = OP_SW;
            instr->rt = reg_num(a);
            instr->rs = reg_num(b);
            return 1;
        }
    }

    if (strcmp(mnemonic, "beq") == 0) {
        n = sscanf(operands, "%[^,], %[^,], %s", a, b, c);
        if (n == 3) {
            instr->op = OP_BEQ;
            instr->rs = reg_num(a);
            instr->rt = reg_num(b);

            if (isalpha((unsigned char)c[0]) || c[0] == '_') {
                target = find_label_address(table, c);
                if (target < 0) return 0;
                instr->imm = target - (current_pc + 1);
            } else {
                instr->imm = (int)strtol(c, NULL, 0);
            }

            return 1;
        }
    }

    if (strcmp(mnemonic, "j") == 0) {
        n = sscanf(operands, "%s", a);
        if (n == 1) {
            instr->op = OP_J;

            if (isalpha((unsigned char)a[0]) || a[0] == '_') {
                target = find_label_address(table, a);
                if (target < 0) return 0;
                instr->imm = target;
            } else {
                instr->imm = (int)strtol(a, NULL, 0);
            }

            return 1;
        }
    }

    if (strcmp(mnemonic, "nop") == 0) {
        instr->op = OP_NOP;
        return 1;
    }

    return 0;
}

int disassemble_word(uint32_t word, Instruction *instr) {
    uint32_t op = OPCODE(word);

    instr->op = OP_INVALID;
    instr->rs = RS(word);
    instr->rt = RT(word);
    instr->rd = RD(word);
    instr->imm = IMM(word);
    instr->label[0] = '\0';

    if (word == 0x00000000u) {
        instr->op = OP_NOP;
        return 1;
    }

    if (op == 0x00) {
        switch (FUNCT(word)) {
            case 0x20: instr->op = OP_ADD; return 1;
            case 0x22: instr->op = OP_SUB; return 1;
            case 0x24: instr->op = OP_AND; return 1;
            case 0x25: instr->op = OP_OR;  return 1;
            default: return 0;
        }
    }

    switch (op) {
        case 0x08:
            instr->op = OP_ADDI;
            return 1;

        case 0x23:
            instr->op = OP_LW;
            return 1;

        case 0x2B:
            instr->op = OP_SW;
            return 1;

        case 0x04:
            instr->op = OP_BEQ;
            return 1;

        case 0x02:
            instr->op = OP_J;
            instr->imm = (int)TARGET(word);
            return 1;

        default:
            return 0;
    }
}