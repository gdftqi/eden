#ifndef __ADAM_UTILS_PROTO_EX_HPP__
#define __ADAM_UTILS_PROTO_EX_HPP__


#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <google/protobuf/message_lite.h>


namespace adam::utils {


/**
 * @brief 序列化 protobuf 消息到调用方缓冲.
 * @return 成功返回 写入字节数(空消息为 0), 否则返回 -1
 */
template<typename T>
int
pb_serialize(uint8_t* buf, size_t buflen, const T& m) noexcept {
    static_assert(std::is_base_of_v<google::protobuf::MessageLite, T>, "T is not based on protobuf MessageLite");

    size_t size = m.ByteSizeLong();
    if (size == 0) {
        return 0;
    }

    if (buflen < size) {
        return -1;
    }

    return m.SerializeToArray(buf, (int)size) ? (int)size : -1;
}


/**
 * @brief 从缓冲反序列化 protobuf 消息.
 * @return 成功返回 0, 否则返回 -1
 */
template<typename T>
int
pb_deserialize(T* out, const uint8_t* buf, size_t buflen) noexcept {
    static_assert(std::is_base_of_v<google::protobuf::MessageLite, T>, "T is not based on protobuf MessageLite");
    return out->ParseFromArray(buf, (int)buflen) ? 0 : -1;
}


} // namespace adam::utils


#endif // __ADAM_UTILS_PROTO_EX_HPP__
