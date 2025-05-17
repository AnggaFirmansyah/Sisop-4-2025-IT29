FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    fuse3 \
    libfuse3-dev \
    gcc \
    make \
    pkg-config \
    nano \
    && rm -rf /var/lib/apt/lists/*

COPY antink.c /antink.c
RUN gcc -Wall $(pkg-config --cflags fuse3) -o /antink /antink.c $(pkg-config --libs fuse3)
CMD ["/antink"]

