#include "tcp/config.hpp"


void
adam::tcp::Conf::load_from_file(const char* fname) noexcept {
    auto root = YAML::LoadFile(fname);
    if (!root["server"] || server_.from_yaml(root["server"]) < 0) {
        xFATAL("server is invalid");
    }

    if (!root["etcd"] || etcd_.from_yaml(root["etcd"]) < 0) {
        xFATAL("etcd is invalid");
    }

    if (root["prof_path"]) {
        prof_path_ = root["prof_path"].as<std::string>();
    }

    if (root["log_path"]) {
        log_path_ = root["log_path"].as<std::string>();
    }
}