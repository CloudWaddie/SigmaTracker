#pragma once
#include <cstdint>
#include <string>
class Display {
    uint16_t *pixels_=nullptr;
    uint8_t *dma_=nullptr;
    void *spi_=nullptr;
    bool send(bool data,const void *buf,unsigned len);
    bool command(uint8_t cmd,const uint8_t *data=nullptr,unsigned n=0);
public:
    static constexpr int W=320,H=170;
    static constexpr uint16_t rgb(unsigned r,unsigned g,unsigned b) { return ((r&248)<<8)|((g&252)<<3)|(b>>3); }
    bool init();
    void rect(int x,int y,int w,int h,uint16_t color);
    void clear(uint16_t color) { rect(0,0,W,H,color); }
    void text(int x,int y,const std::string &s,int scale,uint16_t color);
    void wrapped(int x,int y,int width,int max_lines,const std::string &s,int scale,uint16_t color);
    bool show();
};
