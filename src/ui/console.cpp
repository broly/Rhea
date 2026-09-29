module;

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

module ui;

import std.compat;
import paths;
import cvar;
import log;


// Drop-down console: a log of command output (and logger messages) and an input line.
//
// While typing the first word the popup lists commands and variables matching every typed word
// (fuzzy in the name, plain text in the description), best first. After a known name it offers the
// argument values: enumerators, true / false or whatever the command's completer returns.
namespace
{
    cvar::Var<float> cv_console_height("ui.console.height", 0.4f, "Console height, fraction of the window",
        {.has_range = true, .min = 0.15f, .max = 1.0f});
    cvar::Var<bool> cv_console_log("ui.console.log", true, "Logger messages are shown in the console");
    cvar::Var<int> cv_console_rows("ui.console.completion_rows", 14, "Rows of the completion popup",
        {.has_range = true, .min = 4, .max = 40});

    constexpr size_t max_lines = 5000;
    constexpr size_t max_history = 200;
    constexpr size_t max_candidates = 300;

    enum class LineKind : uint8_t
    {
        info,
        error,
        input,
        log,
        log_error,
    };

    struct Line
    {
        std::string text;
        LineKind kind;
    };

    struct Candidate
    {
        std::string text;               // inserted into the input
        std::string value;              // variables: current value, commands: usage
        std::string_view description;
        std::vector<uint16_t> matches;  // highlighted characters of text
        int score = 0;
        bool is_command = false;
    };

    struct Completion
    {
        bool valid = false;
        std::string input;              // the input it was computed for
        bool show_all = false;
        size_t replace_begin = 0;       // the candidate replaces input[replace_begin..]
        bool is_argument = false;
        // the typed words are not a command / variable, or a partial argument: Enter completes
        bool enter_completes = false;
        std::vector<Candidate> candidates;
        // name / value / description of the command or variable whose arguments are typed
        std::string hint;
        std::string hint_description;
    };

    struct ConsoleState
    {
        bool open = false;
        bool focus_input = false;
        bool cursor_to_end = false;
        bool input_focused = false;
        std::string input;
        std::optional<std::string> pending_input;   // replaces the input on the next frame

        std::mutex lines_mutex;   // the logger can print from any thread
        std::deque<Line> lines;
        bool scroll_to_bottom = true;

        std::vector<std::string> history;
        int history_pos = -1;           // -1: editing a new line
        std::string history_draft;
        bool browsing_history = false;  // suppresses the popup until the next edit
        bool history_loaded = false;

        Completion completion;
        int selected = 0;
        bool selection_moved = false;   // Up / Down used: Enter completes instead of running
        bool scroll_to_selected = false;
        bool dismissed = false;         // Esc hid the popup until the next edit
        bool show_all = false;          // Tab on an empty line lists everything
    };

    ConsoleState& get_state()
    {
        static ConsoleState state;
        return state;
    }

    std::filesystem::path history_path()
    {
        return paths::get_cache_path() / "console_history.txt";
    }

    void add_line(std::string_view text, LineKind kind)
    {
        ConsoleState& state = get_state();
        std::scoped_lock lock(state.lines_mutex);
        // multi-line output is split so every row is one clipper item
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.remove_suffix(1);
        for (size_t start = 0;;)
        {
            const size_t end = text.find('\n', start);
            state.lines.push_back({std::string(text.substr(start, end == std::string_view::npos ? end : end - start)), kind});
            if (end == std::string_view::npos)
                break;
            start = end + 1;
        }
        while (state.lines.size() > max_lines)
            state.lines.pop_front();
    }

    void on_print(std::string_view text, cvar::Output kind)
    {
        add_line(text, kind == cvar::Output::error ? LineKind::error : LineKind::info);
    }

    void on_log(const char* message, bool error)
    {
        if (cv_console_log.get() || error)
            add_line(message, error ? LineKind::log_error : LineKind::log);
    }

    cvar::Command cmd_clear("clear", "Clears the console", [] (cvar::Args) {
        ConsoleState& state = get_state();
        std::scoped_lock lock(state.lines_mutex);
        state.lines.clear();
    });

    /************************************************************************
     * HISTORY
     ***********************************************************************/

    void load_history()
    {
        ConsoleState& state = get_state();
        state.history_loaded = true;
        std::ifstream file(history_path());
        std::string line;
        while (std::getline(file, line))
            if (!line.empty())
                state.history.push_back(line);
    }

    void add_to_history(const std::string& line)
    {
        ConsoleState& state = get_state();
        std::erase(state.history, line);
        state.history.push_back(line);
        if (state.history.size() > max_history)
            state.history.erase(state.history.begin(), state.history.end() - max_history);

        std::filesystem::create_directories(paths::get_cache_path());
        std::ofstream file(history_path(), std::ios::trunc);
        for (const std::string& entry : state.history)
            file << entry << '\n';
    }

    /************************************************************************
     * MATCHING
     ***********************************************************************/

    char lower(char c)
    {
        return (char)std::tolower((unsigned char)c);
    }

    bool is_word_start(std::string_view text, size_t index)
    {
        if (index == 0)
            return true;
        const char prev = text[index - 1];
        return prev == '.' || prev == '_' || prev == ' ' || prev == '-' || prev == '/' ||
            (std::islower((unsigned char)prev) && std::isupper((unsigned char)text[index]));
    }

    // Case-insensitive match of pattern in text: a substring, or else a subsequence ("pdd" in
    // "physics.debug.draw"). Scores contiguous runs, word starts and short texts higher.
    std::optional<int> fuzzy_match(std::string_view text, std::string_view pattern, std::vector<uint16_t>* matches)
    {
        if (pattern.empty())
            return 0;

        // substring: the occurrence at a word start wins
        std::optional<size_t> best_pos;
        for (size_t pos = 0; pos + pattern.size() <= text.size(); pos++)
        {
            bool equal = true;
            for (size_t i = 0; i < pattern.size() && equal; i++)
                equal = lower(text[pos + i]) == lower(pattern[i]);
            if (!equal)
                continue;
            if (!best_pos || (is_word_start(text, pos) && !is_word_start(text, *best_pos)))
                best_pos = pos;
            if (is_word_start(text, pos))
                break;
        }
        if (best_pos)
        {
            if (matches)
                for (size_t i = 0; i < pattern.size(); i++)
                    matches->push_back(uint16_t(*best_pos + i));
            int score = 1000 + int(pattern.size()) * 20 - int(text.size());
            if (*best_pos == 0)
                score += 300;
            else if (is_word_start(text, *best_pos))
                score += 150;
            // the whole last segment ("draw" for physics.debug.draw) or the whole name
            const size_t end = *best_pos + pattern.size();
            if (end == text.size())
                score += 100;
            return score;
        }

        // subsequence; the first pass jumps to word starts ("pdd" -> Physics.Debug.Draw), the second is
        // plain leftmost matching for when the jumps skipped needed characters
        auto match_subsequence = [&] (bool prefer_word_starts) -> std::optional<int> {
            constexpr size_t none = std::string_view::npos;
            int score = 0;
            size_t text_pos = 0;
            size_t prev_match = none;
            std::vector<uint16_t> found;
            for (char c : pattern)
            {
                size_t pos = none;
                for (size_t i = text_pos; i < text.size(); i++)
                {
                    if (lower(text[i]) != lower(c))
                        continue;
                    if (pos == none)
                        pos = i;
                    // a run continues or the character starts a word: take it, otherwise look further
                    const bool continues = prev_match != none && i == prev_match + 1;
                    if (!prefer_word_starts || continues || is_word_start(text, i))
                    {
                        pos = i;
                        break;
                    }
                }
                if (pos == none)
                    return std::nullopt;
                score += 10;
                if (prev_match != none && pos == prev_match + 1)
                    score += 15;
                if (is_word_start(text, pos))
                    score += 20;
                score -= int(std::min<size_t>(pos - text_pos, 10));
                found.push_back(uint16_t(pos));
                prev_match = pos;
                text_pos = pos + 1;
            }
            if (matches)
                matches->insert(matches->end(), found.begin(), found.end());
            return score - int(text.size()) / 4;
        };
        if (auto score = match_subsequence(true))
            return score;
        return match_subsequence(false);
    }

    bool contains_case_insensitive(std::string_view text, std::string_view pattern)
    {
        auto it = std::ranges::search(text, pattern, [] (char a, char b) { return lower(a) == lower(b); });
        return !it.empty();
    }

    // Every word matches the name (fuzzy) or the description (as text, ranked lower)
    std::optional<int> match_words(std::string_view name, std::string_view description,
        std::span<const std::string> words, std::vector<uint16_t>& matches)
    {
        int total = 0;
        for (const std::string& word : words)
        {
            if (auto score = fuzzy_match(name, word, &matches))
                total += *score;
            else if (word.size() >= 2 && contains_case_insensitive(description, word))
                total += 5;
            else
                return std::nullopt;
        }
        return total;
    }

    /************************************************************************
     * COMPLETION
     ***********************************************************************/

    // start of the last whitespace separated token (quotes keep spaces)
    size_t last_token_begin(std::string_view text)
    {
        bool quoted = false;
        size_t begin = 0;
        for (size_t i = 0; i < text.size(); i++)
        {
            if (text[i] == '"')
                quoted = !quoted;
            else if (!quoted && std::isspace((unsigned char)text[i]))
                begin = i + 1;
        }
        return begin;
    }

    std::string describe_value(const cvar::Entry& entry)
    {
        const std::string value = entry.to_string();
        return value.empty() ? "..." : cvar::quote_argument(value);
    }

    void complete_names(Completion& completion, std::span<const std::string> words)
    {
        for (cvar::Command* command : cvar::get_commands())
        {
            Candidate candidate{.text = command->get_name(), .value = command->get_usage(),
                .description = command->get_description(), .is_command = true};
            if (auto score = match_words(candidate.text, candidate.description, words, candidate.matches))
            {
                candidate.score = *score;
                completion.candidates.push_back(std::move(candidate));
            }
        }
        for (cvar::Entry* entry : cvar::get_entries())
        {
            Candidate candidate{.text = entry->get_name(), .value = describe_value(*entry),
                .description = entry->get_description()};
            if (auto score = match_words(candidate.text, candidate.description, words, candidate.matches))
            {
                candidate.score = *score;
                completion.candidates.push_back(std::move(candidate));
            }
        }
    }

    void complete_argument(Completion& completion, std::vector<std::string> values, std::string_view partial,
        const cvar::Entry* entry)
    {
        std::ranges::sort(values);
        values.erase(std::ranges::unique(values).begin(), values.end());
        const std::string current = entry ? entry->to_string() : std::string();
        for (std::string& value : values)
        {
            Candidate candidate{.text = std::move(value)};
            auto score = fuzzy_match(candidate.text, partial, &candidate.matches);
            if (!score)
                continue;
            candidate.score = *score;
            if (!current.empty() && candidate.text == current)
                candidate.value = "current";
            if (const cvar::Entry* named = cvar::find(candidate.text))   // arguments naming variables
            {
                candidate.value = describe_value(*named);
                candidate.description = named->get_description();
            }
            else if (const cvar::Command* command = cvar::find_command(candidate.text))
            {
                candidate.value = command->get_usage();
                candidate.description = command->get_description();
                candidate.is_command = true;
            }
            completion.candidates.push_back(std::move(candidate));
        }
    }

    Completion compute_completion(std::string_view input, bool show_all)
    {
        Completion completion;
        completion.valid = true;
        completion.input = input;
        completion.show_all = show_all;

        // the last ';' separated command is completed
        const std::string_view segment = cvar::split_commands(input).back();
        const size_t segment_begin = size_t(segment.data() - input.data());
        const std::vector<std::string> tokens = cvar::tokenize(segment);
        const bool new_token = !segment.empty() && std::isspace((unsigned char)segment.back());

        const cvar::Command* command = tokens.empty() ? nullptr : cvar::find_command(tokens[0]);
        const cvar::Entry* entry = tokens.empty() || command ? nullptr : cvar::find(tokens[0]);

        if ((command || entry) && (tokens.size() > 1 || new_token))
        {
            // arguments of a known command / variable
            completion.is_argument = true;
            const size_t arg_index = tokens.size() - (new_token ? 1 : 2);
            const std::string_view partial = new_token ? std::string_view() : std::string_view(tokens.back());
            completion.replace_begin = new_token ? input.size() : segment_begin + last_token_begin(segment);

            if (command)
            {
                completion.hint = std::format("{} {}", command->get_name(), command->get_usage());
                completion.hint_description = command->get_description();
                complete_argument(completion, command->complete(arg_index), partial, nullptr);
            }
            else
            {
                completion.hint = std::format("{} = {}", entry->get_name(), describe_value(*entry));
                const reflect::PropertyMeta& meta = entry->get_property().meta;
                if (meta.has_range)
                    completion.hint += std::format("   [{} .. {}]", meta.min, meta.max);
                if (!entry->is_default() && !entry->default_to_string().empty())
                    completion.hint += std::format("   default {}", cvar::quote_argument(entry->default_to_string()));
                completion.hint_description = entry->get_description();
                if (arg_index == 0)
                    complete_argument(completion, cvar::get_value_suggestions(*entry), partial, entry);
            }
            completion.enter_completes = !partial.empty() &&
                std::ranges::none_of(completion.candidates, [&] (const Candidate& c) { return c.text == partial; });
        }
        else if (!tokens.empty() || show_all)
        {
            // search: every typed word must match
            completion.enter_completes = !tokens.empty() && !command && !entry;
            completion.replace_begin = segment_begin + std::min(segment.find_first_not_of(" \t"), segment.size());
            complete_names(completion, tokens);
        }

        std::ranges::stable_sort(completion.candidates, [] (const Candidate& a, const Candidate& b) {
            if (a.score != b.score)
                return a.score > b.score;
            return a.text < b.text;
        });
        if (completion.candidates.size() > max_candidates)
            completion.candidates.resize(max_candidates);
        return completion;
    }

    void update_completion(std::string_view input)
    {
        ConsoleState& state = get_state();
        if (state.completion.valid && state.completion.input == input && state.completion.show_all == state.show_all)
            return;
        state.completion = compute_completion(input, state.show_all);
        state.selected = 0;
        state.selection_moved = false;
        state.scroll_to_selected = true;
    }

    bool completion_visible()
    {
        const ConsoleState& state = get_state();
        return !state.completion.candidates.empty() && !state.browsing_history && !state.dismissed;
    }

    // the input with the selected candidate in place of the typed part
    std::string apply_candidate(int index)
    {
        const ConsoleState& state = get_state();
        const Completion& completion = state.completion;
        const Candidate& candidate = completion.candidates[size_t(index)];
        std::string result = completion.input.substr(0, completion.replace_begin);
        result += completion.is_argument ? cvar::quote_argument(candidate.text) : candidate.text;
        result += ' ';
        return result;
    }

    void run_input()
    {
        ConsoleState& state = get_state();
        const std::string line = state.input;
        state.input.clear();
        state.history_pos = -1;
        state.browsing_history = false;
        state.dismissed = false;
        state.show_all = false;
        state.scroll_to_bottom = true;
        if (line.find_first_not_of(" \t;") == std::string::npos)
            return;
        add_line("> " + line, LineKind::input);
        add_to_history(line);
        cvar::execute(line);
    }

    void replace_text(ImGuiInputTextCallbackData* data, std::string_view text)
    {
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, text.data(), text.data() + text.size());
        data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
    }

    int input_callback(ImGuiInputTextCallbackData* data)
    {
        ConsoleState& state = get_state();
        switch (data->EventFlag)
        {
        case ImGuiInputTextFlags_CallbackCharFilter:
            // the console key: ` ~ and ё Ё on the Russian layout
            if (data->EventChar == '`' || data->EventChar == '~' || data->EventChar == 0x451 || data->EventChar == 0x401)
                return 1;
            break;

        case ImGuiInputTextFlags_CallbackEdit:
            state.browsing_history = false;
            state.history_pos = -1;
            state.dismissed = false;
            state.show_all = false;
            break;

        case ImGuiInputTextFlags_CallbackCompletion:
        {
            const std::string_view text(data->Buf, size_t(data->BufTextLen));
            state.browsing_history = false;
            state.dismissed = false;
            if (text.empty() && !state.show_all)
            {
                state.show_all = true;
                update_completion(text);
                break;
            }
            update_completion(text);
            if (!state.completion.candidates.empty())
                replace_text(data, apply_candidate(state.selected));
            break;
        }

        case ImGuiInputTextFlags_CallbackHistory:
        {
            const bool up = data->EventKey == ImGuiKey_UpArrow;
            if (completion_visible())
            {
                const int count = int(state.completion.candidates.size());
                state.selected = (state.selected + (up ? count - 1 : 1)) % count;
                state.selection_moved = true;
                state.scroll_to_selected = true;
                break;
            }
            if (state.history.empty())
                break;
            if (up)
            {
                if (state.history_pos == -1)
                {
                    state.history_draft.assign(data->Buf, size_t(data->BufTextLen));
                    state.history_pos = int(state.history.size()) - 1;
                }
                else if (state.history_pos > 0)
                    state.history_pos--;
            }
            else if (state.history_pos != -1)
            {
                if (++state.history_pos >= int(state.history.size()))
                    state.history_pos = -1;
            }
            replace_text(data, state.history_pos == -1 ? state.history_draft : state.history[size_t(state.history_pos)]);
            state.browsing_history = state.history_pos != -1;
            break;
        }

        case ImGuiInputTextFlags_CallbackAlways:
            if (state.pending_input)
            {
                replace_text(data, *state.pending_input);
                state.pending_input.reset();
            }
            if (state.cursor_to_end)
            {
                data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
                state.cursor_to_end = false;
            }
            break;
        }
        return 0;
    }

    /************************************************************************
     * DRAWING
     ***********************************************************************/

    ImVec4 line_color(LineKind kind)
    {
        switch (kind)
        {
        case LineKind::error:     return ImVec4(1.0f, 0.42f, 0.4f, 1.0f);
        case LineKind::input:     return ImVec4(0.55f, 0.8f, 1.0f, 1.0f);
        case LineKind::log:       return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        case LineKind::log_error: return ImVec4(1.0f, 0.62f, 0.3f, 1.0f);
        default:                  return ImGui::GetStyleColorVec4(ImGuiCol_Text);
        }
    }

    // text with the matched characters in the accent color
    void draw_highlighted(std::string_view text, const std::vector<uint16_t>& matches, ImU32 color, ImU32 accent)
    {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        size_t begin = 0;
        while (begin < text.size())
        {
            const bool matched = std::ranges::contains(matches, uint16_t(begin));
            size_t end = begin + 1;
            while (end < text.size() && std::ranges::contains(matches, uint16_t(end)) == matched)
                end++;
            const char* first = text.data() + begin;
            const char* last = text.data() + end;
            draw_list->AddText(pos, matched ? accent : color, first, last);
            pos.x += ImGui::CalcTextSize(first, last).x;
            begin = end;
        }
        ImGui::Dummy(ImGui::CalcTextSize(text.data(), text.data() + text.size()));
    }

    void draw_log(float footer_height)
    {
        ConsoleState& state = get_state();
        if (!ImGui::BeginChild("##log", ImVec2(0.0f, -footer_height), ImGuiChildFlags_None,
            ImGuiWindowFlags_HorizontalScrollbar))
        {
            ImGui::EndChild();
            return;
        }

        if (ImGui::BeginPopupContextWindow())
        {
            if (ImGui::MenuItem("Clear"))
                cvar::execute("clear");
            if (ImGui::MenuItem("Copy all"))
            {
                std::string text;
                std::scoped_lock lock(state.lines_mutex);
                for (const Line& line : state.lines)
                    text += line.text + '\n';
                ImGui::SetClipboardText(text.c_str());
            }
            ImGui::EndPopup();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));
        {
            std::scoped_lock lock(state.lines_mutex);
            ImGuiListClipper clipper;
            clipper.Begin(int(state.lines.size()));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                {
                    const Line& line = state.lines[size_t(i)];
                    ImGui::PushStyleColor(ImGuiCol_Text, line_color(line.kind));
                    ImGui::TextUnformatted(line.text.data(), line.text.data() + line.text.size());
                    ImGui::PopStyleColor();
                }
        }
        ImGui::PopStyleVar();

        // follow new output while scrolled to the bottom
        if (state.scroll_to_bottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
            ImGui::SetScrollHereY(1.0f);
        state.scroll_to_bottom = false;
        ImGui::EndChild();
    }

    float completion_row_height()
    {
        return ImGui::GetTextLineHeightWithSpacing() + 2.0f;
    }

    void draw_completion_popup(ImVec2 pos, ImVec2 size)
    {
        ConsoleState& state = get_state();
        const Completion& completion = state.completion;
        const float row_height = completion_row_height();
        const ImGuiStyle& style = ImGui::GetStyle();

        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowBgAlpha(0.97f);
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoDocking;
        ImGui::Begin("##console_completion", nullptr, flags);
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

        const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 accent = IM_COL32(255, 190, 60, 255);
        const ImVec4 value_color(0.55f, 0.8f, 1.0f, 1.0f);

        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##candidates", 3, table_flags))
        {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.36f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.2f);
            ImGui::TableSetupColumn("description", ImGuiTableColumnFlags_WidthStretch, 0.44f);

            for (int i = 0; i < int(completion.candidates.size()); i++)
            {
                const Candidate& candidate = completion.candidates[size_t(i)];
                ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
                ImGui::TableNextColumn();
                ImGui::PushID(i);
                const bool is_selected = i == state.selected;
                if (ImGui::Selectable("##row", is_selected,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                    ImVec2(0.0f, row_height - style.ItemSpacing.y)))
                {
                    state.pending_input = apply_candidate(i);
                    state.focus_input = true;
                }
                if (is_selected && state.scroll_to_selected)
                {
                    ImGui::SetScrollHereY(0.5f);
                    state.scroll_to_selected = false;
                }
                ImGui::SameLine(0.0f, 0.0f);
                draw_highlighted(candidate.text, candidate.matches, text_color, accent);

                ImGui::TableNextColumn();
                if (candidate.is_command)
                    ImGui::TextDisabled("%s", candidate.value.empty() ? "command" : candidate.value.c_str());
                else
                    ImGui::TextColored(value_color, "%s", candidate.value.c_str());

                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextUnformatted(candidate.description.data(), candidate.description.data() + candidate.description.size());
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::End();
    }
}

void ui::detail::init_console()
{
    cvar::set_print_handler(on_print);
    log_listener() = on_log;
}

void ui::detail::shutdown_console()
{
    log_listener() = nullptr;
    cvar::set_print_handler(nullptr);
}

bool ui::detail::console_has_input_focus()
{
    return get_state().open && get_state().input_focused;
}

bool ui::is_console_open()
{
    return get_state().open;
}

void ui::set_console_open(bool open)
{
    ConsoleState& state = get_state();
    if (state.open == open)
        return;
    state.open = open;
    if (open)
    {
        if (!state.history_loaded)
            load_history();
        state.focus_input = true;
        state.scroll_to_bottom = true;
    }
    else
        state.input_focused = false;
}

void ui::toggle_console()
{
    set_console_open(!is_console_open());
}

void ui::detail::draw_console()
{
    ConsoleState& state = get_state();
    const ImGuiStyle& style = ImGui::GetStyle();

    // Esc: hides the popup, then closes the console. ImGui's own Esc handling reverts the text and
    // deactivates the input: undone after InputText
    const bool escape = state.input_focused && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    const std::string input_before_escape = escape ? state.input : std::string();
    if (escape)
    {
        if (!completion_visible())
        {
            set_console_open(false);
            return;
        }
        state.dismissed = true;
        state.show_all = false;
    }

    if (state.pending_input && !state.input_focused)
    {
        state.input = *state.pending_input;
        state.pending_input.reset();
        state.focus_input = true;
    }
    update_completion(state.input);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float height = std::floor(viewport->WorkSize.y * cv_console_height.get());
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));
    ImGui::SetNextWindowBgAlpha(0.94f);
    if (state.focus_input)
        ImGui::SetNextWindowFocus();
    constexpr ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("##console", nullptr, window_flags);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    // log | hint line | input line
    const float footer = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y;
    draw_log(footer);

    ImGui::Separator();
    if (!state.completion.hint.empty())
    {
        ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "%s", state.completion.hint.c_str());
        if (!state.completion.hint_description.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("  %s", state.completion.hint_description.c_str());
        }
    }
    else
        ImGui::TextDisabled("Type to search commands and variables   Tab: complete   Up/Down: select / history   "
            "Enter: run   Esc: close   'help' for more");

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(">");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (state.focus_input)
    {
        ImGui::SetKeyboardFocusHere();
        state.focus_input = false;
        state.cursor_to_end = true;
    }
    constexpr ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory |
        ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackEdit | ImGuiInputTextFlags_CallbackAlways;
    const bool entered = ImGui::InputTextWithHint("##input", "help, or any part of a name / description",
        &state.input, input_flags, input_callback);
    state.input_focused = ImGui::IsItemActive();
    const ImVec2 input_min = ImGui::GetItemRectMin();

    if (escape)
    {
        state.input = input_before_escape;
        state.focus_input = true;
    }

    if (entered)
    {
        if (completion_visible() && (state.selection_moved || state.completion.enter_completes))
            state.input = apply_candidate(state.selected);
        else
            run_input();
        state.focus_input = true;
    }

    const float console_bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
    ImGui::End();

    update_completion(state.input);
    if (completion_visible())
    {
        // under the console, over its log when there is no room below
        const int rows = std::min(int(state.completion.candidates.size()), cv_console_rows.get());
        const float popup_height = rows * completion_row_height() + style.WindowPadding.y * 2.0f + 2.0f;
        const float viewport_bottom = viewport->WorkPos.y + viewport->WorkSize.y;
        const float y = console_bottom + popup_height <= viewport_bottom ? console_bottom
            : std::max(viewport->WorkPos.y, input_min.y - style.ItemSpacing.y - popup_height);
        const float width = std::clamp(viewport->WorkSize.x * 0.7f, 420.0f, 1100.0f);
        draw_completion_popup(ImVec2(input_min.x, y), ImVec2(std::min(width, viewport->WorkPos.x +
            viewport->WorkSize.x - input_min.x), popup_height));
    }
}
