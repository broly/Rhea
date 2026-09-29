module;

#include <json/value.h>
#include <json/reader.h>
#include <json/writer.h>

module cvar;

import std.compat;
import paths;


static std::vector<cvar::Entry*>& get_registry()
{
    static std::vector<cvar::Entry*> registry;
    return registry;
}

static std::filesystem::path resolve_path(const std::filesystem::path& path)
{
    return path.empty() ? paths::get_cache_path() / "cvars.json" : path;
}

void cvar::register_entry(Entry* entry)
{
    get_registry().push_back(entry);
}

void cvar::unregister_entry(Entry* entry)
{
    std::erase(get_registry(), entry);
}

std::vector<cvar::Entry*> cvar::get_entries()
{
    std::vector<Entry*> entries = get_registry();
    std::ranges::sort(entries, {}, &Entry::get_name);
    return entries;
}

cvar::Entry* cvar::find(std::string_view name)
{
    for (Entry* entry : get_registry())
        if (entry->get_name() == name)
            return entry;
    return nullptr;
}

bool cvar::save(const std::filesystem::path& path)
{
    Json::Value root(Json::objectValue);
    for (Entry* entry : get_entries())
    {
        if (!(entry->get_flags() & persistent))
            continue;
        std::string text = entry->to_string();
        if (!text.empty())
            root[entry->get_name()] = text;
    }

    const std::filesystem::path file_path = resolve_path(path);
    std::filesystem::create_directories(file_path.parent_path());
    std::ofstream file(file_path);
    if (!file)
        return false;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "\t";
    file << Json::writeString(builder, root);
    return true;
}

bool cvar::load(const std::filesystem::path& path)
{
    std::ifstream file(resolve_path(path));
    if (!file)
        return false;

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors) || !root.isObject())
        return false;

    for (const std::string& name : root.getMemberNames())
    {
        Entry* entry = find(name);
        if (entry && (entry->get_flags() & persistent) && root[name].isString())
            entry->from_string(root[name].asString());
    }
    return true;
}


/************************************************************************
 * COMMANDS / CONSOLE
 ***********************************************************************/

static std::vector<cvar::Command*>& get_command_registry()
{
    static std::vector<cvar::Command*> registry;
    return registry;
}

static cvar::PrintHandler print_handler = nullptr;

cvar::Command::Command(std::string_view in_name, std::string_view in_description, Handler in_handler,
    std::string_view in_usage, Completer in_completer)
    : name(in_name)
    , description(in_description)
    , usage(in_usage)
    , handler(std::move(in_handler))
    , completer(std::move(in_completer))
{
    get_command_registry().push_back(this);
}

cvar::Command::~Command()
{
    std::erase(get_command_registry(), this);
}

std::vector<cvar::Command*> cvar::get_commands()
{
    std::vector<Command*> commands = get_command_registry();
    std::ranges::sort(commands, {}, &Command::get_name);
    return commands;
}

cvar::Command* cvar::find_command(std::string_view name)
{
    for (Command* command : get_command_registry())
        if (command->get_name() == name)
            return command;
    return nullptr;
}

void cvar::print(std::string_view text, Output kind)
{
    if (print_handler)
        print_handler(text, kind);
    else
        (kind == Output::error ? std::cerr : std::cout) << text << std::endl;
}

void cvar::set_print_handler(PrintHandler handler)
{
    print_handler = handler;
}

std::vector<std::string_view> cvar::split_commands(std::string_view line)
{
    std::vector<std::string_view> result;
    bool quoted = false;
    size_t start = 0;
    for (size_t i = 0; i < line.size(); i++)
    {
        if (line[i] == '"')
            quoted = !quoted;
        else if (line[i] == ';' && !quoted)
        {
            result.push_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
    result.push_back(line.substr(start));
    return result;
}

std::vector<std::string> cvar::tokenize(std::string_view command)
{
    std::vector<std::string> tokens;
    std::string current;
    bool quoted = false;
    bool in_token = false;
    for (char c : command)
    {
        if (c == '"')
        {
            quoted = !quoted;
            in_token = true;
        }
        else if (!quoted && std::isspace((unsigned char)c))
        {
            if (in_token)
                tokens.push_back(std::move(current));
            current.clear();
            in_token = false;
        }
        else
        {
            current += c;
            in_token = true;
        }
    }
    if (in_token)
        tokens.push_back(std::move(current));
    return tokens;
}

std::string cvar::quote_argument(std::string_view value)
{
    const bool plain = !value.empty() && std::ranges::none_of(value, [] (char c) {
        return std::isspace((unsigned char)c) || c == ';' || c == '"';
    });
    return plain ? std::string(value) : std::format("\"{}\"", value);
}

std::vector<std::string> cvar::get_value_suggestions(const Entry& entry)
{
    const reflect::PropertyDesc& property = entry.get_property();
    if (property.type == get_type_id<bool>())
        return {"true", "false"};
    if (property.enum_info)
        return {property.enum_info->names.begin(), property.enum_info->names.end()};
    return {};
}

namespace
{
    bool contains_case_insensitive(std::string_view text, std::string_view pattern)
    {
        if (pattern.empty())
            return true;
        auto it = std::ranges::search(text, pattern, [] (char a, char b) {
            return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
        });
        return !it.empty();
    }

    std::string join(cvar::Args args)
    {
        std::string text;
        for (const std::string& arg : args)
            text += (text.empty() ? "" : " ") + arg;
        return text;
    }

    std::string value_text(const cvar::Entry& entry)
    {
        const std::string text = entry.to_string();
        return text.empty() ? "<no text form, see the Console Variables window>" : cvar::quote_argument(text);
    }

    void describe(const cvar::Entry& entry)
    {
        std::string line = std::format("{} = {}", entry.get_name(), value_text(entry));
        if (!entry.is_default() && !entry.default_to_string().empty())
            line += std::format("   (default {})", cvar::quote_argument(entry.default_to_string()));
        const reflect::PropertyMeta& meta = entry.get_property().meta;
        if (meta.has_range)
            line += std::format("   [{} .. {}]", meta.min, meta.max);
        if (entry.get_flags() & cvar::read_only)
            line += "   (read only)";
        cvar::print(line);
        if (!entry.get_description().empty())
            cvar::print(std::format("    {}", entry.get_description()));
        if (entry.get_property().enum_info)
        {
            std::string list;
            for (const std::string& value : cvar::get_value_suggestions(entry))
                list += (list.empty() ? "" : " | ") + value;
            cvar::print(std::format("    values: {}", list));
        }
    }

    void describe(const cvar::Command& command)
    {
        cvar::print(std::format("{} {}", command.get_name(), command.get_usage()));
        if (!command.get_description().empty())
            cvar::print(std::format("    {}", command.get_description()));
    }

    void set_value(cvar::Entry& entry, cvar::Args args)
    {
        if (entry.get_flags() & cvar::read_only)
        {
            cvar::print(std::format("{} is read only", entry.get_name()), cvar::Output::error);
            return;
        }
        const std::string text = join(args);
        if (!entry.from_string(text))
        {
            cvar::print(std::format("Can't set {} to '{}'", entry.get_name(), text), cvar::Output::error);
            describe(entry);
            return;
        }
        cvar::print(std::format("{} = {}", entry.get_name(), value_text(entry)));
    }

    bool is_bool(const cvar::Entry& entry)
    {
        return entry.get_property().type == get_type_id<bool>();
    }

    // names of the variables and / or the commands, for argument completion
    std::vector<std::string> list_names(bool variables, bool commands, bool bool_only = false)
    {
        std::vector<std::string> names;
        if (variables)
            for (const cvar::Entry* entry : cvar::get_entries())
                if (!bool_only || is_bool(*entry))
                    names.push_back(entry->get_name());
        if (commands)
            for (const cvar::Command* command : cvar::get_commands())
                names.push_back(command->get_name());
        return names;
    }

    cvar::Command cmd_help("help", "Help on a command or variable; lists the commands without an argument",
        [] (cvar::Args args) {
            if (!args.empty())
            {
                if (const cvar::Command* command = cvar::find_command(args[0]))
                    describe(*command);
                else if (const cvar::Entry* entry = cvar::find(args[0]))
                    describe(*entry);
                else
                    cvar::print(std::format("Unknown command or variable '{}'", args[0]), cvar::Output::error);
                return;
            }
            cvar::print("Type any part of a name (or of a description) to search commands and variables:");
            cvar::print("    Up / Down select, Tab completes, Enter runs, Up / Down on an empty line: history");
            cvar::print("    'name' prints a variable, 'name value' sets it, 'a; b' runs both");
            cvar::print("Commands:");
            for (const cvar::Command* command : cvar::get_commands())
                cvar::print(std::format("    {} {}  -  {}", command->get_name(), command->get_usage(),
                    command->get_description()));
        },
        "[name]", [] (size_t) { return list_names(true, true); });

    cvar::Command cmd_find("find", "Lists commands and variables whose name or description contains the text",
        [] (cvar::Args args) {
            const std::string pattern = join(args);
            size_t count = 0;
            for (const cvar::Command* command : cvar::get_commands())
                if (contains_case_insensitive(command->get_name(), pattern) ||
                    contains_case_insensitive(command->get_description(), pattern))
                {
                    cvar::print(std::format("    {} {}  -  {}", command->get_name(), command->get_usage(),
                        command->get_description()));
                    count++;
                }
            for (const cvar::Entry* entry : cvar::get_entries())
                if (contains_case_insensitive(entry->get_name(), pattern) ||
                    contains_case_insensitive(entry->get_description(), pattern))
                {
                    cvar::print(std::format("    {} = {}  -  {}", entry->get_name(), value_text(*entry),
                        entry->get_description()));
                    count++;
                }
            cvar::print(std::format("{} found", count));
        },
        "<text>");

    cvar::Command cmd_reset("reset", "Resets a variable to its default; 'prefix*' resets a group, '*' everything",
        [] (cvar::Args args) {
            if (args.empty())
            {
                cvar::print("reset <name | prefix* | *>", cvar::Output::error);
                return;
            }
            const std::string_view pattern = args[0];
            const bool is_prefix = pattern.ends_with('*');
            const std::string_view prefix = pattern.substr(0, pattern.size() - (is_prefix ? 1 : 0));
            size_t count = 0;
            for (cvar::Entry* entry : cvar::get_entries())
            {
                const bool match = is_prefix ? entry->get_name().starts_with(prefix) : entry->get_name() == pattern;
                if (match && !entry->is_default())
                {
                    entry->reset_to_default();
                    cvar::print(std::format("{} = {}", entry->get_name(), value_text(*entry)));
                    count++;
                }
            }
            if (count == 0)
                cvar::print(is_prefix || cvar::find(pattern) ? "Nothing to reset" : "Unknown variable",
                    is_prefix || cvar::find(pattern) ? cvar::Output::info : cvar::Output::error);
        },
        "<name | prefix* | *>", [] (size_t) { return list_names(true, false); });

    cvar::Command cmd_toggle("toggle", "Flips a bool variable",
        [] (cvar::Args args) {
            cvar::Entry* entry = args.empty() ? nullptr : cvar::find(args[0]);
            if (!entry || !is_bool(*entry))
            {
                cvar::print("toggle <bool variable>", cvar::Output::error);
                return;
            }
            const std::string value = entry->to_string() == "true" ? "false" : "true";
            set_value(*entry, cvar::Args(&value, 1));
        },
        "<bool variable>", [] (size_t) { return list_names(true, false, true); });

    cvar::Command cmd_save("cvars.save", "Saves persistent variables to cache/cvars.json (also done on exit)",
        [] (cvar::Args) { cvar::print(cvar::save() ? "Saved" : "Save failed"); });

    cvar::Command cmd_load("cvars.load", "Loads persistent variables from cache/cvars.json",
        [] (cvar::Args) { cvar::print(cvar::load() ? "Loaded" : "Load failed"); });
}

void cvar::execute(std::string_view line)
{
    for (std::string_view command_text : split_commands(line))
    {
        const std::vector<std::string> tokens = tokenize(command_text);
        if (tokens.empty())
            continue;

        const std::string& name = tokens[0];
        const Args args = Args(tokens).subspan(1);
        if (const Command* command = find_command(name))
        {
            command->run(args);
        }
        else if (Entry* entry = find(name))
        {
            if (args.empty())
                describe(*entry);
            else
                set_value(*entry, args);
        }
        else
        {
            print(std::format("Unknown command or variable '{}'", name), Output::error);
            std::string similar;
            size_t count = 0;
            for (const std::string& candidate : list_names(true, true))
                if (contains_case_insensitive(candidate, name) && count++ < 8)
                    similar += "  " + candidate;
            if (!similar.empty())
                print(std::format("    similar:{}", similar));
        }
    }
}
