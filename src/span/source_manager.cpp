#include "source_manager.hpp"

#include <sstream>
#include <stdexcept>

namespace span {

namespace {
std::vector<size_t> build_line_offsets(const std::string &contents) {
    std::vector<size_t> offsets;
    offsets.push_back(0);
    for (size_t i = 0; i < contents.size(); ++i) {
        if (contents[i] == '\n') {
            offsets.push_back(i + 1);
        }
    }
    return offsets;
}

std::string_view line_slice(const std::string &source, const std::vector<size_t> &offsets, size_t line) {
    if (line == 0 || line > offsets.size()) return {};
    size_t start = offsets[line - 1];
    size_t end = (line < offsets.size()) ? offsets[line] : source.size();
    return std::string_view(source.data() + start, end - start);
}
} // namespace

FileId SourceManager::add_file(std::string path, std::string contents) {
    auto it = path_lookup.find(path);
    if (it != path_lookup.end()) {
        return it->second;
    }

    FileRecord record;
    record.path = std::move(path);
    record.contents = std::move(contents);
    record.line_offsets = build_line_offsets(record.contents);

    FileId id = static_cast<FileId>(files.size());
    files.push_back(std::move(record));
    path_lookup[files.back().path] = id;
    return id;
}

const SourceManager::FileRecord &SourceManager::lookup(FileId file) const {
    if (file >= files.size()) {
        throw std::out_of_range("Invalid FileId");
    }
    return files[file];
}

const std::string &SourceManager::get_filename(FileId file) const { return lookup(file).path; }
const std::string &SourceManager::get_source(FileId file) const { return lookup(file).contents; }
std::string_view SourceManager::get_source_view(FileId file) const { return lookup(file).contents; }

LineCol SourceManager::to_line_col(FileId file, uint32_t offset) const {
    const auto &record = lookup(file);
    const auto &offsets = record.line_offsets;
    if (offsets.empty()) {
        return {1, 1};
    }

    // Find the last line start that is <= offset.
    size_t line_index = 0;
    while (line_index + 1 < offsets.size() && offsets[line_index + 1] <= offset) {
        ++line_index;
    }
    size_t column = offset - offsets[line_index] + 1;
    return {line_index + 1, column};
}

std::string_view SourceManager::line_view(FileId file, size_t line) const {
    const auto &record = lookup(file);
    return line_slice(record.contents, record.line_offsets, line);
}

std::string SourceManager::format_span(const Span &span) const {
    if (!span.is_valid()) return "<unknown span>";

    auto loc = to_line_col(span.file, span.start);
    std::ostringstream oss;
    oss << get_filename(span.file) << ":" << loc.line << ":" << loc.column;
    auto line_text = line_view(span.file, loc.line);
    if (!line_text.empty()) {
        oss << "\n " << loc.line << " | " << line_text;
        oss << "\n " << std::string(std::to_string(loc.line).length(), ' ') << " | ";
        size_t caret_start = loc.column > 0 ? loc.column - 1 : 0;
        oss << std::string(caret_start, ' ') << "^";
        size_t length = (span.end > span.start) ? span.end - span.start : 1;
        if (length > 1) {
            oss << std::string(length - 1, '^');
        }
    }
    return oss.str();
}

} // namespace span
