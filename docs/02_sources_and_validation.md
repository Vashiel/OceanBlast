# OceanBlast: Quellen und technischer Prüfstand

Stand: 6. Oktober 2026. Diese Notiz ergänzt die vorhandene Dokumentation. Der Arbeitsbaum enthält laufende Änderungen; diese Untersuchung verändert keinen Emulatorcode.

## Belastbare externe Grundlagen

- Samsung S3C2410A User's Manual, Revision 1.0, März 2004: https://bitsavers.org/components/samsung/S3C204x/S3C2410/21-S3-C2410A-032004_S3C2410A_Users_Manual_1.0_200403.pdf
  Primärquelle für den Standard-SoC, ARM920T, Register, NAND-Boot, Interrupts, DMA und LCD. Es belegt nicht automatisch alle Eigenschaften der digiBLAST-Platine oder die vollständige Gleichheit des OCEAN-L-20.
- MAME digiBLAST-Treiber: https://github.com/mamedev/mame/blob/master/src/mame/skeleton/digiblast.cpp
  Modelliert ARM9 mit 200 MHz, S3C2410 mit 12-MHz-Eingang und 32 MiB RAM samt Spiegel. Markiert das System als MACHINE_NOT_WORKING und MACHINE_NO_SOUND. Der Kommentar beschreibt beginnendes Booten nach Übernahme der NAND-Anbindung aus ghosteo.cpp und Ergänzung des ID-Kommandos. Das widerlegt die Begründung für eine unbelegte Exklusivitätsbehauptung, beweist aber keinen spielbaren Emulator.
- MAME S3C2410-Modell: https://github.com/mamedev/mame/blob/master/src/devices/machine/s3c2410.cpp
  Vergleichsquelle für Peripherieverhalten; zusätzlich die zugehörigen Header und gemeinsamen S3C24xx-Dateien konsultieren. Ein Emulator ist eine Implementierungsreferenz, keine Messung an der digiBLAST-Hardware.
- MAME Cartridge-Liste: https://github.com/mamedev/mame/blob/master/hash/digiblast_cart.xml
  Referenz für bekannte Dump-Metadaten und Hashes. Ein Eintrag belegt keine Spielbarkeit.

## Korrekturen zur vorhandenen Speicherkarte

Die folgenden S3C2410-Adressen stehen bereits korrekt in src/memory/bus.h. docs/01_memory_map.md weicht davon ab:

| Einheit | Basisadresse | Falscher Eintrag in vorhandener Dokumentation |
| --- | --- | --- |
| IIS Audio | 0x55000000 | 0x5B000000 |
| RTC | 0x57000000 | 0x58000000 |
| ADC | 0x58000000 | 0x59000000 |
| Watchdog | 0x53000000 | dem PWM-Timer bei 0x51000000 zugeordnet |

SPI liegt bei 0x59000000, SD/MMC bei 0x5A000000. Die tatsächliche Verdrahtung von Tasten, Batterieerkennung und externem Audiochip muss gesondert rekonstruiert werden.

## Was der lokale Code aktuell zeigt

- main.cpp lädt eine Cartridge, analysiert sie und führt eine begrenzte Anzahl CPU-Schritte aus. Optionen: --steps N und --trace; Standardlimit: 500000.
- bus.cpp lädt acht Datenbereiche von je 512 Bytes als 4-KiB-Steppingstone. Die Formatwahl zwischen 512 und 528 Bytes pro Seite erfolgt allein über die Dateigröße modulo 528. Das ist eine Heuristik, kein verifiziertes Formatmerkmal.
- SDRAM und sein Spiegel sind vorhanden. NAND-Lesezugriffe, synthetische Geräte-IDs und UART-Ausgabe sind implementiert.
- Viele MMIO-Register sind lediglich gespeicherte Werte. Daraus folgt keine funktionierende Timer-, Interrupt-, DMA-, LCD- oder Audioemulation.
- NAND-OOB-Lesewege erzeugen ECC-Bytes statt die vorhandenen OOB-Bytes eines Raw-Dumps zurückzugeben. Vor weiterer Bootdiagnose klären, welche Dump-Geometrie und ECC-Anordnung die Firmware erwartet. Original-OOB enthält potenziell weitere relevante Metadaten.
- Die Meldung 'Steppingstone Boot Complete' wird bei jedem PC >= 0x30000000 ausgelöst. Auch ein Sprung außerhalb des SDRAM-Bereichs kann sie auslösen. Sie ist kein Beweis für einen erfolgreichen Bootloader-Start.
- Die README beschreibt geplante Subsysteme und erhebt unbelegte Aussagen wie 'world’s first' und 'clean-room'. Solche Aussagen benötigen eigene Nachweise. Die hier gelesenen Dateien belegen noch keinen Linux-Start oder spielbaren Titel.

Diese Untersuchung umfasst Quellen- und Codelektüre; sie enthält keinen neuen Build- oder Laufzeitnachweis.

## Empfohlene nächste Nachweise

1. Eine feste Test-Cartridge mit SHA-256, Dateigröße, Seitengröße und Herkunft der Metadaten dokumentieren. Originaldump unverändert aufbewahren.
2. NAND Read-ID, Daten-/OOB-Lesen, Adresszyklen und ECC anhand des konkret identifizierten NAND-Chips prüfen. Keine Firmwarefehler durch künstlich passende Rückgabewerte verdecken.
3. CPU-Konformität getrennt vom Bootfortschritt prüfen: ARM/Thumb-Wechsel, PC-Semantik, banked registers, Exceptions und CP15/MMU.
4. Bootmeilensteine durch UART-Text und gültige Ausführungsadressen belegen: Steppingstone, U-Boot-Einstieg, Kommandoverarbeitung, Kernelübergabe, Kernelstart, Bildausgabe, Eingabe, Ton, Spiel.
5. Für jede Geräteeigenschaft markieren: Herstellerhandbuch, MAME-Modell, Dump-Beobachtung, Hardwaremessung oder offene Annahme. Besonders Displayauflösung, Takt, Linux-Nutzung und Cartridge-Layout nicht allein aus allgemeinen Systembeschreibungen ableiten.

Die fehlende konsolenspezifische Dokumentation macht das Projekt schwieriger, aber die Standard-SoC-Dokumentation liefert eine konkrete Grundlage. Die offenen Punkte betreffen vor allem Board-Verdrahtung, Cartridge-Protokoll und Firmwareverhalten.
