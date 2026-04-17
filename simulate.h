#ifndef SIMULATE_H
#define SIMULATE_H

#include "CPUParameters.h"

void run_single_cycle(Simulator *sim, const Program *prog, FILE *trace_out);
void run_pipeline(Simulator *sim, const Program *prog, FILE *trace_out);

void write_professor_style_report(FILE *out,
                                  const char *input_name,
                                  const Program *prog,
                                  const Simulator *single_sim,
                                  const Simulator *pipe_sim);

#endif
// done