#include "core.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

namespace steno {
std::string Gemini::feed(uint8_t byte) {
    if (byte & 0x80) { used_ = 0; frame_[used_++] = byte; return {}; }
    if (!used_) return {};
    frame_[used_++] = byte;
    if (used_ != 6) return {};
    used_ = 0;
    // Gemini PR packet: six bytes, seven payload bits per byte, MSB marks first byte.
    auto bit = [&](unsigned i) { return (frame_[i / 7] & (0x40 >> (i % 7))) != 0; };
    std::string left, center, right;
    if (bit(1)||bit(2)||bit(3)||bit(4)||bit(5)||bit(6)||
        bit(35)||bit(36)||bit(37)||bit(38)||bit(39)||bit(40)) left += '#';
    if (bit(7)||bit(8)) left += 'S';
    const char l[] = "TKPWHR";
    for (unsigned i=0;i<6;i++) if(bit(i+9)) left+=l[i];
    if (bit(15)) center+='A';
    if (bit(16)) center+='O';
    if (bit(17)||bit(18)||bit(22)||bit(23)) center+='*';
    if (bit(24)) center+='E';
    if (bit(25)) center+='U';
    const char r[] = "FRPBLGTSD";
    for(unsigned i=0;i<9;i++) if(bit(i+26)) right+=r[i];
    if(bit(41)) right+='Z';
    return left + (center.empty() && !right.empty() ? "-" : center) + right;
}
static uint32_t u32(const uint8_t *p) {
    return p[0] | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24);
}
bool Dictionary::load(const uint8_t *data, size_t size) {
    data_=nullptr; size_=0; count_=0;
    if (!data || size<8 || std::memcmp(data,"STN1",4)) return false;
    unsigned n=u32(data+4);
    if (n>(size-8)/4 || !n) return false;
    size_t start=8+size_t(n)*4;
    const char *prev=nullptr;
    for(unsigned i=0;i<n;i++) {
        uint32_t off=u32(data+8+4*i);
        if(off>=size-start) return false;
        const char *p=reinterpret_cast<const char*>(data+start+off);
        const auto *end=reinterpret_cast<const char*>(data+size);
        const char *key_end=static_cast<const char*>(std::memchr(p,0,end-p));
        if(!key_end || key_end==p || key_end+1>=end ||
            !std::memchr(key_end+1,0,end-key_end-1) || (prev && std::strcmp(prev,p)>=0)) return false;
        prev=p;
    }
    data_=data; size_=size; count_=n; return true;
}
const char *Dictionary::entry(uint32_t i) const {
    return reinterpret_cast<const char*>(data_+8+size_t(count_)*4+u32(data_+8+4*i));
}
const char *Dictionary::find(const std::string &outline) const {
    uint32_t lo=0,hi=count_;
    while(lo<hi) {
        unsigned mid=lo+(hi-lo)/2;
        const char *key=entry(mid);
        int cmp=outline.compare(key);
        if(!cmp) return key+std::strlen(key)+1;
        if(cmp<0) hi=mid; else lo=mid+1;
    }
    return nullptr;
}
void Writer::push(const std::string &s) {
    if(s=="*") { if(!strokes_.empty()) strokes_.pop_back(); return; }
    if(s.empty()) return;
    if(strokes_.size()>=128) strokes_.pop_front();
    strokes_.push_back(s);
}
std::string Writer::text() const {
    std::string out;
    for(size_t i=0;i<strokes_.size();) {
        std::string key, result; size_t matched=0;
        for(size_t j=i;j<strokes_.size() && j<i+6;j++) {
            if(j!=i) key+='/';
            key+=strokes_[j];
            if(auto val=dict_.find(key)) { result=val; matched=j-i+1; }
        }
        if(!matched) { result="["+strokes_[i]+"]"; matched=1; }
        if(!out.empty()) out+=' ';
        out+=result; i+=matched;
    }
    return out;
}
static void inc(uint32_t &x) { if(x!=std::numeric_limits<uint32_t>::max()) ++x; }
void Session::reset() { *this=Session{}; }
void Session::attempt(bool success,bool word_finished,uint64_t now) {
    if(!started) { start_ms=now; started=true; }
    if(success) { inc(correct); if(word_finished) inc(words); } else inc(missed);
}
unsigned Session::accuracy() const {
    uint64_t total=uint64_t(correct)+missed;
    return total?unsigned(uint64_t(correct)*100/total):0;
}
unsigned Session::wpm(uint64_t now) const {
    if(!started || now<=start_ms || now-start_ms<1000) return 0;
    return unsigned(std::min<uint64_t>(9999,uint64_t(words)*60000/(now-start_ms)));
}
Practice::Practice(const Drill *d,size_t n):drills_(d),count_(n),records(n){}
void Practice::start(unsigned group,bool review) {
    sequence_.clear(); position_=stroke_index_=0; session.reset();
    for(size_t i=0;i<count_;i++)
        if(review ? records[i].missed>0 : drills_[i].group==group) sequence_.push_back(i);
    if(review) std::stable_sort(sequence_.begin(),sequence_.end(),[&](size_t a,size_t b){
        return records[a].missed>records[b].missed;
    });
}
void Practice::restart_current(){ stroke_index_=0; }
const Drill *Practice::current() const { return done()?nullptr:&drills_[sequence_[position_]]; }
std::string Practice::expected() const {
    if(done()) return {};
    std::string outline=current()->outline;
    size_t from=0;
    for(size_t i=0;i<stroke_index_;i++){ from=outline.find('/',from); if(from==std::string::npos) return {}; ++from; }
    return outline.substr(from,outline.find('/',from)-from);
}
bool Practice::submit(const std::string &s,const Dictionary &dict,bool demo,uint64_t now){
    if(done()||s.empty()) return false;
    const Drill *d=current();
    auto &rec=records[sequence_[position_]];
    std::string outline=d->outline;
    bool exact=s==expected();
    const char *translation=dict.find(s);
    bool alternate=!d->raw && stroke_index_==0 && translation && std::string(translation)==d->prompt;
    bool ok=exact||alternate;
    size_t n=1+std::count(outline.begin(),outline.end(),'/');
    bool finished=ok&&(alternate||stroke_index_+1==n);
    session.attempt(ok,finished&&!d->raw,now);
    if(!demo){ if(finished) inc(rec.correct); else if(!ok) inc(rec.missed); }
    if(finished) { ++position_; stroke_index_=0; }
    else if(ok) ++stroke_index_;
    return ok;
}
}
