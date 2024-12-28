#include <stdio.h>
#include <stdint.h>
#include <signal.h>
/* unix only */
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

/* ------------------- input buffering ------------------- */
struct termios original_tio;

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

uint16_t check_key()
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

/* ------------------- vm memory ------------------- */

/* memory storage */
#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX];    /* 65536 locations */

/* registers */
enum{
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,   /* program counter */
    R_COND,
    R_COUNT
};

uint16_t reg[R_COUNT];

/* instruction set */
enum
{
    OP_BR = 0, /* branch */
    OP_ADD,    /* add  */
    OP_LD,     /* load */
    OP_ST,     /* store */
    OP_JSR,    /* jump register */
    OP_AND,    /* bitwise and */
    OP_LDR,    /* load register */
    OP_STR,    /* store register */
    OP_RTI,    /* unused */
    OP_NOT,    /* bitwise not */
    OP_LDI,    /* load indirect */
    OP_STI,    /* store indirect */
    OP_JMP,    /* jump */
    OP_RES,    /* reserved (unused) */
    OP_LEA,    /* load effective address */
    OP_TRAP    /* execute trap */
};

/* trap codes */
enum
{
    TRAP_GETC = 0x20,  /* get character from keyboard, not echoed onto the terminal */
    TRAP_OUT = 0x21,   /* output a character */
    TRAP_PUTS = 0x22,  /* output a word string */
    TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25   /* halt the program */
};

/* condition flags */
enum
{
    FL_POS = 1 << 0, /* P */
    FL_ZRO = 1 << 1, /* Z */
    FL_NEG = 1 << 2, /* N */
};

/* memory mapped registers */
enum
{
    MR_KBSR = 0xFE00, /* keyboard status */
    MR_KBDR = 0xFE02  /* keyboard data */
};

/* ------------------- utils ------------------- */

/* sign extend*/
uint16_t sign_extend(uint16_t x, int bit_count)
{
    if ((x >> (bit_count - 1)) & 1) {
        x |= (0xFFFF << bit_count);
    }
    return x;
}

/* update flags */
void update_flags(uint16_t r)
{
    if (reg[r] == 0)
    {
        reg[R_COND] = FL_ZRO;
    }
    else if (reg[r] >> 15) /* a 1 in the left-most bit indicates negative */
    {
        reg[R_COND] = FL_NEG;
    }
    else
    {
        reg[R_COND] = FL_POS;
    }
}

/* to convert little-endian to big endian on uint16_t*/
uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

/* read image file */
void read_image_file(FILE* file)
{
    /* the origin tells us where in memory to place the image */
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    /* we know the maximum file size so we only need one fread */
    uint16_t max_read = MEMORY_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    /* swap to little endian */
    while (read-- > 0)
    {
        *p = swap16(*p);
        ++p;
    }
}

/* read image */
int read_image(const char* image_path)
{
    FILE* file = fopen(image_path, "rb");
    if (!file) { return 0; };
    read_image_file(file);
    fclose(file);
    return 1;
}

/* write in memory */
void mem_write(uint16_t address, uint16_t val)
{
    memory[address] = val;
}

/* read in memory */
uint16_t mem_read(uint16_t address)
{
    if (address == MR_KBSR)
    {
        if (check_key())
        {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        }
        else
        {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

/* ------------------- instructions ------------------- */

/* ADD instruction */
void addInstr(uint16_t instr){
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

/* LDI instruction */
void ldiInstr(uint16_t instr){
    /* destination register (DR) */
    uint16_t r0 = (instr >> 9) & 0x7;
    /* PCoffset 9*/
    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
    /* add pc_offset to the current PC, look at that memory location to get the final address */
    reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
    update_flags(r0);
}

/* bitwise and instruction */
void andInstr(uint16_t instr) {
    /* extract destination register (bits 9-11) */
    uint16_t r0 = (instr >> 9) & 0x7;
    /* extract first source register (bits 6-8) */
    uint16_t r1 = (instr >> 6) & 0x7;
    /* extract immediate flag (bit 5) */
    uint16_t imm_flag = (instr >> 5) & 0x1;

    if (imm_flag) {
        /* sign-extend immediate value (bits 0-4) */
        uint16_t imm5 = sign_extend(instr & 0x1f, 5);
        /* perform bitwise and with immediate */
        reg[r0] = reg[r1] & imm5;
    } else {
        /* extract second source register (bits 0-2) */
        uint16_t r2 = instr & 0x7;
        /* perform bitwise and with register values */
        reg[r0] = reg[r1] & reg[r2];
    }
    /* update condition flags based on result */
    update_flags(r0);
}

/* bitwise not instruction */
void notInstr(uint16_t instr) {
    /* extract destination register (bits 9-11) */
    uint16_t r0 = (instr >> 9) & 0x7;
    /* extract source register (bits 6-8) */
    uint16_t r1 = (instr >> 6) & 0x7;
    /* perform bitwise not operation */
    reg[r0] = ~reg[r1];
    /* update condition flags based on result */
    update_flags(r0);
}

/* branch instruction */
void brInstr(uint16_t instr) {
    /* sign-extend pc offset (bits 0-8) */
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
    /* extract condition flags (bits 9-11) */
    uint16_t cond_flag = (instr >> 9) & 0x7;

    /* check if condition flag matches */
    if (cond_flag & reg[R_COND]) {
        /* update program counter with offset */
        reg[R_PC] += pc_offset;
    }
}

/* jump instruction (also handles ret) */
void jmpInstr(uint16_t instr) {
    /* extract source register (bits 6-8) */
    uint16_t r1 = (instr >> 6) & 0x7;
    /* set program counter to value in source register */
    reg[R_PC] = reg[r1];
}

/* jump register instruction */
void jsrInstr(uint16_t instr) {
    /* extract long flag (bit 11) */
    uint16_t long_flag = (instr >> 11) & 1;
    /* save current program counter in r7 */
    reg[R_R7] = reg[R_PC];

    if (long_flag) {
        /* sign-extend pc offset (bits 0-10) */
        uint16_t long_pc_offset = sign_extend(instr & 0x7ff, 11);
        /* update pc with offset (jsr) */
        reg[R_PC] += long_pc_offset;
    } else {
        /* extract source register (bits 6-8) */
        uint16_t r1 = (instr >> 6) & 0x7;
        /* set pc to value in source register (jsrr) */
        reg[R_PC] = reg[r1];
    }
}

/* load instruction */
void ldInstr(uint16_t instr) {
    /* extract destination register (bits 9-11) */
    uint16_t r0 = (instr >> 9) & 0x7;
    /* sign-extend pc offset (bits 0-8) */
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
    /* read memory at pc + offset into destination register */
    reg[r0] = mem_read(reg[R_PC] + pc_offset);
    /* update condition flags based on result */
    update_flags(r0);
}

/* load register instruction */
void ldrInstr(uint16_t instr) {
    /* extract destination register (bits 9-11) */
    uint16_t r0 = (instr >> 9) & 0x7;
    /* extract base register (bits 6-8) */
    uint16_t r1 = (instr >> 6) & 0x7;
    /* sign-extend offset (bits 0-5) */
    uint16_t offset = sign_extend(instr & 0x3f, 6);
    /* read memory at base register + offset into destination register */
    reg[r0] = mem_read(reg[r1] + offset);
    /* update condition flags based on result */
    update_flags(r0);
}

/* load effective address instruction */
void leaInstr(uint16_t instr) {
    /* extract destination register (bits 9-11) */
    uint16_t r0 = (instr >> 9) & 0x7;
    /* sign-extend pc offset (bits 0-8) */
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
    /* set destination register to pc + offset */
    reg[r0] = reg[R_PC] + pc_offset;
    /* update condition flags based on result */
    update_flags(r0);
}

/* store instruction */
void stInstr(uint16_t instr) {
    /* extract source register (bits 9-11) */
    uint16_t r0 = (instr >> 9) & 0x7;
    /* sign-extend pc offset (bits 0-8) */
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
    /* write value in source register to memory at pc + offset */
    mem_write(reg[R_PC] + pc_offset, reg[r0]);
}

/* store indirect instruction */
void stiInstr(uint16_t instr) {
    /* extract source register (bits 9-11) */
    uint16_t r0 = (instr >> 9) & 0x7;
    /* sign-extend pc offset (bits 0-8) */
    uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
    /* write value in source register to memory at indirect address */
    mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
}

/* store register instruction */
void strInstr(uint16_t instr) {
    /* extract source register (bits 9-11) */
    uint16_t r0 = (instr >> 9) & 0x7;
    /* extract base register (bits 6-8) */
    uint16_t r1 = (instr >> 6) & 0x7;
    /* sign-extend offset (bits 0-5) */
    uint16_t offset = sign_extend(instr & 0x3f, 6);
    /* write value in source register to memory at base register + offset */
    mem_write(reg[r1] + offset, reg[r0]);
}

/* trap instruction */
void trapInstr(uint16_t instr, int *running){
    /* save the program counter in r7*/
    reg[R_R7] = reg[R_PC];

    switch (instr & 0xFF)
    {
        case TRAP_GETC:
            {
                /* read a single ASCII char */
                reg[R_R0] = (uint16_t)getchar();
                update_flags(R_R0);
            }
            break;
        case TRAP_OUT:
            {
                putc((char)reg[R_R0], stdout);
                fflush(stdout);
            }
            break;
        case TRAP_PUTS:
            {
                /* one char per word */
                uint16_t* c = memory + reg[R_R0];
                while (*c)
                {
                    putc((char)*c, stdout);
                    ++c;
                }
                fflush(stdout);
            }
            break;
        case TRAP_IN:
            {
                printf("Enter a character: ");
                char c = getchar();
                putc(c, stdout);
                fflush(stdout);
                reg[R_R0] = (uint16_t)c;
                update_flags(R_R0);
            }
            break;
        case TRAP_PUTSP:
            {
                /* one char per byte (two bytes per word)
                here we need to swap back to
                big endian format */
                uint16_t* c = memory + reg[R_R0];
                while (*c)
                {
                    char char1 = (*c) & 0xFF;
                    putc(char1, stdout);
                    char char2 = (*c) >> 8;
                    if (char2) putc(char2, stdout);
                    ++c;
                }
                fflush(stdout);
            }
            break;
        case TRAP_HALT:
            {
                puts("HALT");
                fflush(stdout);
                *running = 0;
            }
            break;
    }
}

/* ------------------- signal management ------------------- */

/* restore settings when program ends*/
void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

/* ------------------- main ------------------- */

int main(int argc, const char* argv[]){
    /* to handle input in terminal */
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    if(argc < 2){
        /* show usage */
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }

    for(int j = 1; j < argc; ++j){
        if(!read_image(argv[j])){
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }

    /* since exactly one condition flag should be set at any given time, set the Z flag */
    reg[R_COND] = FL_ZRO;

    /* set the PC to starting position */
    /* 0x3000 is the default */
    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;

    int running = 1;
    while (running)
    {
        /* FETCH */
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12;

        /* find instruction for the opcode */
        switch (op)
        {
            case OP_ADD:
                addInstr(instr);
                break;
            case OP_AND:
                andInstr(instr);
                break;
            case OP_NOT:
                notInstr(instr);
                break;
            case OP_BR:
                brInstr(instr);
                break;
            case OP_JMP:
                jmpInstr(instr);
                break;
            case OP_JSR:
                jsrInstr(instr);
                break;
            case OP_LD:
                ldInstr(instr);
                break;
            case OP_LDI:
                ldiInstr(instr);
                break;
            case OP_LDR:
                ldrInstr(instr);
                break;
            case OP_LEA:
                leaInstr(instr);
                break;
            case OP_ST:
                stInstr(instr);
                break;
            case OP_STI:
                stiInstr(instr);
                break;
            case OP_STR:
                strInstr(instr);    
                break;
            case OP_TRAP:
                trapInstr(instr, &running);
                break;
            case OP_RES:
                abort();
            case OP_RTI:
                abort();
            default:
                {
                    printf("invalid opcode\n");
                    abort();
                }
                break;
        }
    }

    /* restore terminal settings */
    restore_input_buffering();
}