
BASEDIR=$(pwd)
TOOLCHAIN="darwin-x86_64"
OUT="${BASEDIR}/android"
TARGET_CPU="armv8-a"
TARGET_ARCH="aarch64"
API=24

get_build_host() {
    case ${ARCH} in
        arm-v7a | arm-v7a-neon)
            echo "arm-linux-androideabi"
        ;;
        arm64-v8a)
            echo "aarch64-linux-android"
        ;;
        x86)
            echo "i686-linux-android"
        ;;
        x86-64)
            echo "x86_64-linux-android"
        ;;
    esac
}

get_clang_target_host() {
    case ${ARCH} in
        arm-v7a | arm-v7a-neon)
            echo "armv7a-linux-androideabi${API}"
        ;;
        arm64-v8a)
            echo "aarch64-linux-android${API}"
        ;;
        x86)
            echo "i686-linux-android${API}"
        ;;
        x86-64)
            echo "x86_64-linux-android${API}"
        ;;
    esac
}

ARCH="arm64-v8a"
BUILD_HOST=$(get_build_host)
ARCH_OPTIONS="	--enable-neon --enable-asm --enable-inline-asm"

export PATH=$PATH:${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${TOOLCHAIN}/bin
export AR=${BUILD_HOST}-ar
export CC=$(get_clang_target_host)-clang
export CXX=$(get_clang_target_host)-clang++
export LD=${BUILD_HOST}-ld
export RANLIB=${BUILD_HOST}-ranlib
export STRIP=${BUILD_HOST}-strip


mkdir -p ${OUT}

./configure \
    --cross-prefix="${BUILD_HOST}-" \
    --sysroot="${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${TOOLCHAIN}/sysroot" \
    --prefix="${OUT}" \
    --pkg-config="${HOST_PKG_CONFIG_PATH}" \
    --enable-version3 \
    --pkg-config="/usr/local/bin/pkg-config" \
    --arch="${TARGET_ARCH}" \
    --cpu="${TARGET_CPU}" \
    --cc="${CC}" \
    --cxx="${CXX}" \
    --target-os=android \
    ${ARCH_OPTIONS} \
    --disable-decoders \
    --disable-encoders \
    --disable-demuxers \
    --disable-muxers  \
    --disable-parsers \
    --disable-bsfs  \
    --enable-protocols \
    --disable-devices \
    --disable-filters \
    --disable-openssl \
    --disable-xmm-clobber-test \
    --disable-neon-clobber-test \
    --disable-postproc \
    --disable-doc \
    --disable-zlib \
    --disable-indevs \
    --disable-htmlpages \
    --disable-manpages \
    --disable-podpages \
    --disable-txtpages \
    --disable-sndio \
    --disable-schannel \
    --disable-securetransport \
    --disable-xlib \
    --disable-cuda \
    --disable-cuvid \
    --disable-nvenc \
    --disable-vdpau \
    --disable-videotoolbox \
    --disable-audiotoolbox \
    --disable-appkit \
    --disable-alsa \
    --disable-cuda \
    --disable-cuvid \
    --disable-nvenc \
    --disable-vdpau \
        \
    --enable-pthreads \
    --enable-encoder=rawvideo \
    --enable-demuxer=rtsp \
    --enable-demuxer=sdp \
    --enable-decoder=h264 \
    --enable-protocol=rtp \
    --enable-protocol=srtp \
    --enable-parser=h264 \
    --enable-filter=scale \
    --enable-filter=crop \
    --enable-outdev=callback \
    --enable-cross-compile \
    --enable-pic \
    --enable-jni \
    --enable-optimizations \
    --enable-swscale \
    --enable-static \
    --enable-shared \
    --enable-v4l2-m2m \


if [ $? -ne 0 ]; then
    echo "failed"
    exit 1
fi

make -j8
make install

cp ${BASEDIR}/config.h ${BASEDIR}/android/include/
