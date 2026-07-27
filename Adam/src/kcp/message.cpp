#include "kcp/message.hpp"


adam::kcp::Message::~Message() noexcept {
    if (arg.ptr == nullptr) {
        return;
    }

    switch (type) {
    case Type::EnsureBackend: {
        auto* p = (EnsureBackendArg*)arg.ptr;
        delete p;
    } break;

    case Type::ForwardToSession: {
        auto* p = (ForwardToSessionArg*)arg.ptr;
        delete p;
    } break;
        
    default: break;
    }
}