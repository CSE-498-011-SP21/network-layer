#!/bin/bash
if [[ $(basename "${PWD}") != "setup" ]]; then
    echo "This script must be run from its containing directory"
    exit
fi
source ../common.sh
assert_root
use_docker=0
init=0
mlnx_ofed=0
memcached=0
memcached_ip="127.0.0.1"
num_opts=0
while getopts "IDMm:h" arg; do
    case $arg in
    I)
        init=1
        ;;
    D)
        use_docker=1
        ;;
    M)
        mlnx_ofed=1
        ;;
    m)
        memcached=1
        memcached_ip=$OPTARG
        ;;
    h)
        echo "Usage..."
        ;;
    *)
        echo "Error parsing arguments"
        exit 1
        ;;
    esac
    num_opts=$((num_opts + 1))
done
## Default options
if [[ $num_opts -eq 0 ]]; then
    init=1
    mlnx_ofed=1
    memcached=1
    memcached_ip="127.0.0.1"
fi
if [[ ${init} -eq 1 ]]; then
    ## Update apt and install necessary utilities
    apt update
    if [[ ${use_docker} -eq 1 ]]; then
        ## Dockerfile will install dependencies
        apt install -y wget docker.io
        ## Allow docker to be used without sudo (log back in for it to take effect)
        usermod -aG docker ${dyno_user}
    else
        ## This is esentially the same as the Dockerfile but assumes that SSH is already configured
        ## Install tools
        apt install -y python3 python3-pip clang-10 lld software-properties-common wget unzip
        ## Install libraries
        apt install -y libibverbs-dev libmemcached-dev libevent-dev libhugetlbfs-dev libnuma-dev numactl libgflags-dev libssl-dev
        ## Install dev tools
        apt -y install gdb ibverbs-utils vim clang-format iputils-ping iproute2 htop tmux git
        ## Install cmake
        wget --quiet -O - https://apt.kitware.com/keys/kitware-archive-latest.asc | gpg --dearmor - >/etc/apt/trusted.gpg.d/kitware.gpg
        apt-add-repository "deb https://apt.kitware.com/ubuntu/ ${dyno_cmake_version} main"
        apt -y install cmake
        ## Install conan
        pip3 install --upgrade conan
        su ${dyno_user_remote} -c 'conan profile new default --detect'
        su ${dyno_user_remote} -c 'conan profile update settings.compiler.libcxx=libstdc++11 default'
    fi
    alias dyno="cd ${dyno_root_remote}"
fi
## Installs and configures Memcached
if [[ ${memcached} -eq 1 ]]; then
    apt update && apt install -y memcached
    ## Launches a local memcached instance for tests
    sed -i "s/-l .*/-l ${memcached_ip}/g" ${dyno_memcached_config}
    if ! cat ${dyno_memcached_config} | grep -q -e '-t'; then
        echo "" >>${dyno_memcached_config}
        echo "# Number of threads available to memcached" >>${dyno_memcached_config}
        echo "-t 1" >>${dyno_memcached_config}
    fi
    service memcached restart
fi
if [[ ${mlnx_ofed} -eq 1 ]]; then
    ## Get the Mellanox OFED drivers
    echo "Attempting to dowload the drivers"
    if ! [[ -e ${dyno_ofed_name}.tgz ]]; then
        wget https://www.mellanox.com/downloads/ofed/MLNX_OFED-${dyno_ofed_version}/${dyno_ofed_name}.tgz
    else
        echo "[WARNING] The drivers are already downloaded..."
    fi
    ## Validate drivers
    echo "Validating the drivers"
    check_file="checksum.txt"
    case ${dyno_ofed_checksum} in
    md5)
        echo "${dyno_ofed_md5sum} ${dyno_ofed_name}.tgz" >${check_file}
        if [ ! $(md5sum --check ${check_file}) ]; then
            echo "Checksum failed"
            cat ${check_file}
            exit
        fi
        ;;
    sha256)
        echo "${dyno_ofed_sha256sum} ${dyno_ofed_name}.tgz" >${check_file}
        if [ ! $(sha256sum --check ${check_file}) ]; then
            echo "Checksum failed"
            cat ${check_file}
            exit
        fi
        ;;
    *)
        echo "No checksum type given"
        exit
        ;;
    esac
    rm ${check_file}
    ## Unpack and build
    tar -xvf ${dyno_ofed_name}.tgz
    if ./${dyno_ofed_name}/mlnxofedinstall -v --force --without-srptools; then
        echo "Successfully installed Mellenox OFED drivers. Please restart your machine now."
    else
        echo "Failed to install the Mellanox OFED drivers"
    fi
    ## Clean up
    rm -rf ${dyno_ofed_name} ${dyno_ofed_name}.tgz
fi

cd ~

wget https://github.com/ofiwg/libfabric/releases/download/v1.9.1/libfabric-1.9.1.tar.bz2 && \
    bunzip2 libfabric-1.9.1.tar.bz2 && tar xf libfabric-1.9.1.tar && cd libfabric-1.9.1 && ./configure && \
    make -j && make install