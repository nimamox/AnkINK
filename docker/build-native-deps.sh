#!/bin/sh
set -eu

OPENSSL_VERSION=${OPENSSL_VERSION:?}
CURL_VERSION=${CURL_VERSION:?}
. /usr/local/lib/kindle-abi.sh

curl -fsSL "https://www.openssl.org/source/openssl-$OPENSSL_VERSION.tar.gz" \
  -o /tmp/openssl.tar.gz
curl -fsSL "https://curl.se/download/curl-$CURL_VERSION.tar.xz" \
  -o /tmp/curl.tar.xz

for KINDLE_DEP_ABI in armel armhf; do
  kindle_abi_configure "$KINDLE_DEP_ABI"
  KINDLE_PREFIX="/opt/kindle-sdk/$KINDLE_ABI/usr"
  OPENSSL_SOURCE="/tmp/openssl-$KINDLE_ABI"
  CURL_SOURCE="/tmp/curl-$KINDLE_ABI"

  mkdir -p "$OPENSSL_SOURCE" "$CURL_SOURCE"
  tar -xzf /tmp/openssl.tar.gz -C "$OPENSSL_SOURCE" --strip-components=1
  tar -xJf /tmp/curl.tar.xz -C "$CURL_SOURCE" --strip-components=1

  (
    cd "$OPENSSL_SOURCE"
    CFLAGS="-O2 $KINDLE_ARCH_FLAGS" \
      ./Configure linux-armv4 no-shared no-tests no-module \
        "--cross-compile-prefix=$KINDLE_GNU_TRIPLET-" \
        "--prefix=$KINDLE_PREFIX"
    make -j"$(nproc)"
    make install_sw
  )

  (
    cd "$CURL_SOURCE"
    PKG_CONFIG_LIBDIR="$KINDLE_PREFIX/lib/pkgconfig" \
      CPPFLAGS="-I$KINDLE_PREFIX/include" \
      LDFLAGS="-L$KINDLE_PREFIX/lib" \
      CFLAGS="-O2 $KINDLE_ARCH_FLAGS" \
      ./configure "--host=$KINDLE_GNU_TRIPLET" \
        "--prefix=$KINDLE_PREFIX" \
        --disable-shared --enable-static --disable-ldap --disable-ldaps \
        --disable-rtsp --disable-dict --disable-telnet --disable-tftp \
        --disable-pop3 --disable-imap --disable-smb --disable-smtp \
        --without-zlib --without-brotli --without-zstd --without-libpsl \
        --without-libidn2 "--with-openssl=$KINDLE_PREFIX"
    make -j"$(nproc)"
    make install
  )

  rm -rf "$OPENSSL_SOURCE" "$CURL_SOURCE"
done

rm -f /tmp/openssl.tar.gz /tmp/curl.tar.xz
