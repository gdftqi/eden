第一步
    执行 install.sh
    成功执行后
        进程最大文件描述符将会设置成为 2097152, 在 "Ubuntu24.04 LTS" 中已经是最大有效值了。
        会更新并安装系统自带的依赖包, 并安装 net-tools, 使 机器具有 netstat 命令。
        会安装最新版的docker 并开启 2375的外网访问端口, 方便加入到 portainer 中进行集中管理。
        会激活root用户名和密码登录。
        最后需要重启生效。
        重启之后，可以在 WinSCP 或 XShell 工具使用 root用户密码进行登录。

第二步
    执行 run.sh
    可以启动基础容器

PS: 
    如果项目录需要启动非基础容器，需要放到docker 目录中，并使用 tar -czf setup.tar.gz ./setup 重构tar包