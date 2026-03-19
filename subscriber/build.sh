#!/bin/sh

# shellcheck disable=SC2086
set -e

alias trace_on='set -x'
alias trace_off='{ set +x; } 2>/dev/null'

target="${1:-debug}"

build=$(readlink -f "$0")
src="$(dirname "$build")"
bin="$src/bin/"
mkdir -p "$bin"

echo "src=$src"
echo "bin=$bin"

main="subscriber.c"
main2="publisher_mqtt.c"
main3="publisher_modbus.c"

program="${bin}/${main%.c}"
program2="${bin}/${main2%.c}"
program3="${bin}/${main3%.c}"

main="${src}/${main}"
main2="${src}/${main2}"
main3="${src}/${main3}"

CC=${CC:-cc}
CFLAGS="$CFLAGS -std=c99 -D_DEFAULT_SOURCE"
CFLAGS="$CFLAGS -Wall -Wextra -Wfatal-errors"
CFLAGS="$CFLAGS -Wno-unused-macros"
CFLAGS="$CFLAGS -Wno-unused-function"

if [ $CC = "clang" ]; then
    CFLAGS="$CFLAGS -Weverything"
    CFLAGS="$CFLAGS -Wno-unsafe-buffer-usage"
    CFLAGS="$CFLAGS -Wno-format-nonliteral"
    CFLAGS="$CFLAGS -Wno-covered-switch-default"
    CFLAGS="$CFLAGS -Wno-implicit-void-ptr-cast"
    CFLAGS="$CFLAGS -Wno-c++-keyword"
    CFLAGS="$CFLAGS -Wno-float-conversion"
    CFLAGS="$CFLAGS -Wno-padded"
    CFLAGS="$CFLAGS -Wno-cast-align"
fi

LDFLAGS_MODBUS=$(pkg-config --cflags --libs libmodbus)
LDFLAGS_MOSQUITTO=$(pkg-config --cflags --libs libmosquitto)
LDFLAGS_LIBPQ=$(pkg-config --cflags --libs libpq)

case "$target" in
"debug")
    CPPFLAGS="$CPPFLAGS -DBIODATA_DEBUG=1"
    CFLAGS="$CFLAGS -g -fsanitize=undefined"
    ;;
"publish"*)
    CPPFLAGS="$CPPFLAGS -DBIODATA_DEBUG=0"
    CFLAGS="$CFLAGS -g -O1"
    ;;
*)
    CPPFLAGS="$CPPFLAGS -DBIODATA_DEBUG=0"
    CFLAGS="$CFLAGS -g -O2"
    ;;
esac

valgrind_flags="--error-exitcode=1"
valgrind_flags="$valgrind_flags --errors-for-leak-kinds=all"
valgrind_flags="$valgrind_flags --track-origins=yes"
valgrind_flags="$valgrind_flags --show-leak-kinds=all"
valgrind_flags="$valgrind_flags --undef-value-errors=yes"
valgrind_flags="$valgrind_flags --leak-check=full"
valgrind_flags="$valgrind_flags -s"

case "$target" in
"build"|"debug")

    dir="$PWD"
    trace_on
    cd "$src" || exit

    ctags ./*.h ./*.c          2> /dev/null || true
    vtags.sed tags > .tags.vim 2> /dev/null || true
    ln tags      "$src/.." || true
    ln .tags.vim "$src/.." || true

    cd "$dir"

    # subscriber
    $CC $CPPFLAGS $CFLAGS -o "${program}"  "${main}" \
        $LDFLAGS $LDFLAGS_MODBUS $LDFLAGS_MOSQUITTO $LDFLAGS_LIBPQ
    # publisher_mqtt
    $CC $CPPFLAGS $CFLAGS -o "${program2}" "${main2}" \
        $LDFLAGS $LDFLAGS_MOSQUITTO
    # publisher_modbus
    $CC $CPPFLAGS $CFLAGS -o "${program3}" "${main3}" \
        $LDFLAGS $LDFLAGS_MODBUS
    trace_off
    ;;

"publish_mqtt")
    trace_on
    $CC $CPPFLAGS $CFLAGS -o "${program2}" "${main2}" \
        $LDFLAGS $LDFLAGS_MOSQUITTO

    MQTT_CA_CERT=$(find     .. -iname "ca.crt"        | head -n 1) \
    MQTT_CLIENT_CERT=$(find .. -iname "publisher.crt" | head -n 1) \
    MQTT_CLIENT_KEY=$(find  .. -iname "publisher.key" | head -n 1) \
    BIODATA_HOST="68.183.63.19" \
    $(find . -iname "publisher_mqtt")
    trace_off
    ;;
"publish_modbus")
    trace_on
    $CC $CPPFLAGS $CFLAGS -o "${program3}" "${main3}" \
        $LDFLAGS $LDFLAGS_MODBUS

    MQTT_CA_CERT=$(find     .. -iname "ca.crt"        | head -n 1) \
    MQTT_CLIENT_CERT=$(find .. -iname "publisher.crt" | head -n 1) \
    MQTT_CLIENT_KEY=$(find  .. -iname "publisher.key" | head -n 1) \
    BIODATA_HOST="68.183.63.19" \
    $(find . -iname "publisher_modbus")
    trace_off
    ;;
"check")
    trace_on
    scan-build --view -analyze-headers --status-bugs ./build.sh
    CC=gcc CFLAGS="-fanalyzer" ./build.sh
    trace_off
    ;;
*)
    printf "usage: $(basename "$build")"
    printf " [ build / debug / publisher_mqtt / publisher_modbus ]\n"
    exit 1
    ;;
esac
