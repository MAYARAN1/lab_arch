#ifndef __SIM_H__
#define __SIM_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// CONSTANTS
#define MEM_SIZE  65536           
#define NUM_REGISTERS 8                              

typedef struct {
    uint32_t memory[MEM_SIZE];
    uint32_t registers[NUM_REGISTERS];
    uint32_t cycle;
    uint16_t PC;
    bool halted;
} SimulatorState;

// Initializes a new simulator
SimulatorState new_simulator(void);
// Loads inputs
int load_inputs(SimulatorState* sim, const char* memin);
// Does one iteration of fetch/decode/execute
void fetch_decode_execute(SimulatorState* sim, FILE* trace_file);
void update_traces(SimulatorState* sim, FILE* trace_file, uint16_t current_pc, uint32_t inst, uint32_t save_regs[NUM_REGISTERS]);
// Main simulation loop runs fetch/decode/execute until HALT
void run_simulator(SimulatorState* sim, FILE* trace_file);
// Writes all output files after simulation completes
void write_outputs(SimulatorState* sim);
#endif