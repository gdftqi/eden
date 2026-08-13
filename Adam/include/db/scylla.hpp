#ifndef __ADAM_DB_SCYLLA_HPP__
#define __ADAM_DB_SCYLLA_HPP__


#include <cassandra.h>
#include <yaml-cpp/yaml.h>
#include "utils/log.hpp"


namespace adam::db {
    

class Scylla {
    Scylla(const Scylla&) = delete;
    Scylla(Scylla&&) = delete;
    Scylla& operator=(const Scylla&) = delete;
    Scylla& operator=(Scylla&&) = delete;


public:
    static Scylla*
    instance() noexcept {
        static Scylla m;
        return &m;
    }


    ~Scylla() noexcept {
        if (session_ != nullptr) {
            ::cass_session_free(session_);
        }

        if (cluster_ != nullptr) {
            ::cass_cluster_free(cluster_);
        }
    }


    int
    init_from_file(const char* fname) noexcept {
        auto root = YAML::LoadFile(fname);
        if (!root["scylla"]) {
            xFATAL("缺少 scylla");
        }

        root = root["scylla"];
        if (!root["hosts"]) {
            xFATAL("scylla.hosts is invalid");
        }
        auto hosts = root["hosts"].as<std::string>();

        uint16_t port = 9042;
        if (root["port"]) {
            port = root["port"].as<uint16_t>();
        }

        std::string user;
        if (root["user"]) {
            user = root["user"].as<std::string>();
        }

        std::string pass;
        if (root["pass"]) {
            pass = root["pass"].as<std::string>();
        }

        return init(hosts.c_str(), port, user.c_str(), pass.c_str());
    }


    int
    init(const char* hosts, uint16_t port = 9042, const char* user = nullptr, const char* pass = nullptr) noexcept {
        if (hosts == nullptr) {
            return -1;
        }

        if (::cass_cluster_set_contact_points(cluster_, hosts) != CASS_OK) {
            xERROR("cass_cluster_set_contact_points failed");
            return -1;
        }

        if (::cass_cluster_set_port(cluster_, port) != CASS_OK) {
            xERROR("cass_cluster_set_port failed");
            return -1;
        }

        if (user != nullptr && pass != nullptr) {
            ::cass_cluster_set_credentials(cluster_, user, pass);
        }

        ::cass_cluster_set_request_timeout(cluster_, 3000);
        ::cass_cluster_set_connect_timeout(cluster_, 5000);
        ::cass_cluster_set_compression(cluster_, CASS_COMPRESSION_LZ4);

        return 0;
    }


    /**
     * @brief 连接集群并选定 keyspace, 必须在 init 之后 exec 之前调一次.
     *        阻塞到握手完成 -- 启动阶段的一次性动作, 连不上就该让进程起不来
     */
    int
    connect(const char* keyspace) noexcept {
        ::CassFuture* f = ::cass_session_connect_keyspace(session_, cluster_, keyspace);
        ::cass_future_wait(f);

        int res = 0;

        if (::cass_future_error_code(f) != CASS_OK) {
            const char* m;
            size_t n;
            ::cass_future_error_message(f, &m, &n);
            xERROR("cass_session_connect_keyspace 失败: {}", std::string(m, n));
            res = -1;
        }

        ::cass_future_free(f);

        return res;
    }


    /**
     * @brief 执行一条 CQL, 并把结果集交给 reader
     *
     * @param reader 可调用对象(const CassResult*), 在结果释放前调用一次.
     *               SELECT / LWT(看 [applied]) 用得上; INSERT 这类拿到的是空结果集
     */
    template<typename F, typename R>
    int
    exec(const char* cql, F&& binder, R&& reader) noexcept {
        ::CassFuture* pf = ::cass_session_prepare(session_, cql);
        ::cass_future_wait(pf);

        if (::cass_future_error_code(pf) != CASS_OK) {
            const char* m;
            size_t n;
            ::cass_future_error_message(pf, &m, &n);
            xERROR("cass_session_prepare 失败: {}", std::string(m, n));
            ::cass_future_free(pf);
            return -1;
        }

        const ::CassPrepared* ps = ::cass_future_get_prepared(pf);
        ::cass_future_free(pf);

        ::CassStatement* st = ::cass_prepared_bind(ps);
        binder(st);

        int res = exec(st, reader);
        ::cass_prepared_free(ps);

        return res;
    }


    /**
     * @brief 执行一条 CQL, 不关心结果集(INSERT / UPDATE)
     */
    template<typename F>
    int
    exec(const char* cql, F&& binder) noexcept {
        return exec(cql, std::forward<F>(binder), [](const ::CassResult*) noexcept {});
    }


private:
    explicit
    Scylla() noexcept {
        cluster_ = ::cass_cluster_new();
        ASSERT(cluster_ != nullptr, "cass_cluster_new 创建 cluster 失败");

        session_ = ::cass_session_new();
        ASSERT(session_ != nullptr, "cass_session_new 创建 session 失败");
    }


    template<typename R>
    int
    exec(::CassStatement* st, R&& reader) noexcept {
        ::CassFuture* f = ::cass_session_execute(session_, st);
        ::cass_future_wait(f);

        int res = 0;

        if (::cass_future_error_code(f) != CASS_OK) {
            const char* m;
            size_t n;
            ::cass_future_error_message(f, &m, &n);
            xERROR("cass_session_execute 执行失败: {}", std::string(m, n));
            res = -1;
        } else {
            const ::CassResult* rs = ::cass_future_get_result(f);
            if (rs != nullptr) {
                reader(rs);
                ::cass_result_free(rs);
            }
        }

        ::cass_statement_free(st);
        ::cass_future_free(f);

        return res;
    }


    ::CassCluster* cluster_ { nullptr };
    ::CassSession* session_ { nullptr };
}; // class Scylla;


} // namespace adam::db


#endif // __ADAM_DB_SCYLLA_HPP__
