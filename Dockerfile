FROM p4lang/behavioral-model:latest
MAINTAINER Seth Fowler <seth.fowler@barefootnetworks.com>

ARG UNIFIED_MAX_CHUNK_SIZE=10

ENV P4C_DEPS automake \
             bison \
             build-essential \
             flex \
             g++ \
             libboost-dev \
             libgc-dev \
             libgmp-dev \
             libtool \
             python2.7 \
             python-ipaddr \
             python-scapy \
             tcpdump
COPY . /p4c/
WORKDIR /p4c/
RUN apt-get update && \
    apt-get install -y --no-install-recommends $P4C_DEPS && \
    ./bootstrap.sh && \
    cd build && \
    make
