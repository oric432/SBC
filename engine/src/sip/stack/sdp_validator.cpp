#include "sdp_validator.hpp"

#include <pjlib.h>

#include "sip/stack/sdp_mangler.hpp"

namespace SbcEngine {

namespace {

constexpr pj_size_t kPoolInitial = 2048;
constexpr pj_size_t kPoolIncrement = 2048;

// Lazily bootstraps just enough of PJLIB to run pjmedia_sdp_parse() outside of
// any live PjsipStack. pj_init() is refcounted (safe alongside the real
// stack's own call, or a test binary's), and SDP parsing never touches
// pjsip_endpt's header-parser tables the way pjsip_parse_rdata() does, so no
// pjsip_endpoint is needed here at all — just a pool factory.
class ScopedPjInit {
public:
    ScopedPjInit() noexcept {
        pj_init();
        pj_caching_pool_init(&caching_pool_, &pj_pool_factory_default_policy, 0);
    }
    ~ScopedPjInit() {
        pj_caching_pool_destroy(&caching_pool_);
        pj_shutdown();
    }
    ScopedPjInit(const ScopedPjInit&) = delete;
    ScopedPjInit& operator=(const ScopedPjInit&) = delete;
    ScopedPjInit(ScopedPjInit&&) = delete;
    ScopedPjInit& operator=(ScopedPjInit&&) = delete;

    [[nodiscard]] pj_pool_factory* factory() const { return &caching_pool_.factory; }

private:
    mutable pj_caching_pool caching_pool_{};
};

bool is_structurally_valid(const std::string& sdp) {
    static const ScopedPjInit init;

    pj_pool_t* pool = pj_pool_create(init.factory(), "sdp_validate", kPoolInitial, kPoolIncrement, nullptr);
    if (pool == nullptr) {
        return false;
    }
    const pjmedia_sdp_session* parsed = Sdp::parse(pool, sdp);
    const bool valid = Sdp::has_valid_media(parsed);
    pj_pool_release(pool);
    return valid;
}

} // namespace

bool SdpValidator::is_valid_offer(const std::string& sdp) {
    return is_structurally_valid(sdp);
}

bool SdpValidator::is_valid_answer(const std::string& sdp) {
    return is_structurally_valid(sdp);
}

} // namespace SbcEngine
