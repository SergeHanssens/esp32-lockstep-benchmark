import serial, time
s = serial.Serial('COM3', 115200, timeout=1)
# reset het bord via RTS zodat we de boot vanaf het begin zien
s.setDTR(False)
s.setRTS(True)
time.sleep(0.1)
s.setRTS(False)
t = time.time()
data = b''
while time.time() - t < 12:
    data += s.read(4096)
s.close()
txt = data.decode('utf-8', errors='replace')
open(r'D:\thesis\hello_world\boot_log_20260808.txt', 'w', encoding='utf-8').write(txt)
print(txt[-1500:])
