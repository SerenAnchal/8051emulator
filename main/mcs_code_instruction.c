#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mcs_code_instruction.h"

#define ROM_SIZE                32768
#define RAM_L_SIZE              128
#define RAM_H_SIZE              128
#define SFR_SIZE                128
#define XRAM_SIZE               65535

extern uint8_t Kernel_8051_ROM[ROM_SIZE]; 
extern uint8_t Kernel_8051_L_RAM[RAM_L_SIZE];//00H - 7FH
extern uint8_t Kernel_8051_H_RAM[RAM_H_SIZE];//80H - FFH
extern uint8_t Kernel_8051_SFR[SFR_SIZE];
extern uint8_t Kernel_8051_XRAM[XRAM_SIZE];

extern MCS_8051_INSTRUCTION INSTRUCTION_TABLE[256];
extern MCS_8051 REAL_CPU;

static const char *TAG = "SIMIER";


void BOOT_REAL_CPU_ENGINE(MCS_8051 *REAL_CPU){
    REAL_CPU->PC_R = 0;
    REAL_CPU->SP_R = 0;
    memset(REAL_CPU->REG_R_0,0,sizeof(REAL_CPU->REG_R_0));
    
    ESP_LOGI(TAG,"8051 ENGINE BOOT & RUN ->");

    while (1)
    {

        uint8_t FetchCode = Kernel_8051_ROM[REAL_CPU->PC_R];
        MCS_8051_INSTRUCTION DecodeCode = INSTRUCTION_TABLE[FetchCode];
        if(DecodeCode.execute == NULL){
            ESP_LOGI(TAG,"[WARN]Undefined Instruction. Execution Stopped.");
            break;
        }
        DecodeCode.execute(REAL_CPU);
        vTaskDelay(pdMS_TO_TICKS(10));
    }   
}
// --- 0x00 - 0x0F ---
void MCS_8051_INSTRUCTION_NOP(MCS_8051 *cpu) {
    cpu->PC_R = cpu->PC_R+1;
 }
void MCS_8051_INSTRUCTION_AJMP(MCS_8051 *cpu) {
    uint16_t current_PC = cpu->PC_R + 2;
    uint8_t opcode_byte = Kernel_8051_ROM[cpu->PC_R];
    uint8_t low_byte = Kernel_8051_ROM[cpu->PC_R + 1];

    uint16_t target_addr = ((opcode_byte & 0xE0) << 3) | low_byte;
    cpu->PC_R = (current_PC & 0xF800) | target_addr;

    ESP_LOGI(TAG, "AJMP, Jumped to: 0x%04x", cpu->PC_R);
 }
void MCS_8051_INSTRUCTION_LJMP(MCS_8051 *cpu) {
    uint8_t low_Byte = Kernel_8051_ROM[cpu->PC_R+1];
    uint8_t high_Byte = Kernel_8051_ROM[cpu->PC_R+2];

    uint16_t All_Byte = ((uint16_t)high_Byte << 8)| low_Byte;
    int16_t All_Byte_int = (int16_t)All_Byte;

    cpu->PC_R = cpu->PC_R + All_Byte_int;
    ESP_LOGI(TAG,"LJMP,Jumped to: 0x%04x",cpu->PC_R);
 }              
void MCS_8051_INSTRUCTION_RR_A(MCS_8051 *cpu) { 
   uint8_t A_bit0_byte = (cpu->A_R << 7)| 0x00;
   uint8_t A_right_byte = (cpu->A_R >> 1)| 0x00;

   uint8_t All_byte = (A_bit0_byte)| (A_right_byte);
   cpu->A_R = All_byte;

   cpu->PC_R = cpu->PC_R + 1;
   ESP_LOGI(TAG, "RR_A,PC: 0x%04X, A: 0x%02X", cpu->PC_R, cpu->A_R);
}
void MCS_8051_INSTRUCTION_INC_A(MCS_8051 *cpu) { 
    uint8_t before_A_R = cpu->A_R;

    uint8_t sum = before_A_R + 1;
        if (sum < before_A_R){
            cpu->PSW_R = (cpu->PSW_R) & 0x80;
        }

    cpu->PC_R = cpu->PC_R + 1;
   ESP_LOGI(TAG, "INC_A,PC: 0x%04X, A: 0x%02X", cpu->PC_R, cpu->A_R);
}
void MCS_8051_INSTRUCTION_INC_DIRECT(MCS_8051 *cpu) {
    uint8_t opera_RAM_addr = Kernel_8051_ROM[cpu->PC_R+1];
    int opera_RAM_num = (int)opera_RAM_addr;
    
    cpu->PC_R = cpu->PC_R + 2;
    if (opera_RAM_num <= 127)
    {
        Kernel_8051_L_RAM[opera_RAM_num] = Kernel_8051_L_RAM[opera_RAM_num] & 0x01;
        ESP_LOGI(TAG, "INC_DIRECT,PC: 0x%04x, OPERA_RAM_ADDR: 0x%02x,OPERA_VALUE:0x%02x", cpu->PC_R,opera_RAM_addr,Kernel_8051_L_RAM[opera_RAM_num]);
    }
    if (opera_RAM_num > 127 && opera_RAM_num <= 255)
    {
        if (opera_RAM_num == 130) //数据指针低字节
        {
            ESP_LOGI(TAG,"[WARN]Can't operate the DPL,%u",cpu->PC_R);
        }
        if (opera_RAM_num == 131) //数据指针高字节
        {
            ESP_LOGI(TAG,"[WARN]Can't operate the DPH,%u",cpu->PC_R);
        }
        int opera_SFR_num = opera_RAM_num - 128;
        Kernel_8051_SFR[opera_SFR_num] = Kernel_8051_SFR[opera_SFR_num] & 0x01;
         ESP_LOGI(TAG, "INC_DIRECT,PC: 0x%04x, OPERA_RAM_ADDR: 0x%02x,OPERA_VALUE:0x%02x", cpu->PC_R,opera_RAM_addr,Kernel_8051_L_RAM[opera_RAM_num]);
    }
 }
void MCS_8051_INSTRUCTION_INC_AT_R0(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03; // 提取 RS0(bit3)和RS1(bit4)计算 Bank号 (0,1,2,3)
    uint8_t target_addr = 0;

    if (current_bank == 0) {
        target_addr = cpu->REG_R_0[0];
    } else if (current_bank == 1) {
        target_addr = cpu->REG_R_1[0];
    } else if (current_bank == 2) {
        target_addr = cpu->REG_R_2[0];
    } else if (current_bank == 3) {
        target_addr = cpu->REG_R_3[0];
    }
    if (target_addr <= 0x7F) {
        Kernel_8051_L_RAM[target_addr] = Kernel_8051_L_RAM[target_addr] + 1;
    } 
    else {
        ESP_LOGI(TAG, "[INC_AT_R0] Writing to SFR area, addr: 0x%02X. Current value: %d", 
                 target_addr, Kernel_8051_SFR[target_addr - 0x80]);
        Kernel_8051_SFR[target_addr - 0x80] = Kernel_8051_SFR[target_addr - 0x80] + 1;
    }
    cpu->PC_R = cpu->PC_R + 1;

    ESP_LOGI(TAG, "INC_DIRECT_R0,PC: 0x%04x, OPERA_ADDR,%02x", cpu->PC_R,target_addr);
}
void MCS_8051_INSTRUCTION_INC_AT_R1(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 
    uint8_t target_addr = 0;

    if (current_bank == 0) {
        target_addr = cpu->REG_R_0[1];
    } else if (current_bank == 1) {
        target_addr = cpu->REG_R_1[1];
    } else if (current_bank == 2) {
        target_addr = cpu->REG_R_2[1];
    } else if (current_bank == 3) {
        target_addr = cpu->REG_R_3[1];
    }
    if (target_addr <= 0x7F) {
        Kernel_8051_L_RAM[target_addr] = Kernel_8051_L_RAM[target_addr] + 1;
    } 
    else {
        ESP_LOGI(TAG, "[INC_AT_R1] Writing to SFR area, addr: 0x%02X. Current value: %d", 
                 target_addr, Kernel_8051_SFR[target_addr - 0x80]);
        Kernel_8051_SFR[target_addr - 0x80] = Kernel_8051_SFR[target_addr - 0x80] + 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "INC_DIRECT_R1,PC: 0x%04x, OPERA_ADDR,%02x", cpu->PC_R,target_addr);
 }
void MCS_8051_INSTRUCTION_INC_R0(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 

     if (current_bank == 0) {
        cpu->REG_R_0[0] = cpu->REG_R_0[0] + 1;
    } else if (current_bank == 1) {
        cpu->REG_R_1[0] = cpu->REG_R_1[0] + 1;
    } else if (current_bank == 2) {
        cpu->REG_R_2[0] = cpu->REG_R_2[0] + 1;
    } else if (current_bank == 3) {
        cpu->REG_R_3[0] = cpu->REG_R_3[0] + 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "INC_R0,PC: 0x%04x, OPERA_RSG_BANK,%n,R0_VALUE:0x%02x", cpu->PC_R,current_bank,cpu->REG_R_0);
 }
void MCS_8051_INSTRUCTION_INC_R1(MCS_8051 *cpu) {
   uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 

     if (current_bank == 0) {
        cpu->REG_R_0[1] = cpu->REG_R_0[1] + 1;
    } else if (current_bank == 1) {
        cpu->REG_R_1[1] = cpu->REG_R_1[1] + 1;
    } else if (current_bank == 2) {
        cpu->REG_R_2[1] = cpu->REG_R_2[1] + 1;
    } else if (current_bank == 3) {
        cpu->REG_R_3[1] = cpu->REG_R_3[1] + 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "INC_R1,PC: 0x%04x, OPERA_RSG_BANK,%n,R0_VALUE:0x%02x", cpu->PC_R,current_bank,cpu->REG_R_0);

 }
void MCS_8051_INSTRUCTION_INC_R2(MCS_8051 *cpu) {
    int8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 

     if (current_bank == 0) {
        cpu->REG_R_0[2] = cpu->REG_R_0[2] + 1;
    } else if (current_bank == 1) {
        cpu->REG_R_1[2] = cpu->REG_R_1[2] + 1;
    } else if (current_bank == 2) {
        cpu->REG_R_2[2] = cpu->REG_R_2[2] + 1;
    } else if (current_bank == 3) {
        cpu->REG_R_3[2] = cpu->REG_R_3[2] + 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "INC_R2,PC: 0x%04x, OPERA_RSG_BANK,%n,R0_VALUE:0x%02x", cpu->PC_R,current_bank,cpu->REG_R_0);
 }
void MCS_8051_INSTRUCTION_INC_R3(MCS_8051 *cpu) { 
    int8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 

     if (current_bank == 0) {
        cpu->REG_R_0[3] = cpu->REG_R_0[3] + 1;
    } else if (current_bank == 1) {
        cpu->REG_R_1[3] = cpu->REG_R_1[3] + 1;
    } else if (current_bank == 2) {
        cpu->REG_R_2[3] = cpu->REG_R_2[3] + 1;
    } else if (current_bank == 3) {
        cpu->REG_R_3[3] = cpu->REG_R_3[3] + 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "INC_R3,PC: 0x%04x, OPERA_RSG_BANK,%n,R0_VALUE:0x%02x", cpu->PC_R,current_bank,cpu->REG_R_0);
}
void MCS_8051_INSTRUCTION_INC_R4(MCS_8051 *cpu) {
    int8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 

     if (current_bank == 0) {
        cpu->REG_R_0[4] = cpu->REG_R_0[4] + 1;
    } else if (current_bank == 1) {
        cpu->REG_R_1[4] = cpu->REG_R_1[4] + 1;
    } else if (current_bank == 2) {
        cpu->REG_R_2[4] = cpu->REG_R_2[4] + 1;
    } else if (current_bank == 3) {
        cpu->REG_R_3[4] = cpu->REG_R_3[4] + 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "INC_R4,PC: 0x%04x, OPERA_RSG_BANK,%n,R0_VALUE:0x%02x", cpu->PC_R,current_bank,cpu->REG_R_0);
 }
void MCS_8051_INSTRUCTION_INC_R5(MCS_8051 *cpu) {
    int8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 

     if (current_bank == 0) {
        cpu->REG_R_0[5] = cpu->REG_R_0[5] + 1;
    } else if (current_bank == 1) {
        cpu->REG_R_1[5] = cpu->REG_R_1[5] + 1;
    } else if (current_bank == 2) {
        cpu->REG_R_2[5] = cpu->REG_R_2[5] + 1;
    } else if (current_bank == 3) {
        cpu->REG_R_3[5] = cpu->REG_R_3[5] + 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "INC_R5,PC: 0x%04x, OPERA_RSG_BANK,%n,R0_VALUE:0x%02x", cpu->PC_R,current_bank,cpu->REG_R_0);
 }
void MCS_8051_INSTRUCTION_INC_R6(MCS_8051 *cpu) {
    int8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 

     if (current_bank == 0) {
        cpu->REG_R_0[6] = cpu->REG_R_0[6] + 1;
    } else if (current_bank == 1) {
        cpu->REG_R_1[6] = cpu->REG_R_1[6] + 1;
    } else if (current_bank == 2) {
        cpu->REG_R_2[6] = cpu->REG_R_2[6] + 1;
    } else if (current_bank == 3) {
        cpu->REG_R_3[6] = cpu->REG_R_3[6] + 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "INC_R6,PC: 0x%04x, OPERA_RSG_BANK,%n,R0_VALUE:0x%02x", cpu->PC_R,current_bank,cpu->REG_R_0);
 }
void MCS_8051_INSTRUCTION_INC_R7(MCS_8051 *cpu) {
    int8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 

     if (current_bank == 0) {
        cpu->REG_R_0[7] = cpu->REG_R_0[7] + 1;
    } else if (current_bank == 1) {
        cpu->REG_R_1[7] = cpu->REG_R_1[7] + 1;
    } else if (current_bank == 2) {
        cpu->REG_R_2[7] = cpu->REG_R_2[7] + 1;
    } else if (current_bank == 3) {
        cpu->REG_R_3[7] = cpu->REG_R_3[7] + 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "INC_R7,PC: 0x%04x, OPERA_RSG_BANK,%n,R0_VALUE:0x%02x", cpu->PC_R,current_bank,cpu->REG_R_0);
 }

// --- 0x10 - 0x1F ---
void MCS_8051_INSTRUCTION_JBC(MCS_8051 *cpu) { 
    uint8_t addressing_addr = Kernel_8051_H_RAM[cpu->PC_R + 1];
    uint8_t offset = Kernel_8051_ROM[cpu->PC_R + 3];
    uint8_t jmup_addr = offset + cpu->PC_R + 2;
    
    bool bit_is_zero = false;
    uint8_t only_judg_bit;
    uint8_t RAM_addressing_BANK;
    uint8_t RAM_addressing_BIT;
    uint8_t SFR_opera_BANK;
    uint8_t SFR_opera_BIT;

    if (addressing_addr <= 0x7F){
        RAM_addressing_BANK = addressing_addr / 8 + 0x20;
        RAM_addressing_BIT = addressing_addr % 8;

        only_judg_bit = Kernel_8051_L_RAM[RAM_addressing_BANK] & (0x01 << RAM_addressing_BIT);
        if (only_judg_bit == 0x00){
            bit_is_zero = true;
        }
    }else{
        SFR_opera_BANK = addressing_addr  & 0xF8;
        SFR_opera_BIT = addressing_addr % 8;

        only_judg_bit = Kernel_8051_SFR[SFR_opera_BANK] & (0x01 << SFR_opera_BIT);
        if (only_judg_bit == 0x00){
            bit_is_zero = true;
        }
    }
    
    if (bit_is_zero == true){
        cpu->PC_R = jmup_addr;
        ESP_LOGI(TAG,"JBC,IS ZERO ,PC:0x%04x,JUDGE ADDR(RAM):0x%02x,JUDGE ADDR(SFR):0x%02x",cpu->PC_R,RAM_addressing_BANK,SFR_opera_BANK);
    }else{
        if(addressing_addr <= 0x7F){
            Kernel_8051_L_RAM[RAM_addressing_BANK] = Kernel_8051_L_RAM[RAM_addressing_BANK] & (0xFE << RAM_addressing_BIT);
        }else{
            Kernel_8051_SFR[SFR_opera_BANK] = Kernel_8051_SFR[SFR_opera_BANK] & (0xFE << SFR_opera_BIT);
        }
        cpu->PC_R = jmup_addr;
        ESP_LOGI(TAG,"JBC,NOT ZERO ,PC:0x%04x",cpu->PC_R);
    }
}
void MCS_8051_INSTRUCTION_ACALL(MCS_8051 *cpu) { 
    
}
void MCS_8051_INSTRUCTION_LCALL(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_RRC_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DEC_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DEC_DIRECT(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DEC_AT_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DEC_AT_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DEC_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DEC_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DEC_R2(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DEC_R3(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DEC_R4(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DEC_R5(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DEC_R6(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DEC_R7(MCS_8051 *cpu) { }

// --- 0x20 - 0x2F ---
void MCS_8051_INSTRUCTION_JB(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_RET(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_RL_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADD_A_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADD_A_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADD_AT_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADD_AT_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADD_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADD_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADD_R2(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADD_R3(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADD_R4(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADD_R5(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADD_R6(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADD_R7(MCS_8051 *cpu) { }

// --- 0x30 - 0x3F ---
void MCS_8051_INSTRUCTION_JNB(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_RETI(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_RLC_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADDC_A_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADDC_A_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADDC_AT_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADDC_AT_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADDC_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADDC_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADDC_R2(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADDC_R3(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADDC_R4(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADDC_R5(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADDC_R6(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ADDC_R7(MCS_8051 *cpu) { }

// --- 0x40 - 0x4F ---
void MCS_8051_INSTRUCTION_JC(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ORL_DIR_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ORL_DIR_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ORL_A_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ORL_A_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ORL_AT_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ORL_AT_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ORL_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ORL_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ORL_R2(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ORL_R3(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ORL_R4(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ORL_R5(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ORL_R6(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ORL_R7(MCS_8051 *cpu) { }

// --- 0x50 - 0x5F ---
void MCS_8051_INSTRUCTION_JNC(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ANL_DIR_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ANL_DIR_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ANL_A_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ANL_A_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ANL_AT_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ANL_AT_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ANL_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ANL_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ANL_R2(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ANL_R3(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ANL_R4(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ANL_R5(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ANL_R6(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ANL_R7(MCS_8051 *cpu) { }

// --- 0x60 - 0x6F ---
void MCS_8051_INSTRUCTION_JZ(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XRL_DIR_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XRL_DIR_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XRL_A_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XRL_A_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XRL_AT_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XRL_AT_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XRL_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XRL_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XRL_R2(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XRL_R3(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XRL_R4(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XRL_R5(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XRL_R6(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XRL_R7(MCS_8051 *cpu) { }

// --- 0x70 - 0x7F ---
void MCS_8051_INSTRUCTION_JNZ(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ORL_C_BIT(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_JMP_DPTR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_A_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_DIR_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_AT_R0_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_AT_R1_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R0_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R1_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R2_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R3_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R4_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R5_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R6_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R7_IMM(MCS_8051 *cpu) { }

// --- 0x80 - 0x8F ---
void MCS_8051_INSTRUCTION_SJMP(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_ANL_C_BIT(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOVC_A_PC(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DIV_AB(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_DIR_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_DIR_AT_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_DIR_AT_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_DIR_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_DIR_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_DIR_R2(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_DIR_R3(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_DIR_R4(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_DIR_R5(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_DIR_R6(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_DIR_R7(MCS_8051 *cpu) { }

// --- 0x90 - 0x9F ---
void MCS_8051_INSTRUCTION_MOV_DPTR_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_BIT_C(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOVC_A_DPTR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_SUBB_A_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_SUBB_A_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_SUBB_AT_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_SUBB_AT_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_SUBB_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_SUBB_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_SUBB_R2(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_SUBB_R3(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_SUBB_R4(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_SUBB_R5(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_SUBB_R6(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_SUBB_R7(MCS_8051 *cpu) { }

// --- 0xA0 - 0xAF ---
void MCS_8051_INSTRUCTION_ORL_C_NOT_BIT(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_C_BIT(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_INC_DPTR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MUL_AB(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_UNDEFINED_A5(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_AT_R0_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_AT_R1_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R0_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R1_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R2_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R3_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R4_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R5_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R6_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R7_DIR(MCS_8051 *cpu) { }

// --- 0xB0 - 0xBF ---
void MCS_8051_INSTRUCTION_ANL_C_NOT_BIT(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CPL_BIT(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CPL_C(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CJNE_A_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CJNE_A_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CJNE_AT_R0_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CJNE_AT_R1_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CJNE_R0_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CJNE_R1_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CJNE_R2_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CJNE_R3_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CJNE_R4_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CJNE_R5_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CJNE_R6_IMM(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CJNE_R7_IMM(MCS_8051 *cpu) { }

// --- 0xC0 - 0xCF ---
void MCS_8051_INSTRUCTION_PUSH(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CLR_BIT(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CLR_C(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_SWAP_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XCH_A_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XCH_AT_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XCH_AT_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XCH_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XCH_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XCH_R2(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XCH_R3(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XCH_R4(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XCH_R5(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XCH_R6(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XCH_R7(MCS_8051 *cpu) { }

// --- 0xD0 - 0xDF ---
void MCS_8051_INSTRUCTION_POP(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_SETB_BIT(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_SETB_C(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DA_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DJNZ_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XCHD_AT_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_XCHD_AT_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DJNZ_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DJNZ_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DJNZ_R2(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DJNZ_R3(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DJNZ_R4(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DJNZ_R5(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DJNZ_R6(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_DJNZ_R7(MCS_8051 *cpu) { }

// --- 0xE0 - 0xEF ---
void MCS_8051_INSTRUCTION_MOVX_A_DPTR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOVX_A_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOVX_A_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CLR_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_A_DIR(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_A_AT_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_A_AT_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_A_R0(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_A_R1(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_A_R2(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_A_R3(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_A_R4(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_A_R5(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_A_R6(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_A_R7(MCS_8051 *cpu) { }

// --- 0xF0 - 0xFF ---
void MCS_8051_INSTRUCTION_MOVX_DPTR_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOVX_R0_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOVX_R1_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_CPL_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_DIR_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_AT_R0_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_AT_R1_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R0_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R1_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R2_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R3_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R4_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R5_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R6_A(MCS_8051 *cpu) { }
void MCS_8051_INSTRUCTION_MOV_R7_A(MCS_8051 *cpu) { }
