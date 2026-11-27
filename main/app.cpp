#include "display.hpp"
#include "input.hpp"
#include "power.hpp"
#include "core.hpp"
#include "lessons_generated.hpp"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <string>

extern const uint8_t dictionary_start[] asm("_binary_dictionary_bin_start");
extern const uint8_t dictionary_end[] asm("_binary_dictionary_bin_end");

namespace {
using input::Action;
constexpr auto bg=Display::rgb(12,18,29), panel=Display::rgb(29,40,59),
    fg=Display::rgb(236,242,246), muted=Display::rgb(147,169,190),
    accent=Display::rgb(93,223,186), red=Display::rgb(255,118,114),
    yellow=Display::rgb(250,205,104);
constexpr size_t ndrills=sizeof(content::drills)/sizeof(content::drills[0]);
constexpr size_t ngroups=sizeof(content::groups)/sizeof(content::groups[0]);
constexpr const char *home[]={"Lessons","Review mistakes","Free writing","Statistics","Diagnostics","Switch input mode"};
constexpr int nhome=sizeof(home)/sizeof(home[0]);
enum class Screen{Home,Lessons,Practice,Results,Writer,Stats,Diag};
Display screen;
steno::Dictionary dict;
steno::Practice practice(content::drills,ndrills);
steno::Writer writer(dict);
nvs_handle_t storage=0;
bool storage_ready=false, dirty=false, demo=true, hint=false, bad=false, review=false;
int selected=0, group=0;
uint64_t last_save=0, last_frame=0, result_time=0;
std::string last_stroke,feedback,writer_text;
Screen view=Screen::Home;
uint64_t now_ms(){return esp_timer_get_time()/1000;}
std::string num(uint64_t n){return std::to_string(n);}
uint32_t layout_id(){
    uint32_t hash=2166136261u;
    for(auto &d:content::drills)for(auto str:{d.prompt,d.outline}){
        for(;*str;++str){hash^=uint8_t(*str);hash*=16777619u;}
        hash^=255;hash*=16777619u;
    }
    return hash;
}
std::string storage_key(){char b[12];snprintf(b,sizeof b,"p%08lx",(unsigned long)layout_id());return b;}
void load(){
    // Never erase caller's NVS automatically. If incompatible, continue without saves.
    if(nvs_flash_init()!=ESP_OK || nvs_open("stenofw",NVS_READWRITE,&storage)!=ESP_OK)return;
    storage_ready=true;
    size_t n=practice.records.size()*sizeof(steno::Record);
    size_t stored=0;
    auto key=storage_key();
    if(nvs_get_blob(storage,key.c_str(),nullptr,&stored)==ESP_OK&&n==stored){
        if(nvs_get_blob(storage,key.c_str(),practice.records.data(),&stored)!=ESP_OK)
            std::fill(practice.records.begin(),practice.records.end(),steno::Record{});
    }
}
void save(){
    if(!dirty || !storage_ready)return;
    auto key=storage_key();
    if(nvs_set_blob(storage,key.c_str(),practice.records.data(),practice.records.size()*sizeof(steno::Record))==ESP_OK
        && nvs_commit(storage)==ESP_OK){dirty=false;last_save=now_ms();}
    else storage_ready=false;
}
void top(const std::string &title){
    screen.clear(bg);screen.rect(0,0,320,28,panel);
    screen.text(9,7,title,2,fg);
    screen.text(267,9,demo?"DEMO":"LIVE",1,demo?yellow:accent);
}
void startup(const std::string &stage){
    top("STENO FW - STARTUP");
    screen.wrapped(12,55,296,3,stage,2,fg);
    screen.text(12,132,"DISPLAY FIX 1",1,accent);
    screen.show();
    vTaskDelay(pdMS_TO_TICKS(300));
}
void bottom(const std::string &s){
    screen.rect(0,151,320,19,panel);screen.text(8,157,s,1,muted);
}
void menu(const char *const *items,int count,int index){
    int start=std::clamp(index-2,0,std::max(0,count-5));
    for(int row=0;row<5&&row+start<count;row++){
        int i=row+start,y=31+row*23;
        if(i==index)screen.rect(5,y,310,21,panel);
        screen.text(12,y+4,i==index?">":" ",2,accent);
        screen.text(30,y+4,items[i],2,i==index?fg:muted);
    }
}
void render(){
    if(!power::healthy()){
        top("POWER CHECK");screen.text(10,47,"PMU NOT VERIFIED",2,red);
        screen.wrapped(10,76,300,3,"Unplug USB. Stop testing and restore the known board firmware.",2,fg);
        bottom("No boost is enabled by this firmware");screen.show();return;
    }
    switch(view){
    case Screen::Home:
        top("STENO FW");menu(home,nhome,selected);bottom("TURN: SELECT  CLICK: OPEN  HOLD: BACK");break;
    case Screen::Lessons:
        top("LESSONS");menu(content::groups,ngroups,selected);bottom("TURN: SELECT  CLICK: START  HOLD: BACK");break;
    case Screen::Practice:{
        top(review?"REVIEW":content::groups[group]);
        auto d=practice.current();
        if(!d){screen.text(10,50,"NO DRILLS",2,muted);break;}
        screen.text(10,34,num(practice.position()+1)+"/"+num(practice.total())+"   "+num(practice.session.accuracy())+"% ACC",1,muted);
        int scale=std::strlen(d->prompt)>16?2:3;
        screen.wrapped(10,51,300,2,d->prompt,scale,fg);
        screen.text(10,101,hint||demo||d->raw?practice.expected():"SIDE FOR HINT",2,accent);
        if(!feedback.empty())screen.text(10,127,feedback,1,bad?red:accent);
        else screen.text(10,127,demo?"CLICK TO TRY EXPECTED STROKE":"STROKE ON YOUR KEYBOARD",1,muted);
        bottom(demo?"CLICK: DEMO  TURN: WRONG DEMO  HOLD: BACK":"SIDE: HINT  CLICK: RESTART STROKE  HOLD: BACK");break;
    }
    case Screen::Results:
        top("SESSION COMPLETE");
        if(!practice.total()){
            screen.text(10,50,"NO RECORDED MISTAKES",2,accent);
            screen.wrapped(10,80,300,2,"Complete a live lesson first.",2,fg);
            bottom("HOLD: BACK");break;
        }
        screen.text(10,40,"ACCURACY",1,muted);screen.text(10,53,num(practice.session.accuracy())+"%",4,accent);
        screen.text(166,40,"WORDS/MIN",1,muted);screen.text(166,53,num(practice.session.wpm(result_time)),4,fg);
        screen.text(10,96,"CORRECT "+num(practice.session.correct),2,fg);
        screen.text(10,120,"MISSED  "+num(practice.session.missed),2,fg);
        bottom(demo?"DEMO - NOT SAVED    CLICK: AGAIN":"CLICK: AGAIN  HOLD: BACK");break;
    case Screen::Writer:{
        top("FREE WRITING");
        std::string t=writer_text;
        if(t.size()>125){t=t.substr(t.size()-125);auto p=t.find(' ');if(p!=std::string::npos)t=t.substr(p+1);}
        screen.wrapped(10,36,300,6,t.empty()?"WRITE A STROKE...":t,2,fg);
        screen.text(10,132,last_stroke.empty()?"* UNDO":last_stroke,1,accent);
        bottom(demo?"CLICK: SAMPLE  SIDE: CLEAR  HOLD: BACK":"*: UNDO  SIDE: CLEAR  HOLD: BACK");break;
    }
    case Screen::Stats:{
        top("LIFETIME PROGRESS");
        uint64_t correct=0,missed=0,completed=0;
        for(auto &r:practice.records){correct+=r.correct;missed+=r.missed;completed+=r.correct>0;}
        screen.text(10,40,"WORDS/DRILLS DONE",1,muted);
        screen.text(10,54,num(completed)+"/"+num(ndrills),3,accent);
        screen.text(10,88,"PASSED "+num(correct),2,fg);screen.text(10,112,"MISSED "+num(missed),2,fg);
        bottom(storage_ready?"LOCAL SAVE ENABLED  HOLD: BACK":"SAVE UNAVAILABLE  HOLD: BACK");break;
    }
    case Screen::Diag:
        top("DIAGNOSTICS");screen.text(10,39,input::status(),2,accent);
        screen.text(10,65,"DICT: "+num(dict.count()),2,fg);
        screen.text(10,89,"STROKE: "+(last_stroke.empty()?std::string("-"):last_stroke),2,fg);
        screen.text(10,117,"PMU CHECK OK / BOOST OFF",1,muted);
        bottom("TEST A STROKE  HOLD: BACK");break;
    }
    if(!screen.show()) ESP_LOGW("steno","LCD update failed");
}
void start(bool is_review){
    review=is_review;
    practice.start(group,review);practice.session.started=true;practice.session.start_ms=now_ms();hint=false;feedback.clear();bad=false;
    result_time=now_ms();view=practice.done()?Screen::Results:Screen::Practice;
}
void stroke(const std::string &s,bool synthetic){
    if(s.empty())return;
    last_stroke=s;
    if(view==Screen::Diag)return;
    // Do not combine simulated and genuine attempts into one session.
    if(synthetic!=demo)return;
    if(view==Screen::Practice&&!practice.done()){
        bool ok=practice.submit(s,dict,demo,now_ms());
        bad=!ok;feedback=ok?"OK: "+s:"TRY AGAIN: "+s;
        if(!demo)dirty=true;
        if(ok)hint=false;
        if(practice.done()){result_time=now_ms();save();view=Screen::Results;}
    }else if(view==Screen::Writer){writer.push(s);writer_text=writer.text();}
}
void back(){
    save();
    if(view==Screen::Practice||view==Screen::Results){
        view=review?Screen::Home:Screen::Lessons;
        selected=review?1:group;
    }else {view=Screen::Home;selected=0;}
}
void act(Action a){
    if(a==Action::Back){back();return;}
    if(view==Screen::Home||view==Screen::Lessons){
        int count=view==Screen::Home?nhome:ngroups;
        if(a==Action::Next)selected=(selected+1)%count;
        if(a==Action::Previous)selected=(selected+count-1)%count;
        if(a!=Action::Select)return;
        if(view==Screen::Lessons){group=selected;start(false);return;}
        switch(selected){
        case 0:view=Screen::Lessons;selected=0;break;
        case 1:start(true);if(practice.done())feedback="NO RECORDED MISTAKES";break;
        case 2:view=Screen::Writer;writer.clear();writer_text.clear();break;
        case 3:view=Screen::Stats;break;
        case 4:view=Screen::Diag;break;
        case 5:demo=!demo;break;
        }return;
    }
    if(view==Screen::Practice){
        if(a==Action::Side)hint=!hint;
        if(a==Action::Select){
            if(demo)stroke(practice.expected(),true);
            else {practice.restart_current();feedback="OUTLINE RESTARTED";bad=false;}
        }
        if(demo&&(a==Action::Previous||a==Action::Next))
            stroke(practice.expected()=="-Z"?"-D":"-Z",true);
    }else if(view==Screen::Results&&a==Action::Select)start(review);
    else if(view==Screen::Writer){
        if(a==Action::Side){writer.clear();writer_text.clear();}
        if(a==Action::Select&&demo){
            static unsigned i=0;const char *sample[]={"KAT","TKOG","KAT","*"};
            stroke(sample[i++%4],true);
        }
    }
}
}
extern "C" void app_main(){
    bool pmu=power::begin();
    if(!screen.init()){ESP_LOGE("steno","LCD initialization failed");return;}
    if(!pmu){render();return;}
    startup("1/4 Power check passed");
    startup("2/4 Checking Lapwing dictionary");
    if(!dict.load(dictionary_start,dictionary_end-dictionary_start)){
        top("DICTIONARY ERROR");screen.wrapped(10,50,300,3,"Dictionary failed integrity check.",2,red);screen.show();return;
    }
    startup("3/4 Starting controls and input");
    input::controls_begin();input::begin();
    startup("4/4 Loading saved progress");
    load();render();
    uint64_t last_tick=now_ms();
    bool redraw=false, power_warning=false;
    while(true){
        if(!power::healthy()){
            if(!power_warning){render();power_warning=true;}
            vTaskDelay(pdMS_TO_TICKS(100));continue;
        }
        if(power_warning){power_warning=false;redraw=true;}
        auto a=input::poll_controls();
        if(a!=Action::None){act(a);redraw=true;}
        auto s=input::receive();
        if(!s.empty()){stroke(s,false);redraw=true;}
        auto n=now_ms();
        if(dirty&&n-last_save>=15000)save();
        if(n-last_tick>=1000){last_tick=n;if(view==Screen::Diag)redraw=true;}
        if(redraw&&n-last_frame>=65){render();last_frame=n;redraw=false;}
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
