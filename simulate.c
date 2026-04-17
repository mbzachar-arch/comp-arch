#include "simulate.h"
#include <stdio.h>
#include <string.h>

static int pipeline_empty(const PipelineState *p) {
    return (!p->if_id.valid &&
            !p->id_ex.valid &&
            !p->ex_mem.valid &&
            !p->mem_wb.valid);
}

static int instr_writes_register(const Instruction *instr) {
    switch (instr->op) {
        case OP_ADD:
        case OP_SUB:
        case OP_AND:
        case OP_OR:
        case OP_ADDI:
        case OP_LW:
            return 1;
        default:
            return 0;
    }
}

static int instr_dest_reg(const Instruction *instr) {
    switch (instr->op) {
        case OP_ADD:
        case OP_SUB:
        case OP_AND:
        case OP_OR:
            return instr->rd;
        case OP_ADDI:
        case OP_LW:
            return instr->rt;
        default:
            return -1;
    }
}

static int uses_rs(const Instruction *instr) {
    switch (instr->op) {
        case OP_ADD:
        case OP_SUB:
        case OP_AND:
        case OP_OR:
        case OP_ADDI:
        case OP_LW:
        case OP_SW:
        case OP_BEQ:
            return 1;
        default:
            return 0;
    }
}

static int uses_rt(const Instruction *instr) {
    switch (instr->op) {
        case OP_ADD:
        case OP_SUB:
        case OP_AND:
        case OP_OR:
        case OP_SW:
        case OP_BEQ:
            return 1;
        default:
            return 0;
    }
}

static int is_load(const Instruction *instr) {
    return instr->op == OP_LW;
}

static int hazard_with_instr(const Instruction *id_instr, const Instruction *older_instr) {
    int dest;

    if (!instr_writes_register(older_instr)) return 0;

    dest = instr_dest_reg(older_instr);
    if (dest <= 0) return 0;

    if (uses_rs(id_instr) && id_instr->rs == dest) return 1;
    if (uses_rt(id_instr) && id_instr->rt == dest) return 1;

    return 0;
}

static int detect_data_hazard(const PipelineState *p, int has_forwarding) {
    if (!p->if_id.valid) return 0;

    if (!has_forwarding) {
        if (p->id_ex.valid && hazard_with_instr(&p->if_id.instr, &p->id_ex.instr)) return 1;
        if (p->ex_mem.valid && hazard_with_instr(&p->if_id.instr, &p->ex_mem.instr)) return 1;
        return 0;
    }

    /* with forwarding enabled, only stall on load-use */
    if (p->id_ex.valid &&
        is_load(&p->id_ex.instr) &&
        hazard_with_instr(&p->if_id.instr, &p->id_ex.instr)) {
        return 1;
    }

    return 0;
}

static int forward_from_mem_wb(const Simulator *sim, int reg_num, int original_value) {
    int dest;

    if (!sim->pipe.mem_wb.valid) return original_value;

    dest = instr_dest_reg(&sim->pipe.mem_wb.instr);
    if (dest <= 0 || dest != reg_num) return original_value;

    if (sim->pipe.mem_wb.instr.op == OP_LW) {
        return sim->pipe.mem_wb.mem_data;
    }

    if (instr_writes_register(&sim->pipe.mem_wb.instr)) {
        return sim->pipe.mem_wb.alu_result;
    }

    return original_value;
}

static int forward_from_ex_mem(const Simulator *sim, int reg_num, int original_value) {
    int dest;

    if (!sim->pipe.ex_mem.valid) return original_value;

    dest = instr_dest_reg(&sim->pipe.ex_mem.instr);
    if (dest <= 0 || dest != reg_num) return original_value;

    /* load result not ready yet in EX/MEM */
    if (sim->pipe.ex_mem.instr.op == OP_LW) {
        return original_value;
    }

    if (instr_writes_register(&sim->pipe.ex_mem.instr)) {
        return sim->pipe.ex_mem.alu_result;
    }

    return original_value;
}

static int get_forwarded_value(const Simulator *sim, int reg_num, int original_value) {
    int value;

    value = forward_from_ex_mem(sim, reg_num, original_value);
    value = forward_from_mem_wb(sim, reg_num, value);

    return value;
}

static void print_single_cycle_state(FILE *out, int cycle, const Instruction *instr) {
    if (out == NULL) return;
    fprintf(out, "Single-Cycle: Cycle %d -> %s\n", cycle, opcode_name(instr->op));
}

static void print_cycle_state(FILE *out, int cycle, const PipelineState *p) {
    if (out == NULL) return;
    fprintf(out, "Pipeline Cycle %d\n", cycle);
    print_pipeline_state(out, p);
    fprintf(out, "\n");
}

static void execute_single_instruction(CPUState *cpu, const Instruction *instr) {
    int addr;

    switch (instr->op) {
        case OP_ADD:
            if (instr->rd != 0) {
                cpu->reg[instr->rd] = cpu->reg[instr->rs] + cpu->reg[instr->rt];
            }
            cpu->pc++;
            break;

        case OP_SUB:
            if (instr->rd != 0) {
                cpu->reg[instr->rd] = cpu->reg[instr->rs] - cpu->reg[instr->rt];
            }
            cpu->pc++;
            break;

        case OP_AND:
            if (instr->rd != 0) {
                cpu->reg[instr->rd] = cpu->reg[instr->rs] & cpu->reg[instr->rt];
            }
            cpu->pc++;
            break;

        case OP_OR:
            if (instr->rd != 0) {
                cpu->reg[instr->rd] = cpu->reg[instr->rs] | cpu->reg[instr->rt];
            }
            cpu->pc++;
            break;

        case OP_ADDI:
            if (instr->rt != 0) {
                cpu->reg[instr->rt] = cpu->reg[instr->rs] + instr->imm;
            }
            cpu->pc++;
            break;

        case OP_LW:
            addr = cpu->reg[instr->rs] + instr->imm;
            if (addr >= 0 && addr < MEM_SIZE && instr->rt != 0) {
                cpu->reg[instr->rt] = cpu->mem[addr];
            }
            cpu->pc++;
            break;

        case OP_SW:
            addr = cpu->reg[instr->rs] + instr->imm;
            if (addr >= 0 && addr < MEM_SIZE) {
                cpu->mem[addr] = cpu->reg[instr->rt];
            }
            cpu->pc++;
            break;

        case OP_BEQ:
            if (cpu->reg[instr->rs] == cpu->reg[instr->rt]) {
                cpu->pc = cpu->pc + 1 + instr->imm;
            } else {
                cpu->pc++;
            }
            break;

        case OP_J:
            cpu->pc = instr->imm;
            break;

        case OP_NOP:
        default:
            cpu->pc++;
            break;
    }

    cpu->reg[0] = 0;
}

static void classify_instruction(InstructionStats *s, const Instruction *instr) {
    switch (instr->op) {
        case OP_LW:
            s->lw_count++;
            break;
        case OP_SW:
            s->sw_count++;
            break;
        case OP_BEQ:
            s->beq_count++;
            break;
        case OP_J:
            s->j_count++;
            break;
        case OP_ADD:
        case OP_SUB:
        case OP_AND:
        case OP_OR:
        case OP_ADDI:
            s->rtype_count++;
            break;
        case OP_NOP:
            s->nop_count++;
            break;
        default:
            break;
    }

    s->total_count++;
}

static void collect_instruction_stats(Simulator *sim, const Program *prog) {
    int i;

    init_instruction_stats(&sim->stats);

    for (i = 0; i < prog->instr_count; i++) {
        classify_instruction(&sim->stats, &prog->instr_mem[i]);
    }
}

static void finalize_nonpipeline_timing(Simulator *sim) {
    InstructionStats *s = &sim->stats;
    PerformanceMetrics *m = &sim->metrics;
    CPUParams *p = &sim->params;
    double total_time_ps;

    total_time_ps =
        (s->lw_count * p->lw_ps) +
        (s->sw_count * p->sw_ps) +
        (s->rtype_count * p->rtype_ps) +
        (s->beq_count * p->beq_ps) +
        (s->j_count * p->j_ps) +
        (s->nop_count * p->nop_ps);

    m->reference_clock_ps = p->single_cycle_reference_clock_ps;
    m->effective_clock_ps = p->single_cycle_reference_clock_ps;
    m->total_execution_time_ps = total_time_ps;

    if (m->instruction_count > 0) {
        m->average_instruction_latency_ps = total_time_ps / m->instruction_count;
        m->cpi = 1.0;
    } else {
        m->average_instruction_latency_ps = 0.0;
        m->cpi = 0.0;
    }

    m->latency_sec = total_time_ps * 1e-12;

    if (m->latency_sec > 0.0) {
        m->throughput_ips = (double)m->instruction_count / m->latency_sec;
    } else {
        m->throughput_ips = 0.0;
    }
}

static void finalize_pipeline_timing(Simulator *sim) {
    PerformanceMetrics *m = &sim->metrics;
    CPUParams *p = &sim->params;

    m->reference_clock_ps = p->single_cycle_reference_clock_ps;
    m->effective_clock_ps = p->pipeline_clock_ps;
    m->total_execution_time_ps = m->total_cycles * p->pipeline_clock_ps;

    if (m->instruction_count > 0) {
        m->average_instruction_latency_ps =
            m->total_execution_time_ps / m->instruction_count;
        m->cpi = (double)m->total_cycles / (double)m->instruction_count;
    } else {
        m->average_instruction_latency_ps = 0.0;
        m->cpi = 0.0;
    }

    m->latency_sec = m->total_execution_time_ps * 1e-12;

    if (m->latency_sec > 0.0) {
        m->throughput_ips = (double)m->instruction_count / m->latency_sec;
    } else {
        m->throughput_ips = 0.0;
    }
}

void run_single_cycle(Simulator *sim, const Program *prog, FILE *trace_out) {
    int executed = 0;

    init_simulator(sim, "Single-Cycle CPU");
    sim->metrics.instruction_count = 0;
    collect_instruction_stats(sim, prog);

    while (sim->cpu.pc >= 0 &&
           sim->cpu.pc < prog->instr_count &&
           executed < prog->instr_count * 1000) {

        print_single_cycle_state(trace_out,
                                 sim->metrics.total_cycles + 1,
                                 &prog->instr_mem[sim->cpu.pc]);

        execute_single_instruction(&sim->cpu, &prog->instr_mem[sim->cpu.pc]);

        sim->metrics.total_cycles++;
        sim->metrics.instruction_count++;
        executed++;
    }

    add_metric_note(&sim->metrics, "Single-cycle model uses per-instruction delays in ps.");
    finalize_nonpipeline_timing(sim);
}

void run_pipeline(Simulator *sim, const Program *prog, FILE *trace_out) {
    PipelineState new_pipe;

    init_simulator(sim, "5-Stage Pipelined CPU");
    sim->metrics.instruction_count = 0;
    collect_instruction_stats(sim, prog);

    while ((sim->cpu.pc < prog->instr_count || !pipeline_empty(&sim->pipe)) &&
           sim->metrics.total_cycles < prog->instr_count * 1000 + 100) {

        int hazard = 0;
        int control_taken = 0;
        int control_target = 0;

        sim->metrics.total_cycles++;
        new_pipe = sim->pipe;

        /* ==================== WB ==================== */
        if (sim->pipe.mem_wb.valid) {
            Instruction *instr = &sim->pipe.mem_wb.instr;

            /* count retired dynamic instructions */
            sim->metrics.instruction_count++;

            switch (instr->op) {
                case OP_ADD:
                case OP_SUB:
                case OP_AND:
                case OP_OR:
                    if (instr->rd != 0) {
                        sim->cpu.reg[instr->rd] = sim->pipe.mem_wb.alu_result;
                    }
                    break;

                case OP_ADDI:
                    if (instr->rt != 0) {
                        sim->cpu.reg[instr->rt] = sim->pipe.mem_wb.alu_result;
                    }
                    break;

                case OP_LW:
                    if (instr->rt != 0) {
                        sim->cpu.reg[instr->rt] = sim->pipe.mem_wb.mem_data;
                    }
                    break;

                default:
                    break;
            }
        }

        sim->cpu.reg[0] = 0;

        /* ==================== MEM ==================== */
        memset(&new_pipe.mem_wb, 0, sizeof(MEM_WB_Reg));
        if (sim->pipe.ex_mem.valid) {
            new_pipe.mem_wb.valid = 1;
            new_pipe.mem_wb.pc = sim->pipe.ex_mem.pc;
            new_pipe.mem_wb.instr = sim->pipe.ex_mem.instr;
            new_pipe.mem_wb.alu_result = sim->pipe.ex_mem.alu_result;
            new_pipe.mem_wb.mem_data = 0;

            switch (sim->pipe.ex_mem.instr.op) {
                case OP_LW:
                    if (sim->pipe.ex_mem.alu_result >= 0 &&
                        sim->pipe.ex_mem.alu_result < MEM_SIZE) {
                        new_pipe.mem_wb.mem_data =
                            sim->cpu.mem[sim->pipe.ex_mem.alu_result];
                    }
                    break;

                case OP_SW:
                    if (sim->pipe.ex_mem.alu_result >= 0 &&
                        sim->pipe.ex_mem.alu_result < MEM_SIZE) {
                        sim->cpu.mem[sim->pipe.ex_mem.alu_result] =
                            sim->pipe.ex_mem.rt_value;
                    }
                    break;

                default:
                    break;
            }
        }

        /* ==================== EX ==================== */
        memset(&new_pipe.ex_mem, 0, sizeof(EX_MEM_Reg));
        if (sim->pipe.id_ex.valid) {
            int rs_val = sim->pipe.id_ex.rs_value;
            int rt_val = sim->pipe.id_ex.rt_value;

            rs_val = get_forwarded_value(sim, sim->pipe.id_ex.instr.rs, rs_val);
            rt_val = get_forwarded_value(sim, sim->pipe.id_ex.instr.rt, rt_val);

            new_pipe.ex_mem.valid = 1;
            new_pipe.ex_mem.pc = sim->pipe.id_ex.pc;
            new_pipe.ex_mem.instr = sim->pipe.id_ex.instr;
            new_pipe.ex_mem.rt_value = rt_val;
            new_pipe.ex_mem.branch_taken = 0;
            new_pipe.ex_mem.branch_target = 0;

            switch (sim->pipe.id_ex.instr.op) {
                case OP_ADD:
                    new_pipe.ex_mem.alu_result = rs_val + rt_val;
                    break;

                case OP_SUB:
                    new_pipe.ex_mem.alu_result = rs_val - rt_val;
                    break;

                case OP_AND:
                    new_pipe.ex_mem.alu_result = rs_val & rt_val;
                    break;

                case OP_OR:
                    new_pipe.ex_mem.alu_result = rs_val | rt_val;
                    break;

                case OP_ADDI:
                    new_pipe.ex_mem.alu_result = rs_val + sim->pipe.id_ex.imm;
                    break;

                case OP_LW:
                case OP_SW:
                    new_pipe.ex_mem.alu_result = rs_val + sim->pipe.id_ex.imm;
                    break;

                case OP_BEQ:
                    if (rs_val == rt_val) {
                        new_pipe.ex_mem.branch_taken = 1;
                        new_pipe.ex_mem.branch_target =
                            sim->pipe.id_ex.pc + 1 + sim->pipe.id_ex.imm;
                    }
                    break;

                case OP_J:
                    new_pipe.ex_mem.branch_taken = 1;
                    new_pipe.ex_mem.branch_target = sim->pipe.id_ex.instr.imm;
                    break;

                default:
                    break;
            }

            if ((new_pipe.ex_mem.instr.op == OP_BEQ ||
                 new_pipe.ex_mem.instr.op == OP_J) &&
                new_pipe.ex_mem.branch_taken) {
                control_taken = 1;
                control_target = new_pipe.ex_mem.branch_target;
            }
        }

        /* ==================== hazard detect ==================== */
        hazard = detect_data_hazard(&sim->pipe, sim->params.has_forwarding);

        /* ==================== ID ==================== */
        memset(&new_pipe.id_ex, 0, sizeof(ID_EX_Reg));

        if (control_taken) {
            sim->metrics.control_hazards++;
            sim->metrics.flush_cycles += 2;
        } else if (hazard) {
            sim->metrics.data_hazards++;
            sim->metrics.stall_cycles++;
        } else if (sim->pipe.if_id.valid) {
            new_pipe.id_ex.valid = 1;
            new_pipe.id_ex.pc = sim->pipe.if_id.pc;
            new_pipe.id_ex.instr = sim->pipe.if_id.instr;
            new_pipe.id_ex.rs_value = sim->cpu.reg[sim->pipe.if_id.instr.rs];
            new_pipe.id_ex.rt_value = sim->cpu.reg[sim->pipe.if_id.instr.rt];
            new_pipe.id_ex.imm = sim->pipe.if_id.instr.imm;
        }

        /* ==================== IF ==================== */
        memset(&new_pipe.if_id, 0, sizeof(IF_ID_Reg));

        if (control_taken) {
            sim->cpu.pc = control_target;
        } else if (hazard) {
            new_pipe.if_id = sim->pipe.if_id;
        } else {
            if (sim->cpu.pc >= 0 && sim->cpu.pc < prog->instr_count) {
                new_pipe.if_id.valid = 1;
                new_pipe.if_id.pc = sim->cpu.pc;
                new_pipe.if_id.instr = prog->instr_mem[sim->cpu.pc];
                sim->cpu.pc++;
            }
        }

        sim->pipe = new_pipe;
        sim->cpu.reg[0] = 0;

        print_cycle_state(trace_out, sim->metrics.total_cycles, &sim->pipe);
    }

    finalize_pipeline_timing(sim);
}

void write_professor_style_report(FILE *out,
                                  const char *input_name,
                                  const Program *prog,
                                  const Simulator *single_sim,
                                  const Simulator *pipe_sim) {
    const InstructionStats *s = &single_sim->stats;

    (void)prog;

    fprintf(out, "Processed 1 file(s): ['%s']\n\n", input_name);

    fprintf(out, "Instruction counts:\n");
    fprintf(out, "lw=%d, sw=%d, R-type=%d, beq=%d",
            s->lw_count, s->sw_count, s->rtype_count, s->beq_count);

    if (s->j_count > 0) fprintf(out, ", j=%d", s->j_count);
    if (s->nop_count > 0) fprintf(out, ", nop=%d", s->nop_count);

    fprintf(out, "\n");
    fprintf(out, "Total instructions: %d\n\n", s->total_count);

    fprintf(out, "=== Non-pipeline mode ===\n");
    fprintf(out, "Per-instruction execution time (ps):\n");
    fprintf(out, "  lw: %.0f\n", single_sim->params.lw_ps);
    fprintf(out, "  sw: %.0f\n", single_sim->params.sw_ps);
    fprintf(out, "  R-type: %.0f\n", single_sim->params.rtype_ps);
    fprintf(out, "  beq: %.0f\n", single_sim->params.beq_ps);
    if (s->j_count > 0) fprintf(out, "  j: %.0f\n", single_sim->params.j_ps);

    fprintf(out, "Average CPI: %.3f\n", single_sim->metrics.cpi);
    fprintf(out, "Total execution time: %.0f ps\n", single_sim->metrics.total_execution_time_ps);
    fprintf(out, "Average instruction latency: %.3f ps\n", single_sim->metrics.average_instruction_latency_ps);
    fprintf(out, "Average throughput: %.3f instr/s\n\n", single_sim->metrics.throughput_ips);

    fprintf(out, "=== Pipeline mode ===\n");
    fprintf(out, "Single-cycle reference clock: %.0f ps\n",
            pipe_sim->metrics.reference_clock_ps);
    fprintf(out, "\tPipelined clock: %.0f ps\n\n",
            pipe_sim->metrics.effective_clock_ps);

    fprintf(out, "\tTotal cycles: %d\n", pipe_sim->metrics.total_cycles);
    fprintf(out, "\tStall cycles: %d\n", pipe_sim->metrics.stall_cycles);
    fprintf(out, "\tFlush cycles: %d\n", pipe_sim->metrics.flush_cycles);
    fprintf(out, "\tData hazards: %d\n", pipe_sim->metrics.data_hazards);
    fprintf(out, "\tControl hazards: %d\n", pipe_sim->metrics.control_hazards);
    fprintf(out, "\tAverage CPI: %.3f\n", pipe_sim->metrics.cpi);
    fprintf(out, "\tTotal execution time: %.0f ps\n", pipe_sim->metrics.total_execution_time_ps);
    fprintf(out, "\tAverage instruction latency: %.3f ps\n", pipe_sim->metrics.average_instruction_latency_ps);
    fprintf(out, "\tAverage throughput: %.3f instr/s\n", pipe_sim->metrics.throughput_ips);
    fprintf(out, "\n");
}