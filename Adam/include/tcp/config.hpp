#ifndef __ADAM_TCP_CONFIG_HPP__
#define __ADAM_TCP_CONFIG_HPP__


#include "core/adam.in.hpp"
#include "utils/etcd.hpp"


namespace adam::tcp {


class Conf {
    Conf(const Conf&) = delete;
    Conf& operator=(const Conf&) = delete;
    Conf(Conf&&) = delete;
    Conf& operator=(Conf&&) = delete;


public:
    static Conf*
    instance() noexcept {
        static Conf m;
        return &m;
    }


    void
    load_from_file(const char* fname) noexcept;


    core::ServerInfo*
    server() noexcept {
        return &server_;
    }


    const utils::EtcdConfig*
    etcd() const noexcept {
        return &etcd_;
    }


    std::string
    log_path() const noexcept {
        return log_path_;
    }


    std::string
    prof_path() const noexcept {
        return prof_path_;
    }


private:
    Conf() noexcept
    {}


    core::ServerInfo  server_;
    utils::EtcdConfig etcd_;
    std::string       log_path_;
    std::string       prof_path_;
}; // class Conf;

    
} // namespace adam::tcp;


#endif // __ADAM_TCP_CONFIG_HPP__
