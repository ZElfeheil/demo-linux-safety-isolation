#include <expected>
#include <iostream>
#include <string>

std::expected<int, std::string> test_fn(bool ok) {
    if (ok) return 42;
    return std::unexpected("error");
}

int main() {
    auto res = test_fn(true);
    if (res) {
        std::cout << "Success: " << *res << std::endl;
    }
    return 0;
}
