FROM debian:bullseye-slim

RUN apt-get update && apt-get install -y \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN curl -L https://github.com/etcd-io/etcd/releases/download/v3.6.5/etcd-v3.6.5-linux-amd64.tar.gz -o /tmp/etcd.tar.gz \
    && tar xzvf /tmp/etcd.tar.gz -C /tmp/ \
    && mv /tmp/etcd-v3.6.5-linux-amd64/etcd /usr/local/bin/ \
    && mv /tmp/etcd-v3.6.5-linux-amd64/etcdctl /usr/local/bin/ \
    && rm -rf /tmp/etcd.tar.gz /tmp/etcd-v3.6.5-linux-amd64

CMD ["etcd"]