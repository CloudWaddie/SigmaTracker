#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <deque>

namespace steno {
class Gemini {
    uint8_t frame_[6]{};
    unsigned used_ = 0;
public:
    void reset() { used_ = 0; }
    std::string feed(uint8_t byte);
};
class Dictionary {
    const uint8_t *data_ = nullptr;
    size_t size_ = 0;
    uint32_t count_ = 0;
    const char *entry(uint32_t i) const;
public:
    bool load(const uint8_t *data, size_t size);
    const char *find(const std::string &outline) const;
    uint32_t count() const { return count_; }
};
class Writer {
    const Dictionary &dict_;
    std::deque<std::string> strokes_;
public:
    explicit Writer(const Dictionary &dict) : dict_(dict) {}
    void push(const std::string &s);
    void clear() { strokes_.clear(); }
    std::string text() const;
};
struct Drill {
    const char *prompt;
    const char *outline;
    unsigned group;
    bool raw;
};
struct Record { uint32_t correct=0, missed=0; };
struct Session {
    uint32_t correct=0, missed=0, words=0;
    uint64_t start_ms=0;
    bool started=false;
    void reset();
    void attempt(bool success, bool word_finished, uint64_t now);
    unsigned accuracy() const;
    unsigned wpm(uint64_t now) const;
};
class Practice {
    const Drill *drills_;
    size_t count_;
    std::vector<size_t> sequence_;
    size_t position_ = 0;
    size_t stroke_index_ = 0;
public:
    Session session;
    std::vector<Record> records;
    Practice(const Drill *drills, size_t count);
    void start(unsigned group, bool review = false);
    void restart_current();
    bool done() const { return position_ >= sequence_.size(); }
    size_t total() const { return sequence_.size(); }
    size_t position() const { return position_; }
    const Drill *current() const;
    std::string expected() const;
    bool submit(const std::string &stroke, const Dictionary &dict, bool demo, uint64_t now);
};
}
