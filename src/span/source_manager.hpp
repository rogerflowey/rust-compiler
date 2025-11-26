#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "span.hpp"

namespace span {

class SourceManager {
public:
    FileId add_file(std::string path, std::string contents);

    const std::string &get_filename(FileId file) const;
    const std::string &get_source(FileId file) const;
    std::string_view get_source_view(FileId file) const;

    LineCol to_line_col(FileId file, uint32_t offset) const;
    std::string_view line_view(FileId file, size_t line) const;
    std::string format_span(const Span &span) const;

private:
    struct FileRecord {
        std::string path;
        std::string contents;
        std::vector<size_t> line_offsets; // start offset of each line
    };

    std::vector<FileRecord> files;
    std::unordered_map<std::string, FileId> path_lookup;

    const FileRecord &lookup(FileId file) const;
};

} // namespace span
