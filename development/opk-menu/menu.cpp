/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

#include "menu.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <optional>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace opk::menu {
namespace {

constexpr std::string_view kAnsiReset = "\x1b[0m";
constexpr std::string_view kAnsiReverseAccent = "\x1b[30;46m";
constexpr std::string_view kAnsiSelected = "\x1b[30;41m";
constexpr std::string_view kAnsiDim = "\x1b[2m";
constexpr std::string_view kAnsiLast = "\x1b[1;33m";
constexpr std::string_view kAnsiError = "\x1b[1;31m";
constexpr std::string_view kAnsiHideCursor = "\x1b[?25l";
constexpr std::string_view kAnsiShowCursor = "\x1b[?25h";
constexpr std::string_view kAnsiClearLine = "\x1b[2K";
constexpr int kEscapeSequenceWaitMilliseconds = 100;
constexpr size_t kDefaultTerminalRows = 24;
constexpr size_t kDefaultTerminalColumns = 80;
constexpr size_t kMinimumIdColumnWidth = 12;
constexpr size_t kMaximumIdColumnWidth = 34;
constexpr std::string_view kMenuTitle = "OPEN PERCEPTION KIT - Demo Pipeline Launcher";

volatile sig_atomic_t terminal_signal = 0; // NOSONAR: shared only with POSIX signal handlers.
volatile sig_atomic_t window_resized = 0;  // NOSONAR: shared only with POSIX signal handlers.

void handle_terminal_signal(int signal_number) {
    terminal_signal = signal_number;
}

void handle_window_resize(int) {
    window_resized = 1;
}

bool write_all(int file_descriptor, std::string_view text) {
    size_t written = 0;
    while (written < text.size()) {
        const auto result = write(file_descriptor, text.data() + written, text.size() - written);
        if (result > 0) {
            written += static_cast<size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR)
            continue;
        return false;
    }
    return true;
}

bool colors_enabled() {
    const char *no_color = std::getenv("NO_COLOR");
    return no_color == nullptr || no_color[0] == '\0';
}

bool supports_interactive_menu() {
    if (isatty(STDIN_FILENO) == 0 || isatty(STDOUT_FILENO) == 0)
        return false;

    const char *term = std::getenv("TERM");
    return term == nullptr || std::strcmp(term, "dumb") != 0;
}

std::string styled(std::string_view text, std::string_view style, bool enable_colors) {
    if (!enable_colors)
        return std::string{text};

    std::string result;
    result.reserve(style.size() + text.size() + kAnsiReset.size());
    result += style;
    result += text;
    result += kAnsiReset;
    return result;
}

std::string sanitize_terminal_text(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const unsigned char character : text) {
        if (character < 0x20 || character == 0x7f) {
            result.push_back(' ');
        } else {
            result.push_back(static_cast<char>(character));
        }
    }
    return result;
}

std::string truncate_text(std::string_view text, size_t maximum_width) {
    if (text.size() <= maximum_width)
        return std::string{text};
    if (maximum_width <= 3)
        return std::string(maximum_width, '.');

    size_t prefix_length = maximum_width - 3;
    while (prefix_length > 0 && prefix_length < text.size() &&
           (static_cast<unsigned char>(text[prefix_length]) & 0xc0) == 0x80) {
        --prefix_length;
    }
    return std::string{text.substr(0, prefix_length)} + "...";
}

std::string display_pipeline_id(std::string_view id) {
    constexpr std::string_view json_extension = ".json";
    if (id.ends_with(json_extension))
        id.remove_suffix(json_extension.size());
    return sanitize_terminal_text(id);
}

std::string pad_right(std::string text, size_t width) {
    if (text.size() < width)
        text.append(width - text.size(), ' ');
    return text;
}

std::vector<std::string>
render_title_box(size_t terminal_columns, std::string_view title, bool enable_colors) {
    const size_t usable_columns = terminal_columns > 1 ? terminal_columns - 1 : 1;
    const auto title_text = truncate_text(title, usable_columns);

    std::vector<std::string> lines{
        std::string(usable_columns, ' '),
        pad_right(title_text, usable_columns),
        std::string(usable_columns, ' '),
    };

    for (auto &line : lines)
        line = styled(line, kAnsiReverseAccent, enable_colors);
    return lines;
}

std::optional<size_t> parse_choice(std::string_view input) {
    if (input.empty())
        return std::nullopt;

    size_t value = 0;
    const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), value, 10);
    if (error != std::errc{} || end != input.data() + input.size())
        return std::nullopt;
    return value;
}

std::optional<size_t>
choice_to_pipeline_index(size_t choice, size_t entry_count, std::optional<size_t> last_index) {
    if (choice == 0)
        return last_index;
    if (choice > entry_count)
        return std::nullopt;
    return choice - 1;
}

Result select_from_line_menu(std::string_view pipelines_directory,
                             std::span<const Entry> entries,
                             std::optional<size_t> last_pipeline_index) {
    std::cout << "Pipelines in: " << sanitize_terminal_text(pipelines_directory) << '\n';
    if (last_pipeline_index.has_value()) {
        const auto &last_entry = entries[*last_pipeline_index];
        std::cout << "0 -> " << sanitize_terminal_text(last_entry.id)
                  << " [LAST: " << sanitize_terminal_text(last_entry.description) << "]\n";
    } else {
        std::cout << "0 -> (no previous selection)\n";
    }

    for (size_t index = 0; index < entries.size(); ++index) {
        const auto &entry = entries[index];
        std::cout << index + 1 << " -> " << sanitize_terminal_text(entry.id) << " ["
                  << sanitize_terminal_text(entry.description) << "]";
        if (!entry.source_info.empty())
            std::cout << " (" << sanitize_terminal_text(entry.source_info) << ")";
        std::cout << '\n';
    }

    while (true) {
        std::cout << "\nSelect (0.." << entries.size() << "): " << std::flush;

        std::string input;
        if (!std::getline(std::cin, input))
            return {};

        const auto first_non_space = input.find_first_not_of(" \t\r\n");
        const auto last_non_space = input.find_last_not_of(" \t\r\n");
        if (first_non_space == std::string::npos) {
            std::cerr << "Invalid choice. Try again.\n";
            continue;
        }

        const auto choice = parse_choice(
            std::string_view{input}.substr(first_non_space, last_non_space - first_non_space + 1));
        if (!choice.has_value() || *choice > entries.size()) {
            std::cerr << "Invalid choice. Try again.\n";
            continue;
        }

        const auto selected_index =
            choice_to_pipeline_index(*choice, entries.size(), last_pipeline_index);
        if (!selected_index.has_value()) {
            std::cout << "No previous selection stored. Choose 1.." << entries.size() << ".\n";
            continue;
        }

        return {ResultKind::Selected, *selected_index, 0};
    }
}

struct TerminalSize {
    size_t rows{kDefaultTerminalRows};
    size_t columns{kDefaultTerminalColumns};
};

TerminalSize terminal_size() {
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0)
        return {};

    return {
        size.ws_row > 0 ? static_cast<size_t>(size.ws_row) : kDefaultTerminalRows,
        size.ws_col > 0 ? static_cast<size_t>(size.ws_col) : kDefaultTerminalColumns,
    };
}

class TerminalSession {
  public:
    TerminalSession() {
        terminal_signal = 0;
        window_resized = 0;

        if (tcgetattr(STDIN_FILENO, &original_attributes_) != 0)
            return;

        termios menu_attributes = original_attributes_;
        menu_attributes.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ISIG));
        menu_attributes.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
        menu_attributes.c_cc[VMIN] = 1;
        menu_attributes.c_cc[VTIME] = 0;

        install_signal_handlers();
        if (tcsetattr(STDIN_FILENO, TCSANOW, &menu_attributes) != 0) {
            restore_signal_handlers();
            return;
        }

        active_ = true;
        (void)write_all(STDOUT_FILENO, kAnsiHideCursor);
    }

    TerminalSession(const TerminalSession &) = delete;
    TerminalSession &operator=(const TerminalSession &) = delete;

    ~TerminalSession() {
        if (!active_)
            return;

        (void)tcsetattr(STDIN_FILENO, TCSANOW, &original_attributes_);
        (void)write_all(STDOUT_FILENO, kAnsiReset);
        (void)write_all(STDOUT_FILENO, kAnsiShowCursor);
        (void)write_all(STDOUT_FILENO, "\r\n");
        restore_signal_handlers();
    }

    [[nodiscard]] bool is_active() const {
        return active_;
    }

  private:
    struct SavedSignalAction {
        int signal_number{};
        struct sigaction action{};
        bool installed{};
    };

    void install_signal_handlers() {
        const std::array<int, 4> terminal_signals{SIGINT, SIGTERM, SIGHUP, SIGQUIT};
        size_t action_index = 0;
        for (const int signal_number : terminal_signals) {
            struct sigaction action{};
            action.sa_handler = handle_terminal_signal;
            sigemptyset(&action.sa_mask);
            action.sa_flags = 0;

            auto &saved = saved_actions_[action_index];
            ++action_index;
            saved.signal_number = signal_number;
            if (sigaction(signal_number, &action, &saved.action) == 0)
                saved.installed = true;
        }

        struct sigaction resize_action{};
        resize_action.sa_handler = handle_window_resize;
        sigemptyset(&resize_action.sa_mask);
        resize_action.sa_flags = 0;
        auto &saved = saved_actions_[action_index];
        saved.signal_number = SIGWINCH;
        if (sigaction(SIGWINCH, &resize_action, &saved.action) == 0)
            saved.installed = true;
    }

    void restore_signal_handlers() const {
        for (const auto &saved : saved_actions_) {
            if (saved.installed)
                (void)sigaction(saved.signal_number, &saved.action, nullptr);
        }
    }

    termios original_attributes_{};
    std::array<SavedSignalAction, 5> saved_actions_{};
    bool active_{};
};

enum class KeyKind {
    Up,
    Down,
    Enter,
    Backspace,
    Digit,
    Quit,
    Interrupt,
    Resize,
    EndOfInput,
    Unknown,
};

struct KeyEvent {
    KeyKind kind{KeyKind::Unknown};
    char digit{};
    int signal_number{};
};

bool input_available(int timeout_milliseconds) {
    pollfd descriptor{STDIN_FILENO, POLLIN, 0};
    while (true) {
        const int result = poll(&descriptor, 1, timeout_milliseconds);
        if (result >= 0)
            return result > 0 && (descriptor.revents & POLLIN) != 0;
        if (errno != EINTR)
            return false;
        if (terminal_signal != 0 || window_resized != 0)
            return false;
    }
}

std::optional<char> read_character() {
    char character = 0;
    while (true) {
        const auto count = read(STDIN_FILENO, &character, 1);
        if (count == 1)
            return character;
        if (count == 0)
            return std::nullopt;
        if (errno != EINTR)
            return std::nullopt;
        if (terminal_signal != 0 || window_resized != 0)
            return std::nullopt;
    }
}

KeyEvent pending_signal_event() {
    if (terminal_signal != 0)
        return {KeyKind::Interrupt, 0, terminal_signal};
    if (window_resized != 0) {
        window_resized = 0;
        return {KeyKind::Resize, 0, 0};
    }
    return {};
}

KeyEvent read_key() {
    if (terminal_signal != 0 || window_resized != 0)
        return pending_signal_event();

    const auto character = read_character();
    if (!character.has_value()) {
        if (terminal_signal != 0 || window_resized != 0)
            return pending_signal_event();
        return {KeyKind::EndOfInput, 0, 0};
    }

    switch (*character) {
    case '\x03':
        return {KeyKind::Interrupt, 0, SIGINT};
    case '\x04':
        return {KeyKind::EndOfInput, 0, 0};
    case '\r':
    case '\n':
        return {KeyKind::Enter, 0, 0};
    case '\x08':
    case '\x7f':
        return {KeyKind::Backspace, 0, 0};
    case 'j':
        return {KeyKind::Down, 0, 0};
    case 'k':
        return {KeyKind::Up, 0, 0};
    case 'q':
        return {KeyKind::Quit, 0, 0};
    case '\x1b':
        break;
    default:
        if (*character >= '0' && *character <= '9')
            return {KeyKind::Digit, *character, 0};
        return {};
    }

    if (!input_available(kEscapeSequenceWaitMilliseconds)) {
        if (terminal_signal != 0 || window_resized != 0)
            return pending_signal_event();
        return {KeyKind::Quit, 0, 0};
    }
    const auto prefix = read_character();
    if (!prefix.has_value() || (*prefix != '[' && *prefix != 'O'))
        return {};
    if (!input_available(kEscapeSequenceWaitMilliseconds)) {
        if (terminal_signal != 0 || window_resized != 0)
            return pending_signal_event();
        return {};
    }
    const auto code = read_character();
    if (!code.has_value())
        return {};
    if (*code == 'A')
        return {KeyKind::Up, 0, 0};
    if (*code == 'B')
        return {KeyKind::Down, 0, 0};
    return {};
}

class MenuState {
  public:
    MenuState(size_t entry_count, std::optional<size_t> last_pipeline_index)
        : entry_count_(entry_count), last_pipeline_index_(last_pipeline_index) {
        selected_row_ = last_pipeline_index_.has_value() ? 0 : 1;
    }

    void move_up() {
        numeric_input_.clear();
        error_message_.clear();
        const size_t first_selectable_row = last_pipeline_index_.has_value() ? 0 : 1;
        selected_row_ = selected_row_ == first_selectable_row ? entry_count_ : selected_row_ - 1;
    }

    void move_down() {
        numeric_input_.clear();
        error_message_.clear();
        const size_t first_selectable_row = last_pipeline_index_.has_value() ? 0 : 1;
        selected_row_ = selected_row_ == entry_count_ ? first_selectable_row : selected_row_ + 1;
    }

    void append_digit(char digit) {
        error_message_.clear();
        if (numeric_input_.size() < 10)
            numeric_input_.push_back(digit);
    }

    void remove_digit() {
        error_message_.clear();
        if (!numeric_input_.empty())
            numeric_input_.pop_back();
    }

    std::optional<size_t> confirm() {
        if (numeric_input_.empty())
            return choice_to_pipeline_index(selected_row_, entry_count_, last_pipeline_index_);

        const auto choice = parse_choice(numeric_input_);
        numeric_input_.clear();
        if (!choice.has_value() || *choice > entry_count_) {
            error_message_ = "Invalid selection; choose 0.." + std::to_string(entry_count_) + ".";
            return std::nullopt;
        }

        const auto selected_index =
            choice_to_pipeline_index(*choice, entry_count_, last_pipeline_index_);
        if (!selected_index.has_value()) {
            error_message_ =
                "No previous selection stored; choose 1.." + std::to_string(entry_count_) + ".";
            return std::nullopt;
        }
        selected_row_ = *choice;
        return selected_index;
    }

    [[nodiscard]] size_t selected_row() const {
        return selected_row_;
    }

    [[nodiscard]] const std::string &numeric_input() const {
        return numeric_input_;
    }

    [[nodiscard]] const std::string &error_message() const {
        return error_message_;
    }

  private:
    size_t entry_count_{};
    std::optional<size_t> last_pipeline_index_;
    size_t selected_row_{};
    std::string numeric_input_;
    std::string error_message_;
};

size_t menu_id_width(std::span<const Entry> entries, size_t terminal_columns) {
    size_t widest_id = kMinimumIdColumnWidth;
    for (const auto &entry : entries)
        widest_id = std::max(widest_id, display_pipeline_id(entry.id).size());

    const size_t width_limit = terminal_columns > 24 ? terminal_columns / 3 : kMinimumIdColumnWidth;
    return std::min({widest_id, kMaximumIdColumnWidth, width_limit});
}

std::optional<size_t> selected_entry_index(const MenuState &state,
                                           std::span<const Entry> entries,
                                           std::optional<size_t> last_pipeline_index) {
    if (!state.numeric_input().empty()) {
        const auto choice = parse_choice(state.numeric_input());
        if (!choice.has_value() || *choice > entries.size())
            return std::nullopt;
        return choice_to_pipeline_index(*choice, entries.size(), last_pipeline_index);
    }

    return choice_to_pipeline_index(state.selected_row(), entries.size(), last_pipeline_index);
}

void append_source_info(std::string &status,
                        std::span<const Entry> entries,
                        std::optional<size_t> selected_index) {
    if (!selected_index.has_value() || *selected_index >= entries.size())
        return;

    const auto source_info = entries[*selected_index].source_info;
    if (source_info.empty())
        return;

    status += "  ";
    status += "(";
    status += sanitize_terminal_text(source_info);
    status += ")";
}

std::string render_entry_row(size_t row,
                             std::span<const Entry> entries,
                             std::optional<size_t> last_pipeline_index,
                             size_t selected_row,
                             size_t terminal_columns,
                             size_t id_width,
                             bool enable_colors) {
    const bool selected = row == selected_row;
    const size_t number_width = std::to_string(entries.size()).size();

    std::string output = std::format("{}[{:>{}}] ", selected ? "> " : "  ", row, number_width);

    bool is_last = false;
    if (row == 0) {
        if (last_pipeline_index.has_value()) {
            output += "Last used: ";
            output += display_pipeline_id(entries[*last_pipeline_index].id);
            is_last = true;
        } else {
            output += "Last used: (no previous selection)";
        }
    } else {
        const size_t entry_index = row - 1;
        const auto id = truncate_text(display_pipeline_id(entries[entry_index].id), id_width);
        output += pad_right(id, id_width);
        is_last = last_pipeline_index == entry_index;
        if (is_last)
            output += "  [LAST]";
        output += "  ";
        output += sanitize_terminal_text(entries[entry_index].description);
    }

    const size_t usable_columns = terminal_columns > 1 ? terminal_columns - 1 : 1;
    const auto row_text = truncate_text(output, usable_columns);
    if (selected)
        return styled(pad_right(row_text, usable_columns), kAnsiSelected, enable_colors);
    if (is_last)
        return styled(row_text, kAnsiLast, enable_colors);
    return row_text;
}

std::vector<std::string> render_menu(std::string_view pipelines_directory,
                                     std::span<const Entry> entries,
                                     std::optional<size_t> last_pipeline_index,
                                     const MenuState &state,
                                     bool enable_colors) {
    const auto [terminal_rows, terminal_columns] = terminal_size();
    const size_t usable_columns = terminal_columns > 1 ? terminal_columns - 1 : 1;
    const auto title_lines = render_title_box(terminal_columns, kMenuTitle, enable_colors);
    const size_t fixed_line_count = title_lines.size() + 5;
    const size_t visible_row_count =
        terminal_rows > fixed_line_count ? terminal_rows - fixed_line_count : 1;
    const size_t total_menu_rows = entries.size() + 1;

    size_t first_visible_row = 0;
    if (state.selected_row() >= visible_row_count)
        first_visible_row = state.selected_row() - visible_row_count + 1;
    if (first_visible_row + visible_row_count > total_menu_rows &&
        total_menu_rows > visible_row_count) {
        first_visible_row = total_menu_rows - visible_row_count;
    }
    const size_t end_visible_row = std::min(total_menu_rows, first_visible_row + visible_row_count);

    std::vector<std::string> lines;
    lines.reserve(fixed_line_count + visible_row_count);
    lines.insert(lines.end(), title_lines.begin(), title_lines.end());
    lines.emplace_back();

    const size_t id_width = menu_id_width(entries, terminal_columns);
    for (size_t row = first_visible_row; row < end_visible_row; ++row) {
        lines.push_back(render_entry_row(row,
                                         entries,
                                         last_pipeline_index,
                                         state.selected_row(),
                                         terminal_columns,
                                         id_width,
                                         enable_colors));
    }

    lines.emplace_back();
    std::string help = "Up/Down or j/k: move  Enter: run  0.." + std::to_string(entries.size()) +
                       ": select  q/Esc: quit";
    if (total_menu_rows > visible_row_count) {
        help += "  [" + std::to_string(first_visible_row + 1) + "-" +
                std::to_string(end_visible_row) + "/" + std::to_string(total_menu_rows) + ']';
    }
    lines.push_back(styled(truncate_text(help, usable_columns), kAnsiDim, enable_colors));
    lines.push_back(styled(
        truncate_text("Pipelines: " + sanitize_terminal_text(pipelines_directory), usable_columns),
        kAnsiDim,
        enable_colors));

    std::string status;
    std::string_view status_style = kAnsiReverseAccent;
    bool include_source_info = true;
    if (!state.error_message().empty()) {
        status = state.error_message();
        status_style = kAnsiError;
        include_source_info = false;
    } else if (!state.numeric_input().empty()) {
        status = "Selection: " + state.numeric_input();
    } else {
        status = "Selection: " + std::to_string(state.selected_row());
    }
    if (include_source_info)
        append_source_info(
            status, entries, selected_entry_index(state, entries, last_pipeline_index));
    lines.push_back(styled(pad_right(truncate_text(status, usable_columns), usable_columns),
                           status_style,
                           enable_colors));
    return lines;
}

class MenuScreen {
  public:
    void draw(const std::vector<std::string> &lines) {
        if (lines.empty())
            return;

        std::string output;
        if (rendered_line_count_ > 0) {
            output += '\r';
            if (rendered_line_count_ > 1)
                output += "\x1b[" + std::to_string(rendered_line_count_ - 1) + 'A';
        }

        const size_t line_count = std::max(rendered_line_count_, lines.size());
        for (size_t line_index = 0; line_index < line_count; ++line_index) {
            output += kAnsiClearLine;
            output += '\r';
            if (line_index < lines.size())
                output += lines[line_index];
            if (line_index + 1 < line_count)
                output += "\r\n";
        }

        if (line_count > lines.size()) {
            output += '\r';
            output += "\x1b[" + std::to_string(line_count - lines.size()) + 'A';
        }

        (void)write_all(STDOUT_FILENO, output);
        rendered_line_count_ = lines.size();
    }

  private:
    size_t rendered_line_count_{};
};

Result select_from_interactive_menu(std::string_view pipelines_directory,
                                    std::span<const Entry> entries,
                                    std::optional<size_t> last_pipeline_index) {
    TerminalSession terminal;
    if (!terminal.is_active())
        return select_from_line_menu(pipelines_directory, entries, last_pipeline_index);

    MenuState state{entries.size(), last_pipeline_index};
    MenuScreen screen;
    const bool enable_colors = colors_enabled();

    while (true) {
        screen.draw(
            render_menu(pipelines_directory, entries, last_pipeline_index, state, enable_colors));

        const auto key = read_key();
        switch (key.kind) {
        case KeyKind::Up:
            state.move_up();
            break;
        case KeyKind::Down:
            state.move_down();
            break;
        case KeyKind::Digit:
            state.append_digit(key.digit);
            break;
        case KeyKind::Backspace:
            state.remove_digit();
            break;
        case KeyKind::Enter:
            if (const auto selected_index = state.confirm(); selected_index.has_value())
                return {ResultKind::Selected, *selected_index, 0};
            break;
        case KeyKind::Resize:
        case KeyKind::Unknown:
            break;
        case KeyKind::Interrupt:
            return {ResultKind::Interrupted, 0, key.signal_number};
        case KeyKind::Quit:
        case KeyKind::EndOfInput:
            return {};
        }
    }
}

} // namespace

Result select_pipeline(std::string_view pipelines_directory,
                       std::span<const Entry> entries,
                       std::optional<size_t> last_pipeline_index) {
    if (entries.empty())
        return {};
    if (last_pipeline_index.has_value() && *last_pipeline_index >= entries.size())
        last_pipeline_index.reset();

    if (!supports_interactive_menu())
        return select_from_line_menu(pipelines_directory, entries, last_pipeline_index);
    return select_from_interactive_menu(pipelines_directory, entries, last_pipeline_index);
}

} // namespace opk::menu
