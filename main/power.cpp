#include "power.hpp"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <cstdint>

namespace power {
namespace {
i2c_master_dev_handle_t device=nullptr;
std::atomic<bool> ok{false};
bool read(uint8_t reg,uint8_t &value) {
    return i2c_master_transmit_receive(device,&reg,1,&value,1,100)==ESP_OK;
}
bool write(uint8_t reg,uint8_t value) {
    uint8_t pair[]={reg,value};
    return i2c_master_transmit(device,pair,2,100)==ESP_OK;
}
// Respect the board manufacturer's 600 mA charge ceiling. Never enable OTG,
// charging, or modify voltage/current protection or battery cutoff settings.
bool refresh(){
    uint8_t id,mode,current;
    if(!read(0x14,id)||(id&0x38)!=0||(id&0x03)!=2) return false;
    if(!read(0x03,mode)||!read(0x04,current))return false;
    if(!write(0x03,(mode&~0x20)|0x40))return false; // OTG off, service watchdog
    if((current&0x7f)>9 && !write(0x04,(current&0x80)|9))return false; // <=576 mA
    if(!read(0x03,mode)||!read(0x04,current))return false;
    return !(mode&0x20)&&(current&0x7f)<=9;
}
void maintenance(void*){
    for(;;){
        vTaskDelay(pdMS_TO_TICKS(5000));
        ok=refresh();
        if(!ok) ESP_LOGE("power","PMU verification failed; unplug USB and stop testing");
    }
}
}
bool begin(){
    i2c_master_bus_config_t bus{};
    bus.i2c_port=I2C_NUM_0;bus.sda_io_num=GPIO_NUM_8;bus.scl_io_num=GPIO_NUM_18;
    bus.clk_source=I2C_CLK_SRC_DEFAULT;bus.glitch_ignore_cnt=7;bus.flags.enable_internal_pullup=true;
    i2c_master_bus_handle_t handle=nullptr;
    if(i2c_new_master_bus(&bus,&handle)!=ESP_OK)return false;
    i2c_device_config_t cfg{};
    cfg.dev_addr_length=I2C_ADDR_BIT_LEN_7;cfg.device_address=0x6b;cfg.scl_speed_hz=100000;
    if(i2c_master_bus_add_device(handle,&cfg,&device)!=ESP_OK)return false;
    ok=refresh();
    if(!ok)return false;
    if(xTaskCreate(maintenance,"power_check",3072,nullptr,6,nullptr)!=pdPASS){ok=false;return false;}
    return true;
}
bool healthy(){return ok;}
}
