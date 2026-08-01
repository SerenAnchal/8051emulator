#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mcs_code_instruction.h"

extern uint8_t Kernel_8051_RAM[32768]; 
extern MCS_8051_INSTRUCTION INSTRUCTION_TABLE[256];

static const char *TAG = "THE SIMIER DEMO";


void MCS_8051_INSTRUCTION_NOP(MCS_8051 *REAL_CPU){

};

void BOOT_REAL_CPU_ENGINE(MCS_8051 *REAL_CPU){
    REAL_CPU->PC_R = 0;
    REAL_CPU->SP_R = 0;
    memset(REAL_CPU->REG_R,0,sizeof(REAL_CPU->REG_R));
    
    ESP_LOGI(TAG,"8051 ENGINE BOOT & RUN ->");

    while (1)
    {
        uint8_t FetchCode = Kernel_8051_RAM[REAL_CPU->PC_R];
        MCS_8051_INSTRUCTION DecodeCode = INSTRUCTION_TABLE[FetchCode];
        if(DecodeCode.execute == NULL){
            ESP_LOGI(TAG,"WARRING! UNDEFIEN CODE! BREAK NOW");
            break;
        }
        DecodeCode.execute(REAL_CPU);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
}