FROM ubuntu:22.04
LABEL maintainer="Arina Neshlyaeva <arina.neshlyaeva@intel.com>"
SHELL ["/bin/bash", "-c"]

ARG REPO=ispc/ispc
ARG SHA=main
ARG LLVM_VERSION=14.0
ARG IGC_VER=12812.24
ARG L0L_VER=1.9.4
ARG CR_VER=22.49.25018.24
ARG CR_VER_SUFFIX=25018.24
ARG VC_INTRINSICS_COMMIT_SHA="3ac855c9253d608a36d10b8ff87e62aa413bbf23"
ARG SPIRV_TRANSLATOR_COMMIT_SHA="c469fa86d54380231123f2b5adb4e1239392b814"

# !!! Make sure that your docker config provides enough memory to the container,
# otherwise LLVM build may fail, as it will use all the cores available to container.

# Packages
RUN apt-get -y update && DEBIAN_FRONTEND=noninteractive apt-get --no-install-recommends install -y clang-12 build-essential libnuma1 opencl-headers ocl-icd-libopencl1 clinfo vim gcc g++ git python3-dev imagemagick \
    m4 bison flex libncurses-dev libtinfo-dev libc6-dev-i386 cpio lsb-core wget netcat-openbsd libtbb2-dev libglfw3-dev pkgconf gdb gcc-multilib g++-multilib curl libomp-12-dev ca-certificates && \
    rm -rf /var/lib/apt/lists/*

# Download and install required version of cmake (3.14) for ISPC build
RUN wget -q --retry-connrefused --waitretry=5 --read-timeout=20 --timeout=15 -t 5 https://cmake.org/files/v3.14/cmake-3.14.0-Linux-x86_64.sh && mkdir /opt/cmake && sh cmake-3.14.0-Linux-x86_64.sh --prefix=/opt/cmake --skip-license && \
    ln -s /opt/cmake/bin/cmake /usr/local/bin/cmake && ln -s /opt/cmake/bin/cmake /usr/bin/cmake && \
    ln -s /opt/cmake/bin/ctest /usr/local/bin/ctest && ln -s /opt/cmake/bin/ctest /usr/bin/ctest && rm cmake-3.14.0-Linux-x86_64.sh

WORKDIR /home/src

RUN git clone https://github.com/$REPO.git ispc

WORKDIR /home/src/ispc
RUN git checkout $SHA
ENV ISPC_HOME=/home/src/ispc

# LLVM
ENV LLVM_HOME=/home/tools/llvm
RUN python3 ./alloy.py -b --version="$LLVM_VERSION" --selfbuild && \
    rm -rf "$LLVM_HOME"/build-"$LLVM_VERSION" "$LLVM_HOME"/llvm-"$LLVM_VERSION" "$LLVM_HOME"/bin-"$LLVM_VERSION"_temp "$LLVM_HOME"/build-"$LLVM_VERSION"_temp

WORKDIR /home/packages

# Compute runtime
RUN wget -q --retry-connrefused --waitretry=5 --read-timeout=20 --timeout=15 -t 5  https://github.com/intel/intel-graphics-compiler/releases/download/igc-1.0.${IGC_VER}/intel-igc-core_1.0.${IGC_VER}_amd64.deb && \
    wget -q --retry-connrefused --waitretry=5 --read-timeout=20 --timeout=15 -t 5  https://github.com/intel/intel-graphics-compiler/releases/download/igc-1.0.${IGC_VER}/intel-igc-opencl_1.0.${IGC_VER}_amd64.deb && \
    wget -q --retry-connrefused --waitretry=5 --read-timeout=20 --timeout=15 -t 5  https://github.com/intel/compute-runtime/releases/download/${CR_VER}/intel-level-zero-gpu-dbgsym_1.3.${CR_VER_SUFFIX}_amd64.ddeb && \
    wget -q --retry-connrefused --waitretry=5 --read-timeout=20 --timeout=15 -t 5  https://github.com/intel/compute-runtime/releases/download/${CR_VER}/intel-level-zero-gpu_1.3.${CR_VER_SUFFIX}_amd64.deb && \
    wget -q --retry-connrefused --waitretry=5 --read-timeout=20 --timeout=15 -t 5  https://github.com/intel/compute-runtime/releases/download/${CR_VER}/intel-opencl-icd-dbgsym_${CR_VER}_amd64.ddeb && \
    wget -q --retry-connrefused --waitretry=5 --read-timeout=20 --timeout=15 -t 5  https://github.com/intel/compute-runtime/releases/download/${CR_VER}/intel-opencl-icd_${CR_VER}_amd64.deb && \
    wget -q --retry-connrefused --waitretry=5 --read-timeout=20 --timeout=15 -t 5  https://github.com/intel/compute-runtime/releases/download/${CR_VER}/libigdgmm12_22.3.0_amd64.deb

# L0 loaader
RUN wget -q --retry-connrefused --waitretry=5 --read-timeout=20 --timeout=15 -t 5 https://github.com/oneapi-src/level-zero/releases/download/v${L0L_VER}/level-zero-devel_${L0L_VER}+u18.04_amd64.deb && \
    wget -q --retry-connrefused --waitretry=5 --read-timeout=20 --timeout=15 -t 5 https://github.com/oneapi-src/level-zero/releases/download/v${L0L_VER}/level-zero_${L0L_VER}+u18.04_amd64.deb

# Install packages and clean up
RUN dpkg -i ./*.deb
WORKDIR /home
RUN rm -rf packages

# vc-intrinsics
WORKDIR /home/src
RUN git clone https://github.com/intel/vc-intrinsics.git
WORKDIR /home/src/vc-intrinsics
RUN git checkout $VC_INTRINSICS_COMMIT_SHA && mkdir -p build
WORKDIR /home/src/vc-intrinsics/build
RUN cmake -DLLVM_DIR=$LLVM_HOME/bin-$LLVM_VERSION/lib/cmake/llvm -DCMAKE_INSTALL_PREFIX=/home/deps -DPYTHON_EXECUTABLE=/usr/bin/python3 ../ && make install -j"$(nproc)"

# SPIRV Translator
WORKDIR /home/src
RUN git clone https://github.com/KhronosGroup/SPIRV-LLVM-Translator.git
WORKDIR /home/src/SPIRV-LLVM-Translator
RUN git checkout $SPIRV_TRANSLATOR_COMMIT_SHA && mkdir -p build
WORKDIR /home/src/SPIRV-LLVM-Translator/build
RUN cmake -DLLVM_DIR=$LLVM_HOME/bin-$LLVM_VERSION/lib/cmake/llvm/ -DCMAKE_INSTALL_PREFIX=/home/deps -DPYTHON_EXECUTABLE=/usr/bin/python3 ../ && make install -j"$(nproc)"

# ISPC build
ENV PATH=$LLVM_HOME/bin-$LLVM_VERSION/bin:$PATH
# A note about libomp discoverability (required for libispcrt.so).
# In Ubuntu 18.04 and earlier installing libomp-dev is what you need.
# Starting Ubuntu 20.04 installing libomp-dev is not installing OpenMP libraries and
# they need to be installed with your compiler. We build them as part of LLVM, so
# adding LLVM lib path to LD_LIBRARY_PATH for libomp discoverability.
ENV LD_LIBRARY_PATH=$LLVM_HOME/bin-$LLVM_VERSION/lib:$LD_LIBRARY_PATH
WORKDIR /home/src/ispc
RUN mkdir -p build
WORKDIR /home/src/ispc/build
RUN cmake -DXE_ENABLED=ON -DCMAKE_INSTALL_PREFIX=/home/ispc/ -DXE_DEPS_DIR=/home/deps ../ && make install -j"$(nproc)"
RUN make check-all -j"$(nproc)"
# Add ISPC to PATH
ENV PATH=/home/ispc/bin:$PATH
