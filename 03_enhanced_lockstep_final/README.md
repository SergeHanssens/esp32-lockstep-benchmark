
# Enhanced Lockstep Benchmark - ESP32-S3

## 🎯 Doel

Deze FreeRTOS-gebaseerde testapplicatie demonstreert een **softwarematige lockstep-architectuur** op de ESP32-S3.  
Het project is opgezet voor gebruik in een masterproef aan KU Leuven - Industriële Ingenieurswetenschappen.

---

## ⚙️ Werking

Er worden **twee taken** gelanceerd:

- `task_core0_main()` draait op **Core 0** en voert een dummy benchmark uit.
- `task_core1_checker()` draait op **Core 1** en voert dezelfde workload uit, vergelijkt resultaten en detecteert afwijkingen.

De synchronisatie gebeurt met een **FreeRTOS queue**, wat een eenvoudige lockstep-simulatie toelaat.

---

## 📦 Bestandsoverzicht

| Bestand              | Beschrijving                            |
|----------------------|------------------------------------------|
| `main.c`             | Startpunt van het programma              |
| `lockstep_task.c/h`  | Taken op Core 0 en Core 1                |
| `lockstep_sync.c/h`  | Queue-synchronisatie tussen cores        |
| `coremark_stub.c/h`  | Dummy workload                           |
| `timing.c/h`         | Deadline-detectie                        |
| `csv_logger.c/h`     | CSV-logging naar UART                    |
| `metadata.c/h`       | Testbordinformatie voor log              |

---

## 🛠️ Compileren & flashen

1. Open een terminal en navigeer naar het project:
```bash
cd enhanced_lockstep
```

2. Selecteer de juiste ESP32 target:
```bash
idf.py set-target esp32s3
```

3. Compileer, flash en open seriële monitor:
```bash
idf.py build flash monitor
```

---

## 🧪 Output

Elke seconde krijg je een CSV-regel in dit formaat:

```
CSV,<seq>,<ref_score>,<check_score>,<delta>,<us>,<DEADLINE>,<MATCH>
```

Bijvoorbeeld:

```
CSV,25,10234,10240,6,9400,OK,MATCH
CSV,26,10234,9800,434,12100,MISS,MISMATCH
```

---

## 📚 Toepassing in masterproef

Dit project kan dienen als:

- Lockstep proof-of-concept op embedded microcontroller
- Basis voor performantievergelijking tussen sync-methodes
- Startpunt voor echte benchmark-integratie zoals CoreMark
- Logging-generator voor CSV-analyse in Python of MATLAB

---

## 📍 Opmerkingen

- Deze implementatie gebruikt nog een **gesimuleerde workload**
- Synchronisatie gebeurt via **Queue** maar uitbreidbaar naar Semaphore/Shared Memory
- Uitgebreide timinganalyse zit in `timing.c`
- CSV-output is klaar voor gebruik in thesisdocumentatie of grafiekplotting

---

Gemaakt met ❤️ voor real-time embedded nerds.
