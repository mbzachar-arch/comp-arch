#ifndef CPUPARAMETERS_H
#define CPUPARAMETERS_H

#include <stdio.h>
#include <string.h>

#define REG_COUNT 32
#define MEM_SIZE 256
#define MAX_INSTR 1024
#define MAX_NOTES 256
#define NOTE_LEN 128

typedef enum {
    OP_ADD,
    OP_SUB,
    OP_AND,
    OP_OR,
    OP_ADDI,
    OP_LW,
    OP_SW,
    OP_BEQ,
    OP_NOP,
    OP_INVALID
} OpCode;

typedef struct {
    OpCode op;
    int rs;
    int rt;
    int rd;
    int imm;
    char label[32];
} Instruction;

typedef struct {
    Instruction instr_mem[MAX_INSTR];
    int instr_count;
} Program;

typedef struct {
    int pc;
    int reg[REG_COUNT];
    int mem[MEM_SIZE];
} CPUState;

typedef struct {
    char name[32];
    double clock_rate_hz;
    int pipeline_depth;
    int single_cycle_ipc;
    int pipelined_ipc;
    int has_forwarding;
} CPUParams;

typedef struct {
    int instruction_count;
    int total_cycles;
    int stall_cycles;
    int flush_cycles;
    int data_hazards;
    int control_hazards;

    double cpi;
    double latency_sec;
    double throughput_ips;
    double speedup;

    char notes[MAX_NOTES][NOTE_LEN];
    int note_count;
} PerformanceMetrics;

typedef struct {
    int valid;
    int pc;
    Instruction instr;
} IF_ID_Reg;

typedef struct {
    int valid;
    int pc;
    Instruction instr;
    int rs_value;
    int rt_value;
    int imm;
} ID_EX_Reg;

typedef struct {
    int valid;
    int pc;
    Instruction instr;
    int alu_result;
    int rt_value;
    int branch_taken;
    int branch_target;
} EX_MEM_Reg;

typedef struct {
    int valid;
    int pc;
    Instruction instr;
    int mem_data;
    int alu_result;
} MEM_WB_Reg;

typedef struct {
    IF_ID_Reg if_id;
    ID_EX_Reg id_ex;
    EX_MEM_Reg ex_mem;
    MEM_WB_Reg mem_wb;
} PipelineState;

typedef struct {
    CPUState cpu;
    CPUParams params;
    PerformanceMetrics metrics;
    PipelineState pipe;
} Simulator;

void init_cpu_state(CPUState *cpu);
void init_cpu_params(CPUParams *params, const char *name);
void init_metrics(PerformanceMetrics *m);
void init_pipeline_state(PipelineState *p);
void init_simulator(Simulator *sim, const char *name);

void add_metric_note(PerformanceMetrics *m, const char *text);

const char *opcode_name(OpCode op);
void print_instruction(FILE *out, const Instruction *instr, int index);
void print_cpu_params(FILE *out, const CPUParams *params);
void print_pipeline_state(FILE *out, const PipelineState *p);
void print_metrics(FILE *out, const PerformanceMetrics *m);

#endif
