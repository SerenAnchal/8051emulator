#include "mcs_code_instruction.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c.h"


#define I2C_MASTER_SCL_IO       GPIO_NUM_6   
#define I2C_MASTER_SDA_IO       GPIO_NUM_5   
#define I2C_MASTER_NUM          0
#define I2C_MASTER_FREQ_HZ      400000
#define EEPROM_ADDR             0x50         
#define EEPROM_SIZE             32768        

static const char *TAG = "THE SIMIER DEMO";

MCS_8051 REAL_CPU;

MCS_8051_INSTRUCTION INSTRUCTION_TABLE[256] = {0};
uint8_t Kernel_8051_RAM[EEPROM_SIZE]; 

void DEFINE_CODE_ASM(void){
    INSTRUCTION_TABLE[0x00] = (MCS_8051_INSTRUCTION){"NOP",1,1,MCS_8051_INSTRUCTION_NOP};
}


static void i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, I2C_MODE_MASTER, 0, 0, 0);
}

esp_err_t kernal_read_all_to_ram(void) {
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (EEPROM_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x00, true); 
    i2c_master_write_byte(cmd, 0x00, true); 
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) return ret;

    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (EEPROM_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, Kernel_8051_RAM, EEPROM_SIZE - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd,Kernel_8051_RAM + (EEPROM_SIZE - 1), I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 2000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

void app_main(void) {
    ESP_LOGI(TAG, " INIT I2C...");
    i2c_master_init();
    ESP_LOGI(TAG, "BEGAN MOVE DATA FROM 24C256 TO RAM");
    
    esp_err_t ret = kernal_read_all_to_ram();

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "MOVE 8051 VIRTUAL RAM -ok");
        

        ESP_LOG_BUFFER_HEXDUMP("RAM_HEAD_KERNAL_DATA", Kernel_8051_RAM, 256, ESP_LOG_INFO);
    } else {
        ESP_LOGE(TAG, "big ERROR!!!", ret);
    }

    DEFINE_CODE_ASM();
    BOOT_REAL_CPU_ENGINE(&REAL_CPU);

    ESP_LOGI(TAG,"END------------------------>");
    while(1)
    {
        vTaskDelay(1000);
    }

}