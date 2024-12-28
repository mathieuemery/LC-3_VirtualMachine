# LC-3 Virtual Machine

This project implements a virtual machine in C capable of executing LC-3 assembly code. The LC-3 (Little Computer 3) is a simple, educational instruction set architecture (ISA) designed for teaching fundamental concepts of computer architecture and programming.

## Goal

The primary goal of this project is to create a functioning LC-3 virtual machine (VM) that can load, decode, and execute LC-3 assembly programs. This project serves as a practical introduction to:

- Understanding low-level programming concepts.
- Working with assembly languages and machine code.
- Simulating computer architecture using C.

The implementation follows the LC-3 instruction set, including features such as loading programs, executing instructions, and handling memory and registers.

## Features

- **Instruction Set Implementation**: Supports LC-3 instructions such as arithmetic operations, bitwise operations, memory access, and control flow.
- **Memory Management**: Simulates memory using an array, enabling storage and retrieval of program data.
- **Register Operations**: Implements the LC-3's general-purpose registers and special-purpose registers like `PC` (program counter) and `COND` (condition codes).
- **Input/Output Handling**: Supports basic I/O operations for interactive programs.
- **Assembly Execution**: Runs LC-3 assembly programs, allowing users to explore how assembly code operates at the machine level.

## Setup

To build and run the LC-3 VM, you need a C compiler (e.g., GCC or Clang). Clone the repository and follow these steps:

1. **Build the Program**:  
   ```bash
   gcc -o lc3_vm lc3.c
   ```

2. **Run the VM with an LC-3 Assembly Program**
    ```bash
    ./lc3_vm ./games/2048.obj
    ```

## Credit

This implementation has been done by following the tutorial “[Building a Virtual Machine for the LC-3](https://www.jmeiners.com/lc3-vm/)” by [Justin Meiners](https://www.jmeiners.com/) and [Ryan Pendleton](https://www.ryanp.me/). The tutorial provides a step-by-step guide to understanding the LC-3 architecture and implementing a virtual machine for it in C.