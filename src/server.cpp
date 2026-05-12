#include "server.hpp"
#include <functional>


void
typhon::Server::run() noexcept {
    auto stopped = State::Stopped;
    if (!state_.compare_exchange_strong(stopped, State::Starting)) {
        return;
    }

    int n = std::thread::hardware_concurrency();
    n = n > 1 ? n - 1 : 1;

    for (int i = 0; i < n; ++i) {
        servers_.emplace_back(std::make_unique<KcpServer>(host_.c_str()));
        threads_.emplace_back(std::thread(std::bind(&KcpServer::run, servers_[i].get())));
    }

    state_.store(State::Running);

    for (auto& t : threads_) {
        t.join();
    }

    servers_.clear();
    threads_.clear();

    state_.store(State::Stopped);
}


void
typhon::Server::stop() noexcept {
    auto running = State::Running;
    if (!state_.compare_exchange_strong(running, State::Stopping)) {
        return;
    }

    for (auto& s : servers_) {
        s->stop();
    }
}