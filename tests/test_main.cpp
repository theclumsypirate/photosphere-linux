#include <iostream>
#include <string>
#include <vector>

namespace ps {
int runRotationTests();
int runSphericalTests();
int runLensModelTests();
int runAmbisonicsTests();
} // namespace ps

int main() {
    int failures = 0;
    const std::vector<std::pair<std::string, int (*)()>> suites = {
        {"rotation", ps::runRotationTests},
        {"spherical", ps::runSphericalTests},
        {"lens_model", ps::runLensModelTests},
        {"ambisonics", ps::runAmbisonicsTests},
    };
    for (const auto& [name, fn] : suites) {
        const int failed = fn();
        std::cout << (failed == 0 ? "PASS " : "FAIL ") << name << "\n";
        failures += failed;
    }
    return failures == 0 ? 0 : 1;
}
