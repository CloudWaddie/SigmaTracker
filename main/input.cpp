#include "input.hpp"
#include "core.hpp"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <atomic>
#include <cstring>
#if CONFIG_STENO_USB_HOST
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"
#else
#include "driver/usb_serial_jtag.h"
#endif
namespace input {
namespace {
steno::Gemini parser;
constexpr gpio_num_t A=GPIO_NUM_4,B=GPIO_NUM_5,SW=GPIO_NUM_0,SIDE=GPIO_NUM_6;
int previous=0,steps=0;
struct Button {
    bool raw=false,stable=false,held=false; int64_t changed=0,pressed=0;
    Action poll(gpio_num_t pin, bool longpress, Action short_action) {
        int64_t now=esp_timer_get_time()/1000;
        bool r=gpio_get_level(pin)==0;
        if(r!=raw){raw=r;changed=now;}
        if(raw!=stable&&now-changed>=25){
            stable=raw;
            if(stable){pressed=now;held=false;}
            else if(!held) return short_action;
        }
        if(longpress&&stable&&!held&&now-pressed>=650){held=true;return Action::Back;}
        return Action::None;
    }
} knob,side;
#if CONFIG_STENO_USB_HOST
QueueHandle_t bytes=nullptr;
std::atomic<bool> online{false}, disconnected{false}, failed{false}, reset_pending{false};
void daemon(void*) {
    while(true){
        uint32_t flags=0;
        usb_host_lib_handle_events(portMAX_DELAY,&flags);
        if(flags&USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
}
bool data_cb(const uint8_t *data,size_t len,void*) {
    for(size_t i=0;i<len;i++){
        if(xQueueSend(bytes,data+i,0)!=pdTRUE){reset_pending=true;break;}
    }
    return true;
}
void event_cb(const cdc_acm_host_dev_event_data_t *event,void*) {
    if(event->type==CDC_ACM_HOST_DEVICE_DISCONNECTED){
        online=false;disconnected=true;reset_pending=true;
    }
}
void host_task(void*) {
    usb_host_config_t h{};h.skip_phy_setup=false;h.intr_flags=0;
    if(usb_host_install(&h)!=ESP_OK){failed=true;vTaskDelete(nullptr);return;}
    if(xTaskCreate(daemon,"usb_events",4096,nullptr,10,nullptr)!=pdPASS){failed=true;vTaskDelete(nullptr);return;}
    if(cdc_acm_host_install(nullptr)!=ESP_OK){failed=true;vTaskDelete(nullptr);return;}
    const cdc_acm_host_device_config_t cfg={
        .connection_timeout_ms=600,
        .out_buffer_size=64,
        .in_buffer_size=256,
        .event_cb=event_cb,
        .data_cb=data_cb,
        .user_arg=nullptr,
    };
    while(true){
        cdc_acm_dev_hdl_t dev=nullptr;
        disconnected=false;
        // Javelin's composite USB device: CDC control interface 3
        // (confirmed on the Polyglot); the data interface is 4.
        // Retain the original stock Polyglot VID/PID fallback.
        if(cdc_acm_host_open(0x9000,0x400D,3,&cfg,&dev)!=ESP_OK) dev=nullptr;
        for(uint8_t i=0;i<6&&!dev;i++){
            if(cdc_acm_host_open(0x9000,0x0003,i,&cfg,&dev)!=ESP_OK) dev=nullptr;
        }
        if(!dev){vTaskDelay(pdMS_TO_TICKS(500));continue;}
        cdc_acm_line_coding_t coding{};
        coding.dwDTERate=9600;coding.bDataBits=8;
        cdc_acm_host_line_coding_set(dev,&coding);
        cdc_acm_host_set_control_line_state(dev,true,false);
        reset_pending=true;online=!disconnected;
        while(!disconnected) vTaskDelay(pdMS_TO_TICKS(50));
        cdc_acm_host_close(dev);
    }
}
#else
bool running=false;
int64_t last_bytes=0;
#endif
}
bool begin(){
#if CONFIG_STENO_USB_HOST
    bytes=xQueueCreate(512,sizeof(uint8_t));
    if(!bytes) return false;
    return xTaskCreate(host_task,"steno_host",6144,nullptr,5,nullptr)==pdPASS;
#else
    usb_serial_jtag_driver_config_t c{};c.tx_buffer_size=256;c.rx_buffer_size=1024;
    running=usb_serial_jtag_driver_install(&c)==ESP_OK;
    return running;
#endif
}
std::string receive(){
    uint8_t b;
#if CONFIG_STENO_USB_HOST
    if(!bytes)return {};
    if(reset_pending.exchange(false)){parser.reset();xQueueReset(bytes);}
    while(xQueueReceive(bytes,&b,0)==pdTRUE){
#else
    if(!running)return {};
    while(usb_serial_jtag_read_bytes(&b,1,0)>0){
        auto now=esp_timer_get_time();
        if(last_bytes && now-last_bytes>500000)parser.reset();
        last_bytes=now;
#endif
        auto s=parser.feed(b);if(!s.empty())return s;
    }
    return {};
}
const char *status(){
#if CONFIG_STENO_USB_HOST
    return failed?"HOST ERROR":online?"KEYBOARD READY":"HOST WAITING";
#else
    return !running?"SERIAL ERROR":last_bytes?"SERIAL INPUT":"USB SERIAL READY";
#endif
}
void controls_begin(){
    gpio_config_t cfg{};cfg.pin_bit_mask=(1ULL<<A)|(1ULL<<B)|(1ULL<<SW)|(1ULL<<SIDE);
    cfg.mode=GPIO_MODE_INPUT;cfg.pull_up_en=GPIO_PULLUP_ENABLE;gpio_config(&cfg);
    previous=(gpio_get_level(A)<<1)|gpio_get_level(B);
}
Action poll_controls(){
    int cur=(gpio_get_level(A)<<1)|gpio_get_level(B);
    if(cur!=previous){
        static constexpr int delta[]={0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
        if((cur^previous)==3)steps=0;
        else steps+=delta[(previous<<2)|cur];
        previous=cur;
        if(steps>=4){steps=0;return Action::Next;}
        if(steps<=-4){steps=0;return Action::Previous;}
    }
    auto a=knob.poll(SW,true,Action::Select);
    if(a!=Action::None)return a;
    return side.poll(SIDE,false,Action::Side);
}
}
