#!/usr/bin/env bash
# Regenerates THIRD_PARTY_NOTICES.txt from the license files of the libraries CMake fetched for the
# online client. Run after a host build: tools/release/third-party-notices.sh [build-linux/_deps]
set -euo pipefail
DEPS="${1:-build-linux/_deps}"
{
  echo "Arcana Survivors usa as bibliotecas abaixo no modo online (PC). Os textos das licenças seguem."
  for lib in "IXWebSocket 11.4.6|ixwebsocket-src/LICENSE.txt" "Mbed TLS 3.6.4|mbedtls-src/LICENSE" \
             "zlib 1.3.1|zlib-src/LICENSE" "nlohmann/json 3.12.0|nlohmann_json-src/LICENSE.MIT"; do
    printf '\n==== %s ====\n\n' "${lib%%|*}"
    cat "$DEPS/${lib##*|}"
  done
} > THIRD_PARTY_NOTICES.txt
echo "THIRD_PARTY_NOTICES.txt atualizado"
