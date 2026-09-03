#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <fast_float/fast_float.h>
#include <nanothread/nanothread.h>

#include "fastnumparse.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <array>
#include <charconv>
#include <execution>

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
    FastLineReader(const FastLineReader&) = delete;
    FastLineReader& operator=(const FastLineReader&) = delete;

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

    void setPosition(const std::size_t offset) {
        const auto length = static_cast<std::size_t>(end_ - begin_);
        if (offset > length) {
            throw std::out_of_range("offset exceeds buffer size");
        }
        cur_ = begin_ + offset;
    }

    std::size_t position() const noexcept {
        return static_cast<std::size_t>(cur_ - begin_);
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
    const int32_t cores = static_cast<int32_t>(core_count());
    maxThreads = maxThreads <= 0 ? cores - 1 : maxThreads;
    maxThreads = std::clamp(maxThreads, 1, cores);
    std::unique_ptr<Pool, decltype(&pool_destroy)> pool(
        pool_create(static_cast<uint32_t>(maxThreads)),
        &pool_destroy
    );

    // task count
    const size_t nLines =  lines.size();

    // dr::blocked_range<size_t> r = dr::blocked_range<size_t>(0, nLines);
    dr::parallel_for(dr::blocked_range<size_t>(0, nLines, 1 * 8192), [&](dr::blocked_range<size_t> r)
    {
        // std::cout << "parse range count: " << (r.end() - r.begin()) << "\n";
        for (size_t i = r.begin(); i != r.end(); ++i) {
            auto& line = lines[i];
            parseOneLine(line, i);
        }    
    }, pool.get());
    // std::cout << "block size:" << 1024*16 << std::endl;
}

/// this is 20x faster than std::istringstream method
/// parallel 4x more faster
/// totally 80x faster
template<typename T>
static size_t parse_fix_number_floats(const std::string_view& line, T* numbers, size_t capacity) {
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

    while (p < end && count < capacity) {
        // skip spaces between tokens
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) {
            ++p;
        }

        if (p >= end) break;

        // parse
        auto r = fast_float::from_chars(p, end, numbers[count]);
        if (r.ec != std::errc()) {
            // stop on parse error
            break;
        }

        // marching
        p = r.ptr;
        ++count;
    }

    return count;
};

template<typename T>
static size_t parse_fix_number_int64(const std::string_view& line, T* numbers, size_t capacity) {
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

    while (p < end && count < capacity) {
        // skip whitespace
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) {
            ++p;
        }

        if (p >= end) break;

        // parse
        int64_t value;
        auto r = std::from_chars(p, end, value);
        if (r.ec != std::errc()) {
            // parse error
            break;
        }

        // marching
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

template<typename T>
static size_t parse_dynamic_number_float(const std::string_view& line, T* numbers) {
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

        // parse
        auto r = fast_float::from_chars(p, end, numbers[count]);
        if (r.ec != std::errc()) {
            // stop on parse error
            break;
        }

        // marching
        p = r.ptr;
        ++count;
    }

    return count;
}


template<typename T>
static size_t parse_dynamic_number_int64(const std::string_view& line, T* numbers) {
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

        // parse
        int64_t value;
        auto r = std::from_chars(p, end, value);
        if (r.ec != std::errc()) {
            // parse error
            break;
        }

        // marching
        numbers[count++] = value;
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


static size_t parse_dynamic_char(const std::string_view& line, char* numbers) {
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
        // numbers.push_back(value);
        *(numbers++) = value;
        ++count;
        ++p;
    }

    return count;
}

template<typename T>
static std::vector<T> parse_fix_column_buffer_as_vector(
    FastLineReader& infile,
    const std::string& comment,
    std::size_t maxRows,
    std::size_t columnCount,
    std::int32_t maxThreads = 16,
    bool verbose = false
) {
    const auto totalStart = std::chrono::steady_clock::now();

    if (columnCount == 0) {
        throw py::value_error("columnCount cannot be zero");
    }

    // TODO: parse_fix_number_xxx pass into buffer instead of std::array
    // so we no column count limit
    if (columnCount > 16) {
        throw py::value_error("columnCount cannot exceed 16");
    }


    // phase 1: parse to lines
    std::string_view line;
    std::vector<std::string_view> lines;
    lines.reserve(maxRows);
    while (lines.size() < maxRows) {
        infile.getline(line);
        lines.push_back(line);
    }
    const auto collectLinesEnd = std::chrono::steady_clock::now();

    // phase 2: parse to number
    std::vector<T> values(lines.size() * columnCount);
    const auto allocateValuesEnd = std::chrono::steady_clock::now();

    // this is 20x faster than std::istringstream method
    // parallel 4x more faster
    // totally 80x faster
    ParallelParseElement(lines, maxThreads, [&](const std::string_view& inputLine, size_t i) {
        if constexpr (std::is_floating_point<T>::value) {
            std::array<double, 16> numbers;
            auto ret = parse_fix_number_floats(inputLine, numbers.data(), numbers.size());
            if (ret != columnCount) {
                std::cerr << "Fatal error: parse line " << i << " columnCount mismatch, expected "
                            << columnCount << " got " << ret << ".\n";
                throw std::runtime_error(
                    "parse line " + std::to_string(i) +
                    " column count mismatch: expected " +
                    std::to_string(columnCount) + ", got " +
                    std::to_string(ret)
                );
            }
            for (size_t j = 0; j < columnCount; ++j) {
                values[i * columnCount + j] = static_cast<T>(numbers[j]);
            }
        } else if constexpr (std::is_integral<T>::value) {
            std::array<int64_t, 16> numbers;
            auto ret = parse_fix_number_int64(inputLine, numbers.data(), numbers.size());
            if (ret != columnCount) {
                std::cerr << "Fatal error: parse line " << i << " columnCount mismatch, expected "
                            << columnCount << " got " << ret << ".\n";
                throw std::runtime_error(
                    "parse line " + std::to_string(i) +
                    " column count mismatch: expected " +
                    std::to_string(columnCount) + ", got " +
                    std::to_string(ret)
                );
            }
            for (size_t j = 0; j < columnCount; ++j) {
                // if (numbers[j] < std::numeric_limits<T>::min() ||
                //     numbers[j] > std::numeric_limits<T>::max()) {
                //     throw std::out_of_range(
                //         "integer value is outside the requested dtype range");
                // }
                values[i * columnCount + j] = static_cast<T>(numbers[j]);
            }
        }
    });

    const auto parallelParseEnd = std::chrono::steady_clock::now();
    const auto elapsedMilliseconds = [](const auto& start, const auto& end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    };

    if (verbose) {
        std::cout
            << "parse_fix_column_buffer_as_vector:\n"
            << "  collect lines:   "
            << elapsedMilliseconds(totalStart, collectLinesEnd) << " ms\n"
            << "  allocate values: "
            << elapsedMilliseconds(collectLinesEnd, allocateValuesEnd) << " ms\n"
            << "  parallel parse:  "
            << elapsedMilliseconds(allocateValuesEnd, parallelParseEnd) << " ms\n"
            << "  total:           "
            << elapsedMilliseconds(totalStart, parallelParseEnd) << " ms\n";
    }

    return values;
}

template<typename T>
static std::pair<py::array, std::size_t> parse_fix_column_buffer_as(
    FastLineReader& infile,
    const std::string& comment,
    std::size_t maxRows,
    std::size_t columnCount,
    std::int32_t ndmin = 0,
    std::int32_t maxThreads = 16
) {
    if (ndmin < 0 || ndmin > 2) {
        throw py::value_error("ndmin must be 0, 1, or 2");
    }

    // phase 1: parse to lines
    // phase 2: parse to number
    auto values = parse_fix_column_buffer_as_vector<T>(
        infile, comment, maxRows, columnCount, maxThreads);

    // phase 3: convert to np.array
    std::vector<py::ssize_t> shape;
    if (ndmin < 2) {
        shape.push_back(static_cast<py::ssize_t>(values.size()));
    } else {
        shape.push_back(static_cast<py::ssize_t>(values.size() / columnCount));
        shape.push_back(static_cast<py::ssize_t>(columnCount));
    }

    py::array_t<T> result(shape);
    std::copy(values.begin(), values.end(), result.mutable_data());
    
    return {std::move(result), infile.position()};
}

// this like csv data with space delimiter and # comment
// each line fix column number, 
// delimiter must be space
// comment at tail and must start with #
// not include line with end symbol such as }
std::pair<py::array, std::size_t> from_string_buffer_csv(
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

    // TODO: comment must be '#'
    if (comment != "#") {
        throw py::value_error("comment must be #");
    }

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
    FastLineReader reader(buffer, buffer_size);
    reader.setPosition(offset);

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
            reader, comment, maxRows, columnCount, ndmin, maxThreads);
    }

    throw py::type_error("unsupported dtype");
}


// this like csv data with space delimiter and # comment
// each line fix column number, 
// delimiter must be space
// comment at tail and must start with #
// not include line with end symbol such as }
template<typename T>
std::vector<T> from_file_csv(
    const std::string& file,
    const std::string& comment,
    std::size_t maxRows,
    std::size_t columnCount,
    std::int32_t maxThreads,
    bool verbose
) {
    // TODO: comment must be '#'
    if (comment != "#") {
        throw py::value_error("comment must be #");
    }

    const auto readStart = std::chrono::steady_clock::now();

    FastLineReader infile(file);
    
    const auto readEnd = std::chrono::steady_clock::now();
    const double readMilliseconds =
        std::chrono::duration<double, std::milli>(readEnd - readStart).count();
    if (verbose) {
        std::cout << "from_file_csv FastLineReader: "
                << readMilliseconds << " ms\n";
    }

    auto values = parse_fix_column_buffer_as_vector<T>(
        infile, comment, maxRows, columnCount, maxThreads, verbose);
    return values;
}

template FAST_NUM_PARSE_API std::vector<double> from_file_csv<double>(
    const std::string&,
    const std::string&,
    std::size_t,
    std::size_t,
    std::int32_t,
    bool
);

template FAST_NUM_PARSE_API std::vector<std::int64_t> from_file_csv<std::int64_t>(
    const std::string&,
    const std::string&,
    std::size_t,
    std::size_t,
    std::int32_t,
    bool
);

template<typename T>
static std::vector<T> parse_dynamic_column_buffer_as_vector(
    FastLineReader& infile,
    const std::string& comment,
    std::string endChar, // 
    std::size_t nelement,
    std::int32_t maxThreads = 16,
    bool verbose = false
) {

    const auto totalStart = std::chrono::steady_clock::now();

    // phase 1: parse to lines

    // Reach until meet }, end of Locations section
    std::string_view line;
    std::vector<std::string_view> lines;
    lines.reserve(nelement);
    bool endCharFound = false;
    size_t previousPosition = infile.position();
    while (!endCharFound && infile.getline(line)) {
        if (line.find(endChar) != std::string::npos) {
            infile.setPosition(previousPosition);  // rewind to the previous position
            endCharFound = true;
        } else {
            previousPosition = infile.position();  // save the current position before reading the next line
            lines.push_back(line);
        }
    }

    const auto collectLinesEnd = std::chrono::steady_clock::now();

    // phase 2: parse to number
    std::vector<T> values(nelement);
    const auto allocateValuesEnd = std::chrono::steady_clock::now();

    // counting pass
    std::vector<int64_t> lineElementCounts;
    lineElementCounts.resize(lines.size());
    ParallelParseElement(lines, maxThreads, [&](const std::string_view& line, size_t i) {
        size_t numElements = 0;
        if constexpr (std::is_same<T, char>::value) {
            numElements = counting_dynamic_char(line);
        } else if constexpr (std::is_floating_point<T>::value || std::is_integral<T>::value) {
            numElements = counting_dynamic_number(line);
        }
        lineElementCounts[i] = static_cast<int64_t>(numElements);
    });

    // parse pass
    std::vector<int64_t> lineElementOffset(lines.size() + 1, 0);
    std::inclusive_scan(std::execution::par, lineElementCounts.begin(), lineElementCounts.end(), lineElementOffset.begin() + 1);
    ParallelParseElement(lines, maxThreads, [&](const std::string_view& line, size_t i) {
        const auto offset = lineElementOffset[i];
        T* numbers = &values[offset];

        if constexpr (std::is_same<T, char>::value) {
            parse_dynamic_char(line, numbers);
        } else if constexpr (std::is_floating_point<T>::value) {
            parse_dynamic_number_float(line, numbers);
        } else if constexpr (std::is_integral<T>::value) {
            parse_dynamic_number_int64(line, numbers);
        }
        // std::cout << "location: line " << i << " parse " << numElements << " elements.\n";        
    });


    const auto parallelParseEnd = std::chrono::steady_clock::now();
    const auto elapsedMilliseconds = [](const auto& start, const auto& end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    };

    if (verbose) {
        std::cout
            << "parse_fix_column_buffer_as_vector:\n"
            << "  collect lines:   "
            << elapsedMilliseconds(totalStart, collectLinesEnd) << " ms\n"
            << "  allocate values: "
            << elapsedMilliseconds(collectLinesEnd, allocateValuesEnd) << " ms\n"
            << "  parallel parse:  "
            << elapsedMilliseconds(allocateValuesEnd, parallelParseEnd) << " ms\n"
            << "  total:           "
            << elapsedMilliseconds(totalStart, parallelParseEnd) << " ms\n";
    }

    return values;
}

template<typename T>
static std::pair<py::array, std::size_t> parse_dynamic_column_buffer_as(
    FastLineReader& infile,
    const std::string& comment,
    std::string endChar, // 
    std::size_t nelement,
    std::int32_t maxThreads = 16
) {
    // phase 1: parse to lines
    // phase 2: parse to number
    auto values = parse_dynamic_column_buffer_as_vector<T>(
        infile, comment, endChar, nelement, maxThreads);

    // phase 3: convert to np.array
    std::vector<py::ssize_t> shape;
    shape.push_back(static_cast<py::ssize_t>(values.size()));

    py::array_t<T> result(shape);
    std::copy(values.begin(), values.end(), result.mutable_data());
    
    return {std::move(result), infile.position()};
}


// this not like csv, 
// but like many data with many rows, 
// the total number count known, but number count per row unknown.
// delimiter must be space
// comment at tail and must start with #
// parse line until meet endChar
std::pair<py::array, std::size_t> from_string_buffer_noncsv(
    py::buffer input,
    std::size_t offset,
    py::object dtypeArg,
    const std::string& comment,
    std::string endChar, // 
    std::size_t nelement,
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

    if (dtype.is(py::dtype::of<char>())) {
        return parse_dynamic_column_buffer_as<char>(
            reader, comment, endChar, nelement, maxThreads);
    }

    if (dtype.is(py::dtype::of<double>())) {
        return parse_dynamic_column_buffer_as<double>(
            reader, comment, endChar, nelement, maxThreads);
    }

    if (dtype.is(py::dtype::of<float>())) {
        return parse_dynamic_column_buffer_as<float>(
            reader, comment, endChar, nelement, maxThreads);
    }

    if (dtype.is(py::dtype::of<std::int64_t>())) {
        return parse_dynamic_column_buffer_as<std::int64_t>(
            reader, comment, endChar, nelement, maxThreads);
    }

    if (dtype.is(py::dtype::of<std::int32_t>())) {
        return parse_dynamic_column_buffer_as<std::int32_t>(
            reader, comment, endChar, nelement, maxThreads);
    }

    throw py::type_error("unsupported dtype");
}


// this not like csv, 
// but like many data with many rows, 
// the total number count known, but number count per row unknown.
// delimiter must be space
// comment at tail and must start with #
// parse line until meet endChar
template<typename T>
std::vector<T> from_file_noncsv(
    const std::string& file,
    const std::string& comment,
    std::string endChar, // 
    std::size_t nelement,
    std::int32_t maxThreads,
    bool verbose
) {
    // TODO: comment must be '#'
    if (comment != "#") {
        throw py::value_error("comment must be #");
    }

    const auto readStart = std::chrono::steady_clock::now();

    FastLineReader infile(file);
    
    const auto readEnd = std::chrono::steady_clock::now();
    const double readMilliseconds =
        std::chrono::duration<double, std::milli>(readEnd - readStart).count();
    if (verbose) {
        std::cout << "from_file_csv FastLineReader: "
                << readMilliseconds << " ms\n";
    }

    auto values = parse_dynamic_column_buffer_as_vector<T>(
        infile, comment, endChar, nelement, maxThreads, verbose);
    return values;
}


template FAST_NUM_PARSE_API std::vector<double> from_file_noncsv(
    const std::string& file,
    const std::string& comment,
    std::string endChar, // 
    std::size_t nelement,
    std::int32_t maxThreads,
    bool verbose
);

template FAST_NUM_PARSE_API std::vector<int64_t> from_file_noncsv(
    const std::string& file,
    const std::string& comment,
    std::string endChar, // 
    std::size_t nelement,
    std::int32_t maxThreads,
    bool verbose
);

template FAST_NUM_PARSE_API std::vector<char> from_file_noncsv(
    const std::string& file,
    const std::string& comment,
    std::string endChar,
    std::size_t nelement,
    std::int32_t maxThreads,
    bool verbose
);


} // namespace fastnumparse

PYBIND11_MODULE(_fastnumparse, module) {
    module.doc() = "Native implementation for fastnumparse";

    module.def(
        "_native_version",
        []() { return FASTNUMPARSE_VERSION; },
        "Return the version compiled into the native extension."
    );

    module.def(
        "from_string_buffer_csv",
        &fastnumparse::from_string_buffer_csv,
        py::arg("input"),
        py::arg("offset"),
        py::arg("dtype"),
        py::arg("comment"),
        py::arg("max_rows"),
        py::arg("column_count"),
        py::arg("ndmin"),
        py::arg("max_threads") = 16,
        "Parse csv like data from string buffer, fix rows and column, space delimeter and # comment."
    );

    module.def(
        "from_string_buffer_noncsv",
        &fastnumparse::from_string_buffer_noncsv,
        py::arg("input"),
        py::arg("offset"),
        py::arg("dtype"),
        py::arg("comment"),
        py::arg("end_char"),
        py::arg("nelement"),
        py::arg("max_threads") = 16,
        "Parse non-CSV data with a known total element count from a string buffer, parse line until meet end char."
    );
}
