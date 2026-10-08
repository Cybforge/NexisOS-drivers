# Native Grafiktreiber (GPU-Pakete)

Stand: 2026-10-08. Ziel: NexisOS verlässt den UEFI-GOP-Modus, fährt die volle Bildwiederholrate des Monitors und
liefert HDMI-Ton. Die Treiber sind **nicht** im Betriebssystem-Image; passend zur Grafikkarte wird genau ein
signiertes Paket im Hintergrund aus GitHub geladen (Repo `Cybforge/NexisOS-drivers`, öffentlich einsehbar).
Es gibt dafür keinen Store-Eintrag.

## Ehrlicher Stand pro Paket

| Paket | Hardware | Stand |
| --- | --- | --- |
| `bochs` v2 | QEMU „standard VGA“ (`1234:1111`) | fertig, in QEMU getestet (virtuelles Testgerät, kein echter Treiber) |
| `rx6600` v1 | AMD RX 6600 / 6650 XT (Navi23, DCN 3.0.2; `1002:73ff`, `1002:73ef`) | **komplette Quelle** (Takte, Fetch, Farbpfad, Timing, PLL, SCDC, HDMI, Audio) mit Rollback; **nur gegen Registermodelle geprüft, nie auf echter Hardware gelaufen** |
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
