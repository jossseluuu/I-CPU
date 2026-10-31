#include "console.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>

#if defined(_WIN32)
#  include <windows.h>
#  include <conio.h>
#  include <fcntl.h>
#  include <io.h>
#  include <shellapi.h>
#else
#  include <termios.h>
#  include <unistd.h>
#endif

namespace icpu::cli {

#if defined(_WIN32)
namespace {
std::string narrow(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? static_cast<std::size_t>(n - 1) : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
}  // namespace
#endif

std::vector<std::string> init_console(int argc, char** argv) {
    std::vector<std::string> args;
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    _setmode(_fileno(stdin), _O_BINARY);
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    for (int i = 1; i < wargc; ++i) args.push_back(narrow(wargv[i]));
    LocalFree(wargv);
#else
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    return args;
}

bool stdin_is_terminal() {
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

std::string read_all_stdin() {
    return std::string(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
}

std::optional<std::string> read_line(const std::string& prompt) {
    std::cout << prompt << std::flush;
#if defined(_WIN32)
    if (_isatty(_fileno(stdin))) {
        std::wstring w;
        wchar_t buf[512];
        for (;;) {
            DWORD n = 0;
            if (!ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), buf, 512, &n, nullptr) || n == 0) {
                if (w.empty()) return std::nullopt;
                break;
            }
            w.append(buf, n);
            if (w.find(L'\n') != std::wstring::npos) break;
        }
        while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r')) w.pop_back();
        if (w.size() == 1 && w[0] == 26) return std::nullopt;  // Ctrl+Z
        return narrow(w.c_str());
    }
#endif
    std::string line;
    if (!std::getline(std::cin, line)) return std::nullopt;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    // Marca BOM UTF-8 al principio (p. ej. texto enviado por tuberia desde PowerShell)
    if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
    return line;
}

std::string read_hidden(const std::string& prompt) {
    std::cerr << prompt << std::flush;
    std::string result;
#if defined(_WIN32)
    if (!_isatty(_fileno(stdin))) return {};
    std::wstring w;
    for (;;) {
        const wint_t c = _getwch();
        if (c == L'\r' || c == L'\n') break;
        if (c == 3) {  // Ctrl+C
            std::cerr << "\n";
            std::exit(130);
        }
        if (c == L'\b') {
            if (!w.empty()) w.pop_back();
            continue;
        }
        if (c == 0 || c == 0xE0) {  // teclas especiales (flechas...): se ignoran
            _getwch();
            continue;
        }
        w.push_back(static_cast<wchar_t>(c));
    }
    result = narrow(w.c_str());
    SecureZeroMemory(w.data(), w.size() * sizeof(wchar_t));
#else
    if (!isatty(STDIN_FILENO)) return {};
    termios old_t{};
    tcgetattr(STDIN_FILENO, &old_t);
    termios new_t = old_t;
    new_t.c_lflag &= static_cast<tcflag_t>(~ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_t);
    std::getline(std::cin, result);
    tcsetattr(STDIN_FILENO, TCSANOW, &old_t);
#endif
    std::cerr << "\n";
    return result;
}

}  // namespace icpu::cli
