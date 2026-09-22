#!/usr/bin/env bash
# Cross-play smoke test: the Node server of ../meu-game in a container and two headless C++ bots
# (arcana_desktop --online-bot) in the same room. Needs a host build first (build-all.sh host).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GAME="$(cd "$ROOT/../meu-game" && pwd)"
cd "$ROOT"
MOUNT="$ROOT"; GAME_MOUNT="$GAME"
if [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* ]]; then
  export MSYS_NO_PATHCONV=1
  MOUNT="$(pwd -W)"; GAME_MOUNT="$(cd "$GAME" && pwd -W)"
fi
# -f, not -x: on Windows the NTFS bind mount doesn't surface the exec bit that host-build.sh set
# inside the container, even though a fresh `docker run` against the same volume honors it (verified).
[[ -f build-linux/arcana_desktop ]] || { echo "rode antes: tools/docker/build-all.sh host"; exit 2; }
[[ -d "$GAME/node_modules/ws" ]] || { echo "rode antes: npm --prefix ../meu-game install"; exit 2; }

NET=arcana-e2e
cleanup() { docker rm -f arcana-e2e-server >/dev/null 2>&1 || true; docker network rm "$NET" >/dev/null 2>&1 || true; }
trap cleanup EXIT
cleanup
docker network create "$NET" >/dev/null
docker run -d --name arcana-e2e-server --network "$NET" -v "$GAME_MOUNT":/game -w /game -e PORT=8081 node:22-slim node server/server.js >/dev/null

# Bounded readiness wait: poll the server container's log for its "listening on" line instead of
# guessing a fixed sleep.
READY=0
for _ in $(seq 1 30); do
  if docker logs arcana-e2e-server 2>&1 | grep -q "listening on"; then
    READY=1
    break
  fi
  sleep 1
done
if [[ "$READY" -ne 1 ]]; then
  echo "online-e2e: FALHOU (servidor não ficou pronto em 30s)"
  docker logs arcana-e2e-server | tail -20
  exit 1
fi

bot() {
  timeout 180s docker run --rm --network "$NET" -v "$MOUNT":/src -w /src -e SDL_AUDIODRIVER=dummy arcana-host \
    build-linux/arcana_desktop --online-bot "$1" --bot-players 2 --server ws://arcana-e2e-server:8081 --insecure-ws --frames 1800 --mute
}
bot create > build-linux/e2e-host.log 2>&1 &
HOST=$!
sleep 4
guest_status=0
bot join > build-linux/e2e-guest.log 2>&1 || guest_status=$?
host_status=0
wait "$HOST" || host_status=$?

grep -h online_players build-linux/e2e-host.log build-linux/e2e-guest.log || true
if [[ "$host_status" -eq 0 && "$guest_status" -eq 0 ]] \
   && grep -q "online_players=2" build-linux/e2e-host.log \
   && grep -q "online_players=2" build-linux/e2e-guest.log; then
  echo "online-e2e: ok"
else
  echo "online-e2e: FALHOU (host_status=$host_status guest_status=$guest_status, logs em build-linux/e2e-*.log)"
  echo "--- e2e-host.log ---"
  tail -20 build-linux/e2e-host.log || true
  echo "--- e2e-guest.log ---"
  tail -20 build-linux/e2e-guest.log || true
  echo "--- server log ---"
  docker logs arcana-e2e-server | tail -20
  exit 1
fi
