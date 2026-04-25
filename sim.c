#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim.h"

// Zero all simulator state arrays and initialize control flags to default values
SimulatorState new_simulator(void) {
    SimulatorState sim;
    memset(sim.memory, 0, sizeof(sim.memory));
    memset(sim.registers, 0, sizeof(sim.registers));
    memset(sim.hwregister, 0, sizeof(sim.hwregister));
    memset(sim.disk, 0, sizeof(sim.disk));
    memset(sim.monitor, 0, sizeof(sim.monitor));
    sim.max_irq2_cycle = 0;
    sim.registers[0] = 0;
    sim.PC = 0;
    sim.cycle = 0;
    sim.halted = false;
    sim.in_interrupt = false;
    sim.bigimm_flag = false;
    sim.disk_cycle_count = 0;
    sim.disk_in_progress = false;

    return sim;
}
void load_inputs(SimulatorState* sim, const char* memin) {
    FILE* f;
    char line[100];

    // Load initial memory contents from memin.txt
    f = fopen(memin, "r");
    if (!f) { perror(memin); exit(1); }

    for (int i = 0; i < MEM_SIZE && fgets(line, sizeof(line), f); i++) {
        sscanf(line, "%x", &sim->memory[i]);
    }
    fclose(f);
}

FILE* load_outputs(SimulatorState* sim,const char* code, FILE* trace_file, FILE* sram_out_file) {
    
    char base_name[256];
    char trace_filename[256];
    char sram_filename[256];

    strncpy(base_name, code, sizeof(base_name) - 1);
    base_name[sizeof(base_name) - 1] = '\0'; 

    char *dot = strrchr(base_name, '.');
    if (dot != NULL) {
        *dot = '\0'; 
    }

    snprintf(trace_filename, sizeof(trace_filename), "%s_trace.txt", base_name);
    snprintf(sram_filename, sizeof(sram_filename), "%s_sram_out.txt", base_name);

    trace_file = fopen(trace_filename, "w");
    if (trace_file == NULL) {
        printf("Error: Could not create %s\n", trace_filename);
        exit(1);
    }

    sram_out_file = fopen(sram_filename, "w");
    if (sram_out_file == NULL) {
        printf("Error: Could not create %s\n", sram_filename);
        fclose(trace_file); 
        exit(1);
    }

    return trace_file;
}

// void write_outputs(SimulatorState* sim, const char* code) {

//     FILE* f;

//     //memout
//     f = fopen(memout, "w");
//     int last_used_index = MEM_SIZE;
//     while (last_used_index >= 0 && sim->memory[last_used_index] == 0)
//         last_used_index--;

//     for (int i = 0; i <= last_used_index; i++)
//         fprintf(f, "%08X\n", sim->memory[i]);
//     fclose(f);

// }

// Fetch the current instruction from memory and decode opcode and operands
void fetch_decode_execute(SimulatorState* sim, const char* trace_file) {

    uint32_t next_pc = sim->PC;
    uint32_t current_cycle = sim->cycle;

    //fetch
    uint32_t inst = sim->memory[sim->PC];
    uint16_t imm16 = inst & 0xFFFF;
    uint32_t imm32 = (uint32_t)(int32_t)(int16_t)imm16;

    //decode
    uint8_t opcode = (inst >> 25) & 0x1F;
    uint8_t rd = (inst >> 22) & 0x7;
    uint8_t rs = (inst >> 19) & 0x7;
    uint8_t rt = (inst >> 16) & 0x7;

    uint32_t* R = sim->registers;

    uint32_t val_rs = (rs == 1) ? imm32 : R[rs];
    uint32_t val_rt = (rt == 1) ? imm32 : R[rt];
    uint32_t val_rd = (rd == 1) ? imm32 : R[rd];

    switch (opcode) {
    
    case 0: if (rd >= 2) R[rd] = val_rs + val_rt; sim->PC = next_pc; break; //add
    case 1: if (rd >= 2) R[rd] = val_rs - val_rt; sim->PC = next_pc; break; //sub
    case 2: if (rd >= 2) R[rd] = val_rs << val_rt; sim->PC = next_pc; break;  //LSF
    case 3: if (rd >= 2) R[rd] = ((int32_t)val_rs) >> val_rt; sim->PC = next_pc; break; //RSF
    case 4: if (rd >= 2) R[rd] = val_rs & val_rt; sim->PC = next_pc; break; //and
    case 5: if (rd >= 2) R[rd] = val_rs | val_rt; sim->PC = next_pc; break; //or
    case 6: if (rd >= 2) R[rd] = val_rs ^ val_rt; sim->PC = next_pc; break; //xor

case 7: 
    if (rd >= 2) R[rd] = (R[rd] & 0x0000FFFF) | (imm32 << 16); 
    sim->PC = next_pc; 
    break; // LHI



    ////////////////////////////////////////////////////////////////////////////////////////////////
        // shifts
    case 6: if (rd >= 2) R[rd] = val_rs << val_rt; sim->PC = next_pc; break;              //shl
    case 3: if (rd >= 2) R[rd] = ((int32_t)val_rs) >> val_rt; sim->PC = next_pc; break;  //shr
    case 8: if (rd >= 2) R[rd] = val_rs >> val_rt; sim->PC = next_pc; break;            //sar

        // conditional jumps
    case 9:  if (val_rs == val_rt) sim->PC = val_rd; else sim->PC = next_pc; break;                   //beq
    case 10: if (val_rs != val_rt) sim->PC = val_rd; else sim->PC = next_pc; break;                   //bne
    case 11: if ((int32_t)val_rs < (int32_t)val_rt) sim->PC = val_rd; else sim->PC = next_pc; break;  //blt
    case 12: if ((int32_t)val_rs > (int32_t)val_rt) sim->PC = val_rd; else sim->PC = next_pc; break;  //bgt
    case 13: if ((int32_t)val_rs <= (int32_t)val_rt) sim->PC = val_rd; else sim->PC = next_pc; break; //ble
    case 14: if ((int32_t)val_rs >= (int32_t)val_rt) sim->PC = val_rd; else sim->PC = next_pc; break; //bge

    case 15: // jal
        if (rd >= 2) {
            R[rd] = next_pc;
            sim->PC = val_rs;
        }
        break;

    case 16:  // lw
        if (rd >= 2)
            if ((val_rs + val_rt) < MEM_SIZE)
                R[rd] = sim->memory[val_rs + val_rt];
        sim->PC = next_pc;
        break;

    case 17: // sw
        sim->memory[val_rs + val_rt] = val_rd;
        sim->PC = next_pc;
        break;

    case 18: // reti
        sim->PC = IO[7];
        sim->in_interrupt = false;
        break;

    case 19: // in 
        if (rd >= 2) {
            R[rd] = IO[val_rs + val_rt];
            update_hwregtrace(sim, hwregtrace_file, "READ", val_rs + val_rt, current_cycle);
            sim->PC = next_pc;
        }
        break;

    case 20: // out 
        IO[val_rs + val_rt] = val_rd;
        update_hwregtrace(sim, hwregtrace_file, "WRITE", val_rs + val_rt, current_cycle);
        sim->PC = next_pc;
        break;

    case 21: //halt
        sim->cycle = current_cycle;
        sim->halted = true;
        break;
    }
}

void update_traces(SimulatorState* sim, const char* trace, const char* leds, const char* seg7, uint32_t prev_regs[NUM_REGISTERS]
    , uint32_t inst, uint32_t imm_value, int16_t current_pc)
{
    //trace
    FILE* ftrace = fopen(trace, "a");
    if (!ftrace) {
        perror("Error opening trace.txt");
    }
    else {
        fprintf(ftrace, "%08X %03X %08X", sim->cycle - 1, current_pc, inst);

        for (int i = 0; i < NUM_REGISTERS; i++) {
            if (i == 0)
                fprintf(ftrace, " %08X", 0);
            else if (i == 1)
                fprintf(ftrace, " %08X", imm_value);
            else
                fprintf(ftrace, " %08X", prev_regs[i]);
        }

        fprintf(ftrace, "\n");
        fclose(ftrace);
    }

    //monitor
    if (sim->hwregister[22] == 1) { // monitorcmd == 1
        uint16_t addr = sim->hwregister[20];   // monitoraddr
        uint8_t value = (uint8_t)(sim->hwregister[21] & 0xFF); // monitordata 
        if (addr < MONITOR_SIZE)
            sim->monitor[addr] = value;
        sim->hwregister[22] = 0;    // monitorcmd = 0
    }

    //leds
    static uint32_t last_leds = 0x00000000; //initial value
    if (sim->hwregister[9] != last_leds) {
        FILE* fleds = fopen(leds, "a");
        if (fleds) {
            fprintf(fleds, "%08X %08X\n", sim->cycle - 1, sim->hwregister[9]);
            fclose(fleds);
            last_leds = sim->hwregister[9];
        }
        else
            perror("Error opening leds.txt");
    }

    //seg7
    static uint32_t last_seg7 = 0x00000000;
    if (sim->hwregister[10] != last_seg7) {
        FILE* fseg = fopen(seg7, "a");
        if (fseg) {
            fprintf(fseg, "%08X %08X\n", sim->cycle - 1, sim->hwregister[10]);
            fclose(fseg);
            last_seg7 = sim->hwregister[10];
        }
        else
            perror("Error opening display7seg.txt");


    }
}

void run_simulator(SimulatorState* sim, const char* code, FILE* trace_file, FILE* sram_out_file)
{
    // Run until HALT instruction is encountered
    while (!sim->halted) {

        // Save the current PC before instruction execution
        int16_t current_pc = sim->PC;

        // Backup registers for trace comparison
        uint32_t save_regs[NUM_REGISTERS];
        for (int i = 0; i < NUM_REGISTERS; i++)
            save_regs[i] = sim->registers[i];

        // Execute instruction
        fetch_decode_execute(sim, trace_file);
        sim->cycle++; // Advance simulation cycle

        // Update trace and monitor/leds/7seg output
       // update_traces(sim, trace_file);

        //Check if we need to handle an interrupt now
        check_if_interrupt_occured(sim);
    }
}

// initialize simulator, load inputs, run simulation and write outputs
int main(int argc, char* argv[]) {

    FILE *trace_file;
    FILE *sram_out_file;

    SimulatorState sim = new_simulator();
    load_inputs(&sim, argv[1]);
    load_outputs(&sim,  argv[1], &trace_file, &sram_out_file);
    run_simulator(&sim, argv[1], trace_file, sram_out_file);
 //   write_outputs(&sim, argv[1], trace_file, sram_out_file);

    return 0;
}

