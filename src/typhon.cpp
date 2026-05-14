#include "typhon.hpp"
#include <functional>


void
typhon::TyService::run() noexcept {
    auto stopped = State::Stopped;
    if (!state_.compare_exchange_strong(stopped, State::Starting)) {
        return;
    }

    uint32_t n = std::thread::hardware_concurrency();
    n = n > 1 ? n - 1 : 1;

    // 加载 BPF 程序，并把 num_workers 写进 .rodata
    int rc = router_.init(bpf_obj_path_.c_str(), n);
    if (rc != 0) {
        state_.store(State::Stopped);
        return;
    }

    // 创建 N 个 KcpServer，每个 ctor 自带 udp_bind（SO_REUSEPORT），sockfd 立即可用
    for (uint32_t i = 0; i < n; ++i) {
        auto s = std::make_unique<KcpServer>(host_.c_str(), serv_ev_);
        if (s->fd() == INVALID_SOCKET) {
            state_.store(State::Stopped);
            return;
        }

        if (router_.register_socket(i, s->fd()) != 0) {
            state_.store(State::Stopped);
            return;
        }

        servers_.emplace_back(std::move(s));
    }

    // 挂 BPF 程序到 SO_REUSEPORT 组（任一 socket 即可，整组共享）
    if (router_.attach(servers_[0]->fd()) != 0) {
        state_.store(State::Stopped);
        return;
    }

    // 启动所有 worker 线程
    for (auto& s : servers_) {
        threads_.emplace_back(std::bind(&KcpServer::run, s.get()));
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
typhon::TyService::stop() noexcept {
    auto running = State::Running;
    if (!state_.compare_exchange_strong(running, State::Stopping)) {
        return;
    }

    for (auto& s : servers_) {
        s->stop();
    }
}
