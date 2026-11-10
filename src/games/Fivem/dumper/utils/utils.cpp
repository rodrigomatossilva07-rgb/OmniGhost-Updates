#include "utils.hpp"

#include <Windows.h>

#include <iostream>

namespace logger {
namespace {
void SetColor(WORD color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
}
}

void log(const std::string& message, int type) {
    constexpr WORD white = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    std::cout << std::string(5, ' ');
    SetColor(white);
    std::cout << '[';
    switch (type) {
    case info: SetColor(FOREGROUND_BLUE | FOREGROUND_INTENSITY); std::cout << '>'; break;
    case success: SetColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY); std::cout << '+'; break;
    case fail: SetColor(FOREGROUND_RED | FOREGROUND_INTENSITY); std::cout << '!'; break;
    default: SetColor(white | FOREGROUND_INTENSITY); std::cout << '?'; break;
    }
    SetColor(white);
    std::cout << "] ";
    SetColor(white | FOREGROUND_INTENSITY);
    std::cout << message << '\n';
    SetColor(white);
}
}
