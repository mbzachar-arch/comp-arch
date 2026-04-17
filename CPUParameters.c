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
    params->has_forwarding = 1;
//values taken from sample output file in the project file
    params->lw_ps = 800.0;
    params->sw_ps = 700.0;
    params->rtype_ps = 600.0;
    params->beq_ps = 500.0;
    params->j_ps = 500.0;
    params->nop_ps = 100.0;

    params->single_cycle_reference_clock_ps = 800.0;
    params->pipeline_clock_ps = 200.0;
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

    m->total_execution_time_ps = 0.0;
    m->average_instruction_latency_ps = 0.0;
    m->reference_clock_ps = 0.0;
    m->effective_clock_ps = 0.0;

    m->note_count = 0;
}

void init_pipeline_state(PipelineState *p) {
    memset(p, 0, sizeof(PipelineState));
}

void init_instruction_stats(InstructionStats *s) {
    s->lw_count = 0;
    s->sw_count = 0;
    s->rtype_count = 0;
    s->beq_count = 0;
    s->j_count = 0;
    s->nop_count = 0;
    s->total_count = 0;
}

void init_simulator(Simulator *sim, const char *name) {
    init_cpu_state(&sim->cpu);
    init_cpu_params(&sim->params, name);
    init_metrics(&sim->metrics);
    init_pipeline_state(&sim->pipe);
    init_instruction_stats(&sim->stats);
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
        case OP_J:    return "j";
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

        case OP_J:
            fprintf(out, "j %d\n", instr->imm);
            break;

        case OP_NOP:
            fprintf(out, "nop\n");
            break;

        default:
            fprintf(out, "invalid\n");
            break;
    }
}

void print_pipeline_state(FILE *out, const PipelineState *p) {
    fprintf(out, "  IF/ID  : %s\n", p->if_id.valid ? opcode_name(p->if_id.instr.op) : "empty");
    fprintf(out, "  ID/EX  : %s\n", p->id_ex.valid ? opcode_name(p->id_ex.instr.op) : "empty");
    fprintf(out, "  EX/MEM : %s\n", p->ex_mem.valid ? opcode_name(p->ex_mem.instr.op) : "empty");
    fprintf(out, "  MEM/WB : %s\n", p->mem_wb.valid ? opcode_name(p->mem_wb.instr.op) : "empty");
}