#!/bin/sh
SOCKET="/tmp/mu-browser-$(date +%s%N).sock"

# Starte Qt Backend im Hintergrund
mu-browser --socket "$SOCKET" --url "https://schinkenpizza.de" &
BROWSER_PID=$!

# Warten, bis das Socket aktiv ist
sleep 0.5

# Starte Vala Controller und übergebe Socket UND die Browser-PID
mu-controller "$SOCKET" "$BROWSER_PID"

# Aufräumen, sobald der Controller sich schließt
kill $BROWSER_PID 2>/dev/null
rm -f "$SOCKET"