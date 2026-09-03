#pragma once
#include <cstddef>
#include <vector>
#include <string>

namespace fastnumparse {

template<typename T>
std::vector<T> from_file_csv(
    const std::string& file,
    const std::string& comment,
    std::size_t maxRows,
    std::size_t columnCount,
    std::int32_t ndmin,
    std::int32_t maxThreads = 16
);

}