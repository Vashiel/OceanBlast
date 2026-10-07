# OceanBlast: Boot-Fortschritt und Meilensteine

Stand: 7. Oktober 2026. Dieses Dokument erfasst alle reproduzierbaren Boot-Meilensteine, verifizierte Reverse-Engineering-Ergebnisse, offene Annahmen und das jeweils nächste konkrete Hindernis.

---

## 1. Verifizierte Meilensteine

### Meilenstein 1: Steppingstone Bootloader (4 KB SRAM)
* **Status:** Erfolgreich verifiziert.
* **Beleg:**
  * CPU startet bei `0x00000000` (internes S3C2410 Steppingstone Boot-SRAM).
  * Die ersten 8 Seiten (je 512 Bytes Nutzdaten) werden aus dem Cartridge-NAND geladen.
  * Der 4-KB-Code konfiguriert den S3C2410 Memory Controller (`0x48000000`), initialisiert SDRAM bei `0x30000000`, schaltet den NAND Flash Controller (`0x4E000000`) scharf, kopiert 136 KB U-Boot nach `0x30F80000` und springt zu `0x30F81B50` (`start_armboot`).

### Meilenstein 2: U-Boot 1.1.2 Initialisierung & UART0-Konsole
* **Status:** Erfolgreich verifiziert.
* **Beleg (Original UART0-Stream):**
  ```text
  U-Boot 1.1.2.greyinnovation.EOL (Dec  1 2005 - 12:30:47)
  U-Boot code: 30F80000 -> 30FA15C0  BSS: -> 30FA5BF8
  RAM Configuration:
  Bank #0: 30000000 16 MB
  FLASH Mpf id is 0x0090, ## Unknown FLASH on Bank 0: ID 0xffff, Size = 0x00000000 = 0 MB
  ## Found CFI FLASH on Bank 0: ID 0xffff, Size = 0x00000000 = 0 MB
  Flash:  0 kB
  NAND:
  ---------------------------------
  Digiblast Nand Flash Boot - release version 1.3.3_I(Fixed_NAND_BOOT_ROM) (Grey Innovation). 
  ---------------------------------
  Flash chip found:
           Manufacturer ID: 0xEC, Chip ID: 0x73 (Samsung KM29U128T)
  1 flash chips found. Total nand_chip size: 16 MB
    16 MB
  *** Warning - bad CRC, using default environment
  In:    serial
  Out:   serial
  Err:   serial
  Hit any key to stop autoboot:  0 
  NAND read: device 0 offset 180224, size 16384, cmd 5... 
   16384 bytes read: OK
  ## Executing script at 30400000
  ```

### Meilenstein 3: NAND-Layout & Boot-Skript Rekonstruktion
* **Status:** Erfolgreich verifiziert.
* **Beleg:**
  * Bei NAND-Offset `180224` (`0x2C000`, Seite 352) liegt ein valides U-Boot uImage Script (`IH_MAGIC = 0x27051956`), das mit folgendem Inhalt nach `0x30400000` geladen wird:
    ```sh
    nidc check ${nkg} 4 ec 79 a5 c0
    nidc check ${nkg} 4 98 76 00 ff
    nidc check ${nkg} 4 98 73 00 ff
    nidc check ${nkg} 4 98 75 00 ff
    nidc check ${nkg} 0
    echo Checking Battery Voltage
    checkbattery boot 620 830 
    echo Displaying splash0.bin
    nand read 0x30300000 0x30000 0x10000
    splash 0x30300000
    echo Loading kernel
    nand read 0x30c00000 0x00050000 0x001c0000
    echo Display splash1.bin
    nand read 0x30300000 0x40000 0x10000
    splash 0x30300000
    nand read 0x30310000 0x40000 0x10000
    set bootargs console=ttySAC0,115200 mem=16M devfs=mount panic=30 init=/linuxrc root=/dev/mtdblock/5 ro rootfstype=squashfs splash=0x30310000
    bootm 0x30c00000
    ```

### Meilenstein 4: `nidc` Security Handshake & `checkbattery` ADC
* **Status:** Erfolgreich verifiziert.
* **Beleg:**
  * Emulation des Toshiba NAND Chip-IDs (`0x98, 0x73, 0x00, 0xFF`) für 16 MB Cartridges.
  * `nidc check ${nkg} 4 98 73 00 ff` besteht mit `X.X....P` und setzt `nkg=1`.
  * S3C2410 ADC-Register (`ADCCON` bei `0x58000000` mit `ECFLG` Bit 15 gesetzt, `ADCDAT0` bei `0x5800000C` mit 750 Counts) liefern korrekte Batteriespannung.
  * U-Boot Log:
    ```text
    X.X....P
    .XChecking Battery Voltage
    adc_read(377): 0 ADCCON [0xffc8] - adc_read(410): CHANNEL[1] = 750, 1, ADCCON[0xffc8]
    disable_interrupts_checkbattery(483): 
    exit: clear all sub pending registers
    ```

### Meilenstein 5: Kerneltransfer & `bootm` Dekompression
* **Status:** Erfolgreich verifiziert.
* **Beleg:**
  * U-Boot lädt den 1,835 MB Kernel-Stream fehlerfrei via NAND (`nand read 0x30c00000 0x00050000 0x001c0000`).
  * `bootm 0x30c00000` verifiziert den uImage Header und CRC-Prüfsumme.
  * Der gzip-komprimierte Kernel wird nach `0x30008000` dekomprimiert:
    ```text
    ## Booting image at 30c00000 ...
       Image Name:   
       Image Type:   ARM Linux Kernel Image (gzip compressed)
       Data Size:    825400 Bytes = 806.1 kB
       Load Address: 30008000
       Entry Point:  30008000
       Verifying Checksum ... OK
       Uncompressing Kernel Image ... OK

    Starting kernel ...
    ```

### Meilenstein 6: ARM920T CP15 MMU & Linear Page Map
* **Status:** Erfolgreich verifiziert.
* **Beleg:**
  * CPU springt bei `0x30008000` in Linux `head-armv.S` mit `r1=0x294` (SMDK2410 Machine Type) und `r2=0x30000100` (ATAGS Parameterblock).
  * CP15 c0 liefert `0x41009200` (ARM920T ARMv4T ID Code).
  * Linux baut Translation Table bei `0x30004000` auf, schaltet MMU und Caches über CP15 c1 ein und springt in den virtuellen Adressraum (`0xC0008000`).

### Meilenstein 7: S3C2410A SoC-Erkennung & Subsystem-Treiber
* **Status:** Erfolgreich verifiziert.
* **Beleg (Echtes Linux Kernel 2.6.11 dmesg-Log):**
  * GPIO `GSTATUS1` bei `0x560000B0` (virtuell `0xF0E000B0`) liefert S3C2410A Chip ID `0x32410002`.
  * Linux initialisiert S3C2410 Clocks, Speicherzonen, Slab Allocator und S3C2410 Timer 4:
    ```text
    Linux version 2.6.11 (shaun.adolphson@malbec.greyinnovation.com) (gcc version 3.4.1) #7 Tue Dec 20 16:04:24 EST 2005
    CPU: ARM920Tid(wb) [41009200] revision 0 (ARMvundefined/unknown)
    Machine: DIGIBLAST
    Memory policy: ECC disabled, Data cache writeback
    CPU S3C2410A (id 0x32410002)
    S3C2410: core 180.000 MHz, memory 90.000 MHz, peripheral 45.000 MHz
    S3C2410 Clock control, (c) 2004 Simtec Electronics
    USB Power Control, (c) 2005 Grey Innovation
    Built 1 zonelists
    Kernel command line: console=ttySAC0,115200 mem=16M devfs=mount panic=30 init=/linuxrc root=/dev/mtdblock/5 ro rootfstype=squashfs splash=0x30310000
    irq: clearing pending status ffffffff
    irq: clearing subpending status 000007ff
    PID hash table entries: 128 (order: 7, 2048 bytes)
    timer tcon=00500008, tcnt 927b, tcfg 0000020c,00000000, usec 00002222
    Console: colour dummy device 80x30
    Dentry cache hash table entries: 4096 (order: 2, 16384 bytes)
    Inode-cache hash table entries: 2048 (order: 1, 8192 bytes)
    Memory: 16MB = 16MB total
    Memory: 14388KB available (1418K code, 289K data, 68K init)
    Calibrating delay loop... 
    ```

---

## 2. Aufklärung der Schleife bei `0x30f90e88`

* **Befund:** Im vorherigen Lauf stoppte die CPU bei `0x30f90e88` mit der Konsolenausgabe `.XXXX`.
* **Disassembly & Beweis:**
  1. Der Befehl ist **nicht** `checkbattery`, sondern **`nidc`** *(NAND ID Check)* an Adresse `0x30f91090`, Unterfunktion `0x30f90e50`.
  2. `nidc` liest 4 ID-Bytes vom NAND-Controller (`0x4E00000C`) und vergleicht sie mit den übergebenen Parametern:
     * Bei Übereinstimmung wird `.` ausgegeben und `nkg=1` gesetzt.
     * Bei Nicht-Übereinstimmung wird `X` ausgegeben.
  3. Nach 4 erfolglosen Prüfungen prüft Zeile 5 `nidc check ${nkg} 0`:
     * Wenn `nkg == 0`, führt der Code bei `0x30f90e88` gezielt `b 0x30f90e88` (`0xeafffffe`, Endlosschleife) als Sicherheitsstopp aus!
  4. **Ursache:** Kommerzielle 16-MB-Cartridges nutzen **Toshiba TC58128FT (`0x98, 0x73, 0x00, 0xFF`)**. Der Emulator gab zuvor `0xEC, 0x73` (Samsung) zurück, wodurch alle 4 Prüfungen scheiterten (`.XXXX`).

---

## 3. OOB- und ECC-Status der Dumps

Die Prüfung aller 11 vorhandenen Dumps ergab zwei unterschiedliche Dump-Kategorien:
1. **Dumps mit echten OOB-Daten:**
   * `Cuccioli Cerca Amici [G] (IT).bin` (20.616 Seiten mit OOB)
   * `Gormiti Agguato nella Valle [G] (IT).bin` (32.351 Seiten mit OOB)
   * `Gormiti Lotta Oscura [G] (IT).bin` (57.914 Seiten mit OOB)
   * *Verhalten:* Für diese Dumps müssen die echten Rohdaten aus dem Dump bytegenau zurückgegeben werden.
2. **Dumps mit geblanktem OOB (`0xFF`):**
   * `Rayman 3`, `Spider-Man`, `Crazy Jack`, `DigiQUAD`, `Superstar Chefs` etc. haben 100% `0xFF` im OOB-Bereich.
   * *Verhalten:* Wie in MAME (`digiblast_cart.xml`) dokumentiert, muss für diese Dumps die Linux MTD 256-Byte ECC dynamisch berechnet werden, da U-Boot sonst mit `Failed ECC read` abbricht.

---

## 4. Offene Annahmen & nächste Hürden

1. **Timer 4 Interrupt & `calibrate_delay()` Hürde:**
   * Linux hängt aktuell bei `Calibrating delay loop...` in `calibrate_delay()` (`0xc000bbe4`).
   * Code: `ticks = jiffies; while (ticks == jiffies);`
   * Ursache: Der Kernel hat Interrupts freigeschaltet (CPSR Mode 0x13, I-Bit = 0). Timer 4 feuert jedoch noch keinen periodischen IRQ in den Interrupt Controller (`0x4A000000`).
   * Lösung: S3C2410 IRQ-Controller (`SRCPND` / `INTPND` / `INTOFFSET` = 14) mit Timer 4 koppeln und CPU IRQ-Exception Handling nach High-Vector `0xFFFF0018` ausführen.
2. **Linux Framebuffer & LCD-Controller:**
   * S3C2410 LCD Controller (`0x4D000000`) konfigurieren und Framebuffer-Adresse (`0x30310000` / `0x30300000`) für grafische Bildausgabe anbinden.
3. **SquashFS Root-Dateisystem & Userspace Init:**
   * Linux Kernel bindet Cartridge MTD Partition 5 (`/dev/mtdblock/5`) als SquashFS ein und startet `/linuxrc`.
