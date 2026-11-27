#include "core.hpp"
#include "lessons_generated.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
#include <string>
int main(){
    std::ifstream f("main/dictionary.bin",std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
    steno::Dictionary dictionary;
    assert(dictionary.load(bytes.data(),bytes.size()));
    assert(dictionary.count()>100000); // Full pinned upstream must be used, not seed.
    for (const auto &d : content::drills) {
        if(d.raw) continue;
        const char *val=dictionary.find(d.outline);
        assert(val&&std::string(val)==d.prompt);
        steno::Writer writer(dictionary);
        std::string outline=d.outline;
        size_t i=0;
        while(i<outline.size()){
            size_t end=outline.find('/',i);
            writer.push(outline.substr(i,end-i));
            if(end==std::string::npos)break;
            i=end+1;
        }
        assert(writer.text()==d.prompt);
    }
    assert(dictionary.find("KAT")&&std::string(dictionary.find("KAT"))=="cat");
    assert(dictionary.find("TKOG")&&std::string(dictionary.find("TKOG"))=="dog");
    std::cout << "PASS: validated " << dictionary.count() << " upstream dictionary entries and all word drills\n";
}
