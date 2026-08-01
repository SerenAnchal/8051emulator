#ifndef MCS_CODE_INSTRUCTION
#define MCS_CODE_INSTRUCTION
#include <stdint.h>

typedef struct {
    unsigned char REG_R[8];
    unsigned char A_R;
    unsigned char PSW_R;
    unsigned short PC_R;
    unsigned char SP_R;
} MCS_8051;

typedef struct {
    const char *asm_code;
    uint8_t code_bytes;
    uint8_t code_cycles;
    void (*execute)(MCS_8051 *);
} MCS_8051_INSTRUCTION;

void BOOT_REAL_CPU_ENGINE(MCS_8051 *REAL_CPU);
void MCS_8051_INSTRUCTION_NOP(MCS_8051 *REAL_CPU);

#endif //MCS_CODE_INSTRUCTION