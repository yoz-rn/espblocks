export CAFILE="$HOME/Projects/espblocks/secrets/pki/ca.crt"
read -rs PW_BACKEND

mosquitto_sub -h 192.168.100.92 -p 8883 --cafile "$CAFILE"   -u backend -P "$PW_BACKEND" -t 'espblocks/+/telemetry' -v