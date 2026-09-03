#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

#if defined(_MSC_VER)
    #if defined(FAST_NUM_PARSE_EXPORTS)
        // Building the DLL
        #define FAST_NUM_PARSE_API __declspec(dllexport)
    #else
        // Using the DLL
        #define FAST_NUM_PARSE_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #if defined(FAST_NUM_PARSE_EXPORTS)
        #define FAST_NUM_PARSE_API __attribute__((visibility("default")))
    #else
        #define FAST_NUM_PARSE_API
    #endif
#else
    #define FAST_NUM_PARSE_API
#endif


namespace fastnumparse {

// this api only for c++ test, not for python
template<typename T>
FAST_NUM_PARSE_API std::vector<T> from_file_csv(
    const std::string& file,
    const std::string& comment,
    std::size_t maxRows,
    std::size_t columnCount,
    std::int32_t maxThreads = 16
);

}
