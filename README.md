![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)

# ESP32 Lockstep Benchmark

🚀 **Multicore benchmark en lockstep-checker op ESP32-S3**, gebouwd met FreeRTOS en ESP-IDF.  
Ontworpen voor betrouwbaarheidsexperimenten en foutdetectie via lockstep-controle.

---

## 📂 Structuur

| Map                        | Beschrijving                                                        |
|---------------------------|---------------------------------------------------------------------|
| `01_esp_lockstep_minimal/`        | Minimale multicore setup met eenvoudige synchronisatie             |
| `02_enhanced_lockstep/`           | Uitgebreide versie met timing-analyse, CSV-logging en checker      |
| `03_enhanced_lockstep_final/`     | Final versie met gescheiden modules, metadata en CSV-logging       |

---

## ⚙️ Features

![Lockstep schema](docs/images/Lockstep_schema.jpg)

- Twee parallelle taken op Core0 en Core1
- CoreMark-stub als benchmark workload
- Synchronisatie via FreeRTOS queues of barriers
- Realtime timing-analyse en CSV-logging
- Lockstep mismatch- en deadline-analyse
- Board metadata logging voor traceerbaarheid

---

## 📋 Benodigdheden

- ✅ ESP32-S3 DevKit (getest op DevKitC & Fri3d board)
- ✅ ESP-IDF v5.5 (compatibel met ESP32-S3)
- ✅ Python 3.11 (voor de ESP-IDF toolchain)
- ✅ USB-UART verbinding (115200 baud)
- ✅ Windows PowerShell of Git Bash met ESP-IDF omgeving geactiveerd

---

## 🔧 Build & Flash

```bash
# Navigeer naar een submap (bijvoorbeeld de enhanced versie)
cd 02_enhanced_lockstep

# Configureer target en project
idf.py set-target esp32s3
idf.py menuconfig

# Compileer project
idf.py build

# Flashen en monitoren
idf.py -p COM6 flash monitor
```
⚠️ Pas COM6 aan naar de seriële poort van jouw DevKit op Windows.

---

## 📈 CSV Output

Bij het uitvoeren van de benchmarks krijg je regelmatige CSV-output zoals:

```
CSV,seq,ref_score,chk_score,delta,duration_us,deadline,match  
CSV,15,2521830716,2521830716,0,1064,OK,OK
```

Deze kun je opslaan en analyseren in Python, Excel, of andere analysetools.  
Handig voor visualisatie van betrouwbaarheid, timing en foutdetectie.

---

## 📷 Hardware foto's

### ESP32-S3 DevKitC
![ESP32-S3 DevKitC voorzijde](docs/images/ESP32-S3-DevKitC-1_voor.jpg)
![ESP32-S3 DevKitC achterzijde](docs/images/ESP32-S3-DevKitC-1_achter.jpg)
[specs](https://www.amazon.nl/dp/B0D6B2CYNW)

### Fri3d Badge 2024
![Fri3d Badge voorzijde](docs/images/ESP32-S3_Fri3d_badge_voor.jpg)
![Fri3d Badge achterzijde](docs/images/ESP32-S3_Fri3d_badge_achter.jpg)
[specs](https://github.com/Fri3dCamp/badge_2024_hw)

### ESP32WROOM32 ontwikkelbord (Wemos)
![ESP32WROOM32 Wemos voorzijde](docs/images/ESP32-ESP32WROOM32_Wemos_voor.jpg)
![ESP32WROOM32 Wemos achterzijde](docs/images/ESP32-ESP32WROOM32_Wemos_achter.jpg)
[specs](https://wiki.geekworm.com/WEMOS_ESP32_Board_with_OLED)

### ESP32WROOM32 ontwikkelbord (Joy-it)
![ESP32WROOM32 Joy-it voorzijde](docs/images/ESP32-ESP32WROOM32_Joy-it_voor.jpg)
![ESP32WROOM32 Joy-it achterzijde](docs/images/ESP32-ESP32WROOM32_Joy-it_achter.jpg)
[specs](https://joy-it.net/en/products/SBC-NodeMCU-ESP32)

### ESP32WROOM32 ontwikkelbord (Otronic)
![ESP32WROOM32 Otronic voorzijde](docs/images/ESP32-ESP32WROOM32_Otronic_voor.jpg)
![ESP32WROOM32 Otronic achterzijde](docs/images/ESP32-ESP32WROOM32_Otronic_achter.jpg)
[specs](https://www.otronic.nl/nl/esp32-wroom-4mb-devkit-v1-met-losse-header-pins.html)

### ESP32-P4 DevKit (Olimex)
![ESP32-P4 Olimex voorzijde](docs/images/ESP32-P4_DevKit_voor.jpg)
![ESP32-P4 Olimex achterzijde](docs/images/ESP32-P4_DevKit_achter.jpg)
[specs](https://www.olimex.com/Products/IoT/ESP32-P4/ESP32-P4-DevKit/open-source-hardware)

---

## 🖼️ Voorbeeld output of architectuur

![Lockstep schema](docs/images/lockstep_architecture.png)

---

## 🪪 Licentie

MIT License — vrij te gebruiken, aan te passen en te delen.  
Zie het bestand [`LICENSE`](./LICENSE) voor details.

---

## 📫 Feedback of bijdragen?

👋 Pull requests zijn welkom!  
Of open gerust een [Issue](https://github.com/SergeHanssens/esp32-lockstep-benchmark/issues) bij vragen, bugs of suggesties.
