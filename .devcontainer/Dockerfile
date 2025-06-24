FROM ubuntu:20.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    gcc-mingw-w64-i686 \
    cmake \
    make \
    gdb-multiarch \
    && rm -rf /var/lib/apt/lists/*

ENV CC=i686-w64-mingw32-gcc
ENV CXX=i686-w64-mingw32-g++

WORKDIR /workspace
RUN mkdir -p /workspace/build

CMD ["sleep", "infinity"] 