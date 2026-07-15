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
    load(const char* cfname) noexcept {
        auto root = YAML::LoadFile(cfname);
        if (!root["server"] || server_.from_yaml(root["server"]) < 0) {
            xFATAL("server is invalid");
        }

        if (!root["etcd"] || etcd_.from_yaml(root["etcd"]) < 0) {
            xFATAL("etcd is invalid");
        }
    }


    const core::ServerInfo*
    server() const noexcept {
        return &server_;
    }


    const utils::EtcdConfig*
    etcd() const noexcept {
        return &etcd_;
    }


private:
    Conf() noexcept
    {}


    core::ServerInfo  server_;
    utils::EtcdConfig etcd_;
}; // class Conf;

    
} // namespace adam::tcp;


#endif // __ADAM_TCP_CONFIG_HPP__