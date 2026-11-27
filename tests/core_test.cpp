#include "core.hpp"
#include "lessons_generated.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
#include <initializer_list>
#include <string>

static std::string packet(steno::Gemini &p,std::initializer_list<unsigned> pressed) {
    uint8_t frame[6]={0x80,0,0,0,0,0};
    for(unsigned k:pressed)frame[k/7]|=0x40>>(k%7);
    std::string result;
    for(auto b:frame){auto s=p.feed(b);if(!s.empty())result=s;}
    return result;
}
int main(){
    steno::Gemini p;
    // Raw byte fixtures: Gemini PR bit positions and first-byte frame marker.
    const uint8_t cat[]={0x80,0x10,0x20,0x00,0x00,0x00}; // K+A; add final T below
    for(int i=0;i<5;i++) assert(p.feed(cat[i]).empty());
    assert(p.feed(0x40).empty()==false); // completed number key packet
    assert(packet(p,{10,15,32})=="KAT");
    assert(packet(p,{9,10,16,31})=="TKOG");
    assert(packet(p,{8,24,25,27,34,41})=="SEURDZ");
    assert(packet(p,{26,27,32})=="-FRT");
    assert(packet(p,{17,18,22,23})=="*");
    assert(packet(p,{1,35,40,7})=="#S");
    assert(packet(p,{0,19,20,21}).empty());
    assert(p.feed(0x12).empty());
    assert(p.feed(0x80).empty());
    assert(p.feed(0x7f).empty());
    assert(packet(p,{41})=="-Z"); // interrupted packet recovers
    for(unsigned i=0;i<100;i++)assert(p.feed(0).empty());

    std::ifstream f("main/dictionary.bin",std::ios::binary);
    std::vector<uint8_t> dat((std::istreambuf_iterator<char>(f)),{});
    steno::Dictionary dict;
    assert(dict.load(dat.data(),dat.size()));assert(dict.count()>50);
    // Every word drill is actually a supported translation.
    for(auto &d:content::drills)if(!d.raw){
        auto val=dict.find(d.outline);assert(val&&std::string(val)==d.prompt);
    }
    steno::Dictionary invalid;
    assert(!invalid.load(dat.data(),5));
    auto corrupt=dat;corrupt[8]=0xff;corrupt[9]=0xff;corrupt[10]=0xff;corrupt[11]=0xff;
    assert(!invalid.load(corrupt.data(),corrupt.size()));
    assert(!dict.find("ZZZZZZ"));
    steno::Writer w(dict);
    w.push("HEL");w.push("HRO");assert(w.text()=="hello");
    w.push("*");assert(w.text()=="hell");
    w.push("KAT");assert(w.text()=="hell cat");
    w.push("ZZZZZZ");assert(w.text()=="hell cat [ZZZZZZ]");
    w.clear();assert(w.text().empty());
    const steno::Drill ds[]={{"cat","KAT",0,false},{"hello","HEL/HRO",0,false},{"-F","-F",1,true}};
    steno::Practice practice(ds,3);
    practice.start(0);assert(practice.total()==2);
    assert(!practice.submit("-Z",dict,false,1000));
    assert(practice.submit("KAT",dict,false,2000));
    assert(practice.expected()=="HEL");
    assert(practice.submit("HEL",dict,false,3000));
    assert(!practice.submit("KAT",dict,false,4000));
    assert(practice.expected()=="HRO");
    assert(practice.submit("HRO",dict,false,5000));
    assert(practice.done());assert(practice.session.accuracy()==60);
    assert(practice.session.wpm(5000)==30);
    assert(practice.records[0].correct==1&&practice.records[0].missed==1);
    assert(practice.records[1].correct==1&&practice.records[1].missed==1);
    practice.start(0,true);assert(practice.total()==2);
    assert(practice.submit("KAT",dict,true,6000));
    assert(practice.records[0].correct==1);
    practice.start(1);assert(practice.submit("-F",dict,false,7000));
    assert(practice.done()&&practice.session.words==0);
    std::cout<<"PASS: packet framing, split/resync, key map, dictionary integrity, lesson consistency, multi-stroke writer, undo, grading, WPM, review and demo isolation\n";
}
