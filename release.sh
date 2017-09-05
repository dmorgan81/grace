#!/bin/sh

GEOCODE_API_KEY=OdzFiSpgRksizA4EM5GACUjQjqq2AEg7
API_KEY=$(echo -n "'\""; echo -n "$GEOCODE_API_KEY"; echo -n "\"'")

pebble clean
echo CFLAGS="-DGEOCODE_API_KEY=$API_KEY" pebble build
CFLAGS="-DGEOCODE_API_KEY=$API_KEY" pebble build

