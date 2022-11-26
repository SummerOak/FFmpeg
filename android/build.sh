
BASEDIR=$(pwd)
TOOLCHAIN="darwin-x86_64"
OUT="${BASEDIR}/android"
# ARCH=aarch64
ARCH=arm-v7a
API=24
BUILD_HOST=""
CC_HOST=""
ARCH_OPTIONS=""

case ${ARCH} in
arm-v7a)
    TARGET_CPU="armv7-a"
    TARGET_ARCH="armv7-a"
    ASM_OPTIONS=" --disable-neon --enable-asm --enable-inline-asm"
    BUILD_HOST="arm-linux-androideabi"
    CC_HOST="armv7a-linux-androideabi${API}"
    ;;
arm-v7a-neon)
    TARGET_CPU="armv7-a"
    TARGET_ARCH="armv7-a"
    ASM_OPTIONS=" --enable-neon --enable-asm --enable-inline-asm --build-suffix=_neon"
    BUILD_HOST="arm-linux-androideabi"
    CC_HOST="armv7a-linux-androideabi${API}"

    ;;
arm64-v8a|aarch64)
    TARGET_CPU="armv8-a"
    TARGET_ARCH="aarch64"
    ASM_OPTIONS=" --enable-neon --enable-asm --enable-inline-asm"
    BUILD_HOST="aarch64-linux-android"
    CC_HOST="aarch64-linux-android${API}"
    ;;
x86)
    TARGET_CPU="i686"
    TARGET_ARCH="i686"
    BUILD_HOST="i686-linux-android"
    CC_HOST="i686-linux-android${API}"

    # asm disabled due to this ticket https://trac.ffmpeg.org/ticket/4928
    ASM_OPTIONS=" --disable-neon --disable-asm --disable-inline-asm"
    ;;
x86-64)
    TARGET_CPU="x86_64"
    TARGET_ARCH="x86_64"
    ASM_OPTIONS=" --disable-neon --enable-asm --enable-inline-asm"
    BUILD_HOST="x86_64-linux-android"
    CC_HOST="x86_64-linux-android${API}"
    ;;
esac

export PATH=$PATH:${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${TOOLCHAIN}/bin
export AR=${BUILD_HOST}-ar
export CC=${CC_HOST}-clang
export CXX=${CC_HOST}-clang++
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
    --enable-encoder=pcm_s16le \
    --enable-decoder=opus \
    --enable-demuxer=rtsp \
    --enable-demuxer=sdp \
    --enable-decoder=h264 \
    --enable-protocol=rtp \
    --enable-protocol=srtp \
    --enable-parser=h264 \
    --enable-filter=scale \
    --enable-filter=crop \
    --enable-filter=volume \
    --enable-filter=aresample \
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
