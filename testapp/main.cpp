#include <fastnumparse.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

template<typename T>
bool test_csv(
    const fs::path& file,
    std::size_t rows,
    std::size_t columns
) {
    const std::vector<T> values = fastnumparse::from_file_csv<T>(
        file.string(), "#", rows, columns, 0);
    const std::size_t expectedSize = rows * columns;

    if (values.size() != expectedSize) {
        std::cerr << file.filename().string()
                  << ": expected " << expectedSize
                  << " values, got " << values.size() << '\n';
        return false;
    }

    std::cout << file.filename().string()
              << ": parsed " << values.size() << " values\n";
    return true;
}

int main(int argc, char* argv[]) {
    const fs::path dataDir = argc > 1
        ? fs::path(argv[1])
        : fs::path(FASTNUMPARSE_DATA_DIR);

    bool passed = true;
    passed &= test_csv<double>(
        dataDir / "csv_float_24608x3.txt", 24608, 3);
    passed &= test_csv<std::int64_t>(
        dataDir / "csv_int_1689166x2.txt", 1689166, 2);

    return passed ? 0 : 1;
}
