#!/bin/bash

# 定义颜色
GREEN="\e[32m"
RED="\e[31m"
RESET="\e[0m"

# 判断是否为 root 用户
if [[ $EUID -ne 0 ]]; then
    echo -e "${RED}请使用 root 用户运行此脚本。${RESET}"
    exit 1
fi

# 部署角色: gateway(网关机, UDP/KCP 海量连接) 或 backend(后端机, TCP 少连接高吞吐)
ROLE="${1:-}"
if [[ "$ROLE" != "gateway" && "$ROLE" != "backend" ]]; then
    echo -e "${RED}用法: $0 <gateway|backend>${RESET}"
    echo -e "${RED}  gateway = 网关机(UDP/KCP 调优)   backend = 后端机(TCP 调优)${RESET}"
    exit 1
fi
echo -e "${GREEN}部署角色: ${ROLE}${RESET}"

# 设置时区
echo -e "${GREEN}设置系统时区为 Asia/Shanghai...${RESET}"
timedatectl set-timezone Asia/Shanghai

# 更新系统源 & 更新系统组件
echo -e "${GREEN}正在更新系统...${RESET}"
apt update && apt upgrade -y && apt install -y net-tools

# 询问是否安装 Docker
echo -en "${GREEN}是否安装 Docker？(Y/n): ${RESET}"
read install_docker
if [[ ! $install_docker =~ ^[Nn]$ ]]; then
    # 安装 Docker 依赖
    echo -e "${GREEN}安装 Docker 依赖...${RESET}"
    apt install -y ca-certificates curl

    # 添加 Docker GPG 密钥
    echo -e "${GREEN}添加 Docker GPG 密钥...${RESET}"
    install -m 0755 -d /etc/apt/keyrings
    curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
    chmod a+r /etc/apt/keyrings/docker.asc

    # 添加 Docker 源
    echo -e "${GREEN}添加 Docker APT 源...${RESET}"
    echo \
"deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu \
$(. /etc/os-release && echo "${UBUNTU_CODENAME:-$VERSION_CODENAME}") stable" | \
tee /etc/apt/sources.list.d/docker.list > /dev/null

    # 更新源并安装 Docker
    echo -e "${GREEN}安装 Docker...${RESET}"
    apt update && apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

    # 配置 Docker 支持远程访问
    echo -e "${GREEN}配置 Docker 支持远程访问...${RESET}"
    DOCKER_SERVICE_FILE="/usr/lib/systemd/system/docker.service"
    if grep -q "tcp://0.0.0.0:2375" "$DOCKER_SERVICE_FILE"; then
        echo -e "${GREEN}已存在 Docker 远程访问配置。${RESET}"
    else
        sed -i 's|ExecStart=/usr/bin/dockerd -H fd:// --containerd=/run/containerd/containerd.sock|ExecStart=/usr/bin/dockerd -H fd:// --containerd=/run/containerd/containerd.sock -H tcp://0.0.0.0:2375|' "$DOCKER_SERVICE_FILE"
    fi
else
    echo -e "${GREEN}跳过 Docker 安装。${RESET}"
fi

# 设置系统最大连接数和相关限制
echo -e "${GREEN}配置最大连接数和资源限制...${RESET}"

LIMITS_CONF="/etc/security/limits.conf"
SYSTEM_CONF="/etc/systemd/system.conf"
USER_CONF="/etc/systemd/user.conf"
SYSCTL_CONF="/etc/sysctl.conf"

cat <<EOF >> $LIMITS_CONF
root soft nofile 2097152
root hard nofile 2097152
root soft nproc 2097152
root hard nproc 2097152
root soft core 0
root hard core 0

ubuntu soft nofile 2097152
ubuntu hard nofile 2097152
ubuntu soft nproc 2097152
ubuntu hard nproc 2097152
ubuntu soft core 0
ubuntu hard core 0
EOF

sed -i '/^DefaultLimitNOFILE/d' $SYSTEM_CONF
echo "DefaultLimitNOFILE=2097152" >> $SYSTEM_CONF

sed -i '/^DefaultLimitNOFILE/d' $USER_CONF
echo "DefaultLimitNOFILE=2097152" >> $USER_CONF

# 加载 BBR 模块(否则 sysctl 设 tcp_congestion_control=bbr 会失败/回退 cubic) + 开机自动加载
echo -e "${GREEN}加载 BBR 拥塞控制模块...${RESET}"
modprobe tcp_bbr
echo "tcp_bbr" > /etc/modules-load.d/bbr.conf
sysctl net.ipv4.tcp_available_congestion_control   # 验证: 输出里应含 bbr

# ===== 通用 sysctl(两角色共用) =====
cat <<EOF >> $SYSCTL_CONF

# ---- 通用 ----
fs.file-max = 2097152
fs.nr_open = 2097152
net.core.somaxconn = 65535
net.core.default_qdisc = fq
net.ipv4.tcp_congestion_control = bbr
net.ipv4.tcp_slow_start_after_idle = 0
EOF

if [[ "$ROLE" == "gateway" ]]; then
    # 网关机: UDP/KCP 为主, 海量连接, 重点防 UDP 收包丢弃
    cat <<EOF >> $SYSCTL_CONF

# ---- 网关(UDP/KCP) ----
net.core.rmem_max = 33554432           # 32MB: UDP SO_RCVBUF 天花板(setsockopt 受它限, 否则被静默截断)
net.core.wmem_max = 16777216           # 16MB
net.core.netdev_max_backlog = 250000   # 网卡→协议栈队列, UDP 高包率防丢
net.core.netdev_budget = 600           # 单次软中断处理包数
net.ipv4.udp_mem = 8388608 12582912 16777216   # UDP 全局内存(页), 海量连接放宽
net.ipv4.udp_rmem_min = 16384
net.ipv4.udp_wmem_min = 16384
# 网关→后端的少量 TCP 长连
net.ipv4.tcp_rmem = 4096 131072 16777216
net.ipv4.tcp_wmem = 4096  65536 16777216
EOF
else
    # 后端机: TCP 为主, 少连接(= 网关数)高吞吐, 靠 autotuning
    cat <<EOF >> $SYSCTL_CONF

# ---- 后端(TCP) ----
net.core.rmem_max = 16777216
net.core.wmem_max = 16777216
net.core.netdev_max_backlog = 65536
net.ipv4.tcp_rmem = 4096 262144 33554432    # max 32MB: 汇聚流量高吞吐, 给 autotuning 留空间
net.ipv4.tcp_wmem = 4096 262144 33554432
net.ipv4.tcp_mem = 786432 1048576 1572864   # TCP 全局内存(页)
net.ipv4.tcp_max_syn_backlog = 65536
net.ipv4.tcp_syncookies = 1
net.ipv4.tcp_fin_timeout = 30
net.ipv4.tcp_max_tw_buckets = 500000
net.ipv4.ip_local_port_range = 1024 65535
EOF
fi

sysctl -p

# SSH 配置相关
echo -e "${GREEN}备份并修改 sshd_config 配置以启用 root 登录...${RESET}"
cp /etc/ssh/sshd_config /etc/ssh/sshd_config.bak

sed -i 's/^#\?PermitRootLogin .*/PermitRootLogin yes/' /etc/ssh/sshd_config
sed -i 's/^#\?PasswordAuthentication .*/PasswordAuthentication yes/' /etc/ssh/sshd_config
sed -i 's/^#\?KbdInteractiveAuthentication .*/KbdInteractiveAuthentication yes/' /etc/ssh/sshd_config

# 设置 root 密码
echo -e "${GREEN}请输入 root 用户的新密码：${RESET}"
passwd_output=$(passwd root)
echo -e "${GREEN}$passwd_output${RESET}"

echo -e "${GREEN}复制 ./portainer 到 /root/docker ...${RESET}"
cp -r ./docker /root/
cp ./run.sh /root/

# 最终提示
echo -e "${GREEN}配置完成, Docker 和 SSH 已配置完毕。${RESET}"

# 重启确认
echo -en "${GREEN}是否现在重启系统？(y/n): ${RESET}"
read confirm
if [[ $confirm == [Yy] ]]; then
    echo -e "${GREEN}正在重启系统...${RESET}"
    reboot
else
    echo -e "${GREEN}系统未重启，请稍后手动重启以确保所有配置生效。${RESET}"
fi