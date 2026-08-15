#!/usr/bin/env python3
"""Passieve seriele lezer: opent de poort en logt N seconden ZONDER RTS/DTR te
togglen, zodat de reset uitsluitend door OpenOCD/gdb bestuurd wordt."""
import sys, time, serial

port = sys.argv[1]
secs = float(sys.argv[2])
out  = sys.argv[3]

s = serial.Serial()
s.port = port
s.baudrate = 115200
s.timeout = 0.2
# reset-lijnen expliciet stabiel houden (geen auto-reset puls)
s.dtr = False
s.rts = False
s.open()
try:
    s.dtr = False
    s.rts = False
except Exception:
    pass

t0 = time.time()
buf = bytearray()
with open(out, "wb") as f:
    while time.time() - t0 < secs:
        data = s.read(4096)
        if data:
            f.write(data)
            f.flush()
            buf += data
s.close()
sys.stdout.write(buf.decode("utf-8", "replace"))
