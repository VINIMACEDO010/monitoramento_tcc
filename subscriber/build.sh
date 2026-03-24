#!/bin/sh
set -e

target="${1:-build}"

build=$(readlink -f "$0")
src="$(dirname "$build")"
bin="$src/bin"
mkdir -p "$bin"

main="$src/subscriber.c"
main2="$src/publisher_mqtt.c"
main3="$src/publisher_modbus.c"

program="$bin/subscriber"
program2="$bin/publisher_mqtt"
program3="$bin/publisher_modbus"

CC=${CC:-cc}
CFLAGS="${CFLAGS} -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Wno-unused-function -Wno-unused-macros"

case "$target" in
  debug)
    CPPFLAGS="${CPPFLAGS} -DBIODATA_DEBUG=1"
    CFLAGS="${CFLAGS} -g -O0"
    ;;
  *)
    CPPFLAGS="${CPPFLAGS} -DBIODATA_DEBUG=0"
    CFLAGS="${CFLAGS} -g -O2"
    ;;
esac

LDFLAGS_MODBUS="$(pkg-config --cflags --libs libmodbus)"
LDFLAGS_MOSQUITTO="$(pkg-config --cflags --libs libmosquitto)"
LDFLAGS_LIBPQ="$(pkg-config --cflags --libs libpq)"

echo "Compilando subscriber..."
$CC $CPPFLAGS $CFLAGS -o "$program" "$main" $LDFLAGS_MOSQUITTO $LDFLAGS_LIBPQ $LDFLAGS_MODBUS

echo "Compilando publisher_mqtt..."
$CC $CPPFLAGS $CFLAGS -o "$program2" "$main2" $LDFLAGS_MOSQUITTO

echo "Compilando publisher_modbus..."
$CC $CPPFLAGS $CFLAGS -o "$program3" "$main3" $LDFLAGS_MODBUS

echo "Build concluído."