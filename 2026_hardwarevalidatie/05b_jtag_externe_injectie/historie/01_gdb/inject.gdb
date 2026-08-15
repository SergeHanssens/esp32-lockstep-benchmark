# JTAG externe foutinjectie in de ONGEWIJZIGDE 05_lockstep_kern firmware.
# Drie bitflips, elk vlak voor een checkpoint-vergelijking, via OpenOCD/USB-JTAG.
# Elke injectie draait exact één bit om in gedeeld geheugen en wordt daarna
# door de checkercore gedetecteerd -> zichtbaar in de seriele log.
set pagination off
set confirm off
set remotetimeout 20
target remote :3333
monitor reset halt

# --- Injectie 1: INVOER (verdict 0x1) -> flip een bit in k.crc_invoer ---
break *0x4200859a
ignore 1 30
commands 1
  set variable k.crc_invoer = k.crc_invoer ^ 0x00000001
  delete 1
  continue
end

# --- Injectie 2: VERWERKING (verdict 0x2) -> flip een bit in k.res_app ---
break *0x420085d1
ignore 2 60
commands 2
  set variable k.res_app = k.res_app ^ 0x00000040
  delete 2
  continue
end

# --- Injectie 3: UITVOER (verdict 0x4) -> flip een bit in k.uitvoer ---
break *0x420085ef
ignore 3 90
commands 3
  set variable k.uitvoer = k.uitvoer ^ 0x00000400
  delete 3
  continue
end

continue
