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
    return (instr->op == OP_LW);
}

static int hazard_with_instr(const Instruction *id_instr, const Instruction *older_instr) {
    int dest;

    if (!instr_writes_register(older_instr)) {
        return 0;
    }

    dest = instr_dest_reg(older_instr);
    if (dest <= 0) {
        return 0;
    }

    if (uses_rs(id_instr) && id_instr->rs == dest) {
        return 1;
    }

    if (uses_rt(id_instr) && id_instr->rt == dest) {
        return 1;
    }

    return 0;
}

static int detect_data_hazard(const PipelineState *p, int has_forwarding) {
    if (!p->if_id.valid) {
        return 0;
    }

    if (!has_forwarding) {
        if (p->id_ex.valid && hazard_with_instr(&p->if_id.instr, &p->id_ex.instr)) {
            return 1;
        }
        if (p->ex_mem.valid && hazard_with_instr(&p->if_id.instr, &p->ex_mem.instr)) {
            return 1;
        }
        return 0;
    }

    if (p->id_ex.valid && is_load(&p->id_ex.instr) &&
        hazard_with_instr(&p->if_id.instr, &p->id_ex.instr)) {
        return 1;
    }

    return 0;
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

        case OP_NOP:
        default:
            cpu->pc++;
            break;
    }

    cpu->reg[0] = 0;
}

static void finalize_metrics(Simulator *sim) {
    PerformanceMetrics *m = &sim->metrics;

    if (m->instruction_count > 0) {
        m->cpi = (double)m->total_cycles / (double)m->instruction_count;
    } else {
        m->cpi = 0.0;
    }

    if (sim->params.clock_rate_hz > 0.0) {
        m->latency_sec = (double)m->total_cycles / sim->params.clock_rate_hz;
    } else {
        m->latency_sec = 0.0;
    }

    if (m->latency_sec > 0.0) {
        m->throughput_ips = (double)m->instruction_count / m->latency_sec;
    } else {
        m->throughput_ips = 0.0;
    }
}

void run_single_cycle(Simulator *sim, const Program *prog) {
    int executed = 0;

    init_simulator(sim, "Single-Cycle CPU");
    sim->metrics.instruction_count = prog->instr_count;

    while (sim->cpu.pc >= 0 &&
           sim->cpu.pc < prog->instr_count &&
           executed < prog->instr_count * 10) {
        execute_single_instruction(&sim->cpu, &prog->instr_mem[sim->cpu.pc]);
        sim->metrics.total_cycles++;
        executed++;
    }

    add_metric_note(&sim->metrics,
                    "Single-cycle model assumes one full cycle per instruction.");
    finalize_metrics(sim);
}

void run_pipeline(Simulator *sim, const Program *prog) {
    PipelineState new_pipe;
    char note[NOTE_LEN];

    init_simulator(sim, "5-Stage Pipelined CPU");
    sim->metrics.instruction_count = prog->instr_count;

    while ((sim->cpu.pc < prog->instr_count || !pipeline_empty(&sim->pipe)) &&
           sim->metrics.total_cycles < prog->instr_count * 20 + 20) {

        int hazard = 0;
        int branch_taken_this_cycle = 0;
        int branch_target = 0;

        sim->metrics.total_cycles++;
        new_pipe = sim->pipe;

        if (sim->pipe.mem_wb.valid) {
            Instruction *instr = &sim->pipe.mem_wb.instr;

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

        memset(&new_pipe.ex_mem, 0, sizeof(EX_MEM_Reg));
        if (sim->pipe.id_ex.valid) {
            new_pipe.ex_mem.valid = 1;
            new_pipe.ex_mem.pc = sim->pipe.id_ex.pc;
            new_pipe.ex_mem.instr = sim->pipe.id_ex.instr;
            new_pipe.ex_mem.rt_value = sim->pipe.id_ex.rt_value;
            new_pipe.ex_mem.branch_taken = 0;
            new_pipe.ex_mem.branch_target = 0;

            switch (sim->pipe.id_ex.instr.op) {
                case OP_ADD:
                    new_pipe.ex_mem.alu_result =
                        sim->pipe.id_ex.rs_value + sim->pipe.id_ex.rt_value;
                    break;

                case OP_SUB:
                    new_pipe.ex_mem.alu_result =
                        sim->pipe.id_ex.rs_value - sim->pipe.id_ex.rt_value;
                    break;

                case OP_AND:
                    new_pipe.ex_mem.alu_result =
                        sim->pipe.id_ex.rs_value & sim->pipe.id_ex.rt_value;
                    break;

                case OP_OR:
                    new_pipe.ex_mem.alu_result =
                        sim->pipe.id_ex.rs_value | sim->pipe.id_ex.rt_value;
                    break;

                case OP_ADDI:
                    new_pipe.ex_mem.alu_result =
                        sim->pipe.id_ex.rs_value + sim->pipe.id_ex.imm;
                    break;

                case OP_LW:
                case OP_SW:
                    new_pipe.ex_mem.alu_result =
                        sim->pipe.id_ex.rs_value + sim->pipe.id_ex.imm;
                    break;

                case OP_BEQ:
                    if (sim->pipe.id_ex.rs_value == sim->pipe.id_ex.rt_value) {
                        new_pipe.ex_mem.branch_taken = 1;
                        new_pipe.ex_mem.branch_target =
                            sim->pipe.id_ex.pc + 1 + sim->pipe.id_ex.imm;
                    }
                    break;

                default:
                    break;
            }

            if (new_pipe.ex_mem.instr.op == OP_BEQ &&
                new_pipe.ex_mem.branch_taken) {
                branch_taken_this_cycle = 1;
                branch_target = new_pipe.ex_mem.branch_target;
            }
        }

        hazard = detect_data_hazard(&sim->pipe, sim->params.has_forwarding);

        memset(&new_pipe.id_ex, 0, sizeof(ID_EX_Reg));

        if (branch_taken_this_cycle) {
            sim->metrics.control_hazards++;
            sim->metrics.flush_cycles += 2;
            add_metric_note(&sim->metrics,
                            "Control hazard: taken BEQ flushed IF and ID stages.");
        } else if (hazard) {
            sim->metrics.data_hazards++;
            sim->metrics.stall_cycles++;

            if (sim->params.has_forwarding) {
                add_metric_note(&sim->metrics,
                                "Data hazard: load-use dependency caused a stall.");
            } else {
                add_metric_note(&sim->metrics,
                                "Data hazard: dependency caused a stall because forwarding is disabled.");
            }
        } else if (sim->pipe.if_id.valid) {
            new_pipe.id_ex.valid = 1;
            new_pipe.id_ex.pc = sim->pipe.if_id.pc;
            new_pipe.id_ex.instr = sim->pipe.if_id.instr;
            new_pipe.id_ex.rs_value = sim->cpu.reg[sim->pipe.if_id.instr.rs];
            new_pipe.id_ex.rt_value = sim->cpu.reg[sim->pipe.if_id.instr.rt];
            new_pipe.id_ex.imm = sim->pipe.if_id.instr.imm;
        }

        memset(&new_pipe.if_id, 0, sizeof(IF_ID_Reg));

        if (branch_taken_this_cycle) {
            sim->cpu.pc = branch_target;

            snprintf(note, sizeof(note),
                     "Branch taken in EX: PC redirected to instruction %d.",
                     branch_target);
            add_metric_note(&sim->metrics, note);
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
    }

    finalize_metrics(sim);
}

void write_report(FILE *out,
                  const Program *prog,
                  const Simulator *single_sim,
                  const Simulator *pipe_sim) {
    int i;

    fprintf(out, "=============================================\n");
    fprintf(out, "ECE 5367 Final Project: Pipelined Performance Analyzer\n");
    fprintf(out, "=============================================\n\n");

    fprintf(out, "=== Program Instructions ===\n");
    for (i = 0; i < prog->instr_count; i++) {
        print_instruction(out, &prog->instr_mem[i], i);
    }
    fprintf(out, "\n");

    print_cpu_params(out, &single_sim->params);
    print_metrics(out, &single_sim->metrics);

    print_cpu_params(out, &pipe_sim->params);
    print_metrics(out, &pipe_sim->metrics);

    fprintf(out, "=== Comparison Summary ===\n");
    fprintf(out, "Single-cycle total cycles : %d\n", single_sim->metrics.total_cycles);
    fprintf(out, "Pipelined total cycles    : %d\n", pipe_sim->metrics.total_cycles);

    if (pipe_sim->metrics.latency_sec > 0.0) {
        fprintf(out, "Observed speedup          : %.4f\n",
                single_sim->metrics.latency_sec / pipe_sim->metrics.latency_sec);
    } else {
        fprintf(out, "Observed speedup          : 0.0000\n");
    }

    if (pipe_sim->metrics.stall_cycles > 0 || pipe_sim->metrics.flush_cycles > 0) {
        fprintf(out, "Pipeline lost cycles due to stalls/flushes caused by hazards.\n");
    } else {
        fprintf(out, "Pipeline operated without detected stalls or flushes.\n");
    }

    fprintf(out, "\n");
}
