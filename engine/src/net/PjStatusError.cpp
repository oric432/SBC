#include "PjStatusError.hpp"

#include <array>

namespace SbcEngine {

std::string pj_status_str(pj_status_t status) {
    std::array<char, PJ_ERR_MSG_SIZE> buf{};
    pj_strerror(status, buf.data(), buf.size());
    return {buf.data()};
}

Error pj_error(const std::string& what, pj_status_t status) {
    return Error("{}: {}", what, pj_status_str(status));
}

} // namespace SbcEngine
