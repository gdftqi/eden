#ifndef __TYPHON_TCP_CONFIG_HPP__
#define __TYPHON_TCP_CONFIG_HPP__


namespace typhon::tcp {


class Conf {
    Conf(const Conf&) = delete;
    Conf& operator=(const Conf&) = delete;
    Conf(Conf&&) = delete;
    Conf& operator=(Conf&&) = delete;


public:
    static const Conf*
    instance() noexcept {
        static Conf m;
        return &m;
    }


    int
    sndbuf() const noexcept {
        return sndbuf_;
    }


    int
    rcvbuf() const noexcept {
        return rcvbuf_;
    }


    int
    timeout() const noexcept {
        return timeout_;
    }


private:
    Conf() noexcept
    {}


    int sndbuf_  { 1024 * 512 };
    int rcvbuf_  { 1024 * 512 };
    int timeout_ { 30000 };
}; // class Conf;

    
} // namespace typhon::tcp;


#endif // __TYPHON_TCP_CONFIG_HPP__