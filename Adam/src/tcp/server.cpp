#include "tcp/server.hpp"


static constexpr int INTERVAL_MS = 5000;
static constexpr int RBUF_SIZE = 1500;


void
adam::tcp::Server::run() noexcept {
    auto expected = core::State::Stopped;
    if (!state_.compare_exchange_strong(expected, core::State::Starting)) {
        return;
    }

    init();
    hook_->on_init(this);

    int i, n;
    constexpr size_t MAX_EVENTS = MAX_CONN + 2;
    ::epoll_event events[MAX_EVENTS];

    state_.store(core::State::Running);

    ASSERT(::listen(lfd_, SOMAXCONN) == 0, "监听 TCP fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));
    while (running()) {
        n = ::epoll_wait(epfd_, events, MAX_EVENTS, INTERVAL_MS);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            xERROR("epoll_wait failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            break;
        }

        tnow_ = utils::systime_ms();
        for (i = 0; i < n; ++i) {
            auto& ev = events[i];
            if (ev.data.fd == stop_evfd_) {
                on_stop_handle(ev);
            } else if (ev.data.fd == lfd_) {
                on_listen_handle(ev);
            } else {
                on_session_handle(ev);
            }
        }

        update_serv();
    }

    release();
    hook_->on_stopped(this);

    state_.store(core::State::Stopped);
}


void
adam::tcp::Server::init() noexcept {
    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ != INVALID_SOCKET, "创建 epoll fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    stop_evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(stop_evfd_ != INVALID_SOCKET, "创建 停止事件 fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    lfd_ = core::tcp_listen(host_);
    ASSERT(lfd_ != INVALID_SOCKET, "创建 TCP 监听 fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    ::epoll_event ev;
    ev.data.fd = stop_evfd_;
    ev.events = EPOLLIN | EPOLLET;
    ASSERT((::epoll_ctl(epfd_, EPOLL_CTL_ADD, stop_evfd_, &ev)) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));

    ev.data.fd = lfd_;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, lfd_, &ev) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));

    int n = std::thread::hardware_concurrency();
    n = n > 2 ? n - 2 : 2;

    for (int i = 0; i < n; ++i) {
        workers_.emplace_back(Worker::create(this, i));
        threads_.emplace_back((std::bind(&Worker::run, workers_[i].get())));
    }
}


void
adam::tcp::Server::release() noexcept {
    for (auto& w: workers_) {
        w->stop();
    }

    for (auto& t: threads_) {
        t.join();
    }
    
    threads_.clear();
    workers_.clear();

    if (epfd_ != INVALID_SOCKET) {
        ::close(epfd_);
        epfd_ = INVALID_SOCKET;
    }

    if (stop_evfd_ != INVALID_SOCKET) {
        ::close(stop_evfd_);
        stop_evfd_ = INVALID_SOCKET;
    }

    if (lfd_ != INVALID_SOCKET) {
        ::close(lfd_);
        lfd_ = INVALID_SOCKET;
    }

    for (auto& s: sesss_) {
        if (s) {
            hook_->on_disconnected(s);
            s = nullptr;
        }
    }
}


void
adam::tcp::Server::on_stop_handle(const ::epoll_event& ev) noexcept {
    if (ev.events & EPOLLIN) {
        while (1) {
            uint64_t event;
            auto n = ::read(stop_evfd_, &event, sizeof(event));
            if (n == -1) {
                int err = errno;
                if (err == EINTR) {
                    continue;
                }

                if (err != EAGAIN && err != EWOULDBLOCK) {
                    xERROR("read failed: errno = {}, errstr = {}", err, ::strerror(err));
                }
                break;
            }
        }
    }
}


void
adam::tcp::Server::on_listen_handle(const ::epoll_event& ev) noexcept {
    int err;
    if (ev.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        socklen_t len = sizeof(err);
        if (::getsockopt(lfd_, SOL_SOCKET, SO_ERROR, &err, &len)) {
            err = errno;
            xERROR("getsockopt failed: errno = {}, errstr = {}", err, ::strerror(err));
            return;
        }

        if (err) {
            xERROR("listen socket error: errno = {}, errstr = {}", err, ::strerror(err));
            return;
        }
    }

    if (ev.events & EPOLLIN) {
        while (1) {
            SOCKET cfd = ::accept4(lfd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (cfd < 0) {
                err = errno;
                if (err == EINTR) {
                    continue;
                }

                if (err != EAGAIN && err != EWOULDBLOCK) {
                    xERROR("accept failed: errno = {}, errstr = {}", err, ::strerror(err));
                }
                break;
            }

            if (cfd >= MAX_CONN) {
                xWARN("已到最大连接数 2048, 请重新设置 tcp::Server::MAX_CONN 大小");
                ::close(cfd);
                continue;
            }

            ::epoll_event event;
            event.data.fd = cfd;
            event.events = EPOLLIN | EPOLLET | EPOLLOUT;
            ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, cfd, &event) == 0, "failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            workers_[cfd % workers_.size()]->notify(new Message(Message::Type::SessionConnected, cfd));
        }
    }
}


void
adam::tcp::Server::on_session_handle(const ::epoll_event& ev) noexcept {
    SOCKET fd = ev.data.fd;
    bool del = false;

    if (ev.events & EPOLLIN) {
        int n;
        static thread_local uint8_t buf[RBUF_SIZE];

        while (1) {
            n = ::recv(fd, buf, RBUF_SIZE, 0);
            if (n <= 0) {
                if (n < 0) {
                    if (errno == EINTR) {
                        continue;
                    }

                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }

                    xERROR("recv failed: errno = {}, errstr = {}", errno, ::strerror(errno));
                }
                // n == 0 (对端 close 的 EOF) 或 n < 0 真错误 → 连接断开
                del = true;
                break;
            } else {
                workers_[fd % workers_.size()]->notify(
                    new Message(
                        Message::Type::SessionInput, new SessionInputArg(fd, n, buf)
                    )
                );
            }
        }
    }

    if (ev.events & EPOLLOUT) {
        workers_[fd % workers_.size()]->notify(new Message(Message::Type::SessionOutput, fd));
    }

    if (ev.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        del = true;
    }

    if (del) {
        ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr) == 0 || errno == ENOENT, "failed to remove session from epoll: errno = {}, errstr = {}", errno, ::strerror(errno));
        workers_[fd % workers_.size()]->notify(new Message(Message::Type::SessionDisconnected, fd));
    }
}


void
adam::tcp::Server::update_serv() noexcept {
    constexpr uint64_t AUTH_INTERVAL = 120000;

    static const adam::utils::EtcdConfig* etcd   = nullptr;
    static const adam::core::ServerInfo*  server = nullptr;

    static bool        put_flag = false;
    static uint64_t    last_update = 0;
    static std::string lease;
    static std::string token;

    if (etcd == nullptr) {
        etcd = Conf::instance()->etcd();
    }

    if (server == nullptr) {
        server = Conf::instance()->server();
    }

    adam::utils::EtcdRsp rsp;
    auto* url = etcd->url.c_str();

    if (token.empty() || tnow_ - last_update > AUTH_INTERVAL) {
        if (adam::utils::etcd_auth(&rsp, url, etcd->user.c_str(), etcd->pass.c_str()) != 0) {
            token.clear();
            return;
        }

        token = rsp.token;
    }

    auto state = state_.load(std::memory_order_relaxed);
    if (state != adam::core::State::Starting && state != adam::core::State::Running) {
        adam::utils::etcd_delete(&rsp, url, token.c_str(), server->key.c_str());
        token.clear();
        return;
    }

    if (tnow_ - last_update < INTERVAL_MS) {
        return;
    }

    if (!put_flag) {
        if (adam::utils::etcd_grant(&rsp, url, etcd->ttl) != 0) {
            token.clear();
            return;
        }

        lease = rsp.id;
        auto k = server->key.c_str();
        auto v = server->val.c_str();

        if (adam::utils::etcd_put(&rsp, url, token.c_str(), k, v, lease.c_str()) != 0) {
            token.clear();
            return;
        }

        put_flag = true;
        xINFO("{} 注册 etcd 成功", k);
    } else {
        if (adam::utils::etcd_keepalive(&rsp, url, token.c_str(), lease.c_str()) != 0) {
            put_flag = false;
            token.clear();
            return;
        }
    }

    last_update = tnow_;
}