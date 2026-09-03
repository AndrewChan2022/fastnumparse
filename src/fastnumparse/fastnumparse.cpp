#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <fast_float/fast_float.h>
#include <nanothread/nanothread.h>

#include <cstddef>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <charconv>

namespace py = pybind11;
namespace dr = drjit;

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

        begin_ = buffer_.data();
        cur_ = begin_;
        end_ = begin_ + file_size_;
    }

    explicit FastLineReader(const char* buffer, const size_t len) {
        if (buffer == nullptr && len != 0) {
            throw std::invalid_argument(
                "buffer must not be null when len is nonzero");
        }

        // Borrow the caller's memory. It must remain alive while this reader
        // is in use.
        // no extra \0 needed
        begin_ = buffer != nullptr ? buffer : "";
        cur_ = begin_;
        end_ = begin_ + len;
    }

    void set_position(const std::size_t offset) {
        const auto length = static_cast<std::size_t>(end_ - begin_);
        if (offset > length) {
            throw std::out_of_range("offset exceeds buffer size");
        }
        cur_ = begin_ + offset;
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
    const char* begin_ = nullptr;   // beginning of the full buffer
    const char* cur_ = nullptr;     // cursor
    const char* end_ = nullptr;     // end of buffer
    std::streamsize file_size_ = 0;
};


template <typename ParseOneLineFunc>
static void ParallelParseElement(
    const std::vector<std::string_view>& lines, 
    int32_t maxThreads, 
    const ParseOneLineFunc& parseOneLine
) {
    // if maxThread == 0, max cpu core
    maxThreads = maxThreads <= 0 ? core_count() - 1 : maxThreads;
    maxThreads = maxThreads <= 0 ? 1 : maxThreads;
    std::unique_ptr<Pool, decltype(&pool_destroy)> pool(
        pool_create(static_cast<uint32_t>(maxThreads)),
        &pool_destroy
    );

    // task count
    const size_t nLines =  lines.size();

    // dr::blocked_range<size_t> r = dr::blocked_range<size_t>(0, nLines);
    dr::parallel_for(dr::blocked_range<size_t>(0, nLines), [&](dr::blocked_range<size_t> r)
    {
        // std:: cout << "parse range count: " << (r.end() - r.begin()) << "\n";
        for (size_t i = r.begin(); i != r.end(); ++i) {
            auto& line = lines[i];
            parseOneLine(line, i);
        }    
    }, pool.get());
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

template<typename T>
static py::array parse_fix_column_buffer_as(
    FastLineReader& infile,
    const std::string& comment,
    std::size_t maxRows,
    std::size_t columnCount,
    std::int32_t ndmin,
    std::int32_t maxThreads = 16
) {
    
    std::vector<std::string_view> vertexLines;
    vertexLines.reserve(mInfo.nb_vertices);
    for (size_t i = 0; i < mInfo.nb_vertices; ++i) {
        infile.getline(line);
        vertexLines.push_back(line);
        // auto hashPos = line.find('#');
        // if (hashPos != std::string::npos) line = line.substr(0, hashPos);
        // std::istringstream ss(line);
        // for (size_t j = 0; j < mInfo.dimension; ++j) {
        //     ss >> mGrid.vertices[static_cast<uint64_t>(i)][static_cast<uint64_t>(j)];
        // }
    }


    // this is 20x faster than below std::istringstream method
    // parallel 4x more faster
    // totally 80x faster
    ParallelParseElement(vertexLines, maxThreads, [&](const std::string_view& line, size_t i) {
        std::array<double, 16> numbers;
        auto ret = parse_fix_number_floats(line, numbers);
        if (ret != mInfo.dimension) {
            std::cerr << "Fatal error: parse vertex line " << i << " dimension mismatch, expected "
                        << mInfo.dimension << " got " << ret << ".\n";
            throw std::runtime_error("parse vertex line dimension mismatch");
        }
        for (size_t j = 0; j < mInfo.dimension; ++j) {
            mGrid.vertices[i].coords[j] = numbers[j];
        }

        // auto hashPos = line.find('#');
        // if (hashPos != std::string::npos) line = line.substr(0, hashPos);
        // stripComment(line);
        // trim(line);
        // std::istringstream ss(line);
        // for (size_t j = 0; j < mInfo.dimension; ++j) {
        //     ss >> mGrid.vertices[i].coords[j];
        // }
    });
}

// this like csv data with space delimiter
// each line fix column number, delimiter must be space
// may have comment at tail
// no other things like } at tail
// return new position                      ---
py::array parse_fix_column_buffer(
    py::buffer input,
    std::size_t offset,
    py::object dtypeArg,
    const std::string& comment,
    std::size_t maxRows,
    std::size_t columnCount,
    std::int32_t ndmin,
    std::int32_t maxThreads = 16
) {
    const py::buffer_info info = input.request();

    // This parser expects a contiguous byte buffer.
    if (info.ndim != 1 || info.itemsize != 1 || info.strides[0] != 1) {
        throw py::value_error(
            "buffer must be a contiguous one-dimensional byte buffer");
    }

    const auto buffer_size = static_cast<std::size_t>(info.size);

    if (offset > buffer_size) {
        throw py::value_error("offset exceeds buffer size");
    }

    const auto* buffer = static_cast<const char*>(info.ptr);
    const char* begin = buffer + offset;
    const std::size_t length = buffer_size - offset;

    FastLineReader reader(begin, length);

    const py::dtype dtype = py::dtype::from_args(dtypeArg);

    if (dtype.is(py::dtype::of<double>())) {
        return parse_fix_column_buffer_as<double>(
            reader, comment, maxRows, columnCount, ndmin, maxThreads);
    }

    if (dtype.is(py::dtype::of<float>())) {
        return parse_fix_column_buffer_as<float>(
            reader, comment, maxRows, columnCount, ndmin, maxThreads);
    }

    if (dtype.is(py::dtype::of<std::int64_t>())) {
        return parse_fix_column_buffer_as<std::int64_t>(
            reader, comment, maxRows, columnCount, ndmin, maxThreads);
    }

    if (dtype.is(py::dtype::of<std::int32_t>())) {
        return parse_fix_column_buffer_as<std::int32_t>(
            reader, comment, maxRows, columnCount, ndmin);
    }

    throw py::type_error("unsupported dtype");
}




// this like csv data with space delimiter, 
// but each line may contain half row data or many row data
// delimiter must be space
// may have comment at tail
// no other things like } at tail
// return new position                      ---
py::array parse_dynamic_column_buffer(
    py::buffer input,
    std::size_t offset,
    py::object dtypeArg,
    const std::string& comment,
    std::size_t maxRows,
    std::string endChar, // 
    std::int32_t ndmin,
    std::int32_t maxThreads = 16
) {
    const py::buffer_info info = input.request();

    // This parser expects a contiguous byte buffer.
    if (info.ndim != 1 || info.itemsize != 1 || info.strides[0] != 1) {
        throw py::value_error(
            "buffer must be a contiguous one-dimensional byte buffer");
    }

    const auto buffer_size = static_cast<std::size_t>(info.size);

    if (offset > buffer_size) {
        throw py::value_error("offset exceeds buffer size");
    }

    const auto* buffer = static_cast<const char*>(info.ptr);
    const char* begin = buffer + offset;
    const std::size_t length = buffer_size - offset;

    FastLineReader reader(begin, length);

    const py::dtype dtype = py::dtype::from_args(dtypeArg);

    if (dtype.is(py::dtype::of<double>())) {
        return parse_dynamic_column_buffer_as<double>(
            reader, comment, maxRows, ndmin);
    }

    if (dtype.is(py::dtype::of<float>())) {
        return parse_dynamic_column_buffer_as<float>(
            reader, comment, maxRows, ndmin);
    }

    if (dtype.is(py::dtype::of<std::int64_t>())) {
        return parse_dynamic_column_buffer_as<std::int64_t>(
            reader, comment, maxRows, ndmin);
    }

    if (dtype.is(py::dtype::of<std::int32_t>())) {
        return parse_dynamic_column_buffer_as<std::int32_t>(
            reader, comment, maxRows, ndmin);
    }

    throw py::type_error("unsupported dtype");
}



}
