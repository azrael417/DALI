#!/bin/bash

# Stop at any error, show all commands
set -ex

usage="ENV1=VAL1 ENV2=VAL2 [...] $(basename "$0") [-h] -- this DALI build helper mean to run from the docker environment.
Please don't call it directly.

where:
    -h  show this help text"

while getopts 'h' option; do
  case "$option" in
    h) echo "$usage"
       exit
       ;;
   \?) printf "illegal option: -%s\n" "$OPTARG" >&2
       echo "$usage" >&2
       exit 1
       ;;
  esac
done
shift $((OPTIND - 1))

export ARCH=${ARCH}
export CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-Release}
export BUILD_TEST=${BUILD_TEST:-ON}
export BUILD_BENCHMARK=${BUILD_BENCHMARK:-ON}
# use a default value as it differs for CUDA 9.x and CUDA 10.x
export BUILD_NVTX=${BUILD_NVTX}
export BUILD_PYTHON=${BUILD_PYTHON:-ON}
export BUILD_LMDB=${BUILD_LMDB:-ON}
export BUILD_JPEG_TURBO=${BUILD_JPEG_TURBO:-ON}
export BUILD_NVJPEG=${BUILD_NVJPEG:-ON}
export BUILD_LIBTIFF=${BUILD_LIBTIFF:-ON}
export BUILD_NVOF=${BUILD_NVOF:-ON}
export BUILD_NVDEC=${BUILD_NVDEC:-ON}
export BUILD_LIBSND=${BUILD_LIBSND:-ON}
export BUILD_NVML=${BUILD_NVML:-ON}
export BUILD_FFTS=${BUILD_FFTS:-ON}
export BUILD_DUMMY_PKG=${BUILD_DUMMY_PKG:-OFF}
export STRIP_BINARY=${STRIP_BINARY:-OFF}
export VERBOSE_LOGS=${VERBOSE_LOGS:-OFF}
export WERROR=${WERROR:-ON}
export BUILD_WITH_ASAN=${BUILD_WITH_ASAN:-OFF}
export NVIDIA_BUILD_ID=${NVIDIA_BUILD_ID:-0}
export GIT_SHA=${GIT_SHA}
export DALI_TIMESTAMP=${DALI_TIMESTAMP}
export NVIDIA_DALI_BUILD_FLAVOR=${NVIDIA_DALI_BUILD_FLAVOR}
export CUDA_TARGET_ARCHS=${CUDA_TARGET_ARCHS}
export WHL_PLATFORM_NAME=${WHL_PLATFORM_NAME:-manylinux1_x86_64}
export PATH=/usr/local/cuda/bin:${PATH}

LD_LIBRARY_PATH="${PWD}:${LD_LIBRARY_PATH}" && \
cmake ../ -DCMAKE_INSTALL_PREFIX=.                 \
      -DARCH=${ARCH}                               \
      -DCUDA_TARGET_ARCHS=${CUDA_TARGET_ARCHS}     \
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}       \
      -DBUILD_TEST=${BUILD_TEST}                   \
      -DBUILD_BENCHMARK=${BUILD_BENCHMARK}         \
      -DBUILD_NVTX=${BUILD_NVTX}                   \
      -DBUILD_PYTHON=${BUILD_PYTHON}               \
      -DBUILD_LMDB=${BUILD_LMDB}                   \
      -DBUILD_JPEG_TURBO=${BUILD_JPEG_TURBO}       \
      -DBUILD_NVJPEG=${BUILD_NVJPEG}               \
      -DBUILD_LIBTIFF=${BUILD_LIBTIFF}             \
      -DBUILD_NVOF=${BUILD_NVOF}                   \
      -DBUILD_NVDEC=${BUILD_NVDEC}                 \
      -DBUILD_LIBSND=${BUILD_LIBSND}               \
      -DBUILD_NVML=${BUILD_NVML}                   \
      -DBUILD_FFTS=${BUILD_FFTS}                   \
      -DVERBOSE_LOGS=${VERBOSE_LOGS}               \
      -DWERROR=${WERROR}                           \
      -DBUILD_WITH_ASAN=${BUILD_WITH_ASAN}         \
      -DDALI_BUILD_FLAVOR=${NVIDIA_DALI_BUILD_FLAVOR} \
      -DTIMESTAMP=${DALI_TIMESTAMP} -DGIT_SHA=${GIT_SHA}
if [ "${WERROR}" = "ON" ]; then
    make -j lint
fi
make -j"$(grep ^processor /proc/cpuinfo | wc -l)"

if [ "${BUILD_PYTHON}" = "ON" ]; then
    pip wheel -v dali/python \
        --build-option --python-tag=$(basename /opt/python/cp${PYV}-*) \
        --build-option --plat-name=${WHL_PLATFORM_NAME} \
        --build-option --build-number=${NVIDIA_BUILD_ID}

    ../dali/python/bundle-wheel.sh nvidia_dali[_-]*.whl

    export UNZIP_PATH="$(mktemp -d)"
    unzip /wheelhouse/nvidia_dali*.whl -d $UNZIP_PATH
    python ../tools/test_bundled_libs.py $(find $UNZIP_PATH -iname *.so* | tr '\n' ' ')
    rm -rf $UNZIP_PATH

    if [ "${STRIP_BINARY}" = "ON" ]; then
        # rename unstriped wheel to debug
        WHEEL=$(ls /wheelhouse/nvidia_dali*.whl)
        mv $WHEEL ${WHEEL%.*}_debug.whl

        WHEEL=$(pwd)/$(ls nvidia_dali[_-]*.whl)
        export UNZIP_PATH="$(mktemp -d)"
        unzip $WHEEL -d $UNZIP_PATH
        for f in $(find $UNZIP_PATH -iname *.so); do
            strip --strip-debug $f
        done
        rm -f $WHEEL
        pushd $UNZIP_PATH
        zip -rq $WHEEL *
        popd
        rm -rf $UNZIP_PATH
        # rerun all things involving patchelf on striped binary
        # we cannot strip after patchelf as according to the documentation
        ###
        ### The `strip' command from binutils generated broken executables when
        ### applied to the output of patchelf (if `--set-rpath' or
        ### `--set-interpreter' with a larger path than the original is used).
        ### This appears to be a bug in binutils
        ### (http://bugs.strategoxt.org/browse/NIXPKGS-85).

        ../dali/python/bundle-wheel.sh nvidia_dali[_-]*.whl
    fi

    if [ "${BUILD_DUMMY_PKG}" = "ON" ]; then
        pushd ../dali/python/dummy
        mkdir -p /wheelhouse/dummy
        source make_nvidia_dali_dummy.sh /wheelhouse/dummy
        popd
    fi
fi
