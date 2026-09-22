// clang-format off
#pragma once

#include <optional>
#include <string>
#include <glaze/glaze.hpp>

// NOLINTBEGIN(readability-identifier-naming)

namespace SbcEngine::Protocols {
// API routes client endpoints.
namespace ClientApiEndpoints {
// Websocket channel the engine connects to for its route/user snapshot and
// live updates (see ControlPlaneClient). /api/b2bua/routes still exists on
// the control plane as a curl-able debug read, but the engine no longer
// calls it -- everything it needs arrives over this connection instead.
constexpr std::string_view kEngineWs = "/ws/engine";
}

struct ApiError {
    std::string message;
    std::string detail;

    struct glaze_json_schema {
        glz::schema message{.description = "Error message description"};
        glz::schema detail{.description = "Detailed error message"};
    };
};

template<typename T>
struct ApiResponse {
    bool success{};
    std::optional<ApiError> error;
    std::optional<T> data;

    struct glaze_json_schema {
        glz::schema success{.description = "Indicates if the API request was successful"};
        glz::schema error{.description = "Optional error details if the request failed"};
        glz::schema data{.description = "Optional response payload", .type = "object"};
    };
};

} // namespace SbcEngine::Protocols

// NOLINTEND(readability-identifier-naming)
