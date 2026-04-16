#include "CPUParameters.h"

void init_cpu_state(CPUState *cpu) {
    int i;
    cpu->pc = 0;

    for (i = 0; i < REG_COUNT; i++) {
        cpu->reg[i] = 0;
    }

    for (i = 0; i < MEM_SIZE; i++) {
        cpu->mem[i] = 0;
    }
}

void init_cpu_params(CPUParams *params, const char *name) {
    strncpy(params->name, name, sizeof(params->name) - 1);
    params->name[sizeof(params->name) - 1] = '\0';

    params->clock_rate_hz = 1e9;
    params->pipeline_depth = 5;
    params->single_cycle_ipc = 1;
    params->pipelined_ipc = 1;
    params->has_forwarding = 0;
}

void init_metrics(PerformanceMetrics *m) {
    m->instruction_count = 0;
    m->total_cycles = 0;
    m->stall_cycles = 0;
    m->flush_cycles = 0;
    m->data_hazards = 0;
    m->control_hazards = 0;

    m->cpi = 0.0;
    m->latency_sec = 0.0;
    m->throughput_ips = 0.0;
    m->speedup = 0.0;

    m->note_count = 0;
}

void init_pipeline_state(PipelineState *p) {
    memset(p, 0, sizeof(PipelineState));
}

void init_simulator(Simulator *sim, const char *name) {
    init_cpu_state(&sim->cpu);
    init_cpu_params(&sim->params, name);
    init_metrics(&sim->metrics);
    init_pipeline_state(&sim->pipe);
}

void add_metric_note(PerformanceMetrics *m, const char *text) {
    if (m->note_count < MAX_NOTES) {
        strncpy(m->notes[m->note_count], text, NOTE_LEN - 1);
        m->notes[m->note_count][NOTE_LEN - 1] = '\0';
        m->note_count++;
    }
}

const char *opcode_name(OpCode op) {
    switch (op) {
        case OP_ADD:  return "add";
        case OP_SUB:  return "sub";
        case OP_AND:  return "and";
        case OP_OR:   return "or";
        case OP_ADDI: return "addi";
        case OP_LW:   return "lw";
        case OP_SW:   return "sw";
        case OP_BEQ:  return "beq";
        case OP_NOP:  return "nop";
        default:      return "invalid";
    }
}

void print_instruction(FILE *out, const Instruction *instr, int index) {
    fprintf(out, "[%3d] ", index);

    switch (instr->op) {
        case OP_ADD:
        case OP_SUB:
        case OP_AND:
        case OP_OR:
            fprintf(out, "%s $%d, $%d, $%d\n",
                    opcode_name(instr->op), instr->rd, instr->rs, instr->rt);
            break;

        case OP_ADDI:
            fprintf(out, "%s $%d, $%d, %d\n",
                    opcode_name(instr->op), instr->rt, instr->rs, instr->imm);
            break;

        case OP_LW:
        case OP_SW:
            fprintf(out, "%s $%d, %d($%d)\n",
                    opcode_name(instr->op), instr->rt, instr->imm, instr->rs);
            break;

        case OP_BEQ:
            fprintf(out, "%s $%d, $%d, %d\n",
                    opcode_name(instr->op), instr->rs, instr->rt, instr->imm);
            break;

        case OP_NOP:
            fprintf(out, "nop\n");
            break;

        default:
            fprintf(out, "invalid\n");
            break;
    }
}

void print_cpu_params(FILE *out, const CPUParams *params) {
    fprintf(out, "=== CPU Parameters ===\n");
    fprintf(out, "Name             : %s\n", params->name);
    fprintf(out, "Clock rate       : %.2f Hz\n", params->clock_rate_hz);
    fprintf(out, "Pipeline depth   : %d\n", params->pipeline_depth);
    fprintf(out, "Single-cycle IPC : %d\n", params->single_cycle_ipc);
    fprintf(out, "Pipelined IPC    : %d\n", params->pipelined_ipc);
    fprintf(out, "Forwarding       : %s\n", params->has_forwarding ? "Yes" : "No");
    fprintf(out, "\n");
}

void print_pipeline_state(FILE *out, const PipelineState *p) {
    fprintf(out, "=== Pipeline Registers ===\n");

    fprintf(out, "IF/ID   : valid=%d, instr=%s\n",
            p->if_id.valid, opcode_name(p->if_id.instr.op));

    fprintf(out, "ID/EX   : valid=%d, instr=%s\n",
            p->id_ex.valid, opcode_name(p->id_ex.instr.op));

    fprintf(out, "EX/MEM  : valid=%d, instr=%s\n",
            p->ex_mem.valid, opcode_name(p->ex_mem.instr.op));

    fprintf(out, "MEM/WB  : valid=%d, instr=%s\n",
            p->mem_wb.valid, opcode_name(p->mem_wb.instr.op));

    fprintf(out, "\n");
}

void print_metrics(FILE *out, const PerformanceMetrics *m) {
    int i;

    fprintf(out, "=== Performance Metrics ===\n");
    fprintf(out, "Instruction count : %d\n", m->instruction_count);
    fprintf(out, "Total cycles      : %d\n", m->total_cycles);
    fprintf(out, "Stall cycles      : %d\n", m->stall_cycles);
    fprintf(out, "Flush cycles      : %d\n", m->flush_cycles);
    fprintf(out, "Data hazards      : %d\n", m->data_hazards);
    fprintf(out, "Control hazards   : %d\n", m->control_hazards);
    fprintf(out, "CPI               : %.4f\n", m->cpi);
    fprintf(out, "Latency (sec)     : %.8e\n", m->latency_sec);
    fprintf(out, "Throughput (IPS)  : %.2f\n", m->throughput_ips);
    fprintf(out, "Speedup           : %.4f\n", m->speedup);
    fprintf(out, "\n");

    if (m->note_count > 0) {
        fprintf(out, "=== Hazard / Execution Notes ===\n");
        for (i = 0; i < m->note_count; i++) {
            fprintf(out, "- %s\n", m->notes[i]);
        }
        fprintf(out, "\n");
    }
}
