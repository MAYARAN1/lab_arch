#ifndef __SIM_H__
#define __SIM_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// CONSTENTS
#define MEM_SIZE 4096
#define DISK_SECTORS 128            
#define SECTOR_SIZE 128             
#define NUM_REGISTERS 16             
#define NUM_HWREGS 23              
#define MONITOR_SIZE (256 * 256)       

// Main structure holding all simulator state variables, including memory, registers,
// hardware registers, disk, monitor, and control flags.

typedef struct {
    uint32_t memory[MEM_SIZE];                      // Main memory: 4096 words
    uint32_t registers[NUM_REGISTERS];              // General-purpose registers R0–R15
    uint32_t hwregister[NUM_HWREGS];                // Hardware registers for I/O and interrupts       
    uint32_t disk[DISK_SECTORS][SECTOR_SIZE];       // Simulated hard disk: 128 sectors of 128 words
    uint8_t monitor[MONITOR_SIZE];                  // 256x256 grayscale monitor frame buffer
    uint32_t max_irq2_cycle;
    bool* irq2_cycles;
   // bool in_bigimm_second_cycle;
    uint32_t cycle;
    uint16_t PC;
    bool in_interrupt;
    bool bigimm_flag;
    bool halted;
    uint32_t disk_cycle_count;
    bool disk_in_progress;
} SimulatorState;

// Initializes a new simulator with all fields zeroed or default
SimulatorState new_simulator(void);

// Loads initial memory, disk, and irq2 cycle triggers from files
void load_inputs(SimulatorState* sim, const char* memin, const char* diskin, const char* irq2in);

// Main simulation loop – runs fetch/decode/execute until HALT
void run_simulator(SimulatorState* sim, const char* memout, const char* regout, const char* hwregout,
    const char* diskout, const char* monitorout, const char* irq2out, const char* diskin,
    const char* irq2in, const char* hwregin, const char* regin);
void check_if_interrupt_occured(SimulatorState* sim);
void fetch_decode_execute(SimulatorState* sim, const char* trace_file, const char* hwregtrace_file);
void update_traces(SimulatorState* sim,
    const char* trace, const char* hwregtrace,
    const char* leds, const char* seg7,
    uint32_t prev_regs[NUM_REGISTERS],
    uint16_t pc, uint32_t inst, uint32_t imm_value);
void update_hwregtrace(SimulatorState* sim, const char* hwregtrace_file, const char* action, uint8_t hwreg_index, uint32_t current_cycle);

// Writes all output files after simulation completes
void write_outputs(SimulatorState* sim, const char* memout, const char* regout,
    const char* cycles_out, const char* leds_out, const char* display7seg_out,
    const char* diskout, const char* monitor_txt);
void load_outputs(SimulatorState* sim, const char* memout, const char* regout, const char* trace_out,
    const char* hwregtraces_out, const char* cycles_out, const char* leds_out, const char* display7seg_out,
    const char* diskout, const char* monitor_txt, const char* monitor_yuv);
#endif