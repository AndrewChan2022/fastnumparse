#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(fastnumparse, module) {
    module.doc() = "Native implementation for fastnumparse";
    module.def(
        "_native_version",
        []() { return FASTNUMPARSE_VERSION; },
        "Return the version compiled into the native extension."
    );
}
