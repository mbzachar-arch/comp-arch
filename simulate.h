#ifndef SIMULATE_H
#define SIMULATE_H

#include "CPUParameters.h"

void run_single_cycle(Simulator *sim, const Program *prog);
void run_pipeline(Simulator *sim, const Program *prog);
void write_report(FILE *out,
                  const Program *prog,
                  const Simulator *single_sim,
                  const Simulator *pipe_sim);

#endif
