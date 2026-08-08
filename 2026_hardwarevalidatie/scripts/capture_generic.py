import sys, time
import serial

port, seconds, outfile = sys.argv[1], float(sys.argv[2]), sys.argv[3]
s = serial.Serial(port, 115200, timeout=1)
s.dtr = False
s.rts = True
time.sleep(0.1)
s.rts = False
data = b""
t0 = time.time()
while time.time() - t0 < seconds:
    chunk = s.read(4096)
    if chunk:
        data += chunk
s.close()
text = data.decode("utf-8", errors="replace")
with open(outfile, "w", encoding="utf-8") as f:
    f.write(text)
print(text)
print(f"--- opgeslagen in {outfile} ({len(text)} tekens) ---")
