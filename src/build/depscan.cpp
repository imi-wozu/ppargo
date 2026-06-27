
#include "build/depscan.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <format>
#include <functional>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include <mutex>

#include "build/compile_cache.hpp"
#include "build/depfile.hpp"
#include "util/environment.hpp"
#include "util/fs.hpp"
#include "util/output.hpp"
#include "util/process.hpp"
#include "util/text.hpp"

namespace {

constexpr std::string_view kDepscanCacheHeader = "ppargo-depscan-cache-v1";
constexpr long long kMissingStamp = LLONG_MIN;
using SourceKind = build::source_scan::SourceKind;

struct CachedSourceEntry {
    std::string path;
    long long source_stamp = kMissingStamp;
    long long depfile_stamp = kMissingStamp;
    SourceKind kind = SourceKind::TranslationUnit;
    std::string provides;
    std::vector<std::string> imports;
    std::vector<std::filesystem::path> file_deps;
};

struct CachedAnalysis {
    std::string key;
    bool used_clang_scan_deps = false;
    std::vector<CachedSourceEntry> sources;
};

void trace_line(std::string_view message) {
    if (util::env::exists("PPARGO_TRACE")) {
        util::output::trace(message);
    }
}

void warn_depscan_timeout_once(const std::filesystem::path& build_root,
                               int timeout_ms, bool modules_enabled) {
    static std::mutex warned_mutex;
    static std::unordered_set<std::string> warned_roots;

    const auto key = util::fs::normalized_path_key(build_root);
    std::lock_guard<std::mutex> lock(warned_mutex);
    if (!warned_roots.insert(key).second) {
        return;
    }

    if (modules_enabled) {
        util::output::warning(std::format(
            "Dependency analysis timed out after {}ms. Falling back to cached and depfile-based ordering.",
            timeout_ms));
    } else {
        trace_line(std::format(
            "depscan: timeout after {}ms; falling back to cached and depfile-based ordering",
            timeout_ms));
    }
}

auto strip_line_comment(std::string_view line) -> std::string_view {
    const auto comment = line.find("//");
    if (comment == std::string_view::npos) {
        return line;
    }
    return line.substr(0, comment);
}

auto source_kind_to_string(SourceKind kind) -> std::string_view {
    switch (kind) {
        case SourceKind::TranslationUnit:
            return "tu";
        case SourceKind::ModuleInterfaceUnit:
            return "interface";
        case SourceKind::ModuleImplementationUnit:
            return "implementation";
    }
    return "tu";
}

auto parse_source_kind(std::string_view value) -> SourceKind {
    if (value == "interface") {
        return SourceKind::ModuleInterfaceUnit;
    }
    if (value == "implementation") {
        return SourceKind::ModuleImplementationUnit;
    }
    return SourceKind::TranslationUnit;
}

auto json_escape(std::string_view value) -> std::string {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

auto is_probably_clang_driver(const std::filesystem::path& compiler) -> bool {
    auto name = compiler.filename().string();
    if (name.empty()) {
        name = compiler.string();
    }
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return name.find("clang") != std::string::npos;
}

auto scanner_candidate_for(const std::filesystem::path& compiler)
    -> std::optional<std::filesystem::path> {
    if (!is_probably_clang_driver(compiler)) {
        return std::nullopt;
    }

    if (!compiler.parent_path().empty()) {
        const auto sibling = compiler.parent_path() / "clang-scan-deps.exe";
        std::error_code ec;
        if (std::filesystem::exists(sibling, ec) && !ec) {
            return sibling;
        }
    }

#ifdef _WIN32
    return std::filesystem::path("clang-scan-deps.exe");
#else
    return std::filesystem::path("clang-scan-deps");
#endif
}

auto scan_cache_path_for(const std::filesystem::path& build_root)
    -> std::filesystem::path {
    return build_root.parent_path() / ".depscan_cache";
}

auto compiler_stamp(const std::filesystem::path& compiler) -> long long {
    const auto stamp = build::compile::detail::file_stamp(compiler);
    return stamp.value_or(kMissingStamp);
}

auto make_cache_key(std::span<const std::filesystem::path> sources,
                    const build::compile::CompilerConfig& config) -> std::string {
    std::vector<std::string> normalized_sources;
    normalized_sources.reserve(sources.size());
    for (const auto& source : sources) {
        normalized_sources.push_back(util::fs::normalized_path_key(source));
    }
    std::sort(normalized_sources.begin(), normalized_sources.end());

    std::ostringstream out;
    out << config.compiler.generic_string() << "\n";
    out << "compiler-stamp=" << compiler_stamp(config.compiler) << "\n";
    out << static_cast<int>(config.mode) << "\n";
    out << "opt=" << static_cast<int>(config.optimization_mode) << "\n";
    out << "modules=" << (config.modules_enabled ? "true" : "false") << "\n";
    out << "module-output=" << config.module_output_dir.generic_string() << "\n";
    out << "aggressive-tu-threshold="
        << config.build_settings.aggressive_tu_threshold << "\n";
    out << "aggressive-stale-threshold="
        << config.build_settings.aggressive_stale_threshold << "\n";
    out << "pch-scan-lines=" << config.build_settings.pch_scan_lines << "\n";
    out << "pch-frequency-threshold="
        << config.build_settings.pch_frequency_threshold << "\n";
    out << "pch-max-headers=" << config.build_settings.pch_max_headers << "\n";
    out << "depscan-timeout-ms=" << config.build_settings.depscan_timeout_ms
        << "\n";
    for (const auto& ext : config.module_interface_exts) {
        out << "ext=" << ext << "\n";
    }
    for (const auto& flag : config.flags) {
        out << "flag=" << flag << "\n";
    }
    for (const auto& include_path : config.include_paths) {
        out << "include=" << include_path.generic_string() << "\n";
    }
    for (const auto& source : normalized_sources) {
        out << "source=" << source << "\n";
    }
    return build::compile::detail::signature_hash_text(out.str());
}

auto resolved_dep_path(const std::filesystem::path& root,
                       const std::filesystem::path& value)
    -> std::filesystem::path {
    if (value.is_absolute()) {
        return value.lexically_normal();
    }
    return (root / value).lexically_normal();
}

auto max_internal_dependency_stamp(const std::filesystem::path& root,
                                   std::span<const std::filesystem::path> file_deps,
                                   long long fallback) -> long long {
    std::error_code ec;
    const auto canonical_root = std::filesystem::weakly_canonical(root, ec);
    if (ec) {
        return fallback;
    }

    long long rank = fallback;
    for (const auto& dep : file_deps) {
        const auto candidate = dep.lexically_normal();
        const auto relative = std::filesystem::relative(candidate, canonical_root, ec);
        if (ec || relative.empty()) {
            ec.clear();
            continue;
        }
        if (*relative.begin() == std::filesystem::path("..")) {
            continue;
        }

        const auto stamp = build::compile::detail::file_stamp(candidate);
        if (stamp) {
            rank = std::max(rank, *stamp);
        }
    }
    return rank;
}

auto is_module_interface_extension(
    const std::filesystem::path& source,
    std::span<const std::string> module_interface_exts) -> bool {
    const auto ext = source.extension().string();
    return std::find(module_interface_exts.begin(), module_interface_exts.end(), ext) !=
           module_interface_exts.end();
}

auto extract_statement_payload(std::string_view line, std::string_view keyword)
    -> std::optional<std::string> {
    auto trimmed = util::text::trim_ascii_view(strip_line_comment(line));
    if (!trimmed.starts_with(keyword)) {
        return std::nullopt;
    }

    trimmed.remove_prefix(keyword.size());
    trimmed = util::text::trim_ascii_view(trimmed);
    const auto end = trimmed.find(';');
    if (end == std::string_view::npos) {
        return std::nullopt;
    }

    const auto payload = util::text::trim_ascii_view(trimmed.substr(0, end));
    if (payload.empty()) {
        return std::nullopt;
    }
    return std::string(payload);
}

auto extract_quoted_include(std::string_view line) -> std::optional<std::string> {
    auto trimmed = util::text::trim_ascii_view(strip_line_comment(line));
    if (!trimmed.starts_with("#")) {
        return std::nullopt;
    }

    trimmed.remove_prefix(1);
    trimmed = util::text::trim_ascii_view(trimmed);
    if (!trimmed.starts_with("include")) {
        return std::nullopt;
    }

    trimmed.remove_prefix(std::string_view{"include"}.size());
    if (!trimmed.empty() &&
        std::isspace(static_cast<unsigned char>(trimmed.front())) == 0) {
        return std::nullopt;
    }

    trimmed = util::text::trim_ascii_view(trimmed);
    if (trimmed.size() < 2 || trimmed.front() != '"') {
        return std::nullopt;
    }

    const auto end = trimmed.find('"', 1);
    if (end == std::string_view::npos || end == 1) {
        return std::nullopt;
    }
    return std::string(trimmed.substr(1, end - 1));
}

auto existing_file(const std::filesystem::path& path)
    -> std::optional<std::filesystem::path> {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return std::nullopt;
    }
    return path.lexically_normal();
}

auto resolve_quoted_include(const std::filesystem::path& source,
                            std::string_view include_target,
                            const build::compile::CompilerConfig& config)
    -> std::optional<std::filesystem::path> {
    const std::filesystem::path target{std::string(include_target)};
    if (target.empty()) {
        return std::nullopt;
    }
    if (target.is_absolute()) {
        return existing_file(target);
    }

    if (const auto from_source_dir =
            existing_file(source.parent_path() / target);
        from_source_dir.has_value()) {
        return from_source_dir;
    }

    for (const auto& include_path : config.include_paths) {
        if (const auto from_include_path = existing_file(include_path / target);
            from_include_path.has_value()) {
            return from_include_path;
        }
    }

    return std::nullopt;
}

auto scan_source_text(const std::filesystem::path& source,
                      const build::compile::CompilerConfig& config)
    -> build::depscan::AnalyzedSource {
    build::depscan::AnalyzedSource analyzed;
    analyzed.source = source;

    if (is_module_interface_extension(source, config.module_interface_exts)) {
        analyzed.kind = SourceKind::ModuleInterfaceUnit;
    }

    std::ifstream input(source);
    if (!input.is_open()) {
        return analyzed;
    }

    std::unordered_set<std::string> imports;
    std::vector<std::filesystem::path> include_deps;
    std::unordered_set<std::string> include_dep_keys;
    std::string line;
    while (std::getline(input, line)) {
        if (const auto include = extract_quoted_include(line);
            include.has_value()) {
            if (const auto resolved =
                    resolve_quoted_include(source, *include, config);
                resolved.has_value()) {
                const auto key = util::fs::normalized_path_key(*resolved);
                if (include_dep_keys.insert(key).second) {
                    include_deps.push_back(*resolved);
                }
            }
        }

        if (const auto provided = extract_statement_payload(line, "export module ");
            provided.has_value()) {
            analyzed.kind = SourceKind::ModuleInterfaceUnit;
            analyzed.provides = *provided;
            continue;
        }

        if (const auto implementation = extract_statement_payload(line, "module ");
            implementation.has_value()) {
            if (*implementation != ":private" && *implementation != "private") {
                analyzed.kind = SourceKind::ModuleImplementationUnit;
                imports.insert(*implementation);
            }
            continue;
        }

        if (const auto import = extract_statement_payload(line, "export import ");
            import.has_value()) {
            if (!import->empty() && import->front() != '<' &&
                import->front() != '"') {
                imports.insert(*import);
            }
            continue;
        }

        if (const auto import = extract_statement_payload(line, "import ");
            import.has_value()) {
            if (!import->empty() && import->front() != '<' &&
                import->front() != '"') {
                imports.insert(*import);
            }
        }
    }

    analyzed.imports.assign(imports.begin(), imports.end());
    std::sort(analyzed.imports.begin(), analyzed.imports.end());
    std::ranges::sort(include_deps, [](const auto& lhs, const auto& rhs) {
        return util::fs::normalized_path_key(lhs) <
               util::fs::normalized_path_key(rhs);
    });
    analyzed.file_deps = std::move(include_deps);
    return analyzed;
}

void normalize_dependency_list(std::vector<std::filesystem::path>& deps) {
    std::ranges::sort(deps, [](const auto& lhs, const auto& rhs) {
        return util::fs::normalized_path_key(lhs) <
               util::fs::normalized_path_key(rhs);
    });
    deps.erase(std::unique(deps.begin(), deps.end(),
                           [](const auto& lhs, const auto& rhs) {
                               return util::fs::normalized_path_key(lhs) ==
                                      util::fs::normalized_path_key(rhs);
                           }),
               deps.end());
}

void assign_dependencies(build::depscan::AnalyzedSource& analyzed,
                         const std::filesystem::path& root,
                         std::span<const std::filesystem::path> raw_deps) {
    analyzed.file_deps.clear();
    analyzed.file_deps.reserve(raw_deps.size());
    for (const auto& dep : raw_deps) {
        analyzed.file_deps.push_back(resolved_dep_path(root, dep));
    }
    normalize_dependency_list(analyzed.file_deps);
}

auto depfile_stamp_at(std::span<const std::filesystem::path> depfiles,
                      std::size_t index) -> long long {
    if (index >= depfiles.size()) {
        return kMissingStamp;
    }
    const auto stamp = build::compile::detail::file_stamp(depfiles[index]);
    return stamp.value_or(kMissingStamp);
}

void apply_recency_ranks(const std::filesystem::path& root,
                         std::vector<build::depscan::AnalyzedSource>& analyzed) {
    for (auto& source : analyzed) {
        const auto source_stamp =
            build::compile::detail::file_stamp(source.source).value_or(0);
        source.recency_rank =
            max_internal_dependency_stamp(root, source.file_deps, source_stamp);
    }
}

struct JsonValue {
    using Object = std::unordered_map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;

    std::variant<std::monostate, std::string, Object, Array> value;

    auto as_string() const -> const std::string* {
        return std::get_if<std::string>(&value);
    }

    auto as_object() const -> const Object* {
        return std::get_if<Object>(&value);
    }

    auto as_array() const -> const Array* {
        return std::get_if<Array>(&value);
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    auto parse() -> std::optional<JsonValue> {
        auto result = parse_value();
        skip_whitespace();
        if (!result.has_value() || pos_ != text_.size()) {
            return std::nullopt;
        }
        return result;
    }

private:
    auto parse_value() -> std::optional<JsonValue> {
        skip_whitespace();
        if (pos_ >= text_.size()) {
            return std::nullopt;
        }

        const char ch = text_[pos_];
        if (ch == '"') {
            auto parsed = parse_string();
            if (!parsed.has_value()) {
                return std::nullopt;
            }
            return JsonValue{.value = std::move(*parsed)};
        }
        if (ch == '{') {
            return parse_object();
        }
        if (ch == '[') {
            return parse_array();
        }
        if (ch == 't' || ch == 'f' || ch == 'n' || ch == '-' ||
            std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            parse_literal();
            return JsonValue{};
        }
        return std::nullopt;
    }

    auto parse_object() -> std::optional<JsonValue> {
        if (!consume('{')) {
            return std::nullopt;
        }

        JsonValue::Object object;
        skip_whitespace();
        if (consume('}')) {
            return JsonValue{.value = std::move(object)};
        }

        while (true) {
            auto key = parse_string();
            if (!key.has_value()) {
                return std::nullopt;
            }
            if (!consume(':')) {
                return std::nullopt;
            }
            auto value = parse_value();
            if (!value.has_value()) {
                return std::nullopt;
            }
            object.emplace(std::move(*key), std::move(*value));
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                return std::nullopt;
            }
        }

        return JsonValue{.value = std::move(object)};
    }

    auto parse_array() -> std::optional<JsonValue> {
        if (!consume('[')) {
            return std::nullopt;
        }

        JsonValue::Array array;
        skip_whitespace();
        if (consume(']')) {
            return JsonValue{.value = std::move(array)};
        }

        while (true) {
            auto value = parse_value();
            if (!value.has_value()) {
                return std::nullopt;
            }
            array.push_back(std::move(*value));
            if (consume(']')) {
                break;
            }
            if (!consume(',')) {
                return std::nullopt;
            }
        }

        return JsonValue{.value = std::move(array)};
    }

    auto parse_string() -> std::optional<std::string> {
        if (!consume('"')) {
            return std::nullopt;
        }

        std::string value;
        while (pos_ < text_.size()) {
            const char ch = text_[pos_++];
            if (ch == '"') {
                return value;
            }
            if (ch != '\\') {
                value.push_back(ch);
                continue;
            }
            if (pos_ >= text_.size()) {
                return std::nullopt;
            }

            const char escaped = text_[pos_++];
            switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    value.push_back(escaped);
                    break;
                case 'b':
                    value.push_back('\b');
                    break;
                case 'f':
                    value.push_back('\f');
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                case 'u':
                    if (pos_ + 4 > text_.size()) {
                        return std::nullopt;
                    }
                    pos_ += 4;
                    value.push_back('?');
                    break;
                default:
                    return std::nullopt;
            }
        }

        return std::nullopt;
    }

    void parse_literal() {
        while (pos_ < text_.size()) {
            const char ch = text_[pos_];
            if (ch == ',' || ch == ']' || ch == '}' ||
                std::isspace(static_cast<unsigned char>(ch)) != 0) {
                break;
            }
            ++pos_;
        }
    }

    void skip_whitespace() {
        while (pos_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[pos_])) != 0) {
            ++pos_;
        }
    }

    auto consume(char expected) -> bool {
        skip_whitespace();
        if (pos_ >= text_.size() || text_[pos_] != expected) {
            return false;
        }
        ++pos_;
        return true;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

auto object_member(const JsonValue& value, std::string_view key)
    -> const JsonValue* {
    const auto* object = value.as_object();
    if (object == nullptr) {
        return nullptr;
    }
    const auto found = object->find(std::string(key));
    if (found == object->end()) {
        return nullptr;
    }
    return &found->second;
}

auto parse_scanner_output(std::string_view text)
    -> std::optional<std::unordered_map<std::string, std::vector<std::filesystem::path>>> {
    JsonParser parser(text);
    const auto root = parser.parse();
    if (!root.has_value()) {
        return std::nullopt;
    }

    const auto* translation_units_value = object_member(*root, "translation-units");
    const auto* translation_units =
        translation_units_value != nullptr ? translation_units_value->as_array()
                                           : nullptr;
    if (translation_units == nullptr) {
        return std::nullopt;
    }

    std::unordered_map<std::string, std::vector<std::filesystem::path>> output;
    for (const auto& unit : *translation_units) {
        const auto* commands_value = object_member(unit, "commands");
        const auto* commands =
            commands_value != nullptr ? commands_value->as_array() : nullptr;
        if (commands == nullptr) {
            continue;
        }

        for (const auto& command : *commands) {
            const auto* input_value = object_member(command, "input-file");
            const auto* input =
                input_value != nullptr ? input_value->as_string() : nullptr;
            const auto* file_deps_value = object_member(command, "file-deps");
            const auto* file_deps =
                file_deps_value != nullptr ? file_deps_value->as_array() : nullptr;
            if (input == nullptr || file_deps == nullptr) {
                continue;
            }

            std::vector<std::filesystem::path> deps;
            deps.reserve(file_deps->size());
            for (const auto& dep_value : *file_deps) {
                const auto* dep = dep_value.as_string();
                if (dep == nullptr) {
                    continue;
                }
                deps.emplace_back(*dep);
            }

            output[util::fs::normalized_path_key(std::filesystem::path(*input))] =
                std::move(deps);
        }
    }

    return output;
}

auto make_compilation_database_json(
    const std::filesystem::path& root,
    std::span<const std::filesystem::path> sources,
    const build::compile::CompilerConfig& config,
    const std::filesystem::path& temp_dir) -> std::string {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (index != 0) {
            out << ",";
        }

        const auto object_placeholder = temp_dir / std::format("scan_{}.obj", index);
        out << "{";
        out << "\"directory\":\"" << json_escape(root.string()) << "\",";
        out << "\"file\":\"" << json_escape(sources[index].string()) << "\",";
        out << "\"arguments\":[";

        bool first_arg = true;
        auto append_arg = [&](std::string_view value) {
            if (!first_arg) {
                out << ",";
            }
            first_arg = false;
            out << "\"" << json_escape(value) << "\"";
        };

        append_arg(config.compiler.string());
        for (const auto& flag : config.flags) {
            append_arg(flag);
        }
        for (const auto& include_path : config.include_paths) {
            append_arg("-I");
            append_arg(include_path.string());
        }
        append_arg("-c");
        append_arg(sources[index].string());
        append_arg("-o");
        append_arg(object_placeholder.string());
        out << "]}";
    }
    out << "]";
    return out.str();
}

auto run_clang_scan_deps(
    const std::filesystem::path& scanner,
    const std::filesystem::path& root,
    const std::filesystem::path& build_root,
    std::span<const std::filesystem::path> sources,
    const build::compile::CompilerConfig& config)
    -> std::optional<std::unordered_map<std::string, std::vector<std::filesystem::path>>> {
    const auto temp_dir = build_root / ".depscan";
    std::error_code ec;
    std::filesystem::create_directories(temp_dir, ec);
    if (ec) {
        trace_line(std::format("depscan: unable to create temp dir {} ({})",
                               temp_dir.string(), ec.message()));
        return std::nullopt;
    }

    const auto compdb_path = temp_dir / "compile_commands.json";
    const auto compdb =
        make_compilation_database_json(root, sources, config, temp_dir);
    if (!util::fs::atomic_write_text_result(compdb_path, compdb)) {
        trace_line("depscan: unable to write compilation database");
        return std::nullopt;
    }

    util::process::RunOptions options{};
    options.working_directory = root;
    options.capture_output = true;
    options.merge_stderr = true;
    options.stdin_mode = util::process::StdinMode::Null;
    options.timeout_ms = config.build_settings.depscan_timeout_ms;

    const std::vector<std::string> args{
        "-format=experimental-full",
        "-compilation-database",
        compdb_path.string(),
    };

    const auto run = util::process::run_result(scanner, args, options);
    if (!run.has_value()) {
        trace_line(std::format("depscan: launch failed for {}", scanner.string()));
        return std::nullopt;
    }
    if (run->canceled) {
        warn_depscan_timeout_once(build_root, config.build_settings.depscan_timeout_ms,
                                  config.modules_enabled);
        trace_line(std::format(
            "depscan: command timed out or was canceled after {}ms ({})",
            config.build_settings.depscan_timeout_ms, scanner.string()));
        return std::nullopt;
    }
    if (run->exit_code != 0) {
        trace_line(std::format("depscan: command failed with exit code {}",
                               run->exit_code));
        return std::nullopt;
    }

    const auto parsed = parse_scanner_output(run->output);
    if (!parsed.has_value()) {
        trace_line("depscan: failed to parse experimental-full output");
        return std::nullopt;
    }

    return parsed;
}

auto load_cached_analysis(const std::filesystem::path& cache_file,
                          std::string_view key,
                          std::span<const std::filesystem::path> sources,
                          std::span<const std::filesystem::path> depfiles,
                          const std::filesystem::path& root)
    -> std::optional<build::depscan::AnalysisResult> {
    std::error_code ec;
    if (!std::filesystem::exists(cache_file, ec) || ec) {
        return std::nullopt;
    }

    std::ifstream input(cache_file);
    if (!input.is_open()) {
        return std::nullopt;
    }

    std::string header;
    if (!std::getline(input, header) || header != kDepscanCacheHeader) {
        return std::nullopt;
    }

    CachedAnalysis cached;
    std::unordered_map<std::string, std::size_t> source_index;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }

        const auto parts = util::text::split_tab_fields(line);
        if (parts.empty()) {
            continue;
        }

        if (parts[0] == "key" && parts.size() >= 2) {
            cached.key = parts[1];
            continue;
        }
        if (parts[0] == "scanner" && parts.size() >= 2) {
            cached.used_clang_scan_deps = parts[1] == "1";
            continue;
        }
        if (parts[0] == "source" && parts.size() >= 6) {
            long long source_stamp = kMissingStamp;
            long long depfile_stamp = kMissingStamp;
            std::istringstream source_in(parts[2]);
            std::istringstream depfile_in(parts[3]);
            source_in >> source_stamp;
            depfile_in >> depfile_stamp;
            cached.sources.push_back(CachedSourceEntry{
                .path = parts[1],
                .source_stamp = source_stamp,
                .depfile_stamp = depfile_stamp,
                .kind = parse_source_kind(parts[4]),
                .provides = parts[5],
                .imports = {},
                .file_deps = {},
            });
            source_index[parts[1]] = cached.sources.size() - 1;
            continue;
        }
        if (parts[0] == "import" && parts.size() >= 3) {
            const auto found = source_index.find(parts[1]);
            if (found != source_index.end()) {
                cached.sources[found->second].imports.push_back(parts[2]);
            }
            continue;
        }
        if (parts[0] == "dep" && parts.size() >= 3) {
            const auto found = source_index.find(parts[1]);
            if (found != source_index.end()) {
                cached.sources[found->second].file_deps.emplace_back(parts[2]);
            }
        }
    }

    if (cached.key != key || cached.sources.size() != sources.size()) {
        return std::nullopt;
    }

    std::unordered_map<std::string, const CachedSourceEntry*> cached_by_path;
    cached_by_path.reserve(cached.sources.size());
    for (const auto& entry : cached.sources) {
        cached_by_path.emplace(entry.path, &entry);
    }

    build::depscan::AnalysisResult result;
    result.used_clang_scan_deps = cached.used_clang_scan_deps;
    result.from_cache = true;
    result.sources.reserve(sources.size());

    for (std::size_t index = 0; index < sources.size(); ++index) {
        const auto key_path = util::fs::normalized_path_key(sources[index]);
        const auto found = cached_by_path.find(key_path);
        if (found == cached_by_path.end()) {
            return std::nullopt;
        }

        const auto* entry = found->second;
        const auto source_stamp =
            build::compile::detail::file_stamp(sources[index]).value_or(kMissingStamp);
        if (source_stamp != entry->source_stamp) {
            return std::nullopt;
        }
        if (depfile_stamp_at(depfiles, index) != entry->depfile_stamp) {
            return std::nullopt;
        }

        build::depscan::AnalyzedSource analyzed;
        analyzed.source = sources[index];
        analyzed.kind = entry->kind;
        analyzed.provides = entry->provides;
        analyzed.imports = entry->imports;
        analyzed.file_deps = entry->file_deps;
        normalize_dependency_list(analyzed.file_deps);
        result.sources.push_back(std::move(analyzed));
    }

    apply_recency_ranks(root, result.sources);
    trace_line(std::format("depscan: cache hit ({})", cache_file.string()));
    return result;
}

void save_cached_analysis(const std::filesystem::path& cache_file,
                          std::string_view key,
                          bool used_clang_scan_deps,
                          std::span<const build::depscan::AnalyzedSource> sources,
                          std::span<const std::filesystem::path> depfiles) {
    std::error_code ec;
    std::filesystem::create_directories(cache_file.parent_path(), ec);
    if (ec) {
        return;
    }

    std::ofstream output(cache_file, std::ios::trunc);
    if (!output.is_open()) {
        return;
    }

    output << kDepscanCacheHeader << "\n";
    output << "key\t" << key << "\n";
    output << "scanner\t" << (used_clang_scan_deps ? "1" : "0") << "\n";
    for (std::size_t index = 0; index < sources.size(); ++index) {
        const auto source_stamp = build::compile::detail::file_stamp(sources[index].source)
                                      .value_or(kMissingStamp);
        output << "source\t"
               << util::fs::normalized_path_key(sources[index].source) << "\t"
               << source_stamp << "\t" << depfile_stamp_at(depfiles, index) << "\t"
               << source_kind_to_string(sources[index].kind) << "\t"
               << sources[index].provides << "\n";

        for (const auto& import_name : sources[index].imports) {
            output << "import\t"
                   << util::fs::normalized_path_key(sources[index].source) << "\t"
                   << import_name << "\n";
        }
        for (const auto& dep : sources[index].file_deps) {
            output << "dep\t"
                   << util::fs::normalized_path_key(sources[index].source) << "\t"
                   << util::fs::normalized_path_key(dep) << "\n";
        }
    }
}

}  // namespace

namespace build::depscan {

auto analyze_sources(const std::filesystem::path& root,
                     const std::filesystem::path& build_root,
                     std::span<const std::filesystem::path> sources,
                     const compile::CompilerConfig& config,
                     std::span<const std::filesystem::path> depfiles)
    -> util::Result<AnalysisResult> {
    AnalysisResult result;
    if (sources.empty()) {
        return result;
    }

    const auto cache_file = scan_cache_path_for(build_root);
    const auto cache_key = make_cache_key(sources, config);
    if (const auto cached =
            load_cached_analysis(cache_file, cache_key, sources, depfiles, root);
        cached.has_value()) {
        return *cached;
    }

    result.sources.reserve(sources.size());
    for (const auto& source : sources) {
        result.sources.push_back(scan_source_text(source, config));
    }

    const bool should_try_scanner = config.modules_enabled;
    if (should_try_scanner) {
        if (const auto scanner = scanner_candidate_for(config.compiler);
            scanner.has_value()) {
            if (const auto scanned = run_clang_scan_deps(*scanner, root, build_root,
                                                         sources, config);
                scanned.has_value()) {
                result.used_clang_scan_deps = true;
                trace_line(std::format("depscan: using clang-scan-deps ({})",
                                       scanner->string()));
                for (auto& analyzed : result.sources) {
                    const auto found =
                        scanned->find(util::fs::normalized_path_key(analyzed.source));
                    if (found == scanned->end()) {
                        continue;
                    }
                    assign_dependencies(analyzed, root, found->second);
                }
            }
        }
    } else if (config.optimization_mode == compile::OptimizationMode::Aggressive &&
               util::env::exists("PPARGO_TRACE")) {
        trace_line("depscan: skipped clang-scan-deps for non-module build");
    }

    for (std::size_t index = 0; index < result.sources.size(); ++index) {
        if (!result.sources[index].file_deps.empty() || index >= depfiles.size()) {
            continue;
        }

        const auto parsed = depfile::parse_dependencies(depfiles[index]);
        if (!parsed) {
            trace_line(std::format("depscan: depfile fallback skipped for {}",
                                   depfiles[index].string()));
            continue;
        }
        assign_dependencies(result.sources[index], root, *parsed);
    }

    apply_recency_ranks(root, result.sources);
    save_cached_analysis(cache_file, cache_key, result.used_clang_scan_deps,
                         result.sources, depfiles);
    return result;
}

auto build_dependency_graph(std::span<const AnalyzedSource> sources)
    -> DependencyGraph {
    DependencyGraph graph;
    std::unordered_map<std::string, std::string> provider_for_module;
    std::vector<std::string> source_keys;
    source_keys.reserve(sources.size());

    for (const auto& source : sources) {
        const auto key = util::fs::normalized_path_key(source.source);
        source_keys.push_back(key);
        graph.deps.emplace(key, std::vector<std::string>{});
        if (!source.provides.empty()) {
            provider_for_module[source.provides] = key;
        }
    }

    for (const auto& source : sources) {
        const auto key = util::fs::normalized_path_key(source.source);
        auto& deps = graph.deps[key];
        for (const auto& import_name : source.imports) {
            const auto found = provider_for_module.find(import_name);
            if (found == provider_for_module.end() || found->second == key) {
                continue;
            }
            deps.push_back(found->second);
        }
        std::sort(deps.begin(), deps.end());
        deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
    }

    enum class VisitState {
        NotVisited,
        Visiting,
        Visited,
    };

    std::unordered_map<std::string, VisitState> visit_state;
    visit_state.reserve(source_keys.size());

    std::function<std::size_t(const std::string&)> compute_depth =
        [&](const std::string& key) -> std::size_t {
        const auto state = visit_state[key];
        if (state == VisitState::Visited) {
            return graph.depths[key];
        }
        if (state == VisitState::Visiting) {
            graph.had_cycle = true;
            return 0;
        }

        visit_state[key] = VisitState::Visiting;
        std::size_t depth = 0;
        const auto found = graph.deps.find(key);
        if (found != graph.deps.end()) {
            for (const auto& dep : found->second) {
                depth = std::max(depth, compute_depth(dep) + 1);
            }
        }
        visit_state[key] = VisitState::Visited;
        graph.depths[key] = depth;
        return depth;
    };

    for (const auto& key : source_keys) {
        compute_depth(key);
    }

    return graph;
}

}  // namespace build::depscan
