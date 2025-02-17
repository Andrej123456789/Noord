#ifndef __CPU_IDT
#define __CPU_IDT

#include <stdint.h>

#define KERNEL_CS 0x08
#define INT_ATTR 0x8E // 0b10001110
#define IDT_ENTRIES 256

// Define the interrupt handler register
typedef struct __attribute__((packed))
{
    uint16_t limit;
    uint64_t base;
} idt_register;

// Define the interrupt handler gate
typedef struct __attribute__((packed))
{
    uint16_t base_low;    // Low 16 bits of the address to jump to
    uint16_t cs_selector; // Code segment selector
    uint8_t zero;         // Must always be 0
    uint8_t attributes;   // Flag bytes
                          //      - Bit 7:     Interrupt is present
                          //      - Bits 6-5:  Privelege level of caller (0=kernel..3=user)
                          //      - Bit 4:     Set to 0 for interrupt gates
                          //      - Bits 3-0:  Always 1110 for 32 bit interrupt gate
    uint16_t base_middle; // Middle 16 bits of the address to jump to
    uint32_t base_high;   // High 16 bits of the address to jump to
    uint32_t reserved;    // Must always be 0
} idt_gate;

// Allow outside access to the gate array and register
extern idt_gate main_idt[IDT_ENTRIES];
extern idt_register main_idt_reg;

// Add functions for setting IDT gates
void set_idt();
void set_idt_gate(uint8_t gate_number, uint64_t handler_address);

#endif
