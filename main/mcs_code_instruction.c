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

#define PC_START                0x0000
#define PC_MAIN_START           0x0200
#define SP_START                0x50
#define PSW_R_START             0x00
#define SP_WARN_LINE            0x7F

extern uint8_t Kernel_8051_ROM[ROM_SIZE]; 
extern uint8_t Kernel_8051_L_RAM[RAM_L_SIZE];//00H - 7FH
extern uint8_t Kernel_8051_H_RAM[RAM_H_SIZE];//80H - FFH
extern uint8_t Kernel_8051_SFR[SFR_SIZE];
extern uint8_t Kernel_8051_XRAM[XRAM_SIZE];

extern MCS_8051_INSTRUCTION INSTRUCTION_TABLE[256];
extern MCS_8051 REAL_CPU;

static const char *TAG = "SIMIER";


void BOOT_REAL_CPU_ENGINE(MCS_8051 *REAL_CPU){
    
    REAL_CPU->PC_R = PC_MAIN_START;//to go over IVT
    REAL_CPU->SP_R = SP_START;
    REAL_CPU->PSW_R = PSW_R_START;

    REAL_CPU->IP_R = 0x00;
    REAL_CPU->IE_R = 0x00;
    REAL_CPU->INT_LATCH = 0x00;
    REAL_CPU->TCON_R = 0x00;

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
        if (REAL_CPU->SP_R > SP_WARN_LINE) {
        ESP_LOGE(TAG, "FATAL ERROR: Stack Overflow! Resetting CPU");

        Kernel_8051_SFR[9] = REAL_CPU->TCON_R;
        Kernel_8051_SFR[41] = REAL_CPU->IE_R;
        Kernel_8051_SFR[51] = REAL_CPU->IP_R;
        
        
    }
        vTaskDelay(pdMS_TO_TICKS(10));
    }   
}
//0x00-0x0F
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
        int opera_SFR_num = opera_RAM_num - 128;
        Kernel_8051_SFR[opera_SFR_num] = Kernel_8051_SFR[opera_SFR_num] & 0x01;
         ESP_LOGI(TAG, "INC_DIRECT,PC: 0x%04x, OPERA_RAM_ADDR: 0x%02x,OPERA_VALUE:0x%02x", cpu->PC_R,opera_RAM_addr,Kernel_8051_L_RAM[opera_RAM_num]);
    }
 }
void MCS_8051_INSTRUCTION_INC_AT_R0(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 
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

 //0x10-0x1F 
void MCS_8051_INSTRUCTION_JBC(MCS_8051 *cpu) { 
    uint8_t addressing_addr = Kernel_8051_H_RAM[cpu->PC_R + 1];
    uint8_t offset = Kernel_8051_ROM[cpu->PC_R + 3];
    uint8_t jmup_addr = offset + cpu->PC_R + 2;
    
    bool bit_is_zero = false;
    uint8_t only_judg_bit;
    uint8_t RAM_addressing_BANK = 0;
    uint8_t RAM_addressing_BIT = 0;
    uint8_t SFR_opera_BANK = 0;
    uint8_t SFR_opera_BIT = 0;

    if (addressing_addr <= 0x7F){
        RAM_addressing_BANK = addressing_addr / 8 ;
        RAM_addressing_BANK = RAM_addressing_BANK + 0x20;
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
        cpu->PC_R = cpu->PC_R + 3;
        ESP_LOGI(TAG,"JBC,IS ZERO ,PC:0x%04x,JUDGE ADDR(RAM):0x%02x,JUDGE ADDR(SFR):0x%02x",cpu->PC_R,RAM_addressing_BANK,SFR_opera_BANK);
    }else{
        if(addressing_addr <= 0x7F){
            cpu->PC_R = jmup_addr;
            Kernel_8051_L_RAM[RAM_addressing_BANK] = Kernel_8051_L_RAM[RAM_addressing_BANK] & ~(0x01 << RAM_addressing_BIT);
        }else{
            cpu->PC_R = jmup_addr;
            Kernel_8051_SFR[SFR_opera_BANK] = Kernel_8051_SFR[SFR_opera_BANK] & ~(0x01 << SFR_opera_BIT);
        }
       
        ESP_LOGI(TAG,"JBC,NOT ZERO ,PC:0x%04x",cpu->PC_R);
    }
}
void MCS_8051_INSTRUCTION_ACALL(MCS_8051 *cpu) {

    uint16_t return_addr = cpu->PC_R + 2;
    cpu->SP_R = cpu->SP_R + 1;
    Kernel_8051_L_RAM[cpu->SP_R] = return_addr & 0xFF;//lowbyte

    cpu->SP_R = cpu->SP_R + 1;
    Kernel_8051_L_RAM[cpu->SP_R] = (return_addr >> 8) & 0xFF;//highbyte

    uint8_t opcode_byte = Kernel_8051_ROM[cpu->PC_R];
    uint8_t low_byte = Kernel_8051_ROM[cpu->PC_R + 1];

    uint16_t target_addr = ((opcode_byte & 0xE0) << 3) | low_byte;

    cpu->PC_R = (return_addr & 0xF800) | target_addr;

    ESP_LOGI(TAG, "LCALL: Jumped To 0x%04x,SP:0x%02x", cpu->PC_R,cpu->SP_R);
}
void MCS_8051_INSTRUCTION_LCALL(MCS_8051 *cpu) { 
    uint16_t return_addr = cpu->PC_R + 3;
    cpu->SP_R = cpu->SP_R + 1;
    Kernel_8051_L_RAM[cpu->SP_R] = (uint8_t)return_addr & 0xFF;

    cpu->SP_R = cpu->SP_R + 1;
    Kernel_8051_L_RAM[cpu->SP_R] = (uint8_t)(return_addr >> 8) & 0xFF;

    uint8_t high_addr = Kernel_8051_ROM[cpu->SP_R + 1];
    uint8_t low_addr = Kernel_8051_ROM[cpu->SP_R + 2];

    uint16_t jump_addr =  (high_addr << 8) | low_addr;
    cpu->PC_R = jump_addr;

    ESP_LOGI(TAG, "LCALL: Jumped To 0x%04x,SP:0x%02x", cpu->PC_R,cpu->SP_R);

}
void MCS_8051_INSTRUCTION_RRC_A(MCS_8051 *cpu) { 
    uint8_t cy_bit_old = cpu->PSW_R & 0x80;
    uint8_t cy_bit_now = cpu->A_R & 0x01;

    cpu->PSW_R = cpu->PSW_R & 0x7F;
    cpu->PSW_R = cpu->PSW_R | cy_bit_now;

    cpu->A_R = (cpu->A_R >> 1);
    cpu->A_R = cpu->A_R | cy_bit_old;

    cpu->PC_R = cpu->PC_R + 1;

    ESP_LOGI(TAG, "RRC_A, PC: 0x%04x", cpu->PC_R);
}
void MCS_8051_INSTRUCTION_DEC_A(MCS_8051 *cpu) {
    cpu->A_R = cpu->A_R - 1;
    cpu->PC_R = cpu->PC_R + 1;

    ESP_LOGI(TAG, "DEC_A, PC: 0x%04x", cpu->PC_R);
 }
void MCS_8051_INSTRUCTION_DEC_DIRECT(MCS_8051 *cpu) { 
    uint8_t opera_RAM_addr = Kernel_8051_ROM[cpu->PC_R+1];
    int opera_RAM_num = (int)opera_RAM_addr;
    
    cpu->PC_R = cpu->PC_R + 2;
    if (opera_RAM_num <= 127)
    {
        Kernel_8051_L_RAM[opera_RAM_num] = Kernel_8051_L_RAM[opera_RAM_num] & 0xFE;
        ESP_LOGI(TAG, "DEC_DIRECT,PC: 0x%04x, OPERA_RAM_ADDR: 0x%02x,OPERA_VALUE:0x%02x", cpu->PC_R,opera_RAM_addr,Kernel_8051_L_RAM[opera_RAM_num]);
    } else if (opera_RAM_num > 127 && opera_RAM_num <= 255)
    {
        int opera_SFR_num = opera_RAM_num - 128;
        Kernel_8051_SFR[opera_SFR_num] = Kernel_8051_SFR[opera_SFR_num] & 0xFE;
         ESP_LOGI(TAG, "DEC_DIRECT,PC: 0x%04x, OPERA_RAM_ADDR: 0x%02x,OPERA_VALUE:0x%02x", cpu->PC_R,opera_RAM_addr,Kernel_8051_L_RAM[opera_RAM_num]);
    }
 }
void MCS_8051_INSTRUCTION_DEC_AT_R0(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t target_addr = 0;

    if(current_bank == 0){
        target_addr = cpu->REG_R_0[0];
    }else if(current_bank == 1){
        target_addr = cpu->REG_R_1[0];
    }else if (current_bank ==2){
        target_addr = cpu->REG_R_2[0];
    }else if (current_bank ==3){
        target_addr = cpu->REG_R_3[0];
    }
    
    if (target_addr <= 0x7F) {
        Kernel_8051_L_RAM[target_addr] = Kernel_8051_L_RAM[target_addr] - 1;
    } 
    else {
        ESP_LOGI(TAG, "[DEC_AT_R0] Writing to SFR area, addr: 0x%02X. Current value: %d", 
                        target_addr, Kernel_8051_SFR[target_addr - 0x80]);
        Kernel_8051_SFR[target_addr - 0x80] = Kernel_8051_SFR[target_addr - 0x80] - 1;
    }
    cpu->PC_R = cpu->PC_R + 1;

    ESP_LOGI(TAG, "DEC_DIRECT_R0,PC: 0x%04x, OPERA_ADDR,%02x", cpu->PC_R,target_addr);
 }
void MCS_8051_INSTRUCTION_DEC_AT_R1(MCS_8051 *cpu) { 
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t target_addr = 0;

    if(current_bank == 0){
        target_addr = cpu->REG_R_0[1];
    }else if(current_bank == 1){
        target_addr = cpu->REG_R_1[1];
    }else if (current_bank ==2){
        target_addr = cpu->REG_R_2[1];
    }else if (current_bank ==3){
        target_addr = cpu->REG_R_3[1];
    }
    
    if (target_addr <= 0x7F) {
        Kernel_8051_L_RAM[target_addr] = Kernel_8051_L_RAM[target_addr] - 1;
    } 
    else {
        ESP_LOGI(TAG, "[DEC_AT_R1] Writing to SFR area, addr: 0x%02X. Current value: %d", 
                        target_addr, Kernel_8051_SFR[target_addr - 0x80]);
        Kernel_8051_SFR[target_addr - 0x80] = Kernel_8051_SFR[target_addr - 0x80] - 1;
    }
    cpu->PC_R = cpu->PC_R + 1;

    ESP_LOGI(TAG, "DEC_DIRECT_R1,PC: 0x%04x, OPERA_ADDR,%02x", cpu->PC_R,target_addr);
}
void MCS_8051_INSTRUCTION_DEC_R0(MCS_8051 *cpu) {
     uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 

     if (current_bank == 0) {
        cpu->REG_R_0[0] = cpu->REG_R_0[0] - 1;
    } else if (current_bank == 1) {
        cpu->REG_R_1[0] = cpu->REG_R_1[0] - 1;
    } else if (current_bank == 2) {
        cpu->REG_R_2[0] = cpu->REG_R_2[0] - 1;
    } else if (current_bank == 3) {
        cpu->REG_R_3[0] = cpu->REG_R_3[0] - 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "DEC_R0,PC: 0x%04x, OPERA_RSG_BANK,%n,R0_VALUE:0x%02x", cpu->PC_R,current_bank,cpu->REG_R_0);
 }
void MCS_8051_INSTRUCTION_DEC_R1(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 

     if (current_bank == 0) {
        cpu->REG_R_0[1] = cpu->REG_R_0[1] - 1;
    } else if (current_bank == 1) {
        cpu->REG_R_1[1] = cpu->REG_R_1[1] - 1;
    } else if (current_bank == 2) {
        cpu->REG_R_2[1] = cpu->REG_R_2[1] - 1;
    } else if (current_bank == 3) {
        cpu->REG_R_3[1] = cpu->REG_R_3[1] - 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "DEC_R1,PC: 0x%04x, OPERA_RSG_BANK,%n,R0_VALUE:0x%02x", cpu->PC_R,current_bank,cpu->REG_R_0);
 }
void MCS_8051_INSTRUCTION_DEC_R2(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 

     if (current_bank == 0) {
        cpu->REG_R_0[2] = cpu->REG_R_0[2] - 1;
    } else if (current_bank == 1) {
        cpu->REG_R_1[2] = cpu->REG_R_1[2] - 1;
    } else if (current_bank == 2) {
        cpu->REG_R_2[2] = cpu->REG_R_2[2] - 1;
    } else if (current_bank == 3) {
        cpu->REG_R_3[2] = cpu->REG_R_3[2] - 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "DEC_R2,PC: 0x%04x, OPERA_RSG_BANK,%n,R0_VALUE:0x%02x", cpu->PC_R,current_bank,cpu->REG_R_0);
 }
void MCS_8051_INSTRUCTION_DEC_R3(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 

     if (current_bank == 0) {
        cpu->REG_R_0[3] = cpu->REG_R_0[3] - 1;
    } else if (current_bank == 1) {
        cpu->REG_R_1[3] = cpu->REG_R_1[3] - 1;
    } else if (current_bank == 2) {
        cpu->REG_R_2[3] = cpu->REG_R_2[3] - 1;
    } else if (current_bank == 3) {
        cpu->REG_R_3[3] = cpu->REG_R_3[3] - 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "DEC_R3,PC: 0x%04x, OPERA_RSG_BANK,%n,R0_VALUE:0x%02x", cpu->PC_R,current_bank,cpu->REG_R_0);
 }
void MCS_8051_INSTRUCTION_DEC_R4(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 

     if (current_bank == 0) {
        cpu->REG_R_0[4] = cpu->REG_R_0[4] - 1;
    } else if (current_bank == 1) {
        cpu->REG_R_1[4] = cpu->REG_R_1[4] - 1;
    } else if (current_bank == 2) {
        cpu->REG_R_2[4] = cpu->REG_R_2[4] - 1;
    } else if (current_bank == 3) {
        cpu->REG_R_3[4] = cpu->REG_R_3[4] - 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "DEC_R4,PC: 0x%04x, OPERA_RSG_BANK,%n,R0_VALUE:0x%02x", cpu->PC_R,current_bank,cpu->REG_R_0);
 }
void MCS_8051_INSTRUCTION_DEC_R5(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 

     if (current_bank == 0) {
        cpu->REG_R_0[5] = cpu->REG_R_0[5] - 1;
    } else if (current_bank == 1) {
        cpu->REG_R_1[5] = cpu->REG_R_1[5] - 1;
    } else if (current_bank == 2) {
        cpu->REG_R_2[5] = cpu->REG_R_2[5] - 1;
    } else if (current_bank == 3) {
        cpu->REG_R_3[5] = cpu->REG_R_3[5] - 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "DEC_R5,PC: 0x%04x, OPERA_RSG_BANK,%n,R0_VALUE:0x%02x", cpu->PC_R,current_bank,cpu->REG_R_0);
 }
void MCS_8051_INSTRUCTION_DEC_R6(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 

     if (current_bank == 0) {
        cpu->REG_R_0[6] = cpu->REG_R_0[6] - 1;
    } else if (current_bank == 1) {
        cpu->REG_R_1[6] = cpu->REG_R_1[6] - 1;
    } else if (current_bank == 2) {
        cpu->REG_R_2[6] = cpu->REG_R_2[6] - 1;
    } else if (current_bank == 3) {
        cpu->REG_R_3[6] = cpu->REG_R_3[6] - 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "DEC_R6,PC: 0x%04x, OPERA_RSG_BANK,%n,R0_VALUE:0x%02x", cpu->PC_R,current_bank,cpu->REG_R_0);
 }
void MCS_8051_INSTRUCTION_DEC_R7(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03; 

     if (current_bank == 0) {
        cpu->REG_R_0[7] = cpu->REG_R_0[7] - 1;
    } else if (current_bank == 1) {
        cpu->REG_R_1[7] = cpu->REG_R_1[7] - 1;
    } else if (current_bank == 2) {
        cpu->REG_R_2[7] = cpu->REG_R_2[7] - 1;
    } else if (current_bank == 3) {
        cpu->REG_R_3[7] = cpu->REG_R_3[7] - 1;
    }

    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "DEC_R7,PC: 0x%04x, OPERA_RSG_BANK,%n,R0_VALUE:0x%02x", cpu->PC_R,current_bank,cpu->REG_R_0);
 }



 //0x20-0x2F
void MCS_8051_INSTRUCTION_JB(MCS_8051 *cpu) { 
     uint8_t addressing_addr = Kernel_8051_H_RAM[cpu->PC_R + 1];
    uint8_t offset = Kernel_8051_ROM[cpu->PC_R + 3];
    uint8_t jmup_addr = offset + cpu->PC_R + 2;
    
    bool bit_is_zero = false;
    uint8_t only_judg_bit;
    uint8_t RAM_addressing_BANK = 0;
    uint8_t RAM_addressing_BIT = 0;
    uint8_t SFR_opera_BANK = 0;
    uint8_t SFR_opera_BIT = 0;

    if (addressing_addr <= 0x7F){
        RAM_addressing_BANK = addressing_addr / 8 ;
        RAM_addressing_BANK = RAM_addressing_BANK + 0x20;
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
        cpu->PC_R = cpu->PC_R + 3;
        ESP_LOGI(TAG,"JBC,IS ZERO ,PC:0x%04x,JUDGE ADDR(RAM):0x%02x,JUDGE ADDR(SFR):0x%02x",cpu->PC_R,RAM_addressing_BANK,SFR_opera_BANK);
    }else{
        if(addressing_addr <= 0x7F){
            cpu->PC_R = jmup_addr;
           
        }else{
            cpu->PC_R = jmup_addr;
        }    
        ESP_LOGI(TAG,"JB,NOT ZERO ,PC:0x%04x",cpu->PC_R);
    }
}
void MCS_8051_INSTRUCTION_RET(MCS_8051 *cpu) {
    uint8_t addr_high_byte = Kernel_8051_L_RAM[cpu->SP_R - 1];
    uint8_t addr_low_byte = Kernel_8051_L_RAM[cpu->SP_R - 2];

    uint16_t all_addr_byte = (addr_high_byte << 8) | addr_low_byte;
    cpu->SP_R = cpu->SP_R - 2;

    cpu->PC_R = all_addr_byte;

    ESP_LOGI(TAG,"RET,PC:0x%4x",cpu->PC_R);
 }
void MCS_8051_INSTRUCTION_RL_A(MCS_8051 *cpu) {
    uint8_t right_byte = (cpu->A_R >> 7) & 0x01;
    uint8_t left_byte = cpu->A_R << 1;

    cpu->A_R = right_byte | left_byte;
    
    cpu->PC_R = cpu->PC_R + 1;
    ESP_LOGI(TAG, "RL_A, PC: 0x%04x", cpu->PC_R);
 }
void MCS_8051_INSTRUCTION_ADD_A_IMM(MCS_8051 *cpu) { 
    uint8_t immediate_byte = Kernel_8051_ROM[cpu->PC_R+1];
    uint16_t sum_byte = immediate_byte + cpu->A_R;

    if (sum_byte > 0xFF)
    {
        cpu->PSW_R = cpu->PSW_R | 0x80;
        cpu->A_R = (sum_byte) & 0xFF;
    }else{
        cpu->A_R = (uint8_t)sum_byte;
    }

    cpu->PC_R = cpu->PC_R + 2;
    ESP_LOGI(TAG, "ADD_A_IMM, PC: 0x%04x", cpu->PC_R);
}
void MCS_8051_INSTRUCTION_ADD_A_DIR(MCS_8051 *cpu) { 
    uint8_t opera_RAM_addr = Kernel_8051_ROM[cpu->PC_R + 1];
    int opera_RAM_num = (int)opera_RAM_addr;

    int value_bufore = (int)cpu->A_R;
    cpu->PC_R = cpu->PC_R + 2;

    if(opera_RAM_num <= 127){
        if(cpu->A_R + Kernel_8051_L_RAM[opera_RAM_num] > 255){
            cpu->A_R = (cpu->A_R + Kernel_8051_L_RAM[opera_RAM_num]) & 0xFF;
            cpu->PSW_R = cpu->PSW_R | 0x80;
            ESP_LOGI(TAG, "ADD_A_DIRECT,PC: 0x%04x, OPERA_RAM_ADDR: 0x%02x,OPERA_VALUE:0x%02x", cpu->PC_R,opera_RAM_addr,Kernel_8051_L_RAM[opera_RAM_num]);
        }else{
            cpu->A_R = cpu->A_R + Kernel_8051_L_RAM[opera_RAM_num];
        }
    } else if (opera_RAM_num > 127 && opera_RAM_num <= 255)
    {
        int opera_SFR_num = opera_RAM_num - 128;
        if (cpu->A_R + Kernel_8051_SFR[opera_SFR_num] > 255){
            cpu->A_R = (cpu->A_R + Kernel_8051_SFR[opera_SFR_num]) & 0xFF;
            cpu->PSW_R = cpu->PSW_R | 0x80;
            ESP_LOGI(TAG, "ADD_A_DIRECT,PC: 0x%04x, OPERA_SFR_ADDR: 0x%02x,OPERA_VALUE:0x%02x", cpu->PC_R,opera_SFR_num,Kernel_8051_SFR[opera_SFR_num]);
        }else{
            cpu->A_R = cpu->A_R + Kernel_8051_SFR[opera_SFR_num];
            ESP_LOGI(TAG, "ADD_A_DIRECT,PC: 0x%04x, OPERA_SFR_ADDR: 0x%02x,OPERA_VALUE:0x%02x", cpu->PC_R,opera_SFR_num,Kernel_8051_SFR[opera_SFR_num]);
        }
    
   }
}
void MCS_8051_INSTRUCTION_ADD_AT_R0(MCS_8051 *cpu) { 
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t target_addr = 0;

    if(current_bank == 0){
        target_addr = cpu->REG_R_0[0];
    }else if(current_bank == 1){
        target_addr = cpu->REG_R_1[0];
    }else if (current_bank ==2){
        target_addr = cpu->REG_R_2[0];
    }else if (current_bank ==3){
        target_addr = cpu->REG_R_3[0];
    }
    
    if(target_addr > 127){
        target_addr = target_addr - 128;
       if(cpu->A_R + Kernel_8051_H_RAM[target_addr] > 255 ){
        cpu->A_R = (cpu->A_R + Kernel_8051_H_RAM[target_addr]) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADD_AT_R0,PC: 0x%04x, OPERA_H_RAM_ADDR: 0x%02x", target_addr);
       }else{
         cpu->A_R = cpu->A_R + Kernel_8051_H_RAM[target_addr];
         ESP_LOGI(TAG, "ADD_AT_R0,PC: 0x%04x, OPERA_H_RAM_ADDR: 0x%02x", target_addr);
       }
       
    }else{
        if(cpu->A_R + Kernel_8051_L_RAM[target_addr]  >255){
            cpu->A_R = (cpu->A_R + Kernel_8051_L_RAM[target_addr]) & 0xFF;
            cpu->PSW_R = cpu->PSW_R | 0x80;
            ESP_LOGI(TAG, "ADD_AT_R0,PC: 0x%04x, OPERA_L_RAM_ADDR: 0x%02x", target_addr);
        }else{
            cpu->A_R = cpu->A_R + Kernel_8051_L_RAM[target_addr];
            ESP_LOGI(TAG, "ADD_AT_R0,PC: 0x%04x, OPERA_L_RAM_ADDR: 0x%02x", target_addr);
        }
    }

    cpu->PC_R = cpu->PC_R + 1;
}
void MCS_8051_INSTRUCTION_ADD_AT_R1(MCS_8051 *cpu) { 
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t target_addr = 0;

    if(current_bank == 0){
        target_addr = cpu->REG_R_0[1];
    }else if(current_bank == 1){
        target_addr = cpu->REG_R_1[1];
    }else if (current_bank ==2){
        target_addr = cpu->REG_R_2[1];
    }else if (current_bank ==3){
        target_addr = cpu->REG_R_3[1];
    }
    
    if(target_addr > 127){
        target_addr = target_addr - 128;
       if(cpu->A_R + Kernel_8051_H_RAM[target_addr] > 255 ){
        cpu->A_R = (cpu->A_R + Kernel_8051_H_RAM[target_addr]) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADD_AT_R1,PC: 0x%04x, OPERA_H_RAM_ADDR: 0x%02x", target_addr);
       }else{
         cpu->A_R = cpu->A_R + Kernel_8051_H_RAM[target_addr];
         ESP_LOGI(TAG, "ADD_AT_R1,PC: 0x%04x, OPERA_H_RAM_ADDR: 0x%02x", target_addr);
       }
       
    }else{
        if(cpu->A_R + Kernel_8051_L_RAM[target_addr]  >255){
            cpu->A_R = (cpu->A_R + Kernel_8051_L_RAM[target_addr]) & 0xFF;
            cpu->PSW_R = cpu->PSW_R | 0x80;
            ESP_LOGI(TAG, "ADD_AT_R1,PC: 0x%04x, OPERA_L_RAM_ADDR: 0x%02x", target_addr);
        }else{
            cpu->A_R = cpu->A_R + Kernel_8051_L_RAM[target_addr];
            ESP_LOGI(TAG, "ADD_AT_R1,PC: 0x%04x, OPERA_L_RAM_ADDR: 0x%02x", target_addr);
        }
    }

    cpu->PC_R = cpu->PC_R + 1;
}
void MCS_8051_INSTRUCTION_ADD_R0(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t addend_num = 0;

    cpu->PC_R = cpu->PC_R + 1;

    if(current_bank == 0){
       addend_num = cpu->REG_R_0[0];
    }else if(current_bank == 1){
       addend_num = cpu->REG_R_1[0];
    }else if (current_bank ==2){
        addend_num = cpu->REG_R_2[0];
    }else if (current_bank ==3){
        addend_num = cpu->REG_R_3[0];
    }

    if(cpu->A_R + addend_num > 255){
        cpu->A_R = (cpu->A_R + addend_num) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADD_R0,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num;
        ESP_LOGI(TAG, "ADD_R0,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    
 }
void MCS_8051_INSTRUCTION_ADD_R1(MCS_8051 *cpu) {
     uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t addend_num = 0;

    cpu->PC_R = cpu->PC_R + 1;

    if(current_bank == 0){
       addend_num = cpu->REG_R_0[1];
    }else if(current_bank == 1){
       addend_num = cpu->REG_R_1[1];
    }else if (current_bank ==2){
        addend_num = cpu->REG_R_2[1];
    }else if (current_bank ==3){
        addend_num = cpu->REG_R_3[1];
    }

    if(cpu->A_R + addend_num > 255){
        cpu->A_R = (cpu->A_R + addend_num) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADD_R1,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num;
        ESP_LOGI(TAG, "ADD_R1,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
 }
void MCS_8051_INSTRUCTION_ADD_R2(MCS_8051 *cpu) {
     uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t addend_num = 0;

    cpu->PC_R = cpu->PC_R + 1;

    if(current_bank == 0){
       addend_num = cpu->REG_R_0[2];
    }else if(current_bank == 1){
       addend_num = cpu->REG_R_1[2];
    }else if (current_bank ==2){
        addend_num = cpu->REG_R_2[2];
    }else if (current_bank ==3){
        addend_num = cpu->REG_R_3[2];
    }

    if(cpu->A_R + addend_num > 255){
        cpu->A_R = (cpu->A_R + addend_num) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADD_R2,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num;
        ESP_LOGI(TAG, "ADD_R2,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
 }
void MCS_8051_INSTRUCTION_ADD_R3(MCS_8051 *cpu) {
     uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t addend_num = 0;

    cpu->PC_R = cpu->PC_R + 1;

    if(current_bank == 0){
       addend_num = cpu->REG_R_0[3];
    }else if(current_bank == 1){
       addend_num = cpu->REG_R_1[3];
    }else if (current_bank ==2){
        addend_num = cpu->REG_R_2[3];
    }else if (current_bank ==3){
        addend_num = cpu->REG_R_3[3];
    }

    if(cpu->A_R + addend_num > 255){
        cpu->A_R = (cpu->A_R + addend_num) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADD_R3,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num;
        ESP_LOGI(TAG, "ADD_R3,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
 }
void MCS_8051_INSTRUCTION_ADD_R4(MCS_8051 *cpu) {
     uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t addend_num = 0;

    cpu->PC_R = cpu->PC_R + 1;

    if(current_bank == 0){
       addend_num = cpu->REG_R_0[4];
    }else if(current_bank == 1){
       addend_num = cpu->REG_R_1[4];
    }else if (current_bank ==2){
        addend_num = cpu->REG_R_2[4];
    }else if (current_bank ==3){
        addend_num = cpu->REG_R_3[4];
    }

    if(cpu->A_R + addend_num > 255){
        cpu->A_R = (cpu->A_R + addend_num) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADD_R4,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num;
        ESP_LOGI(TAG, "ADD_R4,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
 }
void MCS_8051_INSTRUCTION_ADD_R5(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t addend_num = 0;

    cpu->PC_R = cpu->PC_R + 1;

    if(current_bank == 0){
       addend_num = cpu->REG_R_0[5];
    }else if(current_bank == 1){
       addend_num = cpu->REG_R_1[5];
    }else if (current_bank ==2){
        addend_num = cpu->REG_R_2[5];
    }else if (current_bank ==3){
        addend_num = cpu->REG_R_3[5];
    }

    if(cpu->A_R + addend_num > 255){
        cpu->A_R = (cpu->A_R + addend_num) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADD_R5,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num;
        ESP_LOGI(TAG, "ADD_R5,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
 }
void MCS_8051_INSTRUCTION_ADD_R6(MCS_8051 *cpu) {
     uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t addend_num = 0;

    cpu->PC_R = cpu->PC_R + 1;

    if(current_bank == 0){
       addend_num = cpu->REG_R_0[6];
    }else if(current_bank == 1){
       addend_num = cpu->REG_R_1[6];
    }else if (current_bank ==2){
        addend_num = cpu->REG_R_2[6];
    }else if (current_bank ==3){
        addend_num = cpu->REG_R_3[6];
    }

    if(cpu->A_R + addend_num > 255){
        cpu->A_R = (cpu->A_R + addend_num) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADD_R6,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num;
        ESP_LOGI(TAG, "ADD_R6,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
 }
void MCS_8051_INSTRUCTION_ADD_R7(MCS_8051 *cpu) {
     uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t addend_num = 0;

    cpu->PC_R = cpu->PC_R + 1;

    if(current_bank == 0){
       addend_num = cpu->REG_R_0[7];
    }else if(current_bank == 1){
       addend_num = cpu->REG_R_1[7];
    }else if (current_bank ==2){
        addend_num = cpu->REG_R_2[7];
    }else if (current_bank ==3){
        addend_num = cpu->REG_R_3[7];
    }

    if(cpu->A_R + addend_num > 255){
        cpu->A_R = (cpu->A_R + addend_num) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADD_R7,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num;
        ESP_LOGI(TAG, "ADD_R7,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
 }

//0x30-0x3F
void MCS_8051_INSTRUCTION_JNB(MCS_8051 *cpu) { //JUMP IF NOT BIT SET
    uint8_t addressing_addr = Kernel_8051_H_RAM[cpu->PC_R + 1];
    uint8_t offset = Kernel_8051_ROM[cpu->PC_R + 3];
    uint8_t jmup_addr = offset + cpu->PC_R + 2;
    
    bool bit_is_zero = false;
    uint8_t only_judg_bit;
    uint8_t RAM_addressing_BANK = 0;
    uint8_t RAM_addressing_BIT = 0;
    uint8_t SFR_opera_BANK = 0;
    uint8_t SFR_opera_BIT = 0;

    if (addressing_addr <= 0x7F){
        RAM_addressing_BANK = addressing_addr / 8 ;
        RAM_addressing_BANK = RAM_addressing_BANK + 0x20;
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
        ESP_LOGI(TAG,"JNB,IS ZERO ,PC:0x%04x,JUDGE ADDR(RAM):0x%02x,JUDGE ADDR(SFR):0x%02x",cpu->PC_R,RAM_addressing_BANK,SFR_opera_BANK);
    }else{
        if(addressing_addr <= 0x7F){
            cpu->PC_R = cpu->PC_R + 3;
             ESP_LOGI(TAG,"JNB,NOT ZERO ,PC:0x%04x",cpu->PC_R);
        }
    }
}
void MCS_8051_INSTRUCTION_RETI(MCS_8051 *cpu) {
    uint8_t addr_high_byte = Kernel_8051_L_RAM[cpu->SP_R - 1];
    uint8_t addr_low_byte = Kernel_8051_L_RAM[cpu->SP_R - 2];
    uint16_t all_addr_byte = (addr_high_byte << 8) | addr_low_byte;
    cpu->SP_R = cpu->SP_R - 2;

    cpu->PC_R = all_addr_byte;
    cpu->IE_R = cpu->IE_R | 0x80;
    ESP_LOGI(TAG,"RETI,PC:0x%4x",cpu->PC_R);
 }
void MCS_8051_INSTRUCTION_RLC_A(MCS_8051 *cpu) {
    uint8_t cy_bit_old = (cpu->PSW_R >> 7) & 0x01;
    uint8_t cy_bit_now = cpu->A_R & 0x80;

     cpu->PSW_R = cpu->PSW_R & 0x7F;
    cpu->PSW_R = cpu->PSW_R | cy_bit_now;
    cpu->A_R = (cpu->A_R << 1) | cy_bit_old;

    cpu->PC_R = cpu->PC_R + 1;

    ESP_LOGI(TAG, "RLC_A, PC: 0x%04x", cpu->PC_R);
 }
void MCS_8051_INSTRUCTION_ADDC_A_IMM(MCS_8051 *cpu) { 
    uint8_t immediate_num = Kernel_8051_ROM[cpu->PC_R + 1];

    uint8_t temporary_num_0 = 0;
    uint16_t temporary_num_1 = 0;

    uint8_t temporary_num_2 = 0;
    temporary_num_2 = cpu->PSW_R & 0x80;
    if(temporary_num_2 > cpu->PSW_R){
        temporary_num_0 = cpu->A_R;
        cpu->A_R = (uint8_t)cpu->A_R + immediate_num;
        if(cpu->A_R < temporary_num_0){
            cpu->PSW_R = cpu->PSW_R & 0x7F;
            cpu->PSW_R = cpu->PSW_R | 0x80;
            ESP_LOGI(TAG, "ADDC_A_IMM,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
        }else{
            cpu->PSW_R = cpu->PSW_R & 0x7F;
            ESP_LOGI(TAG, "ADDC_A_IMM,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
        }
    }else {//进位置1
        temporary_num_0 =  (cpu->PSW_R + immediate_num) & 0x01;
        temporary_num_1 =  (cpu->PSW_R + immediate_num) & 0x01;
        if(temporary_num_0 < cpu->PSW_R){
            cpu->A_R = (uint8_t)temporary_num_1;
            cpu->PSW_R = cpu->PSW_R & 0x7F;
            cpu->PSW_R = cpu->PSW_R | 0x80;
            ESP_LOGI(TAG, "ADDC_A_IMM,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
        }else{{
            cpu->A_R = (uint8_t)temporary_num_1;
            cpu->PSW_R = cpu->PSW_R & 0x7F;
            ESP_LOGI(TAG, "ADDC_A_IMM,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
        }}
    }
    cpu->PC_R = cpu->PC_R + 2;
}
void MCS_8051_INSTRUCTION_ADDC_A_DIR(MCS_8051 *cpu) {
    uint8_t opera_RAM_addr = Kernel_8051_ROM[cpu->PC_R + 1];
    int opera_RAM_num = (int)opera_RAM_addr;
    
    int value_bufore = (int)cpu->A_R;

    uint16_t temporary_num = 0;
    uint8_t temporary_num_2 = 0;
    temporary_num_2 = cpu->PSW_R & 0x80;
    if(temporary_num_2 > cpu->PSW_R){//未进位
         if(opera_RAM_num <= 127){
        if(cpu->A_R + Kernel_8051_L_RAM[opera_RAM_num] > 255){
            cpu->A_R = (cpu->A_R + Kernel_8051_L_RAM[opera_RAM_num]) & 0xFF;
            cpu->PSW_R = cpu->PSW_R | 0x80;
            ESP_LOGI(TAG, "ADDC_A_DIRECT,PC: 0x%04x, OPERA_RAM_ADDR: 0x%02x,OPERA_VALUE:0x%02x", cpu->PC_R,opera_RAM_addr,Kernel_8051_L_RAM[opera_RAM_num]);
        }else{
            cpu->PSW_R = cpu->PSW_R & 0x7F;
            cpu->A_R = cpu->A_R + Kernel_8051_L_RAM[opera_RAM_num];
        }
    } else if (opera_RAM_num > 127 && opera_RAM_num <= 255)
    {
        int opera_SFR_num = opera_RAM_num - 128;
        if (cpu->A_R + Kernel_8051_SFR[opera_SFR_num] > 255){
            cpu->A_R = (cpu->A_R + Kernel_8051_SFR[opera_SFR_num]) & 0xFF;
            cpu->PSW_R = cpu->PSW_R | 0x80;
            ESP_LOGI(TAG, "ADDC_A_DIRECT,PC: 0x%04x, OPERA_SFR_ADDR: 0x%02x,OPERA_VALUE:0x%02x", cpu->PC_R,opera_SFR_num,Kernel_8051_SFR[opera_SFR_num]);
        }else{
            cpu->PSW_R = cpu->PSW_R & 0x7F;
            cpu->A_R = cpu->A_R + Kernel_8051_SFR[opera_SFR_num];
            ESP_LOGI(TAG, "ADDC_A_DIRECT,PC: 0x%04x, OPERA_SFR_ADDR: 0x%02x,OPERA_VALUE:0x%02x", cpu->PC_R,opera_SFR_num,Kernel_8051_SFR[opera_SFR_num]);
        }
    
   }
    }else{//进位
         if(opera_RAM_num <= 127){
        if(cpu->A_R + Kernel_8051_L_RAM[opera_RAM_num] + 1 > 255){
            temporary_num = cpu->A_R + Kernel_8051_L_RAM[opera_RAM_num];
            temporary_num = temporary_num + 1;
            cpu->A_R = (uint8_t)temporary_num;
            cpu->PSW_R = cpu->PSW_R | 0x80;
            ESP_LOGI(TAG, "ADDC_A_DIRECT,PC: 0x%04x, OPERA_RAM_ADDR: 0x%02x,OPERA_VALUE:0x%02x", cpu->PC_R,opera_RAM_addr,Kernel_8051_L_RAM[opera_RAM_num]);
        }else{
            cpu->PSW_R = cpu->PSW_R & 0x7F;
            cpu->A_R = cpu->A_R + Kernel_8051_L_RAM[opera_RAM_num] + 1;
        }
    } else if (opera_RAM_num > 127 && opera_RAM_num <= 255)
    {
        
        int opera_SFR_num = opera_RAM_num - 128;
        if (cpu->A_R + Kernel_8051_SFR[opera_SFR_num] + 1 > 255){
            temporary_num = cpu->A_R + Kernel_8051_SFR[opera_SFR_num];
            temporary_num = temporary_num + 1;
            cpu->A_R = (uint8_t)temporary_num;
            cpu->PSW_R = cpu->PSW_R | 0x80;
            ESP_LOGI(TAG, "ADDC_A_DIRECT,PC: 0x%04x, OPERA_SFR_ADDR: 0x%02x,OPERA_VALUE:0x%02x", cpu->PC_R,opera_SFR_num,Kernel_8051_SFR[opera_SFR_num]);
        }else{
            cpu->PSW_R = cpu->PSW_R & 0x7F;
            cpu->A_R = cpu->A_R + Kernel_8051_SFR[opera_SFR_num] + 1;
            ESP_LOGI(TAG, "ADDC_A_DIRECT,PC: 0x%04x, OPERA_SFR_ADDR: 0x%02x,OPERA_VALUE:0x%02x", cpu->PC_R,opera_SFR_num,Kernel_8051_SFR[opera_SFR_num]);
        }
   }
    }
    cpu->PC_R = cpu->PC_R + 2;
   
 }
void MCS_8051_INSTRUCTION_ADDC_AT_R0(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t target_addr = 0;

    if(current_bank == 0){
        target_addr = cpu->REG_R_0[0];
    }else if(current_bank == 1){
        target_addr = cpu->REG_R_1[0];
    }else if (current_bank ==2){
        target_addr = cpu->REG_R_2[0];
    }else if (current_bank ==3){
        target_addr = cpu->REG_R_3[0];
    }
    
    uint16_t temporary_num_0 = 0;
    uint8_t temporary_num_2 = 0;

    temporary_num_2 = cpu->PSW_R & 0x80;
    if(temporary_num_2 > cpu->PSW_R){
        if(target_addr > 127){
        target_addr = target_addr - 128;
       if(cpu->A_R + Kernel_8051_H_RAM[target_addr] > 255 ){
        cpu->A_R = (cpu->A_R + Kernel_8051_H_RAM[target_addr]) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADD_AT_R0,PC: 0x%04x, OPERA_H_RAM_ADDR: 0x%02x", target_addr);
       }else{
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        cpu->A_R = cpu->A_R + Kernel_8051_H_RAM[target_addr];
         ESP_LOGI(TAG, "ADD_AT_R0,PC: 0x%04x, OPERA_H_RAM_ADDR: 0x%02x", target_addr);
       }
       
    }else{
        if(cpu->A_R + Kernel_8051_L_RAM[target_addr] > 255){
            cpu->A_R = (cpu->A_R + Kernel_8051_L_RAM[target_addr]) & 0xFF;
            cpu->PSW_R = cpu->PSW_R | 0x80;
            ESP_LOGI(TAG, "ADDC_AT_R0,PC: 0x%04x, OPERA_L_RAM_ADDR: 0x%02x", target_addr);
        }else{
            cpu->PSW_R = cpu->PSW_R & 0x7F;
            cpu->A_R = cpu->A_R + Kernel_8051_L_RAM[target_addr];
            ESP_LOGI(TAG, "ADDC_AT_R0,PC: 0x%04x, OPERA_L_RAM_ADDR: 0x%02x", target_addr);
        }
    }

    }else{
        if(target_addr > 127){
        target_addr = target_addr - 128;
       if(cpu->A_R + Kernel_8051_H_RAM[target_addr] + 1 > 255 ){
        cpu->A_R = (cpu->A_R + Kernel_8051_H_RAM[target_addr] + 1) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_AT_R0,PC: 0x%04x, OPERA_H_RAM_ADDR: 0x%02x", target_addr);
       }else{
        cpu->PSW_R = cpu->PSW_R & 0x7F;
         cpu->A_R = (uint8_t)cpu->A_R + Kernel_8051_H_RAM[target_addr] + 1;
         ESP_LOGI(TAG, "ADDC_AT_R0,PC: 0x%04x, OPERA_H_RAM_ADDR: 0x%02x", target_addr);
       }
       
    }else{
        if(cpu->A_R + Kernel_8051_L_RAM[target_addr]  + 1>255){
            cpu->A_R = (cpu->A_R + Kernel_8051_L_RAM[target_addr] + 1) & 0xFF;
            cpu->PSW_R = cpu->PSW_R | 0x80;
            ESP_LOGI(TAG, "ADDC_AT_R0,PC: 0x%04x, OPERA_L_RAM_ADDR: 0x%02x", target_addr);
        }else{
            cpu->PSW_R = cpu->PSW_R & 0x7F;
            cpu->A_R = (uint8_t)cpu->A_R + Kernel_8051_L_RAM[target_addr] + 1;
            ESP_LOGI(TAG, "ADDC_AT_R0,PC: 0x%04x, OPERA_L_RAM_ADDR: 0x%02x", target_addr);
        }
    }
    }
    
    cpu->PC_R = cpu->PC_R + 1;
 }
void MCS_8051_INSTRUCTION_ADDC_AT_R1(MCS_8051 *cpu) {
     uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t target_addr = 0;

    if(current_bank == 0){
        target_addr = cpu->REG_R_0[1];
    }else if(current_bank == 1){
        target_addr = cpu->REG_R_1[1];
    }else if (current_bank ==2){
        target_addr = cpu->REG_R_2[1];
    }else if (current_bank ==3){
        target_addr = cpu->REG_R_3[1];
    }
    
    uint16_t temporary_num_0 = 0;
    uint8_t temporary_num_2 = 0;

    temporary_num_2 = cpu->PSW_R & 0x80;
    if(temporary_num_2 > cpu->PSW_R){
        if(target_addr > 127){
        target_addr = target_addr - 128;
       if(cpu->A_R + Kernel_8051_H_RAM[target_addr] > 255 ){
        cpu->A_R = (cpu->A_R + Kernel_8051_H_RAM[target_addr]) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_AT_R1,PC: 0x%04x, OPERA_H_RAM_ADDR: 0x%02x", target_addr);
       }else{
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        cpu->A_R = cpu->A_R + Kernel_8051_H_RAM[target_addr];
         ESP_LOGI(TAG, "ADDC_AT_R1,PC: 0x%04x, OPERA_H_RAM_ADDR: 0x%02x", target_addr);
       }
       
    }else{
        if(cpu->A_R + Kernel_8051_L_RAM[target_addr] > 255){
            cpu->A_R = (cpu->A_R + Kernel_8051_L_RAM[target_addr]) & 0xFF;
            cpu->PSW_R = cpu->PSW_R | 0x80;
            ESP_LOGI(TAG, "ADDC_AT_R1,PC: 0x%04x, OPERA_L_RAM_ADDR: 0x%02x", target_addr);
        }else{
            cpu->PSW_R = cpu->PSW_R & 0x7F;
            cpu->A_R = cpu->A_R + Kernel_8051_L_RAM[target_addr];
            ESP_LOGI(TAG, "ADDC_AT_R1,PC: 0x%04x, OPERA_L_RAM_ADDR: 0x%02x", target_addr);
        }
    }

    }else{
        if(target_addr > 127){
        target_addr = target_addr - 128;
       if(cpu->A_R + Kernel_8051_H_RAM[target_addr] + 1 > 255 ){
        cpu->A_R = (cpu->A_R + Kernel_8051_H_RAM[target_addr] + 1) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_AT_R0,PC: 0x%04x, OPERA_H_RAM_ADDR: 0x%02x", target_addr);
       }else{
        cpu->PSW_R = cpu->PSW_R & 0x7F;
         cpu->A_R = (uint8_t)cpu->A_R + Kernel_8051_H_RAM[target_addr] + 1;
         ESP_LOGI(TAG, "ADDC_AT_R0,PC: 0x%04x, OPERA_H_RAM_ADDR: 0x%02x", target_addr);
       }
       
    }else{
        if(cpu->A_R + Kernel_8051_L_RAM[target_addr]  + 1>255){
            cpu->A_R = (cpu->A_R + Kernel_8051_L_RAM[target_addr] + 1) & 0xFF;
            cpu->PSW_R = cpu->PSW_R | 0x80;
            ESP_LOGI(TAG, "ADDC_AT_R0,PC: 0x%04x, OPERA_L_RAM_ADDR: 0x%02x", target_addr);
        }else{
            cpu->PSW_R = cpu->PSW_R & 0x7F;
            cpu->A_R = (uint8_t)cpu->A_R + Kernel_8051_L_RAM[target_addr] + 1;
            ESP_LOGI(TAG, "ADDC_AT_R0,PC: 0x%04x, OPERA_L_RAM_ADDR: 0x%02x", target_addr);
        }
    }
    }
    
    cpu->PC_R = cpu->PC_R + 1;
 }
void MCS_8051_INSTRUCTION_ADDC_R0(MCS_8051 *cpu) { 
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t addend_num = 0;

    if(current_bank == 0){
       addend_num = cpu->REG_R_0[0];
    }else if(current_bank == 1){
       addend_num = cpu->REG_R_1[0];
    }else if (current_bank ==2){
        addend_num = cpu->REG_R_2[0];
    }else if (current_bank ==3){
        addend_num = cpu->REG_R_3[0];
    }

    uint8_t temporary_num_2 = 0;
    temporary_num_2 = cpu->PSW_R & 0x80;

    if(temporary_num_2 > cpu->PSW_R){
         if(cpu->A_R + addend_num > 255){
        cpu->A_R = (cpu->A_R + addend_num) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_R0,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num;
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        ESP_LOGI(TAG, "ADDC_R0,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    }else{
         if(cpu->A_R + addend_num + 1 > 255){
        cpu->A_R = (cpu->A_R + addend_num + 1) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_R0,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num + 1;
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        ESP_LOGI(TAG, "ADDC_R0,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    }
     cpu->PC_R = cpu->PC_R + 1;
}
void MCS_8051_INSTRUCTION_ADDC_R1(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t addend_num = 0;

    if(current_bank == 0){
       addend_num = cpu->REG_R_0[1];
    }else if(current_bank == 1){
       addend_num = cpu->REG_R_1[1];
    }else if (current_bank ==2){
        addend_num = cpu->REG_R_2[1];
    }else if (current_bank ==3){
        addend_num = cpu->REG_R_3[1];
    }

    uint8_t temporary_num_2 = 0;
    temporary_num_2 = cpu->PSW_R & 0x80;

    if(temporary_num_2 > cpu->PSW_R){
         if(cpu->A_R + addend_num > 255){
        cpu->A_R = (cpu->A_R + addend_num) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_R1,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num;
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        ESP_LOGI(TAG, "ADDC_R1,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    }else{
         if(cpu->A_R + addend_num + 1 > 255){
        cpu->A_R = (cpu->A_R + addend_num + 1) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_R1,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num + 1;
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        ESP_LOGI(TAG, "ADDC_R1,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    }
     cpu->PC_R = cpu->PC_R + 1;
 }
void MCS_8051_INSTRUCTION_ADDC_R2(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t addend_num = 0;

    if(current_bank == 0){
       addend_num = cpu->REG_R_0[2];
    }else if(current_bank == 1){
       addend_num = cpu->REG_R_1[2];
    }else if (current_bank ==2){
        addend_num = cpu->REG_R_2[2];
    }else if (current_bank ==3){
        addend_num = cpu->REG_R_3[2];
    }

    uint8_t temporary_num_2 = 0;
    temporary_num_2 = cpu->PSW_R & 0x80;

    if(temporary_num_2 > cpu->PSW_R){
         if(cpu->A_R + addend_num > 255){
        cpu->A_R = (cpu->A_R + addend_num) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_R2,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num;
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        ESP_LOGI(TAG, "ADDC_R2,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    }else{
         if(cpu->A_R + addend_num + 1 > 255){
        cpu->A_R = (cpu->A_R + addend_num + 1) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_R2,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num + 1;
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        ESP_LOGI(TAG, "ADDC_R2,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    }
     cpu->PC_R = cpu->PC_R + 1;
 }
void MCS_8051_INSTRUCTION_ADDC_R3(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t addend_num = 0;

    if(current_bank == 0){
       addend_num = cpu->REG_R_0[3];
    }else if(current_bank == 1){
       addend_num = cpu->REG_R_1[3];
    }else if (current_bank ==2){
        addend_num = cpu->REG_R_2[3];
    }else if (current_bank ==3){
        addend_num = cpu->REG_R_3[3];
    }

    uint8_t temporary_num_2 = 0;
    temporary_num_2 = cpu->PSW_R & 0x80;

    if(temporary_num_2 > cpu->PSW_R){
         if(cpu->A_R + addend_num > 255){
        cpu->A_R = (cpu->A_R + addend_num) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_R3,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num;
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        ESP_LOGI(TAG, "ADDC_R3,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    }else{
         if(cpu->A_R + addend_num + 1 > 255){
        cpu->A_R = (cpu->A_R + addend_num + 1) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_R3,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num + 1;
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        ESP_LOGI(TAG, "ADDC_R3,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    }
     cpu->PC_R = cpu->PC_R + 1;
 }
void MCS_8051_INSTRUCTION_ADDC_R4(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t addend_num = 0;

    if(current_bank == 0){
       addend_num = cpu->REG_R_0[4];
    }else if(current_bank == 1){
       addend_num = cpu->REG_R_1[4];
    }else if (current_bank ==2){
        addend_num = cpu->REG_R_2[4];
    }else if (current_bank ==3){
        addend_num = cpu->REG_R_3[4];
    }

    uint8_t temporary_num_2 = 0;
    temporary_num_2 = cpu->PSW_R & 0x80;

    if(temporary_num_2 > cpu->PSW_R){
         if(cpu->A_R + addend_num > 255){
        cpu->A_R = (cpu->A_R + addend_num) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_R4,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num;
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        ESP_LOGI(TAG, "ADDC_R4,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    }else{
         if(cpu->A_R + addend_num + 1 > 255){
        cpu->A_R = (cpu->A_R + addend_num + 1) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_R4,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num + 1;
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        ESP_LOGI(TAG, "ADDC_R4,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    }
     cpu->PC_R = cpu->PC_R + 1;
 }
void MCS_8051_INSTRUCTION_ADDC_R5(MCS_8051 *cpu) { 
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t addend_num = 0;

    if(current_bank == 0){
       addend_num = cpu->REG_R_0[5];
    }else if(current_bank == 1){
       addend_num = cpu->REG_R_1[5];
    }else if (current_bank ==2){
        addend_num = cpu->REG_R_2[5];
    }else if (current_bank ==3){
        addend_num = cpu->REG_R_3[5];
    }

    uint8_t temporary_num_2 = 0;
    temporary_num_2 = cpu->PSW_R & 0x80;

    if(temporary_num_2 > cpu->PSW_R){
         if(cpu->A_R + addend_num > 255){
        cpu->A_R = (cpu->A_R + addend_num) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_R5,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num;
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        ESP_LOGI(TAG, "ADDC_R5,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    }else{
         if(cpu->A_R + addend_num + 1 > 255){
        cpu->A_R = (cpu->A_R + addend_num + 1) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_R5,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num + 1;
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        ESP_LOGI(TAG, "ADDC_R5,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    }
     cpu->PC_R = cpu->PC_R + 1;
}
void MCS_8051_INSTRUCTION_ADDC_R6(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t addend_num = 0;

    if(current_bank == 0){
       addend_num = cpu->REG_R_0[6];
    }else if(current_bank == 1){
       addend_num = cpu->REG_R_1[6];
    }else if (current_bank ==2){
        addend_num = cpu->REG_R_2[6];
    }else if (current_bank ==3){
        addend_num = cpu->REG_R_3[6];
    }

    uint8_t temporary_num_2 = 0;
    temporary_num_2 = cpu->PSW_R & 0x80;

    if(temporary_num_2 > cpu->PSW_R){
         if(cpu->A_R + addend_num > 255){
        cpu->A_R = (cpu->A_R + addend_num) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_R6,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num;
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        ESP_LOGI(TAG, "ADDC_R6,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    }else{
         if(cpu->A_R + addend_num + 1 > 255){
        cpu->A_R = (cpu->A_R + addend_num + 1) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_R6,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num + 1;
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        ESP_LOGI(TAG, "ADDC_R6,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    }
     cpu->PC_R = cpu->PC_R + 1;
 }
void MCS_8051_INSTRUCTION_ADDC_R7(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t addend_num = 0;

    if(current_bank == 0){
       addend_num = cpu->REG_R_0[7];
    }else if(current_bank == 1){
       addend_num = cpu->REG_R_1[7];
    }else if (current_bank ==2){
        addend_num = cpu->REG_R_2[7];
    }else if (current_bank ==3){
        addend_num = cpu->REG_R_3[7];
    }

    uint8_t temporary_num_2 = 0;
    temporary_num_2 = cpu->PSW_R & 0x80;

    if(temporary_num_2 > cpu->PSW_R){
         if(cpu->A_R + addend_num > 255){
        cpu->A_R = (cpu->A_R + addend_num) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_R7,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num;
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        ESP_LOGI(TAG, "ADDC_R7,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    }else{
         if(cpu->A_R + addend_num + 1 > 255){
        cpu->A_R = (cpu->A_R + addend_num + 1) & 0xFF;
        cpu->PSW_R = cpu->PSW_R | 0x80;
        ESP_LOGI(TAG, "ADDC_R7,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->A_R = cpu->A_R + addend_num + 1;
        cpu->PSW_R = cpu->PSW_R & 0x7F;
        ESP_LOGI(TAG, "ADDC_R7,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
    }
     cpu->PC_R = cpu->PC_R + 1;
 }

//0x40-0x4F
void MCS_8051_INSTRUCTION_JC(MCS_8051 *cpu) { //JUMP IF CARRY 
    uint8_t jump_addr = Kernel_8051_ROM[cpu->PC_R + 1];
    uint8_t judg_bit = cpu->PSW_R & 0x80;

    if(judg_bit == 0){
        cpu->PC_R = cpu->PC_R + 2;
        ESP_LOGI(TAG, "JC,CY=0,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->PC_R = jump_addr;
        ESP_LOGI(TAG, "JC,CY=1,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
}
void MCS_8051_INSTRUCTION_ORL_DIR_A(MCS_8051 *cpu) { 
    uint8_t opera_DIR_addr = Kernel_8051_ROM[cpu->PC_R + 1];
    uint8_t A_RG_value = cpu->A_R;

    int opera_DIR_num = (int)opera_DIR_addr;
    cpu->PC_R = cpu->PC_R + 2;

     if(opera_DIR_num <= 127){
            Kernel_8051_L_RAM[opera_DIR_num] = Kernel_8051_L_RAM[opera_DIR_num] | A_RG_value;
            ESP_LOGI(TAG, "ORL_DIR_A,PC: 0x%04x, OPER_RAM_ADDR: 0x%02x,AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,opera_DIR_addr,Kernel_8051_L_RAM[opera_DIR_num]);
    } else if (opera_DIR_num > 127 && opera_DIR_num <= 255)
    {
        int opera_SFR_num = opera_DIR_num - 128;
            Kernel_8051_SFR[opera_SFR_num] = Kernel_8051_SFR[opera_SFR_num] | A_RG_value;
            ESP_LOGI(TAG, "ORL_DIR_A,PC: 0x%04x, OPER_SFR_ADDR: 0x%02x,AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,(uint8_t)opera_DIR_num,Kernel_8051_SFR[opera_SFR_num]); 
    }
}
void MCS_8051_INSTRUCTION_ORL_DIR_IMM(MCS_8051 *cpu) { 
        uint8_t opera_addr = Kernel_8051_ROM[cpu->PC_R + 1];
        uint8_t opera_imm_num = Kernel_8051_ROM[cpu->PC_R + 2];
        cpu->PC_R = cpu->PC_R + 3;

        int opera_DIR_num = (int)opera_addr;
         if(opera_DIR_num <= 127){
            Kernel_8051_L_RAM[opera_DIR_num] = Kernel_8051_L_RAM[opera_DIR_num] | opera_imm_num;
            ESP_LOGI(TAG, "ORL_DIR_IMM,PC: 0x%04x, OPER_RAM_ADDR: 0x%02x,AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,opera_addr,Kernel_8051_L_RAM[opera_DIR_num]);
    } else if (opera_DIR_num > 127 && opera_DIR_num <= 255)
    {
        int opera_SFR_num = opera_DIR_num - 128;
            Kernel_8051_SFR[opera_SFR_num] = Kernel_8051_SFR[opera_SFR_num] | opera_imm_num;
            ESP_LOGI(TAG, "ORL_DIR_IMM,PC: 0x%04x, OPER_SFR_ADDR: 0x%02x,AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,(uint8_t)opera_DIR_num,Kernel_8051_SFR[opera_SFR_num]); 
    }
}
void MCS_8051_INSTRUCTION_ORL_A_IMM(MCS_8051 *cpu) {
    uint8_t imm_num = Kernel_8051_ROM[cpu->PC_R + 1];
    cpu->PC_R = cpu->PC_R + 2;

    cpu->A_R = cpu->A_R | imm_num;
    ESP_LOGI(TAG,"ORL_A_IMM,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ORL_A_DIR(MCS_8051 *cpu) {
    uint8_t dir_addr = Kernel_8051_ROM[cpu->PC_R + 1];
    cpu->PC_R = cpu->PC_R + 2;

    int opera_DIR_num = (int)dir_addr;
    if(opera_DIR_num <= 127){
        cpu->A_R = cpu->A_R | Kernel_8051_L_RAM[opera_DIR_num];
         ESP_LOGI(TAG, "ORL_A_IMM,PC: 0x%04x, AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,cpu->A_R); 
    }else{
        int opera_SFR_num = opera_DIR_num - 128;
        cpu->A_R = cpu->A_R | Kernel_8051_SFR[opera_SFR_num];
        ESP_LOGI(TAG, "ORL_A_IMM,PC: 0x%04x, AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,cpu->A_R); 
    }
 }
void MCS_8051_INSTRUCTION_ORL_A_AT_R0(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t opera_dir = 0;
    cpu->PC_R = cpu->PC_R + 1;

    if(current_bank == 0){
        opera_dir = cpu->REG_R_0[0];
    }else if(current_bank == 1){
        opera_dir = cpu->REG_R_1[0];
    }else if (current_bank ==2){
        opera_dir = cpu->REG_R_2[0];
    }else if (current_bank ==3){
        opera_dir = cpu->REG_R_3[0];
    }

     if (opera_dir <= 0x7F) {
        cpu->A_R = cpu->A_R | Kernel_8051_L_RAM[opera_dir];
        ESP_LOGI(TAG,"ORL_A_AT_R0,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
    } 
    else {
        cpu->A_R = cpu->A_R | Kernel_8051_SFR[opera_dir - 0x80];
         ESP_LOGI(TAG,"ORL_A_AT_R0,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
    }
 }
void MCS_8051_INSTRUCTION_ORL_A_AT_R1(MCS_8051 *cpu) { 
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t opera_dir = 0;
    cpu->PC_R = cpu->PC_R + 1;

    if(current_bank == 0){
        opera_dir = cpu->REG_R_0[1];
    }else if(current_bank == 1){
        opera_dir = cpu->REG_R_1[1];
    }else if (current_bank ==2){
        opera_dir = cpu->REG_R_2[1];
    }else if (current_bank ==3){
        opera_dir = cpu->REG_R_3[1];
    }

     if (opera_dir <= 0x7F) {
        cpu->A_R = cpu->A_R | Kernel_8051_L_RAM[opera_dir];
        ESP_LOGI(TAG,"ORL_A_AT_R1,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
    } 
    else {
        cpu->A_R = cpu->A_R | Kernel_8051_SFR[opera_dir - 0x80];
         ESP_LOGI(TAG,"ORL_A_AT_R1,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
    }
}
void MCS_8051_INSTRUCTION_ORL_R0(MCS_8051 *cpu) {
    cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = cpu->A_R | cpu->REG_R_0[0];
    }else if(current_bank == 1){
        cpu->A_R = cpu->A_R | cpu->REG_R_1[0];
    }else if (current_bank ==2){
        cpu->A_R = cpu->A_R | cpu->REG_R_2[0];
    }else if (current_bank ==3){
        cpu->A_R = cpu->A_R | cpu->REG_R_3[0];
    }
     ESP_LOGI(TAG,"ORL_A_R0,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ORL_R1(MCS_8051 *cpu) {
     cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = cpu->A_R | cpu->REG_R_0[1];
    }else if(current_bank == 1){
        cpu->A_R = cpu->A_R | cpu->REG_R_1[1];
    }else if (current_bank ==2){
        cpu->A_R = cpu->A_R | cpu->REG_R_2[1];
    }else if (current_bank ==3){
        cpu->A_R = cpu->A_R | cpu->REG_R_3[1];
    }
     ESP_LOGI(TAG,"ORL_A_R1,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ORL_R2(MCS_8051 *cpu) { 
     cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = cpu->A_R | cpu->REG_R_0[2];
    }else if(current_bank == 1){
        cpu->A_R = cpu->A_R | cpu->REG_R_1[2];
    }else if (current_bank ==2){
        cpu->A_R = cpu->A_R | cpu->REG_R_2[2];
    }else if (current_bank ==3){
        cpu->A_R = cpu->A_R | cpu->REG_R_3[2];
    }
     ESP_LOGI(TAG,"ORL_A_R2,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
}
void MCS_8051_INSTRUCTION_ORL_R3(MCS_8051 *cpu) {
     cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = cpu->A_R | cpu->REG_R_0[3];
    }else if(current_bank == 1){
        cpu->A_R = cpu->A_R | cpu->REG_R_1[3];
    }else if (current_bank ==2){
        cpu->A_R = cpu->A_R | cpu->REG_R_2[3];
    }else if (current_bank ==3){
        cpu->A_R = cpu->A_R | cpu->REG_R_3[3];
    }
     ESP_LOGI(TAG,"ORL_A_R3,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ORL_R4(MCS_8051 *cpu) {
     cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = cpu->A_R | cpu->REG_R_0[4];
    }else if(current_bank == 1){
        cpu->A_R = cpu->A_R | cpu->REG_R_1[4];
    }else if (current_bank ==2){
        cpu->A_R = cpu->A_R | cpu->REG_R_2[4];
    }else if (current_bank ==3){
        cpu->A_R = cpu->A_R | cpu->REG_R_3[4];
    }
     ESP_LOGI(TAG,"ORL_A_R4,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ORL_R5(MCS_8051 *cpu) {
     cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = cpu->A_R | cpu->REG_R_0[5];
    }else if(current_bank == 1){
        cpu->A_R = cpu->A_R | cpu->REG_R_1[5];
    }else if (current_bank ==2){
        cpu->A_R = cpu->A_R | cpu->REG_R_2[5];
    }else if (current_bank ==3){
        cpu->A_R = cpu->A_R | cpu->REG_R_3[5];
    }
     ESP_LOGI(TAG,"ORL_A_R5,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ORL_R6(MCS_8051 *cpu) {
     cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = cpu->A_R | cpu->REG_R_0[6];
    }else if(current_bank == 1){
        cpu->A_R = cpu->A_R | cpu->REG_R_1[6];
    }else if (current_bank ==2){
        cpu->A_R = cpu->A_R | cpu->REG_R_2[6];
    }else if (current_bank ==3){
        cpu->A_R = cpu->A_R | cpu->REG_R_3[6];
    }
     ESP_LOGI(TAG,"ORL_A_R6,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ORL_R7(MCS_8051 *cpu) {
     cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = cpu->A_R | cpu->REG_R_0[7];
    }else if(current_bank == 1){
        cpu->A_R = cpu->A_R | cpu->REG_R_1[7];
    }else if (current_bank ==2){
        cpu->A_R = cpu->A_R | cpu->REG_R_2[7];
    }else if (current_bank ==3){
        cpu->A_R = cpu->A_R | cpu->REG_R_3[7];
    }
     ESP_LOGI(TAG,"ORL_A_R7,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }

//0x50-0x5F
void MCS_8051_INSTRUCTION_JNC(MCS_8051 *cpu) {//Jump if No Carry
    int8_t jump_addr = Kernel_8051_ROM[cpu->PC_R + 1];
    uint8_t judg_bit = cpu->PSW_R & 0x80;

    if(judg_bit == 0){
        cpu->PC_R = jump_addr;
        ESP_LOGI(TAG, "JNC,CY=0,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }else{
        cpu->PC_R = cpu->PC_R + 2;
        ESP_LOGI(TAG, "JNC,CY=1,PC:0x%04x,FINAL_VALUE: 0x%02x", cpu->PC_R,cpu->A_R);
    }
 }
void MCS_8051_INSTRUCTION_ANL_DIR_A(MCS_8051 *cpu) { 
     uint8_t opera_addr = Kernel_8051_ROM[cpu->PC_R + 1];
    uint8_t opera_imm_num = Kernel_8051_ROM[cpu->PC_R + 2];
        cpu->PC_R = cpu->PC_R + 3;

        int opera_DIR_num = (int)opera_addr;
         if(opera_DIR_num <= 127){
            Kernel_8051_L_RAM[opera_DIR_num] = (uint8_t)Kernel_8051_L_RAM[opera_DIR_num] & opera_imm_num;
            ESP_LOGI(TAG, "ANL_DIR_IMM,PC: 0x%04x, OPER_RAM_ADDR: 0x%02x,AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,opera_addr,Kernel_8051_L_RAM[opera_DIR_num]);
    } else if (opera_DIR_num > 127 && opera_DIR_num <= 255)
    {
        int opera_SFR_num = opera_DIR_num - 128;
            Kernel_8051_SFR[opera_SFR_num] = (uint8_t)Kernel_8051_SFR[opera_SFR_num] & opera_imm_num;
            ESP_LOGI(TAG, "ANL_DIR_IMM,PC: 0x%04x, OPER_SFR_ADDR: 0x%02x,AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,(uint8_t)opera_DIR_num,Kernel_8051_SFR[opera_SFR_num]); 
    }

}
void MCS_8051_INSTRUCTION_ANL_DIR_IMM(MCS_8051 *cpu) {
      uint8_t opera_addr = Kernel_8051_ROM[cpu->PC_R + 1];
      uint8_t opera_imm_num = Kernel_8051_ROM[cpu->PC_R + 2];
        cpu->PC_R = cpu->PC_R + 3;

        int opera_DIR_num = (int)opera_addr;
         if(opera_DIR_num <= 127){
            Kernel_8051_L_RAM[opera_DIR_num] = (uint8_t)Kernel_8051_L_RAM[opera_DIR_num] & opera_imm_num;
            ESP_LOGI(TAG, "ANL_DIR_IMM,PC: 0x%04x, OPER_RAM_ADDR: 0x%02x,AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,opera_addr,Kernel_8051_L_RAM[opera_DIR_num]);
    } else if (opera_DIR_num > 127 && opera_DIR_num <= 255)
    {
        int opera_SFR_num = opera_DIR_num - 128;
            Kernel_8051_SFR[opera_SFR_num] = (uint8_t)Kernel_8051_SFR[opera_SFR_num] & opera_imm_num;
            ESP_LOGI(TAG, "ANL_DIR_IMM,PC: 0x%04x, OPER_SFR_ADDR: 0x%02x,AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,(uint8_t)opera_DIR_num,Kernel_8051_SFR[opera_SFR_num]); 
    }
 }
void MCS_8051_INSTRUCTION_ANL_A_IMM(MCS_8051 *cpu) {
    uint8_t imm_num = Kernel_8051_ROM[cpu->PC_R + 1];
    cpu->PC_R = cpu->PC_R + 2;

    cpu->A_R = (uint8_t)cpu->A_R & imm_num;
    ESP_LOGI(TAG,"ANL_A_IMM,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ANL_A_DIR(MCS_8051 *cpu) { 
    uint8_t dir_addr = Kernel_8051_ROM[cpu->PC_R + 1];
    cpu->PC_R = cpu->PC_R + 2;

    int opera_DIR_num = (int)dir_addr;
    if(opera_DIR_num <= 127){
        cpu->A_R = (uint8_t)cpu->A_R & Kernel_8051_L_RAM[opera_DIR_num];
         ESP_LOGI(TAG, "ANL_A_IMM,PC: 0x%04x, AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,cpu->A_R); 
    }else{
        int opera_SFR_num = opera_DIR_num - 128;
        cpu->A_R = (uint8_t)cpu->A_R & Kernel_8051_SFR[opera_SFR_num];
        ESP_LOGI(TAG, "ANL_A_IMM,PC: 0x%04x, AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,cpu->A_R); 
    }
}
void MCS_8051_INSTRUCTION_ANL_AT_R0(MCS_8051 *cpu) {
    cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_0[0];
    }else if(current_bank == 1){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_1[0];
    }else if (current_bank ==2){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_2[0];
    }else if (current_bank ==3){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_3[0];
    }
     ESP_LOGI(TAG,"ANL_AT_R0,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ANL_AT_R1(MCS_8051 *cpu) {
    cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_0[1];
    }else if(current_bank == 1){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_1[1];
    }else if (current_bank ==2){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_2[1];
    }else if (current_bank ==3){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_3[1];
    }
     ESP_LOGI(TAG,"ANL_AT_R1,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ANL_R0(MCS_8051 *cpu) {
    cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_0[0];
    }else if(current_bank == 1){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_1[0];
    }else if (current_bank ==2){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_2[0];
    }else if (current_bank ==3){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_3[0];
    }
     ESP_LOGI(TAG,"ANL_A_R0,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ANL_R1(MCS_8051 *cpu) {
     cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_0[1];
    }else if(current_bank == 1){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_1[1];
    }else if (current_bank ==2){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_2[1];
    }else if (current_bank ==3){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_3[1];
    }
     ESP_LOGI(TAG,"ANL_A_R1,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ANL_R2(MCS_8051 *cpu) {
     cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_0[2];
    }else if(current_bank == 1){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_1[2];
    }else if (current_bank ==2){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_2[2];
    }else if (current_bank ==3){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_3[2];
    }
     ESP_LOGI(TAG,"ANL_A_R2,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ANL_R3(MCS_8051 *cpu) {
    cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_0[3];
    }else if(current_bank == 1){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_1[3];
    }else if (current_bank ==2){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_2[3];
    }else if (current_bank ==3){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_3[3];
    }
     ESP_LOGI(TAG,"ANL_A_R3,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ANL_R4(MCS_8051 *cpu) {
     cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_0[4];
    }else if(current_bank == 1){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_1[4];
    }else if (current_bank ==2){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_2[4];
    }else if (current_bank ==3){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_3[4];
    }
     ESP_LOGI(TAG,"ANL_A_R4,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ANL_R5(MCS_8051 *cpu) {
     cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_0[5];
    }else if(current_bank == 1){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_1[5];
    }else if (current_bank ==2){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_2[5];
    }else if (current_bank ==3){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_3[5];
    }
     ESP_LOGI(TAG,"ANL_A_R5,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ANL_R6(MCS_8051 *cpu) {
    cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_0[6];
    }else if(current_bank == 1){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_1[6];
    }else if (current_bank ==2){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_2[6];
    }else if (current_bank ==3){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_3[6];
    }
     ESP_LOGI(TAG,"ANL_A_R6,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_ANL_R7(MCS_8051 *cpu) {
     cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_0[7];
    }else if(current_bank == 1){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_1[7];
    }else if (current_bank ==2){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_2[7];
    }else if (current_bank ==3){
        cpu->A_R = (uint8_t)cpu->A_R & cpu->REG_R_3[7];
    }
     ESP_LOGI(TAG,"ANL_A_R7,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }

//0x60-0x6F
void MCS_8051_INSTRUCTION_JZ(MCS_8051 *cpu) {//Jump if Zero (A=0)
    uint8_t jump_addr = Kernel_8051_ROM[cpu->PC_R + 1];
    uint8_t judg_value = cpu->A_R;

    if(judg_value == 0){
        cpu->PC_R = jump_addr;
        ESP_LOGI(TAG, "JZ,A=0,PC:0x%04x", cpu->PC_R);
    }else{
        cpu->PC_R = cpu->PC_R + 2;
        ESP_LOGI(TAG, "JZ,A=1,PC:0x%04x", cpu->PC_R);
    }
 }
void MCS_8051_INSTRUCTION_XRL_DIR_A(MCS_8051 *cpu) {
    uint8_t opera_DIR_addr = Kernel_8051_ROM[cpu->PC_R + 1];
    uint8_t A_RG_value = cpu->A_R;

    int opera_DIR_num = (int)opera_DIR_addr;
    cpu->PC_R = cpu->PC_R + 2;

     if(opera_DIR_num <= 127){
            Kernel_8051_L_RAM[opera_DIR_num] = ~(Kernel_8051_L_RAM[opera_DIR_num] | A_RG_value);
            ESP_LOGI(TAG, "XRL_DIR_A,PC: 0x%04x, OPER_RAM_ADDR: 0x%02x,AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,opera_DIR_addr,Kernel_8051_L_RAM[opera_DIR_num]);
    } else if (opera_DIR_num > 127 && opera_DIR_num <= 255)
    {
        int opera_SFR_num = opera_DIR_num - 128;
            Kernel_8051_SFR[opera_SFR_num] = ~(Kernel_8051_SFR[opera_SFR_num] | A_RG_value);
            ESP_LOGI(TAG, "XRL_DIR_A,PC: 0x%04x, OPER_SFR_ADDR: 0x%02x,AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,(uint8_t)opera_DIR_num,Kernel_8051_SFR[opera_SFR_num]); 
    }
 }
void MCS_8051_INSTRUCTION_XRL_DIR_IMM(MCS_8051 *cpu) {
        uint8_t opera_addr = Kernel_8051_ROM[cpu->PC_R + 1];
        uint8_t opera_imm_num = Kernel_8051_ROM[cpu->PC_R + 2];
        cpu->PC_R = cpu->PC_R + 3;

        int opera_DIR_num = (int)opera_addr;
         if(opera_DIR_num <= 127){
            Kernel_8051_L_RAM[opera_DIR_num] = ~(Kernel_8051_L_RAM[opera_DIR_num] | opera_imm_num);
            ESP_LOGI(TAG, "XRL_DIR_IMM,PC: 0x%04x, OPER_RAM_ADDR: 0x%02x,AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,opera_addr,Kernel_8051_L_RAM[opera_DIR_num]);
    } else if (opera_DIR_num > 127 && opera_DIR_num <= 255)
    {
        int opera_SFR_num = opera_DIR_num - 128;
            Kernel_8051_SFR[opera_SFR_num] = ~(Kernel_8051_SFR[opera_SFR_num] | opera_imm_num);
            ESP_LOGI(TAG, "XRL_DIR_IMM,PC: 0x%04x, OPER_SFR_ADDR: 0x%02x,AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,(uint8_t)opera_DIR_num,Kernel_8051_SFR[opera_SFR_num]); 
    }

 }
void MCS_8051_INSTRUCTION_XRL_A_IMM(MCS_8051 *cpu) {
    uint8_t imm_num = Kernel_8051_ROM[cpu->PC_R + 1];
    cpu->PC_R = cpu->PC_R + 2;

    cpu->A_R = ~(cpu->A_R | imm_num);
    ESP_LOGI(TAG,"XRL_A_IMM,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
}
void MCS_8051_INSTRUCTION_XRL_A_DIR(MCS_8051 *cpu) { 
    uint8_t dir_addr = Kernel_8051_ROM[cpu->PC_R + 1];
    cpu->PC_R = cpu->PC_R + 2;

    int opera_DIR_num = (int)dir_addr;
    if(opera_DIR_num <= 127){
        cpu->A_R = ~(cpu->A_R | Kernel_8051_L_RAM[opera_DIR_num]);
         ESP_LOGI(TAG, "XRL_A_IMM,PC: 0x%04x, AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,cpu->A_R); 
    }else{
        int opera_SFR_num = opera_DIR_num - 128;
        cpu->A_R = ~(cpu->A_R | Kernel_8051_SFR[opera_SFR_num]);
        ESP_LOGI(TAG, "XRL_A_IMM,PC: 0x%04x, AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,cpu->A_R); 
    }
}
void MCS_8051_INSTRUCTION_XRL_AT_R0(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t opera_dir = 0;
    cpu->PC_R = cpu->PC_R + 1;

    if(current_bank == 0){
        opera_dir = cpu->REG_R_0[0];
    }else if(current_bank == 1){
        opera_dir = cpu->REG_R_1[0];
    }else if (current_bank ==2){
        opera_dir = cpu->REG_R_2[0];
    }else if (current_bank ==3){
        opera_dir = cpu->REG_R_3[0];
    }

     if (opera_dir <= 0x7F) {
        cpu->A_R = ~(cpu->A_R | Kernel_8051_L_RAM[opera_dir]);
        ESP_LOGI(TAG,"ORL_A_AT_R0,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
    } 
    else {
        cpu->A_R = ~(cpu->A_R | Kernel_8051_SFR[opera_dir - 0x80]);
         ESP_LOGI(TAG,"ORL_A_AT_R0,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
    }
 }
void MCS_8051_INSTRUCTION_XRL_AT_R1(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t opera_dir = 0;
    cpu->PC_R = cpu->PC_R + 1;

    if(current_bank == 0){
        opera_dir = cpu->REG_R_0[1];
    }else if(current_bank == 1){
        opera_dir = cpu->REG_R_1[1];
    }else if (current_bank ==2){
        opera_dir = cpu->REG_R_2[1];
    }else if (current_bank ==3){
        opera_dir = cpu->REG_R_3[1];
    }

     if (opera_dir <= 0x7F) {
        cpu->A_R = ~(cpu->A_R | Kernel_8051_L_RAM[opera_dir]);
        ESP_LOGI(TAG,"ORL_A_AT_R1,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
    } 
    else {
        cpu->A_R = ~(cpu->A_R | Kernel_8051_SFR[opera_dir - 0x80]);
         ESP_LOGI(TAG,"ORL_A_AT_R1,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
    }
 }
void MCS_8051_INSTRUCTION_XRL_R0(MCS_8051 *cpu) {
    cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_0[0]);
    }else if(current_bank == 1){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_1[0]);
    }else if (current_bank ==2){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_2[0]);
    }else if (current_bank ==3){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_3[0]);
    }
     ESP_LOGI(TAG,"XRL_A_R0,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_XRL_R1(MCS_8051 *cpu) {
    cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_0[1]);
    }else if(current_bank == 1){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_1[1]);
    }else if (current_bank ==2){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_2[1]);
    }else if (current_bank ==3){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_3[1]);
    }
     ESP_LOGI(TAG,"XRL_A_R1,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_XRL_R2(MCS_8051 *cpu) {
    cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_0[2]);
    }else if(current_bank == 1){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_1[2]);
    }else if (current_bank ==2){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_2[2]);
    }else if (current_bank ==3){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_3[2]);
    }
     ESP_LOGI(TAG,"XRL_A_R2,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_XRL_R3(MCS_8051 *cpu) {
    cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_0[3]);
    }else if(current_bank == 1){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_1[3]);
    }else if (current_bank ==2){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_2[3]);
    }else if (current_bank ==3){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_3[3]);
    }
     ESP_LOGI(TAG,"XRL_A_R3,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_XRL_R4(MCS_8051 *cpu) {
    cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_0[4]);
    }else if(current_bank == 1){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_1[4]);
    }else if (current_bank ==2){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_2[4]);
    }else if (current_bank ==3){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_3[4]);
    }
     ESP_LOGI(TAG,"XRL_A_R4,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_XRL_R5(MCS_8051 *cpu) {
    cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_0[5]);
    }else if(current_bank == 1){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_1[5]);
    }else if (current_bank ==2){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_2[5]);
    }else if (current_bank ==3){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_3[5]);
    }
     ESP_LOGI(TAG,"XRL_A_R5,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_XRL_R6(MCS_8051 *cpu) {
    cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_0[6]);
    }else if(current_bank == 1){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_1[6]);
    }else if (current_bank ==2){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_2[6]);
    }else if (current_bank ==3){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_3[6]);
    }
     ESP_LOGI(TAG,"XRL_A_R6,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_XRL_R7(MCS_8051 *cpu) {
    cpu->PC_R = cpu->PC_R + 1;
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;

    if(current_bank == 0){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_0[7]);
    }else if(current_bank == 1){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_1[7]);
    }else if (current_bank ==2){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_2[7]);
    }else if (current_bank ==3){
        cpu->A_R = ~(cpu->A_R | cpu->REG_R_3[7]);
    }
     ESP_LOGI(TAG,"XRL_A_R7,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,cpu->A_R);
 }

//0x70-0x7F
void MCS_8051_INSTRUCTION_JNZ(MCS_8051 *cpu) {////Jump if Not Zero (A=0)
    uint8_t jump_addr = Kernel_8051_ROM[cpu->PC_R + 1];
    uint8_t judg_value = cpu->A_R;

    if(judg_value == 0){
         cpu->PC_R = cpu->PC_R + 2;
        ESP_LOGI(TAG, "JNZ,A=0,PC:0x%04x", cpu->PC_R);
    }else{
          cpu->PC_R = jump_addr;
        ESP_LOGI(TAG, "JNZ,A=1,PC:0x%04x", cpu->PC_R);
    }
 }
void MCS_8051_INSTRUCTION_ORL_C_BIT(MCS_8051 *cpu) {//bit->位寻址区 可位寻址sfr区 sfr别名区
    uint8_t addressing_addr = Kernel_8051_ROM[cpu->PC_R + 1];
    cpu->PC_R = cpu->PC_R + 2;
    uint8_t cy_state = cpu->PSW_R & 0x80;
    bool cy_state_bool = 0;
    bool judg_bit = 0;

    if(cy_state == 0){
        cy_state_bool = 0;
    }else{
        cy_state_bool = 1;
    }

    uint8_t only_judg_bit;
    uint8_t RAM_addressing_BANK = 0;
    uint8_t RAM_addressing_BIT = 0;
    uint8_t SFR_opera_BANK = 0;
    uint8_t SFR_opera_BIT = 0;

    if (addressing_addr <= 0x7F){
        RAM_addressing_BANK = addressing_addr / 8 ;
        RAM_addressing_BANK = RAM_addressing_BANK + 0x20;
        RAM_addressing_BIT = addressing_addr % 8;

        only_judg_bit = Kernel_8051_L_RAM[RAM_addressing_BANK] & (0x01 << RAM_addressing_BIT);
        if (only_judg_bit == 0x00){
            judg_bit = 0;
        }else{
            judg_bit = 1;
        }
    }else{
        SFR_opera_BANK = addressing_addr  & 0xF8;
        SFR_opera_BIT = addressing_addr % 8;
        only_judg_bit = Kernel_8051_SFR[SFR_opera_BANK] & (0x01 << SFR_opera_BIT);
        if (only_judg_bit == 0x00){
            judg_bit = 0;
        }else{
            judg_bit = 1;
        }
    }
    if(judg_bit == 1){
        cpu->PSW_R = cpu->PSW_R | 0x80;   
    }else{
        cpu->PSW_R = cpu->PSW_R | 0x00;
    }
    ESP_LOGI(TAG,"ORL_C_BIT,PC:0x%04x,PSW_R:0x%02x",cpu->PC_R,cpu->PSW_R);
 }
void MCS_8051_INSTRUCTION_JMP_DPTR(MCS_8051 *cpu) { 
    uint8_t A_value = cpu->A_R;
    uint8_t DPL_value = Kernel_8051_SFR[3];
    uint8_t DPH_value = Kernel_8051_SFR[4];
    uint16_t jmp_addr = 0;

    jmp_addr = 0x0000 | DPH_value << 8;
    jmp_addr = jmp_addr | DPL_value;

    cpu->PC_R = jmp_addr;
    ESP_LOGI(TAG,"JMP_DPTR,WILL JUMP TO:0x%04x",cpu->PC_R);
}
void MCS_8051_INSTRUCTION_MOV_A_IMM(MCS_8051 *cpu) {
        uint8_t imm_num = Kernel_8051_ROM[cpu->PC_R + 1];

        cpu->A_R = imm_num;
        cpu->PC_R = cpu->PC_R + 2;

        ESP_LOGI(TAG,"MOV_A_IMM,PC:0x%04x,A_R:0x%02x:",cpu->PC_R,cpu->A_R);
 }
void MCS_8051_INSTRUCTION_MOV_DIR_IMM(MCS_8051 *cpu) { 
     uint8_t opera_addr = Kernel_8051_ROM[cpu->PC_R + 1];
     uint8_t opera_imm_num = Kernel_8051_ROM[cpu->PC_R + 2];
        cpu->PC_R = cpu->PC_R + 3;

        int opera_DIR_num = (int)opera_addr;
         if(opera_DIR_num <= 127){
            Kernel_8051_L_RAM[opera_DIR_num] = opera_imm_num;
            ESP_LOGI(TAG, "MOV_DIR_IMM,PC: 0x%04x, OPER_RAM_ADDR: 0x%02x,AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,opera_addr,Kernel_8051_L_RAM[opera_DIR_num]);
    } else if (opera_DIR_num > 127 && opera_DIR_num <= 255)
    {
        int opera_SFR_num = opera_DIR_num - 128;
            Kernel_8051_SFR[opera_SFR_num] = opera_imm_num;
            ESP_LOGI(TAG, "MOV_DIR_IMM,PC: 0x%04x, OPER_SFR_ADDR: 0x%02x,AFTER_OPERA_VALUE:0x%02x", cpu->PC_R,(uint8_t)opera_DIR_num,Kernel_8051_SFR[opera_SFR_num]); 
    }
}
void MCS_8051_INSTRUCTION_MOV_AT_R0_IMM(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t imm_num = Kernel_8051_ROM[cpu->PC_R + 1];

    uint8_t opera_dir = 0;
    cpu->PC_R = cpu->PC_R + 2;

    if(current_bank == 0){
        opera_dir = cpu->REG_R_0[0];
    }else if(current_bank == 1){
        opera_dir = cpu->REG_R_1[0];
    }else if (current_bank ==2){
        opera_dir = cpu->REG_R_2[0];
    }else if (current_bank ==3){
        opera_dir = cpu->REG_R_3[0];
    }

     if (opera_dir <= 0x7F) {
        Kernel_8051_L_RAM[opera_dir] = imm_num;
        ESP_LOGI(TAG,"MOV_AT_R0_IMM,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,Kernel_8051_L_RAM[opera_dir]);
    } 
    else {
        Kernel_8051_SFR[opera_dir - 0x80] = imm_num;
        ESP_LOGI(TAG,"MOV_AT_R0_IMM,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,Kernel_8051_SFR[opera_dir - 0x80]);
    }
 }
void MCS_8051_INSTRUCTION_MOV_AT_R1_IMM(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t imm_num = Kernel_8051_ROM[cpu->PC_R + 1];

    uint8_t opera_dir = 0;
    cpu->PC_R = cpu->PC_R + 2;

    if(current_bank == 0){
        opera_dir = cpu->REG_R_0[1];
    }else if(current_bank == 1){
        opera_dir = cpu->REG_R_1[1];
    }else if (current_bank ==2){
        opera_dir = cpu->REG_R_2[1];
    }else if (current_bank ==3){
        opera_dir = cpu->REG_R_3[1];
    }

     if (opera_dir <= 0x7F) {
        Kernel_8051_L_RAM[opera_dir] = imm_num;
        ESP_LOGI(TAG,"MOV_AT_R0_IMM,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,Kernel_8051_L_RAM[opera_dir]);
    } 
    else {
        Kernel_8051_SFR[opera_dir - 0x80] = imm_num;
        ESP_LOGI(TAG,"MOV_AT_R0_IMM,PC: 0x%04x,AFTER_OPERA_VALUE:0x%02x",cpu->PC_R,Kernel_8051_SFR[opera_dir - 0x80]);
    }
 }
void MCS_8051_INSTRUCTION_MOV_R0_IMM(MCS_8051 *cpu) { 
    uint8_t imm_num = Kernel_8051_ROM[cpu->PC_R + 1];
    
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    cpu->PC_R = cpu->PC_R + 2;
    
    uint8_t R0_value = 0;

    if(current_bank == 0){
        cpu->REG_R_0[0] = imm_num;
        R0_value = cpu->REG_R_0[0];
    }else if(current_bank == 1){
        cpu->REG_R_1[0] = imm_num;
        R0_value = cpu->REG_R_1[0]; 
    }else if (current_bank ==2){
        cpu->REG_R_2[0] = imm_num;
        R0_value = cpu->REG_R_2[0];
    }else if (current_bank ==3){
        cpu->REG_R_3[0] = imm_num;
        R0_value = cpu->REG_R_3[0]; 
    }
    
    ESP_LOGI(TAG,"MOV_R0_IMM,PC:0x%04x,OPERA_BANK:%01x,R0_VALUE:0x%02x",cpu->PC_R,current_bank,R0_value);
}
void MCS_8051_INSTRUCTION_MOV_R1_IMM(MCS_8051 *cpu) {
    uint8_t imm_num = Kernel_8051_ROM[cpu->PC_R + 1];
    
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    cpu->PC_R = cpu->PC_R + 2;
    
    uint8_t R0_value = 0;

    if(current_bank == 0){
        cpu->REG_R_0[1] = imm_num;
        R0_value = cpu->REG_R_0[1];
    }else if(current_bank == 1){
        cpu->REG_R_1[1] = imm_num;
        R0_value = cpu->REG_R_1[1]; 
    }else if (current_bank ==2){
        cpu->REG_R_2[1] = imm_num;
        R0_value = cpu->REG_R_2[1];
    }else if (current_bank ==3){
        cpu->REG_R_3[1] = imm_num;
        R0_value = cpu->REG_R_3[1]; 
    }
    
    ESP_LOGI(TAG,"MOV_R1_IMM,PC:0x%04x,OPERA_BANK:%01x,R0_VALUE:0x%02x",cpu->PC_R,current_bank,R0_value);
 }
void MCS_8051_INSTRUCTION_MOV_R2_IMM(MCS_8051 *cpu) {
    uint8_t imm_num = Kernel_8051_ROM[cpu->PC_R + 1];
    
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    cpu->PC_R = cpu->PC_R + 2;
    
    uint8_t R0_value = 0;

    if(current_bank == 0){
        cpu->REG_R_0[2] = imm_num;
        R0_value = cpu->REG_R_0[2];
    }else if(current_bank == 1){
        cpu->REG_R_1[2] = imm_num;
        R0_value = cpu->REG_R_1[2]; 
    }else if (current_bank ==2){
        cpu->REG_R_2[2] = imm_num;
        R0_value = cpu->REG_R_2[2];
    }else if (current_bank ==3){
        cpu->REG_R_3[2] = imm_num;
        R0_value = cpu->REG_R_3[2]; 
    }
    
    ESP_LOGI(TAG,"MOV_R2_IMM,PC:0x%04x,OPERA_BANK:%01x,R0_VALUE:0x%02x",cpu->PC_R,current_bank,R0_value);
 }
void MCS_8051_INSTRUCTION_MOV_R3_IMM(MCS_8051 *cpu) {
    uint8_t imm_num = Kernel_8051_ROM[cpu->PC_R + 1];
    
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    cpu->PC_R = cpu->PC_R + 2;
    
    uint8_t R0_value = 0;

    if(current_bank == 0){
        cpu->REG_R_0[3] = imm_num;
        R0_value = cpu->REG_R_0[3];
    }else if(current_bank == 1){
        cpu->REG_R_1[3] = imm_num;
        R0_value = cpu->REG_R_1[3]; 
    }else if (current_bank ==2){
        cpu->REG_R_2[3] = imm_num;
        R0_value = cpu->REG_R_2[3];
    }else if (current_bank ==3){
        cpu->REG_R_3[3] = imm_num;
        R0_value = cpu->REG_R_3[3]; 
    }
    
    ESP_LOGI(TAG,"MOV_R3_IMM,PC:0x%04x,OPERA_BANK:%01x,R0_VALUE:0x%02x",cpu->PC_R,current_bank,R0_value);
 }
void MCS_8051_INSTRUCTION_MOV_R4_IMM(MCS_8051 *cpu) {
    uint8_t imm_num = Kernel_8051_ROM[cpu->PC_R + 1];
    
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    cpu->PC_R = cpu->PC_R + 2;
    
    uint8_t R0_value = 0;

    if(current_bank == 0){
        cpu->REG_R_0[4] = imm_num;
        R0_value = cpu->REG_R_0[4];
    }else if(current_bank == 1){
        cpu->REG_R_1[4] = imm_num;
        R0_value = cpu->REG_R_1[4]; 
    }else if (current_bank ==2){
        cpu->REG_R_2[4] = imm_num;
        R0_value = cpu->REG_R_2[4];
    }else if (current_bank ==3){
        cpu->REG_R_3[4] = imm_num;
        R0_value = cpu->REG_R_3[4]; 
    }
    
    ESP_LOGI(TAG,"MOV_R4_IMM,PC:0x%04x,OPERA_BANK:%01x,R0_VALUE:0x%02x",cpu->PC_R,current_bank,R0_value);
 }
void MCS_8051_INSTRUCTION_MOV_R5_IMM(MCS_8051 *cpu) {
    uint8_t imm_num = Kernel_8051_ROM[cpu->PC_R + 1];
    
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    cpu->PC_R = cpu->PC_R + 2;
    
    uint8_t R0_value = 0;

    if(current_bank == 0){
        cpu->REG_R_0[5] = imm_num;
        R0_value = cpu->REG_R_0[5];
    }else if(current_bank == 1){
        cpu->REG_R_1[5] = imm_num;
        R0_value = cpu->REG_R_1[5]; 
    }else if (current_bank ==2){
        cpu->REG_R_2[5] = imm_num;
        R0_value = cpu->REG_R_2[5];
    }else if (current_bank ==3){
        cpu->REG_R_3[5] = imm_num;
        R0_value = cpu->REG_R_3[5]; 
    }
    
    ESP_LOGI(TAG,"MOV_R5_IMM,PC:0x%04x,OPERA_BANK:%01x,R0_VALUE:0x%02x",cpu->PC_R,current_bank,R0_value);
 }
void MCS_8051_INSTRUCTION_MOV_R6_IMM(MCS_8051 *cpu) {
    uint8_t imm_num = Kernel_8051_ROM[cpu->PC_R + 1];
    
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    cpu->PC_R = cpu->PC_R + 2;
    
    uint8_t R0_value = 0;

    if(current_bank == 0){
        cpu->REG_R_0[6] = imm_num;
        R0_value = cpu->REG_R_0[6];
    }else if(current_bank == 1){
        cpu->REG_R_1[6] = imm_num;
        R0_value = cpu->REG_R_1[6]; 
    }else if (current_bank ==2){
        cpu->REG_R_2[6] = imm_num;
        R0_value = cpu->REG_R_2[6];
    }else if (current_bank ==3){
        cpu->REG_R_3[6] = imm_num;
        R0_value = cpu->REG_R_3[6]; 
    }
    
    ESP_LOGI(TAG,"MOV_R6_IMM,PC:0x%04x,OPERA_BANK:%01x,R0_VALUE:0x%02x",cpu->PC_R,current_bank,R0_value);
 }
void MCS_8051_INSTRUCTION_MOV_R7_IMM(MCS_8051 *cpu) {
    uint8_t imm_num = Kernel_8051_ROM[cpu->PC_R + 1];
    
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    cpu->PC_R = cpu->PC_R + 2;
    
    uint8_t R0_value = 0;

    if(current_bank == 0){
        cpu->REG_R_0[7] = imm_num;
        R0_value = cpu->REG_R_0[7];
    }else if(current_bank == 1){
        cpu->REG_R_1[7] = imm_num;
        R0_value = cpu->REG_R_1[7]; 
    }else if (current_bank ==2){
        cpu->REG_R_2[7] = imm_num;
        R0_value = cpu->REG_R_2[7];
    }else if (current_bank ==3){
        cpu->REG_R_3[7] = imm_num;
        R0_value = cpu->REG_R_3[7]; 
    }
    
    ESP_LOGI(TAG,"MOV_R7_IMM,PC:0x%04x,OPERA_BANK:%01x,R0_VALUE:0x%02x",cpu->PC_R,current_bank,R0_value);
 }

// --- 0x80 - 0x8F ---
void MCS_8051_INSTRUCTION_SJMP(MCS_8051 *cpu) { 
    uint8_t offset = Kernel_8051_ROM[cpu->PC_R + 1];

    cpu->PC_R = cpu->PC_R + 2 + offset;

    ESP_LOGI(TAG,"SJMP,JUMP TO PC:0x%04x",cpu->PC_R);
}
void MCS_8051_INSTRUCTION_ANL_C_BIT(MCS_8051 *cpu) {
    uint8_t addressing_addr = Kernel_8051_ROM[cpu->PC_R + 1];
    cpu->PC_R = cpu->PC_R + 2;
    uint8_t cy_state = cpu->PSW_R & 0x80;
    bool cy_state_bool = 0;
    bool judg_bit = 0;

    if(cy_state == 0){
        cy_state_bool = 0;
    }else{
        cy_state_bool = 1;
    }

    uint8_t only_judg_bit;
    uint8_t RAM_addressing_BANK = 0;
    uint8_t RAM_addressing_BIT = 0;
    uint8_t SFR_opera_BANK = 0;
    uint8_t SFR_opera_BIT = 0;

    if (addressing_addr <= 0x7F){
        RAM_addressing_BANK = addressing_addr / 8 ;
        RAM_addressing_BANK = RAM_addressing_BANK + 0x20;
        RAM_addressing_BIT = addressing_addr % 8;

        only_judg_bit = Kernel_8051_L_RAM[RAM_addressing_BANK] & (0x01 << RAM_addressing_BIT);
        if (only_judg_bit == 0x00){
            judg_bit = 0;
        }else{
            judg_bit = 1;
        }
    }else{
        SFR_opera_BANK = addressing_addr  & 0xF8;
        SFR_opera_BIT = addressing_addr % 8;
        only_judg_bit = Kernel_8051_SFR[SFR_opera_BANK] & (0x01 << SFR_opera_BIT);
        if (only_judg_bit == 0x00){
            judg_bit = 0;
        }else{
            judg_bit = 1;
        }
    }
    if(judg_bit == 1){
        cpu->PSW_R = (uint8_t)cpu->PSW_R & 0x80;   
    }else{
        cpu->PSW_R = (uint8_t)cpu->PSW_R & 0x00;
    }
    ESP_LOGI(TAG,"ANL_C_BIT,PC:0x%04x,PSW_R:0x%02x",cpu->PC_R,cpu->PSW_R);
 }
void MCS_8051_INSTRUCTION_MOVC_A_PC(MCS_8051 *cpu) {
    uint8_t temp_num = cpu->A_R;
    uint8_t effective_addr = (uint8_t)cpu->PC_R + 1 + temp_num;

    cpu->A_R = Kernel_8051_ROM[effective_addr];
    cpu->PC_R = cpu->PC_R + 2;
    ESP_LOGI(TAG,"MOVC_A_PC,PC:0x%04x,A_VALUE;0x%02x",cpu->PC_R,cpu->A_R);
}
void MCS_8051_INSTRUCTION_DIV_AB(MCS_8051 *cpu) { 
    uint8_t divisor =  cpu->B_R;
    uint8_t dividend_num = cpu->A_R;

    uint8_t quotiuent = divisor / dividend_num;
    uint8_t remainder = divisor % dividend_num;

    cpu->A_R = quotiuent;
    cpu->B_R = remainder;

    if(cpu->B_R == 0){
        cpu->PSW_R = cpu->PSW_R & 0x7F;//CY->0
        cpu->PSW_R = cpu->PSW_R | 0x04;//OV->1
        cpu->PSW_R = cpu->PSW_R & 0xBF;//AC->0
    }else{
       cpu->PSW_R = cpu->PSW_R & 0x7F;//CY->0
       cpu->PSW_R = cpu->PSW_R & 0xFB;//OV->0
       cpu->PSW_R = cpu->PSW_R & 0xBF;//AC->0
    }

    int num_of_1_bits = 0;

    for(int j=0;j<8;j++){
        int temp_value = cpu->A_R & (0x01 << j);
        if (temp_value != 0){
            num_of_1_bits++;
        }
    }


    if(num_of_1_bits & 1){
        cpu->PSW_R = cpu->PSW_R | 0x01;
    }else
    {
        cpu->PSW_R = cpu->PSW_R & 0xFE;
    }

      cpu->PC_R = cpu->PC_R + 1;

      ESP_LOGI(TAG,"DIV_AB,NEXT PC:0x%04x,A_VALUE:0x%02x,B_VALUE:0x%02x",cpu->PC_R,cpu->A_R,cpu->B_R);
}
void MCS_8051_INSTRUCTION_MOV_DIR_DIR(MCS_8051 *cpu) {//dir2->dir1
    uint8_t DIR1_addr = Kernel_8051_ROM[cpu->PC_R + 1];
    uint8_t DIR2_addr = Kernel_8051_ROM[cpu->PC_R + 2];

    uint8_t data_value = 0;

    cpu->PC_R = cpu->PC_R + 2;

    int opera_DIR1_num = (int)DIR1_addr;
    int opera_DIR2_num = (int)DIR2_addr;

if(opera_DIR2_num <= 127)
{
        if(opera_DIR1_num <= 127){
            Kernel_8051_L_RAM[opera_DIR1_num] = Kernel_8051_L_RAM[opera_DIR2_num]; 
            ESP_LOGI(TAG,"MOV_DIR_DIR,NEXT PC:0x%04x,DIR1_RAM_VALUE:0x%02x",cpu->PC_R,Kernel_8051_L_RAM[opera_DIR1_num]);      
            } else if (opera_DIR1_num > 127 && opera_DIR1_num <= 255){
            int opera_SFR_num_0 = opera_DIR1_num - 128;
        Kernel_8051_SFR[opera_SFR_num_0] = Kernel_8051_L_RAM[opera_DIR2_num];
        ESP_LOGI(TAG,"MOV_DIR_DIR,NEXT PC:0x%04x,DIR1_SFR_VALUE:0x%02x",cpu->PC_R,Kernel_8051_SFR[opera_SFR_num_0]);
        
    }
} else if (opera_DIR2_num > 127 && opera_DIR2_num <= 255) {
    int opera_SFR_num_1 = opera_DIR2_num - 128;
        if(opera_DIR1_num <= 127){
            Kernel_8051_L_RAM[opera_DIR1_num] = Kernel_8051_SFR[opera_SFR_num_1];
            ESP_LOGI(TAG,"MOV_DIR_DIR,NEXT PC:0x%04x,DIR1_RAM_VALUE:0x%02x",cpu->PC_R,Kernel_8051_L_RAM[opera_DIR1_num]);
            } else if (opera_DIR1_num > 127 && opera_DIR1_num <= 255){
        int opera_SFR_num_2 = opera_DIR1_num - 128;
           Kernel_8051_SFR[opera_SFR_num_2] = Kernel_8051_SFR[opera_SFR_num_1];
           ESP_LOGI(TAG,"MOV_DIR_DIR,NEXT PC:0x%04x,DIR1_SFR_VALUE:0x%02x",cpu->PC_R,Kernel_8051_SFR[opera_SFR_num_2]);
        }
        
    }
 }
void MCS_8051_INSTRUCTION_MOV_DIR_AT_R0(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t opera_addr = cpu->PC_R + 1;

    uint8_t mapped_addr = 0;

    cpu->PC_R = cpu->PC_R + 2;

    if(current_bank == 0){
        mapped_addr = cpu->REG_R_0[0];
    }else if(current_bank == 1){
        mapped_addr = cpu->REG_R_1[0];
    }else if (current_bank ==2){
        mapped_addr = cpu->REG_R_2[0];
    }else if (current_bank ==3){
        mapped_addr = cpu->REG_R_3[0];
    } 
    
    uint8_t temp_value = 0;
    if (mapped_addr <= 0x7F) {
        temp_value = Kernel_8051_ROM[mapped_addr];
    }else{
        uint8_t mapped_SFR_addr = mapped_addr - 128;
        temp_value = Kernel_8051_SFR[mapped_SFR_addr];
    }

    if (opera_addr <= 0x7F) {
        Kernel_8051_L_RAM[opera_addr] = temp_value;
        ESP_LOGI(TAG,"MOV_DIR_AT_R0,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_L_RAM[opera_addr]);
    }else{
        uint8_t opera_SFR_addr = opera_addr - 128;
        Kernel_8051_SFR[opera_SFR_addr] = temp_value;
        ESP_LOGI(TAG,"MOV_DIR_AT_R0,NEXT PC:0x%04x,OPERA SFR VALUE:0x%02x",cpu->PC_R,Kernel_8051_SFR[opera_SFR_addr]);
    }
 }
void MCS_8051_INSTRUCTION_MOV_DIR_AT_R1(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
    uint8_t opera_addr = cpu->PC_R + 1;

    uint8_t mapped_addr = 0;

    cpu->PC_R = cpu->PC_R + 2;

    if(current_bank == 0){
        mapped_addr = cpu->REG_R_0[1];
    }else if(current_bank == 1){
        mapped_addr = cpu->REG_R_1[1];
    }else if (current_bank ==2){
        mapped_addr = cpu->REG_R_2[1];
    }else if (current_bank ==3){
        mapped_addr = cpu->REG_R_3[1];
    } 
    
    uint8_t temp_value = 0;
    if (mapped_addr <= 0x7F) {
        temp_value = Kernel_8051_ROM[mapped_addr];
    }else{
        uint8_t mapped_SFR_addr = mapped_addr - 128;
        temp_value = Kernel_8051_SFR[mapped_SFR_addr];
    }

    if (opera_addr <= 0x7F) {
        Kernel_8051_L_RAM[opera_addr] = temp_value;
        ESP_LOGI(TAG,"MOV_DIR_AT_R1,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_L_RAM[opera_addr]);
    }else{
        uint8_t opera_SFR_addr = opera_addr - 128;
        Kernel_8051_SFR[opera_SFR_addr] = temp_value;
        ESP_LOGI(TAG,"MOV_DIR_AT_R1,NEXT PC:0x%04x,OPERA SFR VALUE:0x%02x",cpu->PC_R,Kernel_8051_SFR[opera_SFR_addr]);
    }
 }
void MCS_8051_INSTRUCTION_MOV_DIR_R0(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
 
    uint8_t temp_value = 0;

    uint8_t opera_addr = Kernel_8051_ROM[cpu->PC_R + 1];

    cpu->PC_R = cpu->PC_R + 2;

    if(current_bank == 0){
        temp_value = cpu->REG_R_0[0];
    }else if(current_bank == 1){
        temp_value = cpu->REG_R_1[0];
    }else if (current_bank ==2){
        temp_value = cpu->REG_R_2[0];
    }else if (current_bank ==3){
        temp_value = cpu->REG_R_3[0];
    } 
    
    if (opera_addr <= 0x7F) {
        Kernel_8051_L_RAM[opera_addr] = temp_value; 
        ESP_LOGI(TAG,"MOV_DIR_R0,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_L_RAM[opera_addr]);
    }else{
        uint8_t opera_SFR_addr = opera_addr - 128;
        Kernel_8051_SFR[opera_SFR_addr] = temp_value;
        ESP_LOGI(TAG,"MOV_DIR_R0,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_SFR[opera_SFR_addr]);
    }

 }
void MCS_8051_INSTRUCTION_MOV_DIR_R1(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
 
    uint8_t temp_value = 0;

    uint8_t opera_addr = Kernel_8051_ROM[cpu->PC_R + 1];

    cpu->PC_R = cpu->PC_R + 2;

    if(current_bank == 0){
        temp_value = cpu->REG_R_0[1];
    }else if(current_bank == 1){
        temp_value = cpu->REG_R_1[1];
    }else if (current_bank ==2){
        temp_value = cpu->REG_R_2[1];
    }else if (current_bank ==3){
        temp_value = cpu->REG_R_3[1];
    } 
    
    if (opera_addr <= 0x7F) {
        Kernel_8051_L_RAM[opera_addr] = temp_value; 
        ESP_LOGI(TAG,"MOV_DIR_R1,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_L_RAM[opera_addr]);
    }else{
        uint8_t opera_SFR_addr = opera_addr - 128;
        Kernel_8051_SFR[opera_SFR_addr] = temp_value;
        ESP_LOGI(TAG,"MOV_DIR_R1,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_SFR[opera_SFR_addr]);
    }
 }
void MCS_8051_INSTRUCTION_MOV_DIR_R2(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
 
    uint8_t temp_value = 0;

    uint8_t opera_addr = Kernel_8051_ROM[cpu->PC_R + 1];

    cpu->PC_R = cpu->PC_R + 2;

    if(current_bank == 0){
        temp_value = cpu->REG_R_0[2];
    }else if(current_bank == 1){
        temp_value = cpu->REG_R_1[2];
    }else if (current_bank ==2){
        temp_value = cpu->REG_R_2[2];
    }else if (current_bank ==3){
        temp_value = cpu->REG_R_3[2];
    } 
    
    if (opera_addr <= 0x7F) {
        Kernel_8051_L_RAM[opera_addr] = temp_value; 
        ESP_LOGI(TAG,"MOV_DIR_R2,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_L_RAM[opera_addr]);
    }else{
        uint8_t opera_SFR_addr = opera_addr - 128;
        Kernel_8051_SFR[opera_SFR_addr] = temp_value;
        ESP_LOGI(TAG,"MOV_DIR_R2,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_SFR[opera_SFR_addr]);
    }
 }
void MCS_8051_INSTRUCTION_MOV_DIR_R3(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
 
    uint8_t temp_value = 0;

    uint8_t opera_addr = Kernel_8051_ROM[cpu->PC_R + 1];

    cpu->PC_R = cpu->PC_R + 2;

    if(current_bank == 0){
        temp_value = cpu->REG_R_0[3];
    }else if(current_bank == 1){
        temp_value = cpu->REG_R_1[3];
    }else if (current_bank ==2){
        temp_value = cpu->REG_R_2[3];
    }else if (current_bank ==3){
        temp_value = cpu->REG_R_3[3];
    } 
    
    if (opera_addr <= 0x7F) {
        Kernel_8051_L_RAM[opera_addr] = temp_value; 
        ESP_LOGI(TAG,"MOV_DIR_R3,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_L_RAM[opera_addr]);
    }else{
        uint8_t opera_SFR_addr = opera_addr - 128;
        Kernel_8051_SFR[opera_SFR_addr] = temp_value;
        ESP_LOGI(TAG,"MOV_DIR_R3,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_SFR[opera_SFR_addr]);
    }
 }
void MCS_8051_INSTRUCTION_MOV_DIR_R4(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
 
    uint8_t temp_value = 0;

    uint8_t opera_addr = Kernel_8051_ROM[cpu->PC_R + 1];

    cpu->PC_R = cpu->PC_R + 2;

    if(current_bank == 0){
        temp_value = cpu->REG_R_0[4];
    }else if(current_bank == 1){
        temp_value = cpu->REG_R_1[4];
    }else if (current_bank ==2){
        temp_value = cpu->REG_R_2[4];
    }else if (current_bank ==3){
        temp_value = cpu->REG_R_3[4];
    } 
    
    if (opera_addr <= 0x7F) {
        Kernel_8051_L_RAM[opera_addr] = temp_value; 
        ESP_LOGI(TAG,"MOV_DIR_R4,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_L_RAM[opera_addr]);
    }else{
        uint8_t opera_SFR_addr = opera_addr - 128;
        Kernel_8051_SFR[opera_SFR_addr] = temp_value;
        ESP_LOGI(TAG,"MOV_DIR_R4,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_SFR[opera_SFR_addr]);
    }
 }
void MCS_8051_INSTRUCTION_MOV_DIR_R5(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
 
    uint8_t temp_value = 0;

    uint8_t opera_addr = Kernel_8051_ROM[cpu->PC_R + 1];

    cpu->PC_R = cpu->PC_R + 2;

    if(current_bank == 0){
        temp_value = cpu->REG_R_0[5];
    }else if(current_bank == 1){
        temp_value = cpu->REG_R_1[5];
    }else if (current_bank ==2){
        temp_value = cpu->REG_R_2[5];
    }else if (current_bank ==3){
        temp_value = cpu->REG_R_3[5];
    } 
    
    if (opera_addr <= 0x7F) {
        Kernel_8051_L_RAM[opera_addr] = temp_value; 
        ESP_LOGI(TAG,"MOV_DIR_R5,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_L_RAM[opera_addr]);
    }else{
        uint8_t opera_SFR_addr = opera_addr - 128;
        Kernel_8051_SFR[opera_SFR_addr] = temp_value;
        ESP_LOGI(TAG,"MOV_DIR_R5,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_SFR[opera_SFR_addr]);
    }
 }
void MCS_8051_INSTRUCTION_MOV_DIR_R6(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
 
    uint8_t temp_value = 0;

    uint8_t opera_addr = Kernel_8051_ROM[cpu->PC_R + 1];

    cpu->PC_R = cpu->PC_R + 2;

    if(current_bank == 0){
        temp_value = cpu->REG_R_0[6];
    }else if(current_bank == 1){
        temp_value = cpu->REG_R_1[6];
    }else if (current_bank ==2){
        temp_value = cpu->REG_R_2[6];
    }else if (current_bank ==3){
        temp_value = cpu->REG_R_3[6];
    } 
    
    if (opera_addr <= 0x7F) {
        Kernel_8051_L_RAM[opera_addr] = temp_value; 
        ESP_LOGI(TAG,"MOV_DIR_R6,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_L_RAM[opera_addr]);
    }else{
        uint8_t opera_SFR_addr = opera_addr - 128;
        Kernel_8051_SFR[opera_SFR_addr] = temp_value;
        ESP_LOGI(TAG,"MOV_DIR_R6,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_SFR[opera_SFR_addr]);
    }
 }
void MCS_8051_INSTRUCTION_MOV_DIR_R7(MCS_8051 *cpu) {
    uint8_t current_bank = (cpu->PSW_R >> 3) & 0x03;
 
    uint8_t temp_value = 0;

    uint8_t opera_addr = Kernel_8051_ROM[cpu->PC_R + 1];

    cpu->PC_R = cpu->PC_R + 2;

    if(current_bank == 0){
        temp_value = cpu->REG_R_0[7];
    }else if(current_bank == 1){
        temp_value = cpu->REG_R_1[7];
    }else if (current_bank ==2){
        temp_value = cpu->REG_R_2[7];
    }else if (current_bank ==3){
        temp_value = cpu->REG_R_3[7];
    } 
    
    if (opera_addr <= 0x7F) {
        Kernel_8051_L_RAM[opera_addr] = temp_value; 
        ESP_LOGI(TAG,"MOV_DIR_R7,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_L_RAM[opera_addr]);
    }else{
        uint8_t opera_SFR_addr = opera_addr - 128;
        Kernel_8051_SFR[opera_SFR_addr] = temp_value;
        ESP_LOGI(TAG,"MOV_DIR_R7,NEXT PC:0x%04x,OPERA RAM VALUE:0x%02x",cpu->PC_R,Kernel_8051_SFR[opera_SFR_addr]);
    }
 }
//0x90-0x9F
void MCS_8051_INSTRUCTION_MOV_DPTR_IMM(MCS_8051 *cpu) {
    uint8_t L_imm_num = Kernel_8051_ROM[cpu->PC_R + 1];
    uint8_t H_imm_num = Kernel_8051_ROM[cpu->PC_R + 2];

    Kernel_8051_SFR[3] = L_imm_num;
    Kernel_8051_SFR[4] = H_imm_num;

    ESP_LOGI(TAG,"MOV_DPTR_IMM,NEXT PC:0x%04x,DPL:0x%02x,DPH:0x%02x",cpu->PC_R,Kernel_8051_ROM[3],Kernel_8051_SFR[4]);
 }
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
