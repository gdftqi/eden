#include "tcp_server.hpp"


int
typhon::TcpServer::run() noexcept {
    State expected = State::Stopped;
    if (!state_.compare_exchange_strong(expected, State::Starting)) {
        return -1;
    }

    // TODO
}


int
typhon::TcpServer::init() noexcept {

}


void
typhon::TcpServer::release() noexcept {
    
}