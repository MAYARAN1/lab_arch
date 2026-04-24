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

void load_inputs(SimulatorState* sim, const char* memin, const char* diskin, const char* irq2in) {
    FILE* f;
    char line[100];

    // Load initial memory contents from memin.txt
    f = fopen(memin, "r");
    if (!f) { perror(memin); exit(1); }

    for (int i = 0; i < MEM_SIZE && fgets(line, sizeof(line), f); i++) {
        sscanf(line, "%x", &sim->memory[i]);
    }
    fclose(f);

    // Load initial disk contents from diskin.txt
    f = fopen(diskin, "r");
    if (!f) { perror("diskin.txt"); exit(1); }
    int s = 0, w = 0;
    while (fgets(line, sizeof(line), f)) {
        sscanf(line, "%x", &sim->disk[s][w++]);
        if (w == SECTOR_SIZE) { s++; w = 0; }
    }
    fclose(f);

    // Load IRQ2 activation cycles from irq2in.txt and allocate bool array
    f = fopen(irq2in, "r");

    int val;
    while (fscanf(f, "%d", &val) == 1) {
        if (val > sim->max_irq2_cycle)
            sim->max_irq2_cycle = val;
    }
    rewind(f);
    sim->irq2_cycles = calloc(sim->max_irq2_cycle + 1, sizeof(bool));

    while (fscanf(f, "%d", &val) == 1) {
        sim->irq2_cycles[val] = true;
    }
    fclose(f);
}

void load_outputs(SimulatorState* sim, const char* memout, const char* regout, const char* trace_out,
    const char* hwregtraces_out, const char* cycles_out, const char* leds_out, const char* display7seg_out,
    const char* diskout, const char* monitor_txt, const char* monitor_yuv) {
    FILE* f;
    f = fopen(memout, "w");fclose(f);
    f = fopen(regout, "w");fclose(f);
    f = fopen(trace_out, "w");fclose(f);
    f = fopen(hwregtraces_out, "w");fclose(f);
    f = fopen(cycles_out, "w");fclose(f);
    f = fopen(leds_out, "w");fclose(f);
    f = fopen(display7seg_out, "w");fclose(f);
}

void write_outputs(SimulatorState* sim, const char* memout, const char* regout, const char* trace_out,
    const char* hwregtraces_out, const char* cycles_out, const char* leds_out, const char* display7seg_out,
    const char* diskout, const char* monitor_txt, const char* monitor_yuv) {

    FILE* f;

    //memout
    f = fopen(memout, "w");
    int last_used_index = MEM_SIZE;
    while (last_used_index >= 0 && sim->memory[last_used_index] == 0)
        last_used_index--;

    for (int i = 0; i <= last_used_index; i++)
        fprintf(f, "%08X\n", sim->memory[i]);
    fclose(f);

    //regout
    f = fopen(regout, "w");
    for (int i = 2; i < NUM_REGISTERS; i++) fprintf(f, "%08X\n", sim->registers[i]);
    fclose(f);

    //cycles
    f = fopen(cycles_out, "w");
    fprintf(f, "%08llX\n", sim->cycle);
    free(sim->irq2_cycles);
    fclose(f);

    //diskout
    f = fopen(diskout, "w");
    for (int i = 0; i < DISK_SECTORS; i++)
        for (int j = 0; j < SECTOR_SIZE; j++)
            fprintf(f, "%08X\n", sim->disk[i][j]);
    fclose(f);

    //monitor_txt
    f = fopen(monitor_txt, "w");
    for (int i = 0; i < MONITOR_SIZE; i++)
        fprintf(f, "%02X\n", sim->monitor[i]);
    fclose(f);

    //monitor_yuv
    f = fopen(monitor_yuv, "wb");
    if (f) {
        fwrite(sim->monitor, sizeof(uint8_t), MONITOR_SIZE, f);
        fclose(f);
    }
}

void update_irq2(SimulatorState* sim) {

    if (sim->cycle <= sim->max_irq2_cycle && sim->irq2_cycles[sim->cycle]) {
        sim->hwregister[5] = 1;
    }
}

// Handle disk read/write commands using DMA-like simulation with 1024 cycles per operation
void update_disk(SimulatorState* sim) {

    if (!sim->disk_in_progress && sim->hwregister[14] != 0) {
        // diskcmd == 1 (read) or 2 (write)
        sim->disk_in_progress = true;
        sim->disk_cycle_count = 1024;
        sim->hwregister[17] = 1; // diskstatus = busy
    }
    if (sim->disk_in_progress) {
        sim->disk_cycle_count--;
        if (sim->disk_cycle_count == 0) {
            uint32_t sector = sim->hwregister[15];
            uint32_t buffer = sim->hwregister[16];
            if (sim->hwregister[14] == 1) { // read
                for (int i = 0; i < SECTOR_SIZE; i++)
                    sim->memory[buffer + i] = sim->disk[sector][i];
            }
            else if (sim->hwregister[14] == 2) { // write
                for (int i = 0; i < SECTOR_SIZE; i++)
                    sim->disk[sector][i] = sim->memory[buffer + i];
            }
            sim->hwregister[14] = 0;  // diskcmd = 0
            sim->hwregister[17] = 0;  // diskstatus = idle
            sim->hwregister[4] = 1;   // irq1status = 1
            sim->disk_in_progress = false;
        }
    }
}

// If timer is enabled, increment current value and trigger irq0 when limit reached
void update_timer(SimulatorState* sim) {
    if (sim->hwregister[11]) { // timerenable == 1
        sim->hwregister[12]++; // timercurrent++
        if (sim->hwregister[12] == sim->hwregister[13])  // == timermax
            sim->hwregister[0] = 1; // irq0status = 1
        if (sim->hwregister[12] == sim->hwregister[13] + 1)
            sim->hwregister[12] = 0; // reset timercurrent
    }
}

void check_if_interrupt_occured(SimulatorState* sim) {

    // Check if any enabled interrupt is currently triggered
    bool irq0 = sim->hwregister[0] && sim->hwregister[3]; // irq0enable & irq0status
    bool irq1 = sim->hwregister[1] && sim->hwregister[4]; // irq1enable & irq1status
    bool irq2 = sim->hwregister[2] && sim->hwregister[5]; // irq2enable & irq2status
    bool irq = irq0 || irq1 || irq2;

    // If so, and not already in an interrupt, jump to irqhandler and set irqreturn
    if (irq && !sim->in_interrupt) {
        sim->hwregister[7] = sim->PC; //save return address
        sim->PC = sim->hwregister[6]; //jump to interrupt handler
        sim->in_interrupt = true;
    }
}

// Fetch the current instruction from memory and decode opcode and operands
void fetch_decode_execute(SimulatorState* sim, const char* trace_file, const char* hwregtrace_file) {

    uint32_t next_pc = sim->PC;
    uint32_t current_cycle = sim->cycle;

    //fetch
    uint32_t inst = sim->memory[sim->PC];
    int8_t imm8 = inst & 0xFF;
    int32_t imm32 = 0;

    //decode
    uint8_t opcode = (inst >> 24) & 0x1F;
    uint8_t rd = (inst >> 20) & 0xF;
    uint8_t rs = (inst >> 16) & 0xF;
    uint8_t rt = (inst >> 12) & 0xF;

    // Determine if instruction uses a 32-bit immediate, and load it if needed
    sim->bigimm_flag = ((inst >> 8) & 1);
    if (sim->bigimm_flag) {
        imm32 = sim->memory[sim->PC + 1];
        next_pc += 2;
    }
    else {
        imm32 = (int32_t)imm8;
        next_pc++;
    }

    uint32_t* R = sim->registers;
    uint32_t* IO = sim->hwregister;

    uint32_t val_rs = (rs == 1) ? imm32 : R[rs];
    uint32_t val_rt = (rt == 1) ? imm32 : R[rt];
    uint32_t val_rd = (rd == 1) ? imm32 : R[rd];

    switch (opcode) {
        // arithmetic
    case 0: if (rd >= 2) R[rd] = val_rs + val_rt; sim->PC = next_pc; break; //add
    case 1: if (rd >= 2) R[rd] = val_rs - val_rt; sim->PC = next_pc; break; //sub
    case 2: if (rd >= 2) R[rd] = val_rs * val_rt; sim->PC = next_pc; break; //mul

        // logical
    case 3: if (rd >= 2) R[rd] = val_rs & val_rt; sim->PC = next_pc; break; //and
    case 4: if (rd >= 2) R[rd] = val_rs | val_rt; sim->PC = next_pc; break; //or
    case 5: if (rd >= 2) R[rd] = val_rs ^ val_rt; sim->PC = next_pc; break; //xor

        // shifts
    case 6: if (rd >= 2) R[rd] = val_rs << val_rt; sim->PC = next_pc; break;              //shl
    case 7: if (rd >= 2) R[rd] = ((int32_t)val_rs) >> val_rt; sim->PC = next_pc; break;  //shr
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

void update_hwregtrace(SimulatorState* sim, const char* hwregtrace_file, const char* action, uint8_t hwreg_index, uint32_t current_cycle) {

    const char* hwreg_names[] = {
        "irq0enable", "irq1enable", "irq2enable",
        "irq0status", "irq1status", "irq2status",
        "irqhandler", "irqreturn", "clks", "leds", "display7seg",
        "timerenable", "timercurrent", "timermax",
        "diskcmd", "disksector", "diskbuffer", "diskstatus",
        "reserved", "reserved", "monitoraddr", "monitordata", "monitorcmd"
    };

    FILE* f = fopen(hwregtrace_file, "a");
    if (!f) {
        perror("Failed to open hwregtrace file");
        exit(1);
    }

    if (hwreg_index <= NUM_HWREGS) {
        fprintf(f, "%08llX %s %s %08X\n",
            current_cycle,
            action,
            hwreg_names[hwreg_index],
            sim->hwregister[hwreg_index]);
    }
    fclose(f);
}

void run_simulator(SimulatorState* sim,
    const char* memout, const char* regout,
    const char* trace, const char* hwregtrace, const char* cycles,
    const char* leds, const char* seg7,
    const char* diskout, const char* monitor_txt, const char* monitor_yuv)
{
    // Run until HALT instruction is encountered
    while (!sim->halted) {

        // Save the current PC before instruction execution
        int16_t current_pc = sim->PC;

        // Update interrupt-related hardware: IRQ2 line, disk (IRQ1), and timer (IRQ0)
        update_irq2(sim);
        update_disk(sim);
        update_timer(sim);

        // Fetch instruction and immediate field
        uint32_t inst = sim->memory[sim->PC];
        int8_t imm8 = inst & 0xFF;
        sim->bigimm_flag = ((inst >> 8) & 1); //Check if instruction uses big immediate (32-bit)
        int32_t imm32 = 0;

        // Load the correct immediate value (imm8 or imm32)
        if (sim->bigimm_flag)
            imm32 = sim->memory[sim->PC + 1];
        else
            imm32 = (int32_t)imm8;

        // Backup registers for trace comparison
        uint32_t save_regs[NUM_REGISTERS];
        for (int i = 0; i < NUM_REGISTERS; i++)
            save_regs[i] = sim->registers[i];

        // Execute instruction
        fetch_decode_execute(sim, trace, hwregtrace);
        sim->cycle++; // Advance simulation cycle

        // If instruction had bigimm, we simulate 2nd cycle
        if (sim->bigimm_flag) {
            update_irq2(sim);
            update_timer(sim);
            update_disk(sim);
            sim->cycle++;
        }

        // Update trace and monitor/leds/7seg output
        update_traces(sim, trace, leds, seg7, save_regs, inst, imm32, current_pc);

        //Check if we need to handle an interrupt now
        check_if_interrupt_occured(sim);
    }
}

// initialize simulator, load inputs, run simulation and write outputs
int main(int argc, char* argv[]) {\

    SimulatorState sim = new_simulator();
    load_inputs(&sim, argv[1], argv[2], argv[3]);
    load_outputs(&sim, argv[4], argv[5], argv[6], argv[7], argv[8], argv[9], argv[10], argv[11], argv[12], argv[13]);
    run_simulator(&sim, argv[4], argv[5], argv[6], argv[7], argv[8], argv[9], argv[10], argv[11], argv[12], argv[13]);
    write_outputs(&sim, argv[4], argv[5], argv[6], argv[7], argv[8], argv[9], argv[10], argv[11], argv[12], argv[13]);

    return 0;
}

