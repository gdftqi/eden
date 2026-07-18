#include "moses.hpp"


// static int
// h1(adam::kcp::Session::Ptr s, adam::core::Package* pk) noexcept {
//     return 0;
// }


// static int
// h2(adam::kcp::Session::Ptr s, adam::core::Package* pk) noexcept {
//     return 0;
// }


// static int
// h3(adam::kcp::Session::Ptr s, adam::core::Package* pk) noexcept {
//     return 0;
// }


KCP_SERVER_MAIN(
    Moses,
    "config.yml",
    // KCP_PK_HANDLE(PKID_CUSTOM + 1, h1)
    // KCP_PK_HANDLE(PKID_CUSTOM + 2, h2)
    // KCP_PK_HANDLE(PKID_CUSTOM + 3, h3)
)