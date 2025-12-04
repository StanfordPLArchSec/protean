FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=1

# Install dependencies.
RUN apt-get update && apt-get install -y build-essential git curl wget cmake ninja-build emacs-nox python3-dev \
    scons m4 libgoogle-perftools-dev time autoconf automake libtool \
    pkg-config unzip multitail
# Get LLD, since it links gem5 faster.
RUN apt-get update && apt-get install -y lld


# Set up the compiler cache.
RUN apt-get update && apt-get install -y ccache
ENV CCACHE_DIR=/ccache
ENV PATH="/usr/lib/ccache:${PATH}"

RUN ccache --set-config=cache_dir=/ccache

ENV CMAKE_GENERATOR=Ninja

# Set up the m5 build cache.
ENV M5_BUILD_CACHE=/m5cache

WORKDIR /protean

RUN apt-get update && apt-get install -y libssl-dev
RUN apt-get update && apt-get install -y python3-tabulate
RUN apt-get update && apt-get install -y snakemake
RUN apt-get install -y texlive-latex-base
RUN apt-get update && apt-get install -y \
    texlive-latex-recommended \
    texlive-latex-extra \
    texlive-fonts-recommended
