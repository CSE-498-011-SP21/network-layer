###### setup.sh configs ######
## The below configurations were used in our testbeds and are presented as an example. For the correct drivers for your system, please refer to the "Download" tab at the bottom of https://www.mellanox.com/products/infiniband-drivers/linux/mlnx_ofed.
# dyno_ofed_checksum=sha256
# dyno_ofed_sha256sum="24f46e586f73e0a9be42aa019c7c567acd4ed8b1960e216902a1885186c9c6c1"
dyno_ofed_version="5.2-2.2.0.0"
dyno_ofed_dist="ubuntu20.04"
dyno_ofed_arch="x86_64"
# dyno_ofed_arch="aarch64"
# The following is the necessary driver version and checksum for the ConnectX-3 cards.
dyno_ofed_checksum=md5
dyno_ofed_md5sum="04c91c210e5931b19bee600492c4c4c2"
dyno_ofed_version="4.9-2.2.4.0"
dyno_ofed_name=MLNX_OFED_LINUX-${dyno_ofed_version}-${dyno_ofed_dist}-${dyno_ofed_arch}
## Required when building without docker. It corresponds to the OS distro.
dyno_cmake_version="focal"
####### Build configs ######
dyno_build_root="~/dyno"
dyno_build_profile="x86-gcc9-sanitize"
dyno_memcached_config="/etc/memcached.conf"
####### Development script configs #######
##
## These configurations are used by `dev/push.sh` and `dev/testbeds.sh` to
## Locations of local and remote directories
dyno_root_local=""     #+ Sanitize
dyno_user_remote=""                  #+ Sanitize
dyno_dest_dir="" #+ Sanitize
dyno_root_remote=""   #+ Sanitize
dyno_id_file=""
###### Helper functions ######
# Use pids to track background processes for each script and clean them up whenever receiving SIGTERM or SIGINT
pids=()
function cleanup() {
    for p in ${pids[@]}; do
        kill $1 $p
    done
    eval $2 # Additional steps to take, if needed
}
trap cleanup TERM INT
assert_root() {
    if [[ $(id -u) -ne 0 ]]; then
        echo "Please re-run as root"
        exit
    fi
}
set_testbed_config() {
    # Get testbed configs
    if [[ $1 == "cloudlab" ]]; then
        # Check input is valid
        if [[ $# -lt 3 ]]; then
            echo "You must provide a node type and node ids for the CloudLab testbed"
            return 0
        elif [[ $2 = "" ]]; then
            echo "You must provide a node type for the CloudLab testbed"
            return 0
        elif [[ $3 = "" ]]; then
            echo "You must provide node ids for the CloudLab testbed"
            return 0
        fi
        # Profile specific configurations
        if [[ $2 == "d6515" ]]; then
            for id in ${@:3}; do
                hosts+=("amd${id}.utah.cloudlab.us")
            done
            return 1
        elif [[ $2 == "xl170" ]]; then
            for id in ${@:3}; do
                hosts+=("hp${id}.utah.cloudlab.us")
            done
            return 1
        elif [[ $2 == "m400" ]] || [[ $2 == "m510" ]]; then
            for id in ${@:3}; do
                hosts+=("ms${id}.utah.cloudlab.us")
            done
            return 1
        elif [[ $2 == "r320" ]]; then
            for id in ${@:3}; do
                hosts+=("apt${id}.apt.emulab.net")
            done
            return 1
        else
            echo "No configuration found for CloudLab node type: $2"
            return 0
        fi
    else
        echo "Invalid testbed name: $1"
        return 0
    fi
}