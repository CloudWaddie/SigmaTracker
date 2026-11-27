#pragma once
#include <string>
namespace input {
bool begin();
std::string receive();
const char *status();
enum class Action { None, Previous, Next, Select, Back, Side };
void controls_begin();
Action poll_controls();
}
