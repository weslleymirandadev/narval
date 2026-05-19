#include "frontend/interactive/line_editor.hpp"
#include "frontend/syntax_highlighter.hpp"

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

namespace nv::line_editor {
namespace {

#ifdef HAVE_READLINE
std::string active_prompt;
bool key_bindings_installed = false;
int rendered_rows = 0;
constexpr int TAB_WIDTH = 4;

struct CursorPosition {
    int row = 0;
    int column = 0;
};

CursorPosition rendered_cursor;
CursorPosition rendered_end;

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

void highlighted_redisplay() {
    const char* buffer = rl_line_buffer ? rl_line_buffer : "";
    const size_t point = rl_point >= 0 ? static_cast<size_t>(rl_point) : 0;
    const int end = rl_end >= 0 ? rl_end : 0;

    std::string line(buffer, static_cast<size_t>(end));
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
#endif

} // namespace

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
#else
    std::cout << prompt;
    std::cout.flush();
    std::string line;
    std::getline(std::cin, line);
    return line;
#endif
}

} // namespace nv::line_editor
