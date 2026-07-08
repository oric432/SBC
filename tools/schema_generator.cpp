#include <exception>
#include <iostream>
#include <filesystem>
#include <glaze/glaze.hpp>
#include <print>
#include "protocols/SipRoutes.hpp"

using namespace ::SbcEngine::Protocols;
namespace Fs = std::filesystem;


namespace {

template <typename T>
void export_schema(const Fs::path& output_path) {
    Fs::create_directories(output_path.parent_path());

    constexpr auto kopts = glz::opts{.prettify = true, .error_on_missing_keys = true};

    auto schema_result = glz::write_json_schema<T, kopts>();

    if (schema_result) {
        std::ofstream file(output_path);
        file << schema_result.value() << "\n";
        std::cout << "[SUCCESS] Wrote formatted schema to: " << output_path.string() << "\n";
    }
    else {
        std::cerr << "[FAILED] Could not generate schema for " << output_path.string() << "\n";
    }
}
} // namespace

int main() {
    try {
        const Fs::path output_directory = "schemas/b2bua";

        export_schema<SipRouteRule>(output_directory / "sip_route_rule.json");
        export_schema<SipRouteSnapshot>(output_directory / "sip_route_snapshot.json");
        export_schema<SipRouteUpdate>(output_directory / "sip_route_update.json");


    } catch (const std::exception& err) {
        std::println("Failed to generate schemas {}", err.what());
    }

    return 0;
}
