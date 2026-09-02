#include <fast_float/fast_float.h>
#include <nanothread/nanothread.h>

#include <cstddef>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <charconv>

namespace fastnumparse {


/// 10x faster line reader relative to std::getline
/// 
/// todo: even faster with parallel parse
///     parallel_for to find line boundary and store to lineOffsets
///     now the buffer is CSR style
///     we can parallel parse lines with lineOffsets
///     or even better, we no need parse lines any more,
///     the lineOffsets can directly used to access each line zero-copy
/// 
/// implement:
///     1. split buffer to chunks
///     2. each chunk find line boundaries and store to chunk vectors
///     3. merge chunk vectors to global lineOffsets
///     4. now we can parallel parse lines with lineOffsets
///     estimate:  this will also get 5x ~ 10x speedup on large files (>100MB)
/// 
/// string_view: instead of string 4x more faster
///
/// total gain: get 200x ~ 400x speedup relative to std::getline
/// 
class FastLineReader {
public:
    explicit FastLineReader(const std::string& file) {
        // file is text file
        std::ifstream inFile(file, std::ios::binary | std::ios::ate);
        if (!inFile) {
            throw std::runtime_error("Cannot open file: " + file);
        }

        // Get file size and read all content
        file_size_ = inFile.tellg();
        inFile.seekg(0, std::ios::beg);
        buffer_.resize(file_size_ + 1); // +1 for null terminator
        inFile.read(buffer_.data(), file_size_);
        buffer_[file_size_] = '\0';
        inFile.close();

        cur_ = buffer_.data();
        end_ = buffer_.data() + file_size_;
    }

    explicit FastLineReader(const char* buffer, const size_t len) {
    }

    // Zero-copy access (FASTEST)
    bool getline(const char*& line_begin, const char*& line_end) {
        if (cur_ >= end_) {
            return false;
        }

        line_begin = cur_;
        line_end = cur_;

        while (line_end < end_ && *line_end != '\n' && *line_end != '\r') {
            ++line_end;
        }

        cur_ = line_end;
        while (cur_ < end_ && (*cur_ == '\n' || *cur_ == '\r')) {
            ++cur_;
        }

        return true;
    }

    // Compatibility API (creates std::string)
    bool getline(std::string& line) {
        const char* b;
        const char* e;
        if (!getline(b, e)) {
            return false;
        }
        line.assign(b, e);
        return true;
    }

    // very lightweight, zero-copy std::string_view, string_view is just like std::pair<const char*, size_t>
    bool getline(std::string_view& line) {
        const char* b;
        const char* e;
        if (!getline(b, e)) {
            return false;
        }
        line = std::string_view(b, static_cast<size_t>(e - b));
        return true;
    }

    void close() {
    }

private:
    std::ifstream inFile_;
    std::vector<char> buffer_;
    const char* cur_ = nullptr;     // cursor
    const char* end_ = nullptr;     // end of buffer
    std::streamsize file_size_ = 0;
};


template <typename ParseOneLineFunc>
static void ParallelParseElement(const std::vector<std::string_view>& lines, const ParseOneLineFunc& parseOneLine) {
    
    const size_t nLines =  lines.size();

    // tbb::blocked_range<size_t> r = tbb::blocked_range<size_t>(0, nLines);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, nLines), [&](const tbb::blocked_range<size_t>& r) 
    {
        // std:: cout << "parse range count: " << (r.end() - r.begin()) << "\n";
        for (size_t i = r.begin(); i != r.end(); ++i) {
            auto& line = lines[i];
            parseOneLine(line, i);
        }
    });
}

/// this is 20x faster than std::istringstream method
/// parallel 4x more faster
/// totally 80x faster
static size_t parse_fix_number_floats(const std::string_view& line, std::array<double, 16>& numbers) {
    const char* begin = line.data();
    const char* end   = begin + line.size();

    // remove comment
    if (const char* hash = static_cast<const char*>(memchr(begin, '#', end - begin))) {
        end = hash;
    }

    // trim leading spaces
    while (begin < end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }

    // trim trailing spaces
    while (end > begin && std::isspace(static_cast<unsigned char>(end[-1]))) {
        --end;
    }

    size_t count = 0;
    const char* p = begin;

    while (p < end && count < numbers.size()) {
        // skip spaces between tokens
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) {
            ++p;
        }

        if (p >= end) break;

        auto r = fast_float::from_chars(p, end, numbers[count]);
        if (r.ec != std::errc()) {
            // stop on parse error
            break;
        }

        p = r.ptr;
        ++count;
    }

    return count;
};

static size_t parse_fix_number_int32(const std::string_view& line, std::array<int64_t, 16>& numbers) {
    const char* begin = line.data();
    const char* end   = begin + line.size();

    // remove comment
    if (const char* hash = static_cast<const char*>(memchr(begin, '#', end - begin))) {
        end = hash;
    }

    // trim leading spaces
    while (begin < end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }

    // trim trailing spaces
    while (end > begin && std::isspace(static_cast<unsigned char>(end[-1]))) {
        --end;
    }

    size_t count = 0;
    const char* p = begin;

    while (p < end && count < numbers.size()) {
        // skip whitespace
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) {
            ++p;
        }

        if (p >= end) break;

        int64_t value;
        auto r = std::from_chars(p, end, value);
        if (r.ec != std::errc()) {
            // parse error
            break;
        }

        numbers[count++] = value;
        p = r.ptr;
    }

    return count;
}


// after remove tailing # comment,  each connected token is counted as one
static size_t counting_dynamic_number(const std::string_view& line) {
    const char* begin = line.data();
    const char* end   = begin + line.size();

    // remove comment
    if (const char* hash = static_cast<const char*>(memchr(begin, '#', end - begin))) {
        end = hash;
    }

    // trim leading spaces
    while (begin < end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }

    // trim trailing spaces
    while (end > begin && std::isspace(static_cast<unsigned char>(end[-1]))) {
        --end;
    }


    size_t count = 0;
    bool in_token = false;

    for (const char* p = begin; p < end; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);

        if (std::isspace(c)) {
            in_token = false;
        } else {
            // only count when entering a new token
            if (!in_token) {
                ++count;      // new token starts
                in_token = true;
            }
        }
    }

    return count;
}


static size_t parse_dynamic_number_int32(const std::string_view& line, std::vector<int64_t>& numbers) {
    const char* begin = line.data();
    const char* end   = begin + line.size();

    // remove comment
    if (const char* hash = static_cast<const char*>(memchr(begin, '#', end - begin))) {
        end = hash;
    }

    // trim leading spaces
    while (begin < end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }

    // trim trailing spaces
    while (end > begin && std::isspace(static_cast<unsigned char>(end[-1]))) {
        --end;
    }

    size_t count = 0;
    const char* p = begin;

    while (p < end) {
        // skip whitespace
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) {
            ++p;
        }

        if (p >= end) break;

        int64_t value;
        auto r = std::from_chars(p, end, value);
        if (r.ec != std::errc()) {
            // parse error
            break;
        }

        numbers.push_back(value);
        count++;
        p = r.ptr;
    }

    return count;
}


// after remove tailing # comment,  each connected token is counted as one
static size_t counting_dynamic_char(const std::string_view& line) {
    const char* begin = line.data();
    const char* end   = begin + line.size();


    // remove comment
    if (const char* hash = static_cast<const char*>(memchr(begin, '#', end - begin))) {
        end = hash;
    }

    // trim leading spaces
    while (begin < end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }

    // trim trailing spaces
    while (end > begin && std::isspace(static_cast<unsigned char>(end[-1]))) {
        --end;
    }


    size_t count = 0;
    for (const char* p = begin; p < end; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (std::isspace(c)) {
        } else {
            ++count;      // new token starts
        }
    }

    return count;
}


static size_t parse_dynamic_char(const std::string_view& line, std::vector<char>& numbers) {
    const char* begin = line.data();
    const char* end   = begin + line.size();

    // remove comment
    if (const char* hash = static_cast<const char*>(memchr(begin, '#', end - begin))) {
        end = hash;
    }

    // trim leading spaces
    while (begin < end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }

    // trim trailing spaces
    while (end > begin && std::isspace(static_cast<unsigned char>(end[-1]))) {
        --end;
    }

    size_t count = 0;
    const char* p = begin;
    
    while (p < end) {
        // skip whitespace
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) {
            ++p;
        }

        if (p >= end) break;

        char value = *p;   // <-- THIS is the parse
        numbers.push_back(value);
        ++count;
        ++p;
    }

    return count;
}



}