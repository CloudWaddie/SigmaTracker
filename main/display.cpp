#include "display.hpp"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstring>
#include <cctype>

namespace {
constexpr gpio_num_t DC=GPIO_NUM_16, BL=GPIO_NUM_21, CS=GPIO_NUM_41;
constexpr unsigned STRIPE=8;
const uint8_t letters[26][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,27,17},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};
const uint8_t nums[10][7]={
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14}
};
uint8_t glyph(char c,int row) {
    static const uint8_t lower[26][7]={
      {0,0,14,1,15,17,15},{16,16,30,17,17,17,30},{0,0,14,16,16,17,14},
      {1,1,15,17,17,17,15},{0,0,14,17,31,16,14},{6,9,8,28,8,8,8},
      {0,0,15,17,15,1,14},{16,16,22,25,17,17,17},{4,0,12,4,4,4,14},
      {2,0,6,2,2,18,12},{16,16,18,20,24,20,18},{12,4,4,4,4,4,14},
      {0,0,26,21,21,21,21},{0,0,30,17,17,17,17},{0,0,14,17,17,17,14},
      {0,0,30,17,30,16,16},{0,0,15,17,15,1,1},{0,0,22,25,16,16,16},
      {0,0,15,16,14,1,30},{8,8,28,8,8,9,6},{0,0,17,17,17,19,13},
      {0,0,17,17,17,10,4},{0,0,17,17,21,21,10},{0,0,17,10,4,10,17},
      {0,0,17,17,15,1,14},{0,0,31,2,4,8,31}
    };
    if(c>='a'&&c<='z') return lower[c-'a'][row];
    if(c>='A'&&c<='Z') return letters[c-'A'][row];
    if(c>='0'&&c<='9') return nums[c-'0'][row];
    switch(c){
    case '-': return row==3?14:0;
    case '_': return row==6?31:0;
    case ':': return row==2||row==5?4:0;
    case '.': return row==6?4:0;
    case ',': return row==5?4:row==6?8:0;
    case '/': return row==0?1:row==1?2:row==2||row==3?4:row==4?8:row==5?16:0;
    case '#': { static const uint8_t g[]={10,10,31,10,31,10,10};return g[row]; }
    case '*': { static const uint8_t g[]={0,21,14,31,14,21,0};return g[row]; }
    case '%': { static const uint8_t g[]={17,2,4,4,8,16,17};return g[row]; }
    case '+': return row==3?31:row>=1&&row<=5?4:0;
    case '[': return row==0||row==6?14:8;
    case ']': return row==0||row==6?14:2;
    case '!': return row<5||row==6?4:0;
    case '?': { static const uint8_t g[]={14,17,1,2,4,0,4};return g[row]; }
    case '\'': return row<2?4:0;
    case '>': { static const uint8_t g[]={8,4,2,1,2,4,8};return g[row]; }
    case ' ': return 0;
    default: return 0;
    }
}
}
bool Display::send(bool data,const void *buf,unsigned len){
    if(!len) return true;
    gpio_set_level(DC,data);
    spi_transaction_t t{}; t.length=len*8;
    if(len<=4){ t.flags=SPI_TRANS_USE_TXDATA; std::memcpy(t.tx_data,buf,len); }
    else t.tx_buffer=buf;
    return spi_device_polling_transmit(static_cast<spi_device_handle_t>(spi_),&t)==ESP_OK;
}
bool Display::command(uint8_t cmd,const uint8_t *data,unsigned n){
    gpio_set_level(CS,0);
    bool ok=send(false,&cmd,1)&&(!n||send(true,data,n));
    gpio_set_level(CS,1);
    return ok;
}
bool Display::init(){
    pixels_=static_cast<uint16_t*>(heap_caps_malloc(W*H*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!pixels_) pixels_=static_cast<uint16_t*>(heap_caps_malloc(W*H*2,MALLOC_CAP_8BIT));
    dma_=static_cast<uint8_t*>(heap_caps_malloc(W*STRIPE*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));
    if(!pixels_||!dma_) return false;
    // De-select the other devices on the shared SPI bus.
    for(auto p:{GPIO_NUM_12,GPIO_NUM_13,GPIO_NUM_41,DC,BL}){
        gpio_reset_pin(p); gpio_set_direction(p,GPIO_MODE_OUTPUT); gpio_set_level(p,p==BL?0:1);
    }
    spi_bus_config_t bus{}; bus.mosi_io_num=9;bus.miso_io_num=-1;bus.sclk_io_num=11;
    bus.quadwp_io_num=-1;bus.quadhd_io_num=-1;bus.data4_io_num=-1;bus.data5_io_num=-1;
    bus.data6_io_num=-1;bus.data7_io_num=-1;bus.max_transfer_sz=W*STRIPE*2;
    if(spi_bus_initialize(SPI3_HOST,&bus,SPI_DMA_CH_AUTO)!=ESP_OK) return false;
    spi_device_interface_config_t dev{};
    dev.clock_speed_hz=10000000;dev.mode=0;dev.spics_io_num=-1;dev.queue_size=1;
    spi_device_handle_t handle=nullptr;
    if(spi_bus_add_device(SPI3_HOST,&dev,&handle)!=ESP_OK) return false;
    spi_=handle;
    if(!command(0x01)) return false;
    vTaskDelay(pdMS_TO_TICKS(150));
    if(!command(0x11)) return false;
    vTaskDelay(pdMS_TO_TICKS(120));
    // ST7789 register settings from the T-Embed CC1101 panel configuration.
    // Use the vendor's non-INIT_SEQUENCE_3 panel parameters, then rotate.
    // Keep CS asserted across each command and its parameters.
    struct Reg { uint8_t addr, len, data[14]; };
    static constexpr Reg init[]={
        {0x13,0,{}}, {0x36,1,{0xA0}}, {0xB6,2,{0x0A,0x82}},
        {0xB0,2,{0x00,0xE0}}, {0x3A,1,{0x55}},
        {0xB2,5,{0x0C,0x0C,0x00,0x33,0x33}},
        {0xB7,1,{0x35}}, {0xBB,1,{0x28}}, {0xC0,1,{0x0C}},
        {0xC2,2,{0x01,0xFF}}, {0xC3,1,{0x10}}, {0xC4,1,{0x20}},
        {0xC6,1,{0x0F}}, {0xD0,2,{0xA4,0xA1}},
        {0xE0,14,{0xD0,0x00,0x02,0x07,0x0A,0x28,0x32,0x44,0x42,0x06,0x0E,0x12,0x14,0x17}},
        {0xE1,14,{0xD0,0x00,0x02,0x07,0x0A,0x28,0x31,0x54,0x47,0x0E,0x1C,0x17,0x1B,0x1E}},
        {0x21,0,{}},
    };
    for(const auto &r:init){
        if(!command(r.addr,r.data,r.len)) return false;
        if(r.addr==0x3A) vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(120));
    if(!command(0x29))return false;
    vTaskDelay(pdMS_TO_TICKS(120));
    clear(0); if(!show()) return false;
    gpio_set_level(BL,1);
    return true;
}
void Display::rect(int x,int y,int w,int h,uint16_t c){
    if(!pixels_||w<=0||h<=0) return;
    int x1=std::clamp(x+w,0,W),y1=std::clamp(y+h,0,H);
    x=std::clamp(x,0,W);y=std::clamp(y,0,H);
    if(x>=x1||y>=y1) return;
    for(int j=y;j<y1;j++) std::fill(pixels_+j*W+x,pixels_+j*W+x1,c);
}
void Display::text(int x,int y,const std::string &s,int scale,uint16_t c){
    for(char ch:s){
        if(x+5*scale>W) break;
        for(int row=0;row<7;row++) {
            uint8_t bits=glyph(ch,row);
            for(int col=0;col<5;col++) if(bits&(16>>col)) rect(x+col*scale,y+row*scale,scale,scale,c);
        }
        x+=6*scale;
    }
}
void Display::wrapped(int x,int y,int width,int max_lines,const std::string &s,int scale,uint16_t c){
    size_t cap=std::max(1,width/(6*scale)), pos=0; int line=0;
    while(pos<s.size()&&line<max_lines) {
        size_t n=std::min(cap,s.size()-pos);
        if(pos+n<s.size()){
            auto p=s.rfind(' ',pos+n);
            if(p!=std::string::npos&&p>pos) n=p-pos;
        }
        text(x,y+line*(8*scale+2),s.substr(pos,n),scale,c);
        pos+=n; while(pos<s.size()&&s[pos]==' ') ++pos;
        ++line;
    }
}
bool Display::show(){
    if(!spi_||!pixels_) return false;
    const uint8_t xs[]={0,0,1,63},ys[]={0,35,0,204};
    if(!command(0x2A,xs,4)||!command(0x2B,ys,4)) return false;
    const uint8_t ramwr=0x2C;
    gpio_set_level(CS,0);
    if(!send(false,&ramwr,1)){gpio_set_level(CS,1);return false;}
    for(int y=0;y<H;y+=STRIPE){
        unsigned count=W*std::min<int>(STRIPE,H-y);
        for(unsigned i=0;i<count;i++){
            auto v=pixels_[y*W+i]; dma_[2*i]=v>>8;dma_[2*i+1]=v&255;
        }
        if(!send(true,dma_,count*2)){gpio_set_level(CS,1);return false;}
    }
    gpio_set_level(CS,1);
    return true;
}
