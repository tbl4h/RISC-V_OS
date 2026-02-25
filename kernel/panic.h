#ifndef PANIC_H
#define PANIC_H

static inline void disable_interrupts(void)
{
    asm volatile("csrci sstatus, 2"); // clear SIE
}

static inline void debug_break(void)
{
    asm volatile("ebreak");
}

static void stop_other_harts(void);

__attribute__((noreturn))
void panic(const char *msg);


#endif