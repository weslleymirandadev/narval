#include "frontend/interactive/line_editor.hpp"
#include "frontend/syntax_highlighter.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <string>

#ifdef HAVE_READLINE
#include <unistd.h>
#include <readline/history.h>
#include <readline/readline.h>
#ifdef RETURN
#undef RETURN
#endif
#endif

#ifdef _WIN32
// windows.h defines min/max as macros, which turns std::min below into a syntax error.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <conio.h>
#endif

namespace nv::line_editor {
namespace {

// ── Rendering ────────────────────────────────────────────────────────────────
// Shared by both readers: the readline redisplay hook and the raw-mode reader used on
// Windows (there is no readline there, so the line is redrawn by hand after every key).
std::string active_prompt;
int rendered_rows = 0;
constexpr int TAB_WIDTH = 4;

struct CursorPosition {
    int row = 0;
    int column = 0;
};

CursorPosition rendered_cursor;
CursorPosition rendered_end;

int row_count(const std::string& text) {
    int rows = 1;
    for (char ch : text) {
        if (ch == '\n') {
            ++rows;
        }
    }
    return rows;
}

CursorPosition cursor_position_for(const std::string& text, size_t point) {
    CursorPosition position;
    point = std::min(point, text.size());

    for (size_t i = 0; i < point; ++i) {
        if (text[i] == '\n') {
            ++position.row;
            position.column = 0;
        } else if (text[i] == '\t') {
            position.column += TAB_WIDTH;
        } else {
            ++position.column;
        }
    }

    return position;
}

void clear_previous_render() {
    if (rendered_rows <= 0) {
        return;
    }

    if (rendered_cursor.row < rendered_rows - 1) {
        std::cout << "\033[" << (rendered_rows - 1 - rendered_cursor.row) << "B";
    }

    for (int row = rendered_rows - 1; row >= 0; --row) {
        std::cout << "\r\033[2K";
        if (row > 0) {
            std::cout << "\033[1A";
        }
    }
}

void move_to_position(CursorPosition current, CursorPosition target) {
    if (current.row > target.row) {
        std::cout << "\033[" << (current.row - target.row) << "A";
    } else if (current.row < target.row) {
        std::cout << "\033[" << (target.row - current.row) << "B";
    }

    std::cout << "\r";
    if (target.column > 0) {
        std::cout << "\033[" << target.column << "C";
    }
}

void finish_rendered_line() {
    if (rendered_rows <= 0) {
        std::cout << "\n";
        return;
    }

    move_to_position(rendered_cursor, rendered_end);
    std::cout << "\n";
    std::cout.flush();
}

// Draw the prompt plus the highlighted line, leaving the cursor where `point` is.
void render_line(const std::string& line, size_t point) {
    std::string plain_render = active_prompt + line;
    std::string highlighted = active_prompt + syntax_highlighter::highlight_source(line);

    clear_previous_render();
    std::cout << highlighted;

    rendered_end = cursor_position_for(plain_render, plain_render.size());
    rendered_cursor = cursor_position_for(plain_render, active_prompt.size() + point);
    move_to_position(rendered_end, rendered_cursor);

    rendered_rows = row_count(plain_render);
    std::cout.flush();
}

#ifdef HAVE_READLINE
bool key_bindings_installed = false;

int insert_tab(int, int) {
    rl_insert_text("\t");
    return 0;
}

void install_key_bindings() {
    if (key_bindings_installed) {
        return;
    }

    rl_bind_key('\t', insert_tab);

    rl_bind_keyseq("\\e[D", rl_backward_char);
    rl_bind_keyseq("\\e[C", rl_forward_char);
    rl_bind_keyseq("\\e[A", rl_get_previous_history);
    rl_bind_keyseq("\\e[B", rl_get_next_history);

    rl_bind_keyseq("\\e[H", rl_beg_of_line);
    rl_bind_keyseq("\\eOH", rl_beg_of_line);
    rl_bind_keyseq("\\e[1~", rl_beg_of_line);
    rl_bind_keyseq("\\e[7~", rl_beg_of_line);

    rl_bind_keyseq("\\e[F", rl_end_of_line);
    rl_bind_keyseq("\\eOF", rl_end_of_line);
    rl_bind_keyseq("\\e[4~", rl_end_of_line);
    rl_bind_keyseq("\\e[8~", rl_end_of_line);

    key_bindings_installed = true;
}

void highlighted_redisplay() {
    const std::string line(rl_line_buffer ? rl_line_buffer : "",
                           rl_end >= 0 ? static_cast<size_t>(rl_end) : 0);
    render_line(line, rl_point >= 0 ? static_cast<size_t>(rl_point) : 0);
}
#endif

#ifdef _WIN32
// A console only interprets the ANSI escapes the highlighter emits after being asked to
// (VT processing); without this the line comes out full of "←[38;2;...". CP_UTF8 keeps
// accented source from turning into mojibake.
void enable_colored_output_impl() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (out != INVALID_HANDLE_VALUE && out != nullptr && GetConsoleMode(out, &mode)) {
        SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    SetConsoleOutputCP(CP_UTF8);
}

// Raw-mode reader: no readline on Windows, so the console is switched to character-at-a-time
// input and the line is re-rendered on every key, which is what makes the colours show up
// there too. Arrow keys and function keys arrive as a 0/0xE0 prefix and are ignored (there is
// no history to walk: that lives in readline).
std::string read_line_windows(const std::string& prompt) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD in_mode = 0;
    if (in == INVALID_HANDLE_VALUE || in == nullptr || out == INVALID_HANDLE_VALUE ||
        out == nullptr || !GetConsoleMode(in, &in_mode)) {
        // Redirected input/output (a script feeding the REPL): no editing, no colours.
        std::cout << prompt;
        std::cout.flush();
        std::string line;
        std::getline(std::cin, line);
        return line;
    }

    enable_colored_output_impl();
    SetConsoleMode(in, (in_mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT))
                           | ENABLE_EXTENDED_FLAGS);

    active_prompt = prompt;
    rendered_rows = 0;
    rendered_cursor = {};
    rendered_end = {};

    std::string line;
    render_line(line, 0);

    for (;;) {
        int ch = _getch();
        if (ch == '\r' || ch == '\n') {
            break;
        }
        if (ch == 3) {
            // Ctrl+C cancels the line: the console is in raw mode, so it arrives as a
            // character instead of killing the process. An empty line is a no-op for the REPL.
            line.clear();
            break;
        }
        if (ch == 8 || ch == 127) {
            if (!line.empty()) line.pop_back();
        } else if (ch == 0 || ch == 224) {
            _getch();  // arrow/function key: the second byte carries no text
            continue;
        } else if (ch >= 32 || ch == '\t') {
            line.push_back(static_cast<char>(ch));
        }
        render_line(line, line.size());
    }

    finish_rendered_line();
    SetConsoleMode(in, in_mode);
    active_prompt.clear();
    rendered_rows = 0;
    rendered_cursor = {};
    rendered_end = {};
    return line;
}
#endif

} // namespace

void enable_colored_output() {
#ifdef _WIN32
    enable_colored_output_impl();
#endif
}

std::string read_line(const std::string& prompt, bool add_to_history_enabled) {
#ifdef HAVE_READLINE
    if (!isatty(fileno(stdin)) || !isatty(fileno(stdout)) || std::cin.rdbuf()->in_avail() > 0) {
        std::cout << prompt;
        std::cout.flush();
        std::string line;
        std::getline(std::cin, line);
        return line;
    }

    install_key_bindings();

    active_prompt = prompt;
    rendered_rows = 0;
    rendered_cursor = {};
    rendered_end = {};
    auto* previous_redisplay = rl_redisplay_function;
    rl_redisplay_function = highlighted_redisplay;

    char* raw_line = readline("");
    if (raw_line) {
        finish_rendered_line();
    }

    rl_redisplay_function = previous_redisplay;
    active_prompt.clear();
    rendered_rows = 0;
    rendered_cursor = {};
    rendered_end = {};

    if (!raw_line) {
        return "";
    }

    std::string line(raw_line);
    std::free(raw_line);
    if (add_to_history_enabled && !line.empty()) {
        add_history(line.c_str());
    }
    return line;
#elif defined(_WIN32)
    (void)add_to_history_enabled;  // history lives in readline, which Windows does not have
    return read_line_windows(prompt);
#else
    std::cout << prompt;
    std::cout.flush();
    std::string line;
    std::getline(std::cin, line);
    return line;
#endif
}

} // namespace nv::line_editor
