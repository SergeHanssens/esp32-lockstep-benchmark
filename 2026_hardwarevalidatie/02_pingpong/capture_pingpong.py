import serial, time
s = serial.Serial('COM3', 115200, timeout=1)
s.setDTR(False)
s.setRTS(True)
time.sleep(0.1)
s.setRTS(False)
t = time.time()
data = b''
while time.time() - t < 8:
    data += s.read(4096)
s.close()
txt = data.decode('utf-8', errors='replace')
open(r'D:\thesis\02_pingpong\pingpong_log_20260808.txt', 'w', encoding='utf-8').write(txt)
start = txt.find('=== Pingpong')
print(txt[start:start+2200] if start >= 0 else txt[-2200:])
