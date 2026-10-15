# Native Grafiktreiber (GPU-Pakete)

Stand: 2026-10-08. Ziel: NexisOS verlässt den UEFI-GOP-Modus, fährt die volle Bildwiederholrate des Monitors und
liefert HDMI-Ton. Die Treiber sind **nicht** im Betriebssystem-Image; passend zur Grafikkarte wird genau ein
signiertes Paket im Hintergrund aus GitHub geladen (Repo `Cybforge/NexisOS-drivers`, öffentlich einsehbar).
Es gibt dafür keinen Store-Eintrag.

## Ehrlicher Stand pro Paket

| Paket | Hardware | Stand |
| --- | --- | --- |
| `bochs` v2 | QEMU „standard VGA“ (`1234:1111`) | fertig, in QEMU getestet (virtuelles Testgerät, kein echter Treiber) |
| `rx6600` v8 | AMD RX 6600 / 6650 XT (Navi23, DCN 3.0.2; `1002:73ff`, `1002:73ef`) | **komplette Quelle** (Takte, Fetch, Farbpfad, Timing, PLL, SCDC, HDMI, Audio) mit Rollback; **nur gegen Registermodelle geprüft, nie auf echter Hardware gelaufen** |
| AMD RDNA2 groß (RX 6700/6800/6900) | Navi21/22 | **nicht implementiert**, kein Paket |
| AMD Polaris/Vega (RX 580 …) | DCE 11/12, DCN 1 | **nicht implementiert**, kein Paket |
| AMD RDNA1 (RX 5700 …) | Navi10, DCN 2.0 | **nicht implementiert**, kein Paket |
| Intel UHD / Iris Xe | Gen9–Gen12 Display-Engine | **nicht implementiert**, kein Paket |

Für eine Karte ohne Paket wird **nichts** heruntergeladen oder installiert; der Firmware-Framebuffer bleibt.
Warum nicht mehr: ein Anzeigetreiber besteht aus tausenden kartenspezifischen Registerfolgen, die ohne die
jeweilige Hardware nicht prüfbar sind. Das RX6600-Paket ist eine solche Folge, die gegen ein Modell läuft – ob
echtes Silizium sich wie das Modell verhält, zeigt erst ein Test. Weitere Familien blind zu veröffentlichen hätte
den Wert „Treiber ist da“ vorgetäuscht, ohne dass jemand ihn prüfen kann. Sie werden nachgezogen, sobald das
RX6600-Paket auf echter Hardware Rückmeldung gebracht hat und Testgeräte vorhanden sind.

## Ablauf auf dem Rechner

Aufruf: `gpu_packages_poll()` aus dem Idle-Job des Stores (`kernel/drivers/gpu/packages.c`, dort steht die
vollständige Beschreibung).

1. **Erkennen:** PCI-IDs des von UEFI genutzten Adapters (Boot-Info) gegen die kompilierte Tabelle
   `kernel/drivers/gpu/pins.h` (erzeugt aus `tools/gpu-driver/catalog.json`).
2. **Installiertes System:** liegt `/opt/nexis-drivers/<name>.ndpk` geprüft im Cache, wird es sofort geladen; der
   Download ist dann nur noch eine Update-Prüfung (neue Version gilt ab dem nächsten Start).
   **Live-Medium / erster Start nach Installation:** `<name>.ndpk` wird im Hintergrund geladen, Signatur geprüft,
   gecacht und sofort aktiviert. Das Live-Medium hält `/opt` im RAM und lädt deshalb bei jedem Start neu; der
   Installer kopiert das geprüfte Paket in das persistente `/opt` des installierten Systems
   (`gpu_install_payload`).
3. **Aktivieren:** Modul laden, Wunschmodus setzen, per Rückauslesen prüfen, Framebuffer des Desktops auf den
   nativen Modus umstellen (ohne Neustart), HDMI-Audio-Endpunkt an den HD-Audio-Treiber übergeben
   (`audio_rescan_hdmi`, bis zu 20 Versuche im 500-ms-Abstand).

Spiegel (in dieser Reihenfolge): `raw.githubusercontent.com/Cybforge/NexisOS-drivers/main/packages/` und der alte
Kontoname `jojojonas169-debug` (leitet weiter).

## Paketformat und Vertrauen

* **NDPK**: `"NDPK"`, Format 1, Modulgröße, Version, Name[16], Modul, danach 64 Byte **ECDSA P-256/SHA-256**
  (roh r‖s). Prüfung im Kernel mit BearSSL (`package_verify.c`). Der öffentliche Schlüssel steht in `pins.h`.
* **Privater Schlüssel:** `C:\Users\jonas\.nexis\gpu-signing-p256.pem`. Wird nie hochgeladen (nicht in der
  Allow-List des Upload-Skripts). **Bitte sichern** – ohne ihn lassen sich keine Updates signieren; ein neuer
  Schlüssel erfordert einen neuen Kernel.
* **Mindestversion** pro Paket (`min_version` im Katalog): alte, gültig signierte Pakete werden abgelehnt.
* Das Modul ist ein lagenunabhängiges „retained“ NDRV-v2-Modul ohne Importe und ohne Laufzeit-Relokationen
  (`scripts/build_gpu_module_v2.py`, Zig 0.13, Linker-Skript `module_v2.ld`). Seine Seiten sind nach dem Laden
  read-only/ausführbar bzw. schreibbar/NX. Die Dienste-ABI (136 Byte) liefert Registerzugriff, Zeit, PCI-Ressourcen,
  `log` und das EDID des Monitors (`kernel/drivers/gpu/runtime.c`, `tools/gpu-driver/include/nexis_gpu_v2.h`).
* Ein Treiber ist Code im Kernel-Modus: Vertrauensanker ist die Signatur, nicht die GitHub-URL.

## Sicherheitsnetze für ungeprüfte Treiber

Alle Hardware-Pakete sind `"trial": true`.

1. **Start-Countdown (10 s):** nach der Prüfung erscheint „Start in 10 s – Esc = überspringen“. Esc lässt das
   Paket in diesem Start aus (es bleibt im Cache). Ohne diesen Schutz würde ein Treiber, der den Rechner beim
   Start einfriert, **jeden** Live-Start einfrieren, weil dort nichts den Absturz übersteht.
2. **Trial-Abfrage (15 s):** nach der Umschaltung: Enter behält den Modus, Esc oder keine Eingabe stellt den
   Firmware-Modus wieder her. Ein schwarzer Bildschirm kann nicht dauerhaft werden.
3. **Absturzmarker:** `/opt/nexis-drivers/<name>.try` wird vor dem Start geschrieben und nach sauberem Ende gelöscht.
   Bleibt er stehen (Hänger im Treiber), wird diese Paketversion beim nächsten Start übersprungen (nur auf
   installierten Systemen sinnvoll; im Live-Betrieb liegt er im RAM).
4. **Selbsttest des Modus-Wechsels:** der RX6600-Treiber führt die ganze Folge zuerst für den **aktuellen** Modus aus
   und wechselt nur bei Erfolg zum Zielmodus.
5. **Kernel-Optionen:** `nogpudriver` (nie laden), `gpuautokeep` (kein Countdown, keine Trial-Abfrage),
   `gpudriverlocal` (Download von `http://10.0.2.2:8930/`, nur QEMU-Tests).

## RX6600: der Modus-Wechsel

Referenz ist der Linux-amdgpu-Display-Core v6.12 (`link_dpms.c`, `dcn20_hwseq.c`, `dce_audio.c`,
`dcn10_link_encoder.c`). Die Orchestrierung steht in `tools/gpu-driver/amd/rx6600_modeset.c`, der Folgeläufer mit
Rückgängig-machen in `tools/gpu-driver/common/modeset_seq.c`:

| Phase | Schritte |
| --- | --- |
| Plan (Bild läuft weiter) | SMU-Mindesttakte; DFS/DTO-Anforderung, DML-Bandbreite und Wasserzeichen, Vorbereitung von HUBP/HUBBUB/DPP/Timing/Resync, Audio-Plan aus dem EDID, SCDC-Fähigkeit des Monitors |
| Umschalten (Bild dunkel) | OTG aus → Sender aus → Takte → Fetch (HUBP/HUBBUB) → DPP → ATOM-`SetPixelClock` → Pixel-Resync → OTG-Timing → SCDC (Scrambling, 1:40 ab 340 MHz) → HDMI/AFMT → Audio-Endpunkt + DTO → ATOM-UNIPHY-Sender an → OTG an |
| Prüfen/Abschluss | Pixeltakt am Frame-Zähler messen, SCDC-Status, HUBP sichtbar, AVMUTE aus, Audio-Pakete + Endpunkt an, Routing neu beweisen |

* **Fehler:** jeder gescheiterte Schritt macht die abgeschlossenen in umgekehrter Reihenfolge rückgängig und
  „relightet“ den alten Modus (PLL, Sender, OTG, HUBP, ggf. alter Audio-Endpunkt). Scheitert das Rückgängigmachen
  selbst, bleibt die Ausgabe **aus** und das Modul meldet „nicht bereit“ – ein halb hergestellter Zustand wird nie
  eingeschaltet.
* **Audio:** nur wenn der Monitor 2-Kanal-LPCM bei 48 kHz/16 Bit meldet (CTA-861-Audioblock, `cta_audio.c`).
  Audio-Fehler fallen auf „nur Bild“ zurück und kosten nie das Bild. Wanduhr: DCCG-Audio-DTO (Phase 240000,
  Modul = Pixeltakt·10 kHz).
* **Zielmodus:** höchste Wiederholrate des Monitor-EDIDs bei der **aktuellen Auflösung** (die Auflösung bleibt die
  von UEFI gewählte, damit sich der Framebuffer des Desktops nicht bewegt), soweit die Strecke sie trägt
  (`edid_select_rgb8`). Über 340 MHz Pixeltakt wird Scrambling per SCDC über DDC eingeschaltet. Taktbereich
  25 MHz bis zum maximalen TMDS-Takt des Monitors; Werte außerhalb werden vor dem ersten Registerzugriff abgelehnt.
* **Logs:** alle Schritte schreiben ins Kernel-Log (`[GPU:rx6600] …`).

## Was getestet ist – und was nicht

| Prüfung | Aufruf | Ergebnis (2026-10-08) |
| --- | --- | --- |
| Modus-Wechsel, Rollback, Relight, Audio gegen Registermodell mit Fehlereinschleusung | `python scripts/test_rx6600_modeset.py` | 1.849 Szenarien bestanden; 939 Fehler endeten mit wiederhergestelltem Bild, 42 mit „Ausgabe aus“ |
| Einzeltransaktionen, Modul an zwei PIC-Adressen, UBSan | `scripts/test_rx6600.py`, `test_dcn302*.py` | bestanden |
| Folgeläufer, EDID-Audioparser (200.000 Fuzz-Fälle) | `test_modeset_seq.py`, `test_cta_audio.py` | bestanden |
| Plattform in QEMU (Download, Signatur, manipuliertes Paket, nicht passende Karte) | `scripts/test_gpu_platform_qemu.py` | bestanden |
| Installer (Paket persistiert, zwei Neustarts ohne Netz) | `scripts/test_gpu_install_qemu.py` | bestanden |
| Countdown, Esc-Überspringen, automatisches Zurücksetzen | `scripts/test_gpu_countdown_qemu.py --image …` | bestanden (Start nach 10,0 s; braucht ein Test-Image mit „trial“-markiertem bochs-Paket) |
| Echter HTTPS-Download von GitHub | QEMU, bochs-Paket | bestanden |
| **Echte RX 6600, echter Monitor, echter HDMI-Ton** | – | **nicht geprüft** |

Alle Registertests belegen die Logik gegen ein Modell, **nicht** das Verhalten echter Chips.

## Erster Hardwarelauf (2026-10-08)

Auf einer echten RX 6600 (`1002:73ff`): Download, Signaturprüfung und Countdown liefen; der Treiber (Paket v1) brach
danach in der Start-Prüfung ab: „Retained module initialization rejected (2)“, Code 2 = `RX6600_RESOURCE` – die
Prüfung der PCI-Aperturen (VRAM-BAR0 / Register-BAR5). Das Bild blieb auf dem Firmware-Modus, der Rechner lief weiter.
Das Paket v1 verlangte für BAR5 mindestens 1 MiB – eine Annahme, die nie an echter Hardware geprüft war; der Treiber
braucht von dieser Aperture nur die unteren 0x5d000 Byte. Paket v2 akzeptiert ≥ 512 KiB. **Ob das die tatsächliche Ursache war,
ist ungeprüft** – deshalb meldet v2 jede Station des Starts im Kernel-Log (`[GPU] BAR…`, `[GPU:rx6600] probe stopped
at: …`), sodass der nächste Lauf die Stelle exakt benennt, falls es woanders hängt.

## Neunter Hardwarelauf (2026-10-15) und Paket v10: erstes erfolgreiches Laden, aber 60 Hz ohne Ton

Fotos in `rx6600 test bilder`: v9 läuft durch die Probe („probe ok, 1920x1080 at 148512 kHz“), das Modul bleibt geladen, der Nutzer bestätigt den
Modus. Es gab aber **keinen Moduswechsel** (kein „mode switch“-Eintrag), „HDMI packets 0“ (kein Ton), und die Einstellungen zeigten weiter „Modus 1 von 9
(UEFI GOP)“. Der Treiber war also aktiv, hat aber nichts getan. Die SMU antwortete weiterhin nicht, „PMFW driver mailbox is silent too“.

* **Mögliche Ursache der stummen Firmware (neu gefunden, Navi23-spezifisch):** Linux `dimgrey_cavefish_ip_offset.h` setzt für die RX 6600 (Navi23)
  `MP1_BASE__INST0_SEG0 = 0x00016200`, Navi21 dagegen 0x16000. v8/v9 fragten nur die Navi21-Adressen ab (DAL-Mailbox 0x1628A/0x16273/0x16274 laut
  Linux-Hartcodierung, PMFW-Treibermailbox C2PMSG_66/82/90 bei 0x16282/…). v10 probiert **beide Registersätze** (Navi21 und +0x200 Dwords =
  Navi23) und nimmt den, der antwortet; das Log zeigt `power management: SMU answers on mailbox set N`. Antwortet keiner, bleibt der Modus ohne
  Taktverwaltung (v9). Beim Start loggt das Modul **alle sechs Mailbox-Register beider Sätze** (`mailbox set 0/1 …`) vor und nach den Abfragen und
  fragt beide PMFW-Treibermailboxen mit `GetSmuVersion` (nur lesen). Ob die Karte nun antwortet, ist **ungeprüft**.
* **Kernel (neues ISO nötig):** `kernel/drivers/gpu/runtime.c` wählt jetzt bis zu **drei Zielmodi** aus dem EDID (höchste Hz zuerst, je Hz einer) und
  probiert sie der Reihe nach; ein abgelehnter Wechsel (Treiber stellt das Bild wieder her) ist nicht mehr das Ende. Zuletzt wird der **laufende
  Firmware-Modus** mit Audio-Einrichtung versucht – das ist der Fall der v9-Fotos: ohne EDID-Ziel wurde nie `set_mode` aufgerufen, also lief die
  Sequenz mit der Audio-Einrichtung nie. Neue Logzeilen: `Moduswahl: Monitor-EDID vorhanden/fehlt, N Zielmodi …`, `Versuch n: …`.
* **Modul:** liest das Monitor-EDID selbst über DDC (Adresse 0x50, Prüfsumme je Block), wenn der Kernel keins hat; logt `EDID from the kernel / over DDC:
  monitor …, LPCM …, stereo 48 kHz 16 bit yes/NO`. Schlägt die Sequenz für den **aktuellen** Modus einmal fehl (`selftest_failed`), wird sie nicht
  wiederholt (kein weiterer Blackout).
* **Einstellungen → Anzeige:** zeigt bei aktivem nativen Treiber „Grafiktreiber / Bildwiederholrate / HDMI-Audio“ statt „UEFI GOP“.
* **240 Hz bleibt ohne Taktverwaltung unerreichbar** (DISPCLK/DPPCLK bleiben bei ca. 166 MHz). Selbst mit den Linux-Grenzwerten des niedrigsten
  Spannungszustands (DISPCLK 562, DPPCLK 300 MHz) reicht ein Pixel pro Takt nur bis etwa 290 MHz; 558 MHz bräuchten eine zweite Pipe (ODM-Combine)
  oder eine höhere Spannungsstufe über die SMU.
* Getestet nur im Host-Modell (neu: zwei Mailbox-Sätze, EDID über DDC, Selbsttest-Sperre, EDID-Kandidatenliste); QEMU-Tests bestehen
  (`Moduswahl … Versuch 1 … Firmware-Modus mit Audio-Einrichtung` erscheint im QEMU-Log). **Nicht auf der echten Karte geprüft.**

## Achter Hardwarelauf (2026-10-15) und Paket v9: die Power-Management-Firmware antwortet nicht

Mit v8 kam die Probe bis „power-management (SMU) mailbox (error 4, test reply 0x0, version 0x0, last response 0x0)“: Der DAL-Mailbox
(`0x1628A/0x16273/0x16274`, Linux `dcn30_clk_mgr_smu_msg.c`) antwortet auf **keine** Nachricht, auch nicht auf die allererste. Eine nur vom
Video-BIOS gestartete Karte hat vom Betriebssystem noch keine SMU-Initialisierung bekommen. Linux setzt in diesem Fall `smu_present = false`
(`dcn30_clk_mgr.c`: nur wenn `GetSmuVersion` klappt) und **überspringt jede Taktverwaltung**. v9 macht dasselbe:

* **Neuer Zustand „absent“** (`dcn302_smu.absent`): bleibt die erste Nachricht über das volle Fenster unbeantwortet (Antwortregister immer 0), ist
  das kein Protokollfehler mehr, sondern „niemand hört zu“. Danach wird **nie wieder** etwas an diese Mailbox gesendet.
* **Display-Takte bleiben, wie das BIOS sie ließ** (DISPCLK/DPPCLK ≈ 165,9 MHz). Der Treiber hebt sie nicht an, weil ohne SMU keine Spannung
  angehoben werden kann. Der Plan akzeptiert nur, was diese Takte tragen (das Bandbreitenmodell entscheidet).
* **Speicher-/Fabric-/PHY-Takt sind nicht lesbar.** Das Modell bekommt die niedrigsten plausiblen Stufen als Platzhalter (SOC 400, Speicher 96,
  DCF 400, PHY 300 MHz, `RX6600_ASSUMED_*` in `rx6600.h`): mehr Latenz, größere Watermarks – die sichere Richtung. Das sind **Annahmen, keine
  Messwerte**; das Log nennt sie.
* **Das Kernel-Limit für den Pixeltakt** (`max_pixel_khz`) sinkt in diesem Zustand auf das, was DISPCLK trägt (ca. 158 MHz, mindestens der
  laufende Modus). Dadurch wählt der Kernel keinen Modus, den die festen Takte nicht schaffen: **240 Hz geht ohne SMU nicht**, es bleibt 1080p60.
* **Ein unveränderter Modus läuft jetzt trotzdem einmal durch die native Sequenz**, wenn der Monitor Ton kann und noch keine Audio-Einrichtung
  stattfand (`rx6600_set_mode`): das ist der erste echte Test von Sequenz, Rückfall und HDMI-Audio auf der Karte.
* **Diagnose für die nächste Aufnahme:** Das Log nennt „power management: the SMU does not answer the DAL mailbox …; PMFW driver mailbox
  answers / is silent too (version …)“. Dafür wird die zweite PMFW-Mailbox (amdgpu „swsmu“, C2PMSG_66/82/90) einmal mit `GetSmuVersion`
  (nur lesen) gefragt. Der Registerabzug bei Fehlern enthält jetzt auch MSG/ARG/RESP beider Mailboxen und MP1_C2PMSG_35/81.

Getestet nur im Host-Modell (`dead_smu()` in `test_rx6600.c`, `real_card(true)` in `test_rx6600_modeset.c`, neue Fälle in `test_dcn302_smu.c`):
Probe klappt, kein weiterer Mailbox-Zugriff, 1080p60 geht mit Platzhaltertakten durch das Bandbreitenmodell, die Sequenz läuft einmal mit
Audio, 240 Hz wird im Plan abgelehnt, bevor der Bildschirm dunkel wird. **Nicht auf der echten Karte geprüft.** Offen bleibt, **warum** die SMU nicht
antwortet (Adressen sind gegen Linux geprüft; vermutlich bedient die Firmware den DAL-Kanal erst nach der Treiber-Initialisierung der swsmu-Seite)
und wie 240 Hz ohne SMU-Spannungsanhebung erreichbar wäre. Die nächste Aufnahme zeigt außerdem erstmals, ob die Sequenz ab „plan“ auf echter
Hardware läuft.

## Siebter Hardwarelauf (2026-10-15) und Paket v8: die echten Ebenen-Register

Der erweiterte Abzug von v7 zeigte die echten Werte: für die aktive Ebene 0 steht `MPCC_BOT_SEL` auf **0** (jeder ungenutzte MPCC i hat
top = bottom = opp = i; Linux kennt „kein unterer Layer“ als 15), `MPCC_CONTROL` 0xffff0xxx, Viewport 0x04380780 (1920×1080), Crossbar
0x00e40000, `DCSURF_SURFACE_CONTROL` 0, OPP-Mux 0x4100. Der Host-Test `real_card()` reproduziert mit diesen Werten genau „code 3“.
Behoben: „kein unterer Layer“ ist `BOT_SEL` = 15 **oder** die eigene MPCC-Nummer (ein anderer MPCC wäre eine zweite Schicht); der
MPCC-Modus wird nicht mehr geprüft (er sagt nichts darüber, welche Fläche gezeigt wird). Ungeprüft bleibt: das Kachelmuster
(`SW_MODE`) stand nicht im Foto; alle Schritte nach der Oberflächen-Bindung (Pixeltakt-Messung, SMU, DDC, Moduswechsel).

## Sechster Hardwarelauf (2026-10-15) und Paket v7: Oberflächen-Prüfung

Mit v6 kam die Probe (echte BIOS-Tabellen) bis zu „scan-out surface does not match the desktop framebuffer (code 3)“ =
`DCN302_SURFACE_UNSUPPORTED`. Welche der Teilbedingungen greift, steht nicht im Log (nicht alle Register waren im Abzug). Daher in v7:

* Abzug nach jedem Abbruch erweitert: je Ebene OPP-Mux, MPCC (top/bottom/opp/mode), HUBP (tiling, viewport start/size, crossbar,
  control) und OTG-Source. Aus einer Aufnahme lässt sich die verletzte Bedingung ablesen.
* Entschärft (jeweils begründet): der R/G/B-Crossbar wird nicht mehr verlangt (das Modul schreibt ihn nie; höchstens vertauschte
  Farben), MPCC-Modus 1/2/3 (Durchreichen/nur oben/Mischen) statt nur 1/2 (kein unterer Layer ist durch BOT_SEL geprüft).
* Weiterhin gefordert (wesentlich für „dieselbe Fläche wie der Desktop“): Format ARGB8888, linear (SW_MODE 0), kein DCC/TMZ, kein
  Rotieren/Spiegeln, Viewport = 1920×1080 ab (0,0), Pitch, Adresse im VRAM-Fenster und gleich der GOP-Framebuffer-Adresse.

## Fünfter Hardwarelauf (2026-10-15) und Paket v6: erstmals mit den echten BIOS-Tabellen getestet

Das Fehlerprotokoll von v5 enthielt zum ersten Mal die rohen BIOS-Tabellen der Karte (object_info, gpio_pin_lut, dce_info, vram_info,
`tests/host/real_rx6600_vbios.h`). Mit ihnen stoppte der Host-Test genau an den Stellen der echten Karte; behoben:

* **Anschlusstabelle (stoppte bei „board data, code 2“, Ursache gefunden):** die Endmarkierung einer Record-Kette (`0xff`) ist **ein**
  Byte; beim HDMI-Anschluss ist sie das allerletzte Byte der Tabelle, der Parser verlangte aber auch dafür ein zweites Byte.
* **DDC-Pin:** die BIOS-Tabelle nennt den **Daten**-Pin der DDC-Leitung (Bit 8 von `DC_GPIO_DDCn_A`, Takt ist Bit 0); der Code verglich nur mit dem Takt-Bit.
* **HPD-Polarität:** `plugin_pin_state` des BIOS-Eintrags ist 0 auf allen Anschlüssen der Karte, Linux wertet das Feld nie aus
  (`bios_parser2.c` kopiert es nach `hpd_active`, gelesen wird es nirgends); „verbunden“ ist ein hoher HPD-Pegel.
* **VRAM-Tabelle (v2.5):** die Module sind 45–55 Byte groß (Linux-Struktur: 84), der Parser verlangte 84; die Module sind
  Alternativen (das BIOS-Scratch-Register wählt eins), kein Addieren: Geometrie vom ersten (8192 MB GDDR6, 8 Kanäle × 16 Bit).
* Der Host-Test `real_card()` läuft jetzt mit den **echten** Tabellen und Registerwerten komplett durch die Probe.
  Weiter ungeprüft auf echter Hardware: SMU-Antworten, Pixeltakt-Messung, der Moduswechsel selbst, DDC/SCDC, Audio.

## Vierter Hardwarelauf (2026-10-15, ohne Log) und Paket v5: Abgleich mit Linux v6.12

Da kein Log vorlag, wurde der ganze Pfad (Probe und Modus-Wechsel) von einem eigenen Prüf-Lauf Zeile für Zeile mit dem echten
Linux-Quelltext verglichen und mit den echten Registerwerten aus dem Fehlerabzug auf dem Rechner nachgestellt
(`real_card()` in `tests/host/test_rx6600.c`). Jeder Fund ist mit einer Linux-Stelle belegt:

* **Oberfläche/Pitch (stoppte die Probe bei „scan-out surface … code 3“):** die GOP lässt HUBP-`PITCH` = 1920 (0x780) stehen;
  Linux schreibt `surface_pitch − 1` (`dcn20_hubp.c`). Beide Kodierungen werden jetzt akzeptiert (`dcn302_surface.c`).
* **ATOM-Interpreter:** Operandenarten FB und ID waren vertauscht (Linux `atom.h`: `ATOM_ARG_FB 3`, `ATOM_ARG_ID 4`); damit
  verlor jede Firmware-Anweisung mit solchen Operanden den Bytecode-Takt (betraf Sender-Aus/-An des Moduswechsels).
  Außerdem Linux-gleiche Toleranz: Indirect-IO-Index endet am ersten Nicht-Start-Byte (`atom.c:1315`), PLL-/MC-Operanden lesen 0,
  Opcodes 56/57 (setport), undefinierte indirekte Methoden lesen 0.
* **SMU-Taktstufen:** feinkörnige Takte haben immer genau 2 Stufen, Antworten werden auf 16 Bit maskiert
  (`dcn30_clk_mgr.c: dcn3_init_single_clock`).
* Alle Modelltests (26 GPU-Tests) laufen; auf echter Hardware **nicht geprüft**.

Offen / vermutet, **nicht** geändert (keine belegte Abweichung): ATOM-VM dekodiert Tabellen vorab strenger als Linux; Sender wird vor
dem OTG eingeschaltet (Linux: OTG zuerst); DDC-Engine verweigert einen im GPIO-Modus belassenen Pad; SCDC wartet nur 100 ms und verlangt
Sink-Version ≠ 0; VRAM-Tabelle wählt kein Einzelmodul wie Linux; SMU-Stufe mit 0 MHz stoppt die Probe; `SetPixelClock` setzt
`FORCE_PROG_PPLL`; der VCO-Rückfall 3,65 GHz ist eine Annahme; 6-Gbit/s-HDMI-Fähigkeit wird bei Objekttabelle v1.5 nicht gefunden.

## Dritter Hardwarelauf (2026-10-15) und Paket v4

Mit dem BAR-Fix kam der Treiber (v3) erstmals über die Aperturprüfung hinaus und stoppte bei „board data of the video BIOS
(code 2)“. Die Registerübersicht des Abbruchs zeigte den Zustand der Karte nach UEFI/GOP: ein Pfad aktiv (OTG 0, 2200×1125 gesamt,
Pitch 1920, Fläche bei MC-Adresse 0x80_0000_0000), SMU-Postfach 0/0, `HUBBUB_GLOBAL_TIMER` 0x1002 (wie erwartet),
DIG-BE 0 im **DVI-Modus (2)** auf HPD 2, `DENTIST` 0x4c194c4c (Teiler-ID 76), `CLK0_CLK_PLL_REQ` 0x10000001. Folgen:

* **Ursache von Code 2 (mein Fehler):** die GPIO-Pin-Tabelle hat 8-Byte-Einträge (`u32 data_a_reg_index`, Linux
  `struct atom_gpio_pin_assignment`); eine frühere Durchsicht behauptete 6 Byte, das wurde ungeprüft übernommen und
  ließ die Tabellengrößenprüfung scheitern. Zurückgestellt und diesmal gegen `atomfirmware.h`/`bios_parser2.c` (Linux v6.12) geprüft.
* **Board-Parser** schlägt nur noch bei einem defekten **HDMI**-Pfad fehl (Linux löst jeden Anschluss einzeln auf); nicht
  benutzte dce_info-Zahlen werden nicht mehr bereichsgeprüft; bei jedem Abbruch landen die rohen Tabellen (object_info,
  gpio_pin_lut, dce_info, vram_info) im Log.
* **DVI-Modus:** UEFI-GOP lässt die TMDS-Verbindung im DVI-Modus. Route akzeptiert 2 und 3; `dcn302_hdmi_prepare` stellt
  `DIG_MODE` bei ausgeschaltetem Link auf HDMI (3) (Linux `dcn10_link_encoder_setup`) und `restore` stellt DVI wieder her.
* **DFS:** `DENTIST_DISPCLK_CHG_MODE` ≠ 0 (hier 2) ist normal (Linux fasst es nie an) und wird nicht mehr abgelehnt.
  `CLK0_CLK_PLL_REQ` ist hier kein nutzbarer Multiplikator (ergäbe ~106 MHz); wie Linux (`dcn30_clk_mgr.c`) wird dann der
  Standard-VCO 3,65 GHz genommen (Teiler 76 → 166 MHz Display-/DPP-Takt, plausibel). Der Kernel-Log nennt Quelle und Takte.
* Geprüft mit Modelltests (mit den echten Registerwerten); **nicht** auf Hardware. Weiter ungeprüft: VRAM-Tabelle,
  Pixeltakt-Messung, SMU-Antworten, der eigentliche Modus-Wechsel.

## Zweiter Hardwarelauf (2026-10-15): die eigentliche Ursache des Abbruchs bei „Retained module initialization rejected (2)“

Mit Paket v3 zeigte das Log: BAR0 (VRAM, 256 MiB) war in Ordnung, die Register-BAR wurde aber mit „BAR does not match the
UEFI-reported extent“ abgelehnt – dieselbe Meldung wie schon bei v1/v2, die Lockerung der BAR-Größe war also nie die Ursache.
Ursache liegt im **Kernel, nicht im Modul**: UEFI (EDK2) nummeriert die BAR-*Einträge* des PCI-Headers fortlaufend – ein
64-Bit-BAR (zwei Konfigurationsregister) und ein I/O-BAR belegen je einen Eintrag. Bei der RX 6600 (BAR0 64-Bit VRAM, BAR2
64-Bit Doorbell, BAR4 I/O, BAR5 Register) liegen die Register deshalb unter UEFI-Eintrag 3, der Kernel suchte sie unter
Registernummer 5 und fand „0 + 0“. `gpu_pci_firmware_ranges` (`kernel/drivers/gpu/pci_resource.c`) übersetzt jetzt Einträge
in Registernummern (mit Rückfall auf Firmware, die nach Registernummer indiziert; der Eintrag muss immer zur live
gelesenen BAR-Adresse passen). Getestet mit den echten Werten aus dem Log (`tests/host/test_gpu_resource.c`). Dadurch
kommt der Treiber erstmals weiter als bis zur Aperturprüfung – **alle Schritte danach sind weiterhin ungeprüft auf echter Hardware.**

## Paket v3 (2026-10-15): zweite Fehlersuche vor dem nächsten Hardwarelauf

Eine erneute, gezielte Durchsicht der ganzen RX6600-Kette gegen das, was eine **nur vom Video-BIOS/GOP initialisierte**
Karte tatsächlich liefert (nicht gegen das, was Linux-DC nach seinem eigenen Init vorfindet). Behoben, jeweils mit
angepassten Modelltests (alle Host-Tests grün; auf echter Hardware weiterhin **nicht** geprüft):

* **GPIO-Pin-Tabelle** wurde mit falschem Eintragsformat gelesen (8 Byte/u32-Index statt 6 Byte/u16-Index) → falsche
  HPD-/DDC-Pins. Das war die wahrscheinlichste Ursache für ein Scheitern an „finding the HDMI output“.
* **Surface-Bindung:** VRAM-Offset aus `FB_BASE` statt `FB_OFFSET`; Statusregister werden maskiert verglichen;
  ein klebriger HUBP-Underflow-Status macht die GOP-Fläche nicht mehr „nicht unterstützt“.
* **VBIOS:** Abbild-Prüfsumme und PCIR-Geräte-ID werden nur gemeldet, nicht mehr erzwungen (Board-Partner-Abbilder);
  keine 1-MiB-Obergrenze mehr; VRAM-Tabelle (`channel_enable` wird von Linux nicht genutzt, mehrere Module addieren sich);
  `dpphy_refclk = 0` ist erlaubt.
* **Pixeltakt-Messung** mit Wiederholung bei einer einzelnen Abtastlücke; **`alive()`** schlägt nach einer langen
  Pause ohne Poll nicht mehr dauerhaft fehl (Neuabtastung).
* **SMU-Postfach:** das Antwortregister einer nie benutzten Karte darf anfangs 0 sein (Linux ignoriert diese Wartezeit);
  Header-/Interface-Version werden nur noch geloggt; Feature-Wort wird wie unter Linux maskiert.
* **HPD:** „verbunden“ aus dem Live-`SENSE`-Bit (nicht mehr `SENSE == SENSE_DELAYED`); ein kurzer HPD-Puls
  (Eingangswechsel, HDMI-Re-Auth) beendet den Treiber nicht, erst 3 s Dauer-Low; `DIG_HPD_SELECT` der Firmware ist nicht
  maßgeblich, der Pin aus dem BIOS-Objekt ist es.
* **DFS:** Teiler-ID < 8 wird wie unter Linux auf 8 angehoben statt abgelehnt.
* **Diagnose:** jeder Abbruch schreibt eine Registerübersicht ins Kernel-Log (Framebuffer-Fenster, SMU-Postfach,
  DFS/Dentist/DTO, HUBBUB-Referenztakt, DIG/HPD je Verbindung, OTG-/HUBP-Zustand je Pipe) und die rohen SMU-Antworten.
* Der Kernel merkt sich eine abgelehnte Probezeit-Version (`declined`), damit nach „Esc/Revert“ nicht sofort neu gestartet wird.

**Weiterhin offen / ungeprüft:** der HUBBUB-Referenztakt (`REFCLK_CNTL`/`GLOBAL_TIMER`) wird verlangt, wie ihn Linux-DC
programmiert; hat die Firmware ihn nicht so hinterlassen, hält der Treiber dort an (das Register wird im Log
ausgegeben). Mehrere gleichzeitig aktive Monitore/OTGs bleiben bewusst nicht unterstützt (Ambiguität → Firmware-Bild).
Alles andere (Taktfolge, Modus-Wechsel, HDMI-Audio) ist nur gegen Registermodelle bewiesen.

## Erster Test auf der echten Karte

1. Ethernet anschließen, NexisOS starten (Live oder installiert). Nach dem Desktop erscheint der 10-s-Countdown.
2. Nichts drücken: der Treiber startet. Funktioniert die Anzeige, Enter drücken (behalten). Bei Schwarzbild oder
   Fehlverhalten nichts tun oder Esc: nach spätestens 15 s kommt der Firmware-Modus zurück.
3. Bleibt der Rechner hängen: neu starten und im Countdown Esc drücken (oder die Option `nogpudriver` setzen).
4. Rückmeldung hilfreich: Kernel-Log mit den Zeilen `[GPU` / `[GPU:rx6600]` – sie zeigen, bis zu welchem Schritt der
   Wechsel kam, und ob Video und Audio aktiv wurden.

## Pakete bauen, signieren, veröffentlichen

```bat
set NEXIS_ZIG=C:/Users/jonas/Desktop/AetherOS/tools/zig/zig-windows-x86_64-0.13.0/zig.exe
python scripts\build_gpu_catalog.py           REM baut + signiert alle Pakete, schreibt pins.h und build\gpu-drivers\
python scripts\build.py                       REM Kernel/Image neu bauen (nur nötig, wenn sich pins.h ändert)
python scripts\publish_gpu_drivers.py         REM Trockenlauf: zeigt, was hochgeladen würde
python scripts\publish_gpu_drivers.py --publish   REM ein Commit per Git-Data-API
```

* Das Upload-Skript sendet nur Dateien einer festen Allow-List; der Signaturschlüssel, Build-Ausgaben außer den
  signierten Paketen, ISO-Images und alles außerhalb des Projekts sind nicht dabei. Das Token (`Desktop\Git key
  (wirklich).txt`) geht ausschließlich im Authorization-Header an `api.github.com` und wird nie ausgegeben.
* Ein Treiber-Update braucht nur einen neuen Upload mit **höherer Version**; ein Kernel-Neubau ist nur nötig, wenn
  sich Katalogtabelle oder Schlüssel ändern.
* ECDSA-Signaturen sind randomisiert: jeder Lauf von `build_gpu_catalog.py` erzeugt andere Paketbytes (gleicher
  Inhalt, neue Signatur) – das ist kein Fehler.

## Dateiübersicht

* Plattform (Kernel): `kernel/drivers/gpu/` (`packages.c` Ablauf/Countdown/Trial, `runtime.c` Modul-Laufzeit,
  `package_verify.c`, `module_image.c`, `catalog.h`, generiertes `pins.h`), `kernel/drivers/framebuffer.c`
  (`fb_native_activate/deactivate`), `kernel/drivers/audio/hda.c` + `kernel/audio/audio.c` (HDMI-Codec-Umschaltung),
  `kernel/sys/install.c` (Installer-Persistenz), `gui/wm/wm.c` (Tasten für Countdown/Trial).
* Treiber: `tools/gpu-driver/` (`bochs/`, `amd/` – RX6600/DCN 3.0.2, ATOM-Interpreter, DML-Portierung –,
  `common/`, `include/nexis_gpu_v2.h`, `catalog.json`).
* Werkzeuge: `scripts/gpu_package.py` (Signieren/Packen/Prüfen), `build_gpu_catalog.py`, `build_gpu_module_v2.py`,
  `publish_gpu_drivers.py`, `generate_dcn302_*.py` (Register aus festgehaltenen Linux-v6.12-Quellen).
* Tests: `tests/host/`, `scripts/test_*`.

Weitere Hintergründe zum RX6600-Aufbau: [PHYSICAL_DISPLAY_2026-10-07.md](PHYSICAL_DISPLAY_2026-10-07.md).
