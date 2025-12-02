FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=1

RUN apt-get update
RUN apt-get install -y sudo build-essential tmux git curl wget
RUN apt-get install -y cmake ninja-build emacs-nox python3-dev
RUN apt-get install -y libstdc++-14-dev libc++-dev
RUN apt-get install -y scons m4 lld libgoogle-perftools-dev
RUN apt-get install -y protobuf-compiler libcapstone-dev libhdf5-dev
RUN apt-get install -y libpng-dev

ARG USERNAME=""
ARG UID=nan
ARG GID=nan

# Create the user with the specified UID and GID
# Apparently my group already exists:
RUN groupadd -g ${GID} ${USERNAME}
RUN useradd -m -u ${UID} -g ${GID} -s /bin/bash ${USERNAME}
RUN echo "${USERNAME} ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

# Install linux perf.
# NOTE: May need updating with never versions of ubuntu.
RUN apt-get update
RUN apt-get install -y linux-tools-common linux-tools-generic
RUN wget http://mirrors.edge.kernel.org/ubuntu/pool/main/l/linux/linux-tools-5.4.0-48_5.4.0-48.52_amd64.deb -O /tmp/linux-tools.deb
RUN dpkg -i /tmp/linux-tools.deb
RUN wget http://mirrors.edge.kernel.org/ubuntu/pool/main/l/linux/linux-tools-5.4.0-48-generic_5.4.0-48.52_amd64.deb -O /tmp/linux-tools-generic.deb
RUN dpkg -i /tmp/linux-tools-generic.deb

RUN apt-get install -y time snakemake

ARG KVM_GID=nan
RUN groupadd -g ${KVM_GID} kvm
RUN usermod -aG kvm ${USERNAME}

RUN apt-get install -y python3.12-venv
RUN python3 -m venv /venv
WORKDIR /bitwuzla
RUN wget -O- https://github.com/bitwuzla/bitwuzla/archive/refs/tags/0.7.0.tar.gz | tar --gzip -x --strip-components=1
RUN /venv/bin/pip install .
# RUN ./configure.py
# WORKDIR /bitwuzla/build
# RUN meson compile
# RUN /venv/bin/pip install .
ENV PATH="/venv/bin:${PATH}"
RUN python3 -c "import bitwuzla"
RUN python3 -m pip install humanfriendly

RUN apt-get update -y && apt-get install -y libx11-dev gettext freeglut3-dev libomp-dev pkg-config libxmu-dev libglib2.0-dev libxi-dev

RUN apt-get update -y && apt-get install -y gdb lldb

RUN apt-get update -y && apt-get install -y autoconf automake libtool m4 pkg-config

RUN apt-get update -y && apt-get install -y unzip

# Set the default user for the container
USER ${USERNAME}
ENV CMAKE_GENERATOR=Ninja
WORKDIR /cafe/u/${USERNAME}
