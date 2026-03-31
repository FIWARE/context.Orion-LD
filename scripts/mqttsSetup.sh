#!/bin/bash
# Copyright 2026 FIWARE Foundation e.V.
#
# This file is part of Orion-LD Context Broker.
#
# Orion-LD Context Broker is free software: you can redistribute it and/or
# modify it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# Orion-LD Context Broker is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
# General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with Orion-LD Context Broker. If not, see http://www.gnu.org/licenses/.
#
# For those usages not covered by this license please contact with
# orionld at fiware dot org
#
# mqttsSetup.sh - Generate self-signed certs and start a mosquitto TLS listener
#
# Usage:
#   mqttsSetup.sh [start|stop|certs]
#
#   start  - Generate certs (if needed), write mosquitto config, and start the MQTTS listener
#   stop   - Stop the MQTTS mosquitto listener
#   certs  - Only generate certificates
#

MQTTS_PORT=${MQTTS_BROKER_PORT:-8883}
CERTS_DIR="/tmp/mosquitto-mqtts"
CONF_FILE="/tmp/mosquitto-mqtts.conf"
PID_FILE="/tmp/mosquitto-mqtts.pid"


# -----------------------------------------------------------------------------
#
# generateCerts - Create a CA + server cert for localhost
#
function generateCerts()
{
  if [ -f "$CERTS_DIR/server.crt" ] && [ -f "$CERTS_DIR/ca.crt" ]; then
    return 0
  fi

  mkdir -p "$CERTS_DIR"

  # CA key and cert
  openssl req -new -x509 -days 3650 -nodes \
    -keyout "$CERTS_DIR/ca.key" \
    -out    "$CERTS_DIR/ca.crt" \
    -subj   "/CN=MQTT Test CA" \
    2>/dev/null

  # Server key
  openssl genrsa -out "$CERTS_DIR/server.key" 2048 2>/dev/null

  # Server CSR
  openssl req -new \
    -key    "$CERTS_DIR/server.key" \
    -out    "$CERTS_DIR/server.csr" \
    -subj   "/CN=localhost" \
    2>/dev/null

  # Sign with CA
  openssl x509 -req -days 3650 \
    -in       "$CERTS_DIR/server.csr" \
    -CA       "$CERTS_DIR/ca.crt" \
    -CAkey    "$CERTS_DIR/ca.key" \
    -CAcreateserial \
    -out      "$CERTS_DIR/server.crt" \
    2>/dev/null

  chmod 644 "$CERTS_DIR"/*
}


# -----------------------------------------------------------------------------
#
# startMqtts - Start mosquitto with TLS on the MQTTS port
#
function startMqtts()
{
  if ! command -v mosquitto &>/dev/null; then
    return 0
  fi

  # Stop any existing instance
  stopMqtts

  generateCerts

  cat > "$CONF_FILE" << EOF
listener $MQTTS_PORT
cafile $CERTS_DIR/ca.crt
certfile $CERTS_DIR/server.crt
keyfile $CERTS_DIR/server.key
allow_anonymous true
EOF

  mosquitto -c "$CONF_FILE" -d
  sleep 0.3

  # Save PID
  pgrep -f "mosquitto -c $CONF_FILE" > "$PID_FILE" 2>/dev/null
}


# -----------------------------------------------------------------------------
#
# stopMqtts - Stop the MQTTS mosquitto listener
#
function stopMqtts()
{
  if [ -f "$PID_FILE" ]; then
    kill $(cat "$PID_FILE") 2>/dev/null
    rm -f "$PID_FILE"
  fi

  # Also kill by config file pattern in case PID file was lost
  pkill -f "mosquitto -c $CONF_FILE" 2>/dev/null || true
}


# -----------------------------------------------------------------------------
#
# Main
#
case "${1:-start}" in
  start)  startMqtts ;;
  stop)   stopMqtts  ;;
  certs)  generateCerts ;;
  *)
    echo "Usage: $0 [start|stop|certs]"
    exit 1
    ;;
esac
