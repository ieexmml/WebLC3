#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "usleep.h"

#include <emscripten.h>

#include "architecture.h"
#include "bit_manip.h"

// Allocate 65536 memory locations
uint16_t memory[UINT16_MAX];
// Registers are stored in an array
uint16_t reg[R_COUNT];	

// We will load program binaries here
uint16_t program_space_avalilable = 0xFDFF - 0x3000;
uint8_t image[program_space_avalilable];

#include "virtual_terminal.h"

int iskeydown = 0;
int chardown = 257;

int running = 1;
int fast = 1;

#include "memory.h"
#include "files.h"

char *op_str[16];

extern void halt(); // Halts program execution

int EMSCRIPTEN_KEEPALIVE next_instruction(int debug, int _chardown){
		chardown = _chardown;
		iskeydown = (chardown != 257);
		// Fetch instruction at R_PC
		uint16_t instr = mem_read(reg[R_PC]);
		// Increment R_PC
		reg[R_PC]++;
		// The operator is saved at the left 4 bits of the instruction
		uint16_t op = instr >> 12;
		if(debug == 1){
			_printstring(op_str[op]);;
			_printstring("\n");
		}else if(debug == 2){
			int c = reg[R_PC]-0x3000;
			if(c % 50 == 0){
				_printstring("CNT: ");
				_printint(c);
				printchar('\n');
			}
		}
		switch(op){
			case OP_ADD:
				{
                    /* destination register (DR) */
                    uint16_t r0 = (instr >> 9) & 0x7;
                    /* first operand (SR1) */
                    uint16_t r1 = (instr >> 6) & 0x7;
                    /* whether we are in immediate mode */
                    uint16_t imm_flag = (instr >> 5) & 0x1;
                
                    if (imm_flag)
                    {
                        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                        reg[r0] = reg[r1] + imm5;
                    }
                    else
                    {
                        uint16_t r2 = instr & 0x7;
                        reg[r0] = reg[r1] + reg[r2];
                    }
                
                    update_flags(r0);
				}
				break;
			case OP_AND:
				{
					// Most of this is coppied from the ADD instruction
					// Only difference is that the operator is & instead of +

					// DR - Destination register is in bits 9-11 inclusive
					// We bitshift 9 times and & with 0x7 (111 binary) to get 3 DR bits

					uint16_t r0 = (instr >> 9) & 0x7;

					// SR1 - First operand is in bits 6-8 inclusive. Same procedure above
					uint16_t r1 = (instr >> 6) & 0x7;

					// Immediate mode flag is bit 5
					uint16_t imm_flag = (instr >> 5) & 0x1;

					// Immediate mode means we'll not be reading a register for
					// addition but will instead use a 3+1 bit number from
					// the last 4 bits of the instruction

					if(imm_flag){
						// 0x1F is 11111 in binary. Gets imm5 unextended
						uint16_t imm5 =  sign_extend(instr & 0x1F, 5);
						reg[r0] = reg[r1] & imm5;
					}else{
						uint16_t r2 = instr & 0x7; // Last 3 bits of instruction
						reg[r0] = reg[r1] & reg[r2];
					}

					update_flags(r0);

				}
				break;
			case OP_NOT:
				{
					// NOT - Bit-Wise Complement
					// DR  
					uint16_t r0 = (instr >> 9) & 0x7;
					// SR
					uint16_t r1 = (instr >> 6) & 0x7;
					reg[r0] = ~reg[r1];
					update_flags(r0);
				}
				break;
			case OP_BR:
				{
                    uint16_t pc_offset = sign_extend((instr) & 0x1ff, 9);
                    uint16_t cond_flag = (instr >> 9) & 0x7;
                    if (cond_flag & reg[R_COND])
                    {
                        reg[R_PC] += pc_offset;
                    }
				}
				break;
			case OP_JMP:
				{
					// JMP - Jump to location in register specified by bits 8-6
					uint16_t r1 = (instr >> 6) & 0x7;
					reg[R_PC] = reg[r1];
				}
				break;
			case OP_JSR:
				{
					// JSR - Jump SubRoutine
					// If bit 11 is set jump to adress at register specified in bits 6-8 inclusive - JSR
					// If not add PCoffset11 to PC - bits 0-10 inclusive - JSRR
					 reg[R_R7] = reg[R_PC];
					if((instr >> 11) & 1){
						reg[R_PC] += sign_extend(instr & 0x7ff, 11); 
					}
					else{
						reg[R_PC] = reg[((instr >> 6) & 0x7)];
					}
					break;
				}
				break;
			case OP_LD:
				{
					// LD
					uint16_t r1 = (instr >> 9) & 0x7;
					uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
					reg[r1] = mem_read(reg[R_PC] + pc_offset);
					update_flags(r1);
				}
				break;
			case OP_LDI:
				{
					// LDI - "Load indirect"
					// Loads a value from a memory address to a register
					// Instruction is compised of: 4 instr bits, 3 DR bits, and 9 PCoffset9 bits
					// PCoffset9 bits is a relative memory addres. Adding it to the incremented
					// PC counter gives us the address of the address of our value in memory
					// huh..

					uint16_t r0 = (instr >> 9) & 0x7;
					uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);//0x1ff is 111111111 binary
					reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
					update_flags(r0);
				}
				break;
			case OP_LDR:
				{
					// DR
					uint16_t r0 = (instr >> 9) & 0x7;
					// BaseR
					uint16_t r1 = (instr >> 6) & 0x7;
					// offset6
					uint16_t offset = sign_extend(instr & 0x3f, 6);
					reg[r0] = mem_read(reg[r1] + offset);
					update_flags(r0);
				}
				break;
			case OP_LEA:
				{
					// LEA - Load Effective Address
					uint16_t r0 = (instr >> 9) & 0x7;
					uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
					reg[r0] = reg[R_PC] + pc_offset;
					update_flags(r0);
				}
				break;
			case OP_ST:
				{
					uint16_t r1 = (instr >> 9) & 0x7;
					uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
					mem_write(reg[R_PC] + pc_offset, reg[r1]);
				}
				break;
			case OP_STI:
				{
	    				uint16_t r0 = (instr >> 9) & 0x7;
    					uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
    					mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
				}    break;
			case OP_STR:
				{
					uint16_t r0 = (instr >> 9) & 0x7;
					uint16_t r1 = (instr >> 6) & 0x7;
					uint16_t offset = sign_extend(instr & 0x3F, 6);
					mem_write(reg[r1] + offset, reg[r0]);
				}
				break;
			case OP_TRAP:
				{
						
						switch (instr & 0xFF){
			    			case TRAP_GETC:
							{
								int charset = 0;
								if(fast){
									fast = 0;
									reg[R_PC]--;
								}else{
									if(chardown != 257){
										reg[R_R0] = chardown;
										charset = 1;
										fast = 1;
									}else
										reg[R_PC]--;
								}
							}
								break;
		        			case TRAP_OUT:
								printchar(reg[R_R0]);	
	       						break;
    						case TRAP_PUTS:
								{
									uint16_t* c = memory + reg[R_R0];
									while(*c){
										printchar(*c);
										++c;
									}
								}
        						break;
    						case TRAP_IN:
								_printstring("Enter a character: ");
								if(fast){
									fast = 0;
									reg[R_PC]--;
								}else{
									if(chardown != 257){
										reg[R_R0] = chardown;
										printchar(chardown);
										fast = 1;
									}else
										reg[R_PC]--;
								}
								break;
					    	case TRAP_PUTSP:
							{
								uint16_t* c = memory + reg[R_R0];
								while(*c){
									char char1 = (*c) & 0xFF;
									printchar(*c);
									char char2 = (*c) >> 8;
									if(char2)
										printchar(char2);
									++c;
								}
							}
							break;
					    	case TRAP_HALT:
							{
								_printstring("HALT");
								running = 0;
								halt();
							}
							break;
					}
				}
				break;
			case OP_RES:
			case OP_RTI:
			default:
				break;
		}
	return fast;
}

int EMSCRIPTEN_KEEPALIVE main(){	
	// Set PC to starting position
	// 0x3000 is the default
	_printstring("LOADING PROGRAM\n");
	const uint16_t PC_START = load_image();
	if(PC_START == -1){
		_printstring("MAX PROGRAM SIZE EXCEEDED");
		halt();
	}
	_printstring("LOADED PROGRAM\n");
	_printstring("\e[1;1H\e[2J");
	reg[R_PC] = PC_START;
	op_str[0] = "BRANCH";
	op_str[1] = "ADD";
	op_str[2] = "LOAD";
	op_str[3] = "STORE";
	op_str[4] = "JUMP REG";
	op_str[5] = "AND";
	op_str[6] = "LOAD REG";
	op_str[7] = "STORE REG";
	op_str[8] = "RESERVED";
	op_str[9] = "NOT";
	op_str[10] = "LOAD INDIRECT";
	op_str[11] = "STORE INDIRECT";
	op_str[12] = "JUMP";
	op_str[13] = "RESERVED";
	op_str[14] = "LEA";
	op_str[15] = "TRAP";
	return 0;
}
