#pragma once

#include "frontend/diagnostic.hpp"

#include <stdexcept>
#include <string>
#include <utility>

class LexicalError : public std::runtime_error {
public:
    LexicalError(
        std::string filename,
        size_t line,
        size_t col_start,
        size_t col_end,
        std::string message
    )
        : std::runtime_error(message),
          filename_(std::move(filename)),
          line_(line),
          col_start_(col_start),
          col_end_(col_end),
          message_(what()) {}

    const std::string& filename() const { return filename_; }
    size_t line() const { return line_; }
    size_t col_start() const { return col_start_; }
    size_t col_end() const { return col_end_; }

    nv::Diagnostic diagnostic() const {
        return {filename_, line_, col_start_, col_end_, 1, message_};
    }

private:
    std::string filename_;
    size_t line_;
    size_t col_start_;
    size_t col_end_;
    std::string message_;
};
