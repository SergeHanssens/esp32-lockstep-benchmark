# ESP32 Lockstep Benchmark

🚀 Multicore benchmark en lockstep-checker op ESP32-S3, gebouwd met FreeRTOS en ESP-IDF. Ontworpen voor betrouwbaarheidsexperimenten en foutdetectie via lockstep-controle.

## 📂 Structuur

| Map                         | Beschrijving                                                  |
|----------------------------|---------------------------------------------------------------|
| `01_esp_lockstep_minimal/` | Minimale multicore setup met eenvoudige synchronisatie        |
| `02_enhanced_lockstep/`    | Uitgebreide versie met timing-analyse, CSV-logging en checker |
| `03_enhanced_lockstep_final/` | Final versie met gescheiden modules, metadata en logging    |

## ⚙️ Features

- Twee taken op verschillende cores (Core0 & Core1)
- CoreMark-stub als benchmark workload
- Synchronisatie via FreeRTOS queues of barriers
- Realtime timing-analyse en CSV-logging voor evaluatie
- Lockstep mismatch- en deadline-analyse

## 📋 Benodigdheden

- ESP32-S3 DevKit (getest op DevKitC)
- [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)
- Python 3.11 (voor ESP-IDF tools)
- USB-UART verbinding (115200 baud)

## 🔧 Build & Flash

```bash
# Ga naar een van de submappen (bv. enhanced versie)
cd 02_enhanced_lockstep

# Configureer en compileer
idf.py set-target esp32s3
idf.py menuconfig
idf.py build

# Flashen en monitoren
idf.py -p COM6 flash monitor
