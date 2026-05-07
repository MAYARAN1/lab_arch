#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h>
#include <string.h>
#include "sim.h"

// Zero all simulator state arrays and initialize control flags to default values
SimulatorState new_simulator(void) {
    SimulatorState sim;
    memset(sim.memory, 0, sizeof(sim.memory));       // Clears all memory slots
    memset(sim.registers, 0, sizeof(sim.registers)); // Clears all registers
    sim.PC = 0;                                      // Starts the Program Counter at 0
    sim.cycle = 0;                                   // Starts the clock at cycle 0
    sim.halted = false;                              // Ensures the processor is running
    return sim;
}

int load_inputs(SimulatorState* sim, const char* memin) {
    FILE* f;
    char line[100];
    int i = 0;

    // Load initial memory contents from memin.txt
    f = fopen(memin, "r");
    if (!f) { perror(memin); exit(1); }

    for (i = 0; i < MEM_SIZE && fgets(line, sizeof(line), f); i++) {
        sscanf(line, "%x", &sim->memory[i]);
    }

    fclose(f);
    return i; // Return the number of lines read for first line in trace.txt
}

void write_outputs(SimulatorState* sim) {
    FILE* f = fopen("sram_out.txt", "w");
    if (!f) {
        perror("Error opening sram_out.txt");
        return;
    }

    for (int i = 0; i <= MEM_SIZE - 1; i++) {
        fprintf(f, "%08x\n", sim->memory[i]);
    }
    fclose(f);
}

// Fetch the current instruction from memory and decode opcode and operands
void fetch_decode_execute(SimulatorState* sim, FILE* trace_file) {

    uint16_t next_pc = sim->PC + 1; // PC + 1

    // Fetch
    uint32_t inst = sim->memory[sim->PC];
    uint16_t imm16 = inst & 0xFFFF;
    uint32_t imm32 = (uint32_t)(int32_t)(int16_t)imm16;
  
    // Decode
    uint8_t opcode = (inst >> 25) & 0x1F;
    uint8_t dst = (inst >> 22) & 0x7;
    uint8_t src0 = (inst >> 19) & 0x7;
    uint8_t src1 = (inst >> 16) & 0x7;

    uint32_t* R = sim->registers;
    sim->registers[1] = imm32; // Load immediate value into R[1]
    
    // Resolve source values 0 is 0, 1 is immediate
    uint32_t val_src0 = (src0 == 0) ? 0 : (src0 == 1) ? imm32 : R[src0];
    uint32_t val_src1 = (src1 == 0) ? 0 : (src1 == 1) ? imm32 : R[src1];

    // Execute
    switch (opcode) {
    case 0: if (dst >= 2) R[dst] = val_src0 + val_src1; sim->PC = next_pc; break; // ADD
    case 1: if (dst >= 2) R[dst] = val_src0 - val_src1; sim->PC = next_pc; break; // SUB
    case 2: if (dst >= 2) R[dst] = val_src0 << val_src1; sim->PC = next_pc; break;  // LSF
    case 3: if (dst >= 2) R[dst] = ((int32_t)val_src0) >> val_src1; sim->PC = next_pc; break; // RSF
    case 4: if (dst >= 2) R[dst] = val_src0 & val_src1; sim->PC = next_pc; break; // AND
    case 5: if (dst >= 2) R[dst] = val_src0 | val_src1; sim->PC = next_pc; break; // OR
    case 6: if (dst >= 2) R[dst] = val_src0 ^ val_src1; sim->PC = next_pc; break; // XOR
    case 7: if (dst >= 2) R[dst] = (R[dst] & 0x0000FFFF) | (imm32 << 16); sim->PC = next_pc; break; // LHI

    case 8: if (dst >= 2) { if (val_src1 < MEM_SIZE) R[dst] = sim->memory[val_src1]; } sim->PC = next_pc; break; // LD
    case 9: if (val_src1 < MEM_SIZE) sim->memory[val_src1] = val_src0; sim->PC = next_pc; break; // ST

    case 16: if ((int32_t)val_src0 < (int32_t)val_src1) { R[7] = next_pc; sim->PC = imm32; }
           else sim->PC = next_pc; break; // JLT
    case 17: if ((int32_t)val_src0 <= (int32_t)val_src1) { R[7] = next_pc; sim->PC = imm32; }
           else sim->PC = next_pc; break; // JLE 
    case 18: if (val_src0 == val_src1) { R[7] = next_pc; sim->PC = imm32; }
           else sim->PC = next_pc; break; // JEQ 
    case 19: if (val_src0 != val_src1) { R[7] = next_pc; sim->PC = imm32; }
           else sim->PC = next_pc; break; // JNE 
    case 20: R[7] = next_pc; sim->PC = val_src0; break; // JIN
    case 24: sim->halted = true; break; // HLT

    default:
        printf("Unknown opcode %d at PC %04x\n", opcode, sim->PC);
        sim->PC = next_pc;
        break;
    }
}

// Update the current trace
void update_traces(SimulatorState* sim, FILE* trace_file, uint16_t current_pc, uint32_t inst, uint32_t save_regs[NUM_REGISTERS]) {

    // Decode instruction fields for printing
    uint16_t imm16 = inst & 0xFFFF;
    uint32_t imm32 = (uint32_t)(int32_t)(int16_t)imm16;
    uint8_t opcode = (inst >> 25) & 0x1F;
    uint8_t dst = (inst >> 22) & 0x7;
    uint8_t src0 = (inst >> 19) & 0x7;
    uint8_t src1 = (inst >> 16) & 0x7;

    // Map opcode numbers to strings
    const char* opcode_names[] = {
        "ADD", "SUB", "LSF", "RSF", "AND", "OR", "XOR", "LHI",
        "LD", "ST", "UNKNOWN", "UNKNOWN", "UNKNOWN", "UNKNOWN", "UNKNOWN", "UNKNOWN",
        "JLT", "JLE", "JEQ", "JNE", "JIN", "UNKNOWN", "UNKNOWN", "UNKNOWN", "HLT"
    };

    const char* op_name = (opcode <= 24) ? opcode_names[opcode] : "UNKNOWN";

     uint32_t* R = sim->registers;
     
    // Instruction cycle @ PC
    fprintf(trace_file, "--- instruction %d (%04x) @ PC %d (%04x) -----------------------------------------------------------\n",
        sim->cycle, sim->cycle, current_pc, current_pc);

    // Instruction breakdown
    fprintf(trace_file, "pc = %04d, inst = %08x, opcode = %d (%s), dst = %d, src0 = %d, src1 = %d, immediate = %08x\n",
        current_pc, inst, opcode, op_name, dst, src0, src1, imm32);

    // Registers 0-3
    fprintf(trace_file, "r[0] = %08x r[1] = %08x r[2] = %08x r[3] = %08x \n",
        save_regs[0], R[1], save_regs[2], save_regs[3]);

    // Registers 4-7
    fprintf(trace_file, "r[4] = %08x r[5] = %08x r[6] = %08x r[7] = %08x \n",
        save_regs[4], save_regs[5], save_regs[6], save_regs[7]);
    
    //execution result
    if (opcode <=  7) {  // ALU ops
        fprintf(trace_file, "\n>>>> EXEC: R[%d] = %d %s %d <<<<\n", 
                dst, (src0 == 1) ? imm32 : save_regs[src0], op_name, (src1 == 1 ? imm32 : save_regs[src1]));
    }else if (opcode == 8) { // LD
        fprintf(trace_file, "\n>>>> EXEC: R[%d] = MEM[%d] = %08x <<<<\n", 
                dst, sim->registers[src1], sim->memory[sim->registers[src1]]);
    }else if (opcode == 9) { // ST 
        fprintf(trace_file, "\n>>>> EXEC: MEM[%d] = R[%d] = %08x <<<<\n", 
                sim->registers[src1], src0, sim->registers[src0]);
    } else if (opcode >= 16 && opcode <= 19){//jumps
            fprintf(trace_file, "\n>>>> EXEC: %s %d, %d, %d <<<<\n", op_name, 
            sim->registers[src0], sim->registers[src1], sim->PC); 
    }else if (opcode == 24) { // HLT
        fprintf(trace_file, "\n>>>> EXEC: HALT at PC %04x<<<<\n", current_pc);\
        fprintf(trace_file, "sim finished at pc %d, %d instructions", sim->PC, sim->cycle + 1);
        return;
    }else 
        fprintf(trace_file, "\n>>>> EXEC: %s <<<<\n", op_name);
    

    fprintf(trace_file, "\n"); // Blank line
}

void run_simulator(SimulatorState* sim, FILE* trace_file)
{
    // Run until HALT instruction is encountered
    while (!sim->halted) {

        // Save the current PC and instruction BEFORE execution
        uint16_t current_pc = sim->PC;
        uint32_t inst = sim->memory[current_pc];

        // Backup registers BEFORE execution for the trace output
        uint32_t save_regs[NUM_REGISTERS];
        for (int i = 0; i < NUM_REGISTERS; i++) {
            save_regs[i] = sim->registers[i];
        }

        // Execute instruction
        fetch_decode_execute(sim, trace_file);

        // Update trace
        update_traces(sim, trace_file, current_pc, inst, save_regs);

        // Advance simulation cycle
        sim->cycle++;
    }
}

// initialize simulator, load inputs, run simulation and write outputs
int main(int argc, char* argv[]) {

    if (argc != 2) {
        return 1;
    }

    SimulatorState sim = new_simulator();
    int num_lines = load_inputs(&sim, argv[1]);

    // Open trace file
    FILE* trace_file = fopen("trace.txt", "w");
    if (!trace_file) {
        return 1;
    }
    fprintf(trace_file, "program %s loaded, %d lines\n\n", argv[1], num_lines);

    // Run the simulation
    run_simulator(&sim, trace_file);

    // Close trace file and write final memory state
    fclose(trace_file);
    write_outputs(&sim);

    return 0;
}