# Kühlschrank-Monitor – Teileliste

Stand: 2026-11-06 · alles bei Amazon.de · kein Löten nötig

## Bestellung

| # | Teil | Artikel | Link | Preis |
|---|---|---|---|---|
| 1 | Mikrocontroller | ELEGOO ESP-32, 2 Stück, USB-C | https://www.amazon.de/dp/B0D8T7LZF2 | 15,99 € |
| 2 | Temperaturfühler | DS18B20-Set: 2 wasserdichte Fühler mit Adapterplatinen (Pull-up-Widerstand drauf) und Dupont-Kabeln | https://www.amazon.de/dp/B0GT3XBDXM | 8,99 € |
| 3 | Anzeige | TM1637, 7-Segment, 4-stellig, 0,36", 3 Stück (Stifte vorgelötet) | https://www.amazon.de/dp/B0D9Q563BD | 5,99 € |
| 4 | Stromversorgung | USB-A-Ladegerät mit USB-C-Kabel | https://www.amazon.de/dp/B0D1NWZZHZ | 8,28 € |
| 5 | Kabel | Dupont-Jumperkabel, 120 Stück, 20 cm (davon 40× Buchse–Buchse) | https://www.amazon.de/dp/B0HFQSZ2PX | 6,99 € |
| | | | **Summe** | **~46 €** |

Ersatzteile für ein zweites Gerät sind dabei, außer dem Ladegerät.

### Hinweise zum Kauf
- **Ladegerät:** Es muss **USB-A** sein (rechteckiger Stecker), dazu ein Kabel **USB-A auf USB-C**. Viele günstige ESP32-Boards laufen an reinen USB-C-Ladegeräten (C-auf-C) nicht.
- **Kabel:** Mit **Datenübertragung**, damit es auch zum Flashen am PC taugt.
- **Fühler-Set:** Auf der Adapterplatine sind der Widerstand und die Stifte schon aufgelötet. Liegt zusätzlich ein loser Widerstand bei, wird er nicht gebraucht.
- **Display:** Es hat einen Doppelpunkt und keine Dezimalpunkte. Der Doppelpunkt wird in der Firmware abgeschaltet, angezeigt werden ganze Grad.

## Verkabelung

Das ELEGOO-Board ist ein ESP32 DevKit mit 30 Stiften. Die Pins sind mit „D23“, „RX2“ usw. beschriftet. Bebilderte Anleitung: [anleitung.html](https://samba09.github.io/fridgeMonitor/anleitung.html).

**Erst alles verkabeln, dann USB einstecken.** Nach dem Aufdruck neben den Pins gehen, z. B. „23“, „D23“ oder „G23“.

### Fühler an die Adapterplatine (Schraubklemmen)
| Fühlerdraht (übliche Farbe) | Adapter |
|---|---|
| rot | VCC |
| schwarz | GND |
| gelb / weiß / blau | DAT (DQ) |

### Fühler-Adapter an den ESP32
| Adapter | ESP32 |
|---|---|
| VCC | **RX2** (= GPIO16, rechts, 10. Stift von oben; der ESP schaltet darüber den Fühler-Strom) |
| GND | **GND rechts** (vorletzter Stift) |
| DAT | **D4** (rechts, unter RX2) |

**Zwei Fühler:** Beide Fühler kommen an **eine** Adapterplatine, die zweite Platine bleibt übrig. In jede Schraubklemme kommen also zwei Drähte gleicher Farbe (rot+rot, schwarz+schwarz, gelb+gelb). So braucht es nur eine Kabelverbindung zum ESP.

### Display (TM1637) an den ESP32
| Display | ESP32 |
|---|---|
| VCC | **3V3** (rechts, letzter Stift; nicht an VIN) |
| GND | **GND links** (vorletzter Stift, neben VIN) |
| DIO | **D23** (rechts, oberster Stift) |
| CLK | **D18** (rechts, 7. Stift von oben) |

Das Display läuft mit 3,3 V, damit keine 5 V an die Pins des ESP32 kommen. Der einzige 3V3-Pin ist deshalb für das Display reserviert, der Fühler bekommt seinen Strom über Pin RX2 (GPIO16).

### Vor dem Einstecken prüfen
- Fühler-VCC an **RX2**, Display-VCC an **3V3**?
- VCC und GND nirgends vertauscht?

## Montage
- ESP und Display **außen** am Kühlschrank anbringen, vorne oder seitlich, nicht hinter der Metall-Rückwand (WLAN-Empfang).
- Nur das Fühlerkabel geht durch die Türdichtung nach innen.
- Befestigung mit Klett- oder Klebestreifen. Die Kabel zwischen ESP und Display mit Klebeband bündeln.
