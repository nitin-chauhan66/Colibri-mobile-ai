#include "colibri_mobile/engine.hpp"
#include "colibri_mobile/simd.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

int main() {
    const auto path =
        std::filesystem::temp_directory_path() / "colibri_mobile_expert.bin";
    {
        std::ofstream output(path, std::ios::binary);
        for (unsigned char value = 0; value < 32; ++value) {
            output.put(static_cast<char>(value));
        }
    }

    colibri::mobile::MemoryManager memory(256 * 1024 * 1024);
    memory.registerBaseResident(1024 * 1024);
    colibri::mobile::TensorRouter router(
        memory, {{"expert-0", path.string(), 3, 8}}, 2);
    auto mapping = router.route("expert-0");
    assert(mapping->bytes().size() == 8);
    assert(std::to_integer<unsigned char>(mapping->bytes()[0]) == 3);
    mapping.reset();

    const std::vector<std::uint32_t> tokens{1, 2, 3, 4};
    colibri::mobile::DualThreadInference loop(router);
    const auto metrics = loop.run(
        tokens, [](std::uint32_t) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        },
        [](std::uint32_t) { return std::string("expert-0"); });
    assert(metrics.tokens == tokens.size());
    assert(metrics.tokens_per_second > 0.0);
    assert(colibri::mobile::dotProductF32(nullptr, nullptr, 0) == 0.0F);

    std::filesystem::remove(path);
    return 0;
}
