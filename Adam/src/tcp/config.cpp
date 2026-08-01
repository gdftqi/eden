#include "tcp/config.hpp"


void
adam::tcp::Conf::load_from_file(const char* fname) noexcept {
    auto root = YAML::LoadFile(fname);

    // from_yaml 里已经按字段打过具体原因, 这里补上是哪个文件 + 返回码
    if (!root["server"]) {
        xFATAL("{}: 缺少 server 段", fname);
    }

    int rc = server_.from_yaml(root["server"]);
    if (rc < 0) {
        xFATAL("{}: server 段无效, {}({})", fname, rc, core::str_error(rc));
    }

    if (!root["etcd"]) {
        xFATAL("{}: 缺少 etcd 段", fname);
    }

    rc = etcd_.from_yaml(root["etcd"]);
    if (rc < 0) {
        xFATAL("{}: etcd 段无效, {}({})", fname, rc, core::str_error(rc));
    }

    if (root["prof_path"]) {
        prof_path_ = root["prof_path"].as<std::string>();
    }

    if (root["log_path"]) {
        log_path_ = root["log_path"].as<std::string>();
    }
}
