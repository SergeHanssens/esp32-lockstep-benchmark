import serial, time
s = serial.Serial('COM3', 115200, timeout=1)
s.setDTR(False)
s.setRTS(True)
time.sleep(0.1)
s.setRTS(False)
t = time.time()
data = b''
klaar = b'KLAAR: beide passes'
while time.time() - t < 95:
    data += s.read(4096)
    if klaar in data:
        time.sleep(1)
        data += s.read(4096)
        break
s.close()
txt = data.decode('utf-8', errors='replace')
open(r'D:\thesis\04_coremark\coremark_baseline_log_20260808.txt', 'w', encoding='utf-8').write(txt)
start = txt.find('=== EEMBC CoreMark')
print(txt[start:] if start >= 0 else txt[-3000:])
