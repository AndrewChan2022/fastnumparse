#include <fastnumparse.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
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
    const auto start = std::chrono::steady_clock::now();
    const std::vector<T> values = fastnumparse::from_file_csv<T>(
        file.string(), "#", rows, columns, 16, true);
    const auto end = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(end - start).count();
    const double milliseconds = seconds * 1000.0;
    const double mebibytes = static_cast<double>(fs::file_size(file))
        / (1024.0 * 1024.0);
    const double throughput = mebibytes / seconds;
    const std::size_t expectedSize = rows * columns;

    if (values.size() != expectedSize) {
        std::cerr << file.filename().string()
                  << ": expected " << expectedSize
                  << " values, got " << values.size() << '\n';
        return false;
    }

    std::cout << file.filename().string()
              << ": parsed " << values.size() << " values in "
              << std::fixed << std::setprecision(2) << milliseconds
              << " ms (" << throughput << " MiB/s)\n";
    return true;
}

int main(int argc, char* argv[]) {
    const fs::path dataDir = argc > 1
        ? fs::path(argv[1])
        : fs::path(FASTNUMPARSE_DATA_DIR);

    bool passed = true;
    passed &= test_csv<double>(dataDir / "csv_float_244768x3.txt", 244768, 3);
    passed &= test_csv<std::int64_t>(dataDir / "csv_int_2854577x4.txt", 2854577, 4);

    return passed ? 0 : 1;
}
