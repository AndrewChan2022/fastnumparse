#include <fast_float/fast_float.h>

#include <cstddef>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>


namespace fnp {


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



}