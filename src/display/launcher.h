#pragma once
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objidl.h>
#include <gdiplus.h>
#include "windows_input.h"
#include "../core/mp3_storage.h"
#include "../core/cartridge_nvram.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace oceanblast {
namespace launcher {

// Hidden compatibility controls synced with FrontendSettings for automated inspection tests.
static HWND pathBox = nullptr, scaleBox = nullptr, soundBox = nullptr, profileBox = nullptr;
static HWND debugBox = nullptr, formatBox = nullptr, nvramBox = nullptr, timingBox = nullptr;
static HWND startButton = nullptr, stopButton = nullptr, statusBox = nullptr, windowModeBox = nullptr;

static HWND window = nullptr, settingsWindow = nullptr, deviceCombo = nullptr, learningLabel = nullptr;
static std::vector<HWND> settingsChildren;
static int settingsPage = 0, learnAction = -1;
static bool learnKeyboard = false;
static bool mainFullscreen = false;
static WINDOWPLACEMENT savedPlacement{};
static HMENU mainMenu = nullptr;
static HANDLE process = nullptr;
static std::wstring executable, folder, activeRomPath;
static win::FrontendSettings settings;
static win::InputManager input;
static win::HostPad previousPad{};
static ULONG_PTR gdiplusToken = 0;
static IStream* logoStream = nullptr;
static IStream* symbolStream = nullptr;
static std::unique_ptr<Gdiplus::Image> logo, symbol;
static HICON appIcon = nullptr;

enum {
    OpenRom = 100,
    RestartRom = 101,
    StopRom = 102,
    ToggleFullscreenCmd = 103,
    EscapeFromChildCmd = 104,
    ExitApp = 105,
    SettingsBase = 200,
    MediaPlayPause = 210,
    MediaStop = 211,
    MediaPrev = 212,
    MediaNext = 213,
    MediaSeekBack = 214,
    MediaSeekFwd = 215,
    CartLangBase = 220,
    ManageMp3Storage = 240
};

struct UiText {
    const wchar_t* categories[6];
    const wchar_t* menuFile;
    const wchar_t* menuPlayback;
    const wchar_t* menuSettings;
    const wchar_t* menuOpenRom;
    const wchar_t* menuRestartRom;
    const wchar_t* menuStopRom;
    const wchar_t* menuExit;
    const wchar_t* menuPlayPause;
    const wchar_t* menuStopMedia;
    const wchar_t* menuPrevTrack;
    const wchar_t* menuNextTrack;
    const wchar_t* menuSeekBack;
    const wchar_t* menuSeekFwd;
    const wchar_t* cartLanguageLabel;
    const wchar_t* cartLanguageOptions[10];
    const wchar_t* idleWindowTitle;
    const wchar_t* settingsWindowTitle;
    const wchar_t* startScreenText;
    const wchar_t* closeBtn;
    const wchar_t* savedLocallyNote;
    const wchar_t* saveError;
    const wchar_t* invalidRomWarning;
    const wchar_t* logOpenError;
    const wchar_t* launchError;
    const wchar_t* chooseFolderTitle;
    const wchar_t* romFilter;
    // Page 0: Folders & System
    const wchar_t* languageLabel;
    const wchar_t* romFolderLabel;
    const wchar_t* defaultFolderText;
    const wchar_t* browseFolderBtn;
    const wchar_t* selectedRomLabel;
    const wchar_t* noRomSelectedText;
    const wchar_t* browseFileBtn;
    const wchar_t* startGameBtn;
    const wchar_t* stopGameBtn;
    const wchar_t* nvramHeader;
    const wchar_t* nvramCheck;
    const wchar_t* page0Tip;
    // Page 1: Controls
    const wchar_t* selectControllerLabel;
    const wchar_t* keyboardOnlyItem;
    const wchar_t* disconnectedControllerItem;
    const wchar_t* refreshBtn;
    const wchar_t* presetLabel;
    const wchar_t* presets[3];
    const wchar_t* mappingColumnHeader;
    const wchar_t* clickToBindHint;
    const wchar_t* pressControllerPrompt;
    const wchar_t* pressKeyboardPrompt;
    const wchar_t* controllerConnectedStatus;
    const wchar_t* controllerDisconnectedStatus;
    // Page 2: Sound
    const wchar_t* soundHeader;
    const wchar_t* enableAudioCheck;
    const wchar_t* volumeLabel;
    const wchar_t* soundInfoText;
    // Page 3: Video
    const wchar_t* displayModeLabel;
    const wchar_t* windowModes[3];
    const wchar_t* aspectRatioLabel;
    const wchar_t* aspectRatios[3];
    const wchar_t* integerScaleCheck;
    const wchar_t* smoothScaleCheck;
    const wchar_t* scaleFactorLabel;
    const wchar_t* scaleFactors[3];
    const wchar_t* decoderLabel;
    const wchar_t* decoders[5];
    // Page 4: Timing & Diagnostics
    const wchar_t* timingHeader;
    const wchar_t* timingModes[6];
    const wchar_t* fpsLogCheck;
    const wchar_t* debugLogCheck;
    const wchar_t* timingNote;
    // Page 5: Hotkeys & Help
    const wchar_t* hotkeysHeader;
    const wchar_t* hotkeysBody;
};

static constexpr const wchar_t* kLanguageNames[7]{
    L"English (Default)",
    L"Deutsch",
    L"Español",
    L"Italiano",
    L"Nederlands",
    L"Français",
    L"Türkçe"
};

static constexpr UiText kUiText[7]{
    // 0: English (Default)
    {
        {L"Folders & System", L"Controls", L"Sound", L"Video", L"Timing & Diagnostics", L"Hotkeys & Help"},
        L"File", L"Playback", L"Settings",
        L"Load ROM...\tCtrl+O", L"Restart Game\tCtrl+R", L"Stop Game", L"Exit",
        L"Play / Pause\tP / F10",
        L"Stop / Episode Menu\tBackspace / F9",
        L"|<< Previous / Rewind\tB / F8",
        L">>I Next / Fast-Forward\tN / F12",
        L"<< Seek Backward (Hold |<<)",
        L">> Seek Forward (Hold >>I)",
        L"Cartridge / Video Language",
        {
            L"Auto (Match UI Language)",
            L"Always Ask in Cartridge (Flag Menu)",
            L"English (EN)",
            L"Deutsch (DE)",
            L"Español (ES)",
            L"Italiano (IT)",
            L"Nederlands (NL)",
            L"Français (FR)",
            L"Türkçe (TR)",
            L"Keep Saved EEPROM Value"
        },
        L"OceanBlast — Ctrl+O: Open ROM, or drag ROM here",
        L"OceanBlast — Settings",
        L"Load ROM: Ctrl+O · Drag & drop a ROM into this window\nSettings: System, Controller, Sound, Video and Timing",
        L"Close", L"Changes are saved locally.",
        L"Could not save settings.",
        L"Please select a valid digiBLAST ROM file (*.bin;*.rom) first.",
        L"Could not open session log file.",
        L"Could not start emulator process.",
        L"Select default folder for digiBLAST ROMs",
        L"digiBLAST ROM (*.bin;*.rom)\0*.bin;*.rom\0All Files (*.*)\0*.*\0",
        L"UI Language",
        L"ROM Folder (default directory for Ctrl+O)", L"Default directory", L"Browse...",
        L"Selected digiBLAST ROM", L"No ROM selected yet", L"File...",
        L"Start Game", L"Stop Game",
        L"Persistent Device Storage (2 KB I2C EEPROM)",
        L"Save device settings and game progress",
        L"Tip: You can drag & drop ROM files (*.bin; *.rom) directly into the main window at any time or open them with Ctrl+O.",
        L"Select Controller", L"Keyboard Only", L"Saved Controller (disconnected)", L"Refresh",
        L"Preset",
        {L"Stadia / Xbox — Layout", L"PlayStation — Layout", L"digiBLAST / USB — Custom"},
        L"digiBLAST  Controller             Keyboard",
        L"Click a binding, then press a button or key. Esc cancels.",
        L"Press a controller button or direction now … (Esc: Cancel)",
        L"Press a keyboard key now … (Esc: Cancel)",
        L"Connected · Live inputs are highlighted",
        L"No connected input device",
        L"Sound", L"Enable audio output", L"Volume",
        L"S3C2410 IIS + DMA2 stereo audio · 22.05 kHz standard rate with dynamic resampling.",
        L"Display Mode",
        {L"digiBLAST Console Skin", L"Fullscreen (F11 toggles)", L"Standard Window (LCD in main window)"},
        L"Aspect Ratio",
        {L"Original digiBLAST proportions (3:2)", L"4:3", L"Stretch to window"},
        L"Integer pixel scaling (overrides aspect ratio)",
        L"Smooth image scaling (GDI Halftone)",
        L"Standalone scale factor (--gui)",
        {L"2× (480 × 320)", L"3× (720 × 480, Default)", L"4× (960 × 640)"},
        L"Display Decoder",
        {
            L"Automatic (Default, 12-bit LCD / 16-bit RGB565)",
            L"RGB444, 480-byte rows (Diagnostic)",
            L"RGB444 packed, 360-byte rows (Diagnostic)",
            L"RGB565, 480-byte rows (Diagnostic)",
            L"LCD registers only (Diagnostic)"
        },
        L"CPU & Peripheral Timing",
        {
            L"Automatic ROM settings (Default)",
            L"Standard: 1× (20 MIPS base)",
            L"Automatic register clocks (experimental)",
            L"More CPU work: 2× (40 MIPS)",
            L"More CPU work: 4× (80 MIPS)",
            L"More CPU work: 8× (160 MIPS, fast combat in Wade Hixton)"
        },
        L"Log FPS & audio metrics to sessions\\performance.csv",
        L"Enable extended debug logging to sessions\\session.log",
        L"Note: Changes to timing mode or display decoder take effect the next time you start or restart (Ctrl+R) the ROM.",
        L"Hotkeys & Controls",
        L"• Ctrl+O: Load ROM    ·    Ctrl+R: Restart game\n"
        L"• Drag ROM file into window: Launch game directly via drag & drop\n"
        L"• F11 or Alt+Enter: Toggle fullscreen    ·    Esc: Exit fullscreen / Stop\n"
        L"• F5: Pause / Resume    ·    F6: Single CPU step    ·    F7: Save snapshot\n"
        L"• Video Play/Pause: P / F10 / Space / Media Play\n"
        L"• Video Stop / Episode Menu: Backspace / F9 / Enter / Media Stop\n"
        L"• Video Previous / Rewind |<<: B / F8 / PageUp / , (hold to seek backward)\n"
        L"• Video Next / Fast-Forward >>|: N / F12 / PageDown / . (hold to seek forward)\n\n"
        L"Use the 'Playback' menu in the top bar for one-click video transport controls and multilingual audio selection."
    },
    // 1: Deutsch
    {
        {L"Ordner & System", L"Steuerung", L"Sound", L"Video", L"Timing & Diagnose", L"Kurztasten & Hilfe"},
        L"Datei", L"Wiedergabe", L"Einstellungen",
        L"ROM laden...\tStrg+O", L"Spiel neu starten\tStrg+R", L"Spiel stoppen", L"Beenden",
        L"Play / Pause\tP / F10",
        L"Stopp / Episoden-Menü\tRücktaste / F9",
        L"|<< Zurück / Rücklauf\tB / F8",
        L">>I Weiter / Vorlauf\tN / F12",
        L"<< Rückwärts spulen (|<< halten)",
        L">> Vorwärts spulen (>>I halten)",
        L"Modul- / Videosprache",
        {
            L"Automatisch (wie UI-Sprache)",
            L"Immer im Modul fragen (Flaggen-Menü)",
            L"English (EN)",
            L"Deutsch (DE)",
            L"Español (ES)",
            L"Italiano (IT)",
            L"Nederlands (NL)",
            L"Français (FR)",
            L"Türkçe (TR)",
            L"Gespeicherten EEPROM-Wert behalten"
        },
        L"OceanBlast — Strg+O: ROM öffnen, ROM hierher ziehen",
        L"OceanBlast — Einstellungen",
        L"ROM laden: Strg+O · ROM ins Fenster ziehen\nEinstellungen: System, Controller, Sound, Video und Timing",
        L"Schließen", L"Änderungen werden lokal gespeichert.",
        L"Einstellungen konnten nicht gespeichert werden.",
        L"Bitte zuerst eine gültige digiBLAST-ROM-Datei (*.bin;*.rom) auswählen.",
        L"Session-Logdatei konnte nicht geöffnet werden.",
        L"Emulator-Prozess konnte nicht gestartet werden.",
        L"Standard-Ordner für digiBLAST-ROMs auswählen",
        L"digiBLAST ROM (*.bin;*.rom)\0*.bin;*.rom\0Alle Dateien (*.*)\0*.*\0",
        L"Oberflächensprache",
        L"ROM-Ordner (Startverzeichnis für Strg+O)", L"Standardverzeichnis", L"Auswählen...",
        L"Ausgewähltes digiBLAST-ROM", L"Noch kein ROM ausgewählt", L"Datei...",
        L"Spiel starten", L"Spiel stoppen",
        L"Persistenter Gerätespeicher (2 KB I2C-EEPROM)",
        L"Geräteeinstellungen und Spielstände speichern",
        L"Tipp: Du kannst ROM-Dateien (*.bin; *.rom) jederzeit direkt per Drag & Drop in das Hauptfenster ziehen oder mit Strg+O öffnen.",
        L"Controller auswählen", L"Nur Tastatur", L"Gespeicherter Controller (nicht verbunden)", L"Aktualisieren",
        L"Preset",
        {L"Stadia / Xbox — Positionen", L"PlayStation — Positionen", L"digiBLAST / USB — anlernbar"},
        L"digiBLAST  Controller             Tastatur",
        L"Belegung anklicken, dann Taste drücken. Esc bricht ab.",
        L"Jetzt einen Controller-Knopf oder eine Richtung drücken … (Esc: Abbrechen)",
        L"Jetzt eine Tastaturtaste drücken … (Esc: Abbrechen)",
        L"Verbunden · Eingaben werden live hervorgehoben",
        L"Kein verbundenes Eingabegerät",
        L"Sound", L"Audio-Ausgabe aktivieren", L"Lautstärke",
        L"S3C2410 IIS + DMA2 Stereo-Audio · 22,05 kHz Standardrate mit dynamischem Resampling.",
        L"Darstellungsmodus",
        {L"digiBLAST-Gehäuse (Console Skin)", L"Vollbild (F11 schaltet um)", L"Standard-Fenster (LCD im Hauptfenster)"},
        L"Bildformat",
        {L"Originale digiBLAST-Proportionen (3:2)", L"4:3", L"Fensterfüllend (gestreckt)"},
        L"Ganzzahlige Pixelskalierung (hat Vorrang vor Bildformat)",
        L"Weiche Bildskalierung (GDI Halftone)",
        L"Stand-Alone-Skalierungsfaktor (--gui)",
        {L"2× (480 × 320)", L"3× (720 × 480, Standard)", L"4× (960 × 640)"},
        L"Display-Decoder",
        {
            L"Automatisch (Standard, 12-Bit LCD / 16-Bit RGB565)",
            L"RGB444, 480-Byte Zeilen (Diagnose)",
            L"RGB444 gepackt, 360-Byte Zeilen (Diagnose)",
            L"RGB565, 480-Byte Zeilen (Diagnose)",
            L"Nur LCD-Register (Diagnose)"
        },
        L"CPU- & Peripherie-Timing",
        {
            L"Automatische ROM-Einstellungen (Standard)",
            L"Standard: 1× (20 MIPS Basis)",
            L"Automatische Register-Takte (experimentell)",
            L"Mehr CPU-Arbeit: 2× (40 MIPS)",
            L"Mehr CPU-Arbeit: 4× (80 MIPS)",
            L"Mehr CPU-Arbeit: 8× (160 MIPS, schneller Kampf in Wade Hixton)"
        },
        L"FPS- & Audio-Metriken in sessions\\performance.csv protokollieren",
        L"Erweitertes Debug-Logging in sessions\\session.log aktivieren",
        L"Hinweis: Änderungen am Timing-Modus oder Display-Decoder werden beim nächsten Start bzw. Neustart (Strg+R) des ROMs übernommen.",
        L"Kurztasten & Steuerung",
        L"• Strg+O: ROM laden    ·    Strg+R: Spiel neu starten\n"
        L"• ROM-Datei ins Fenster ziehen: Spiel direkt per Drag & Drop starten\n"
        L"• F11 oder Alt+Eingabe: Vollbild umschalten    ·    Esc: Vollbild verlassen / Stopp\n"
        L"• F5: Pause / Weiter    ·    F6: Einzelner CPU-Schritt    ·    F7: Snapshot speichern\n"
        L"• Video Play/Pause: P / F10 / Leertaste / Media-Play\n"
        L"• Video Stopp / Episoden-Menü: Rücktaste (Backspace) / F9 / Eingabe / Media-Stopp\n"
        L"• Video Zurück / Rücklauf |<<: B / F8 / Bild-Auf / , (gedrückt halten zum Zurückspulen)\n"
        L"• Video Weiter / Vorlauf >>|: N / F12 / Bild-Ab / . (gedrückt halten zum Vorspulen)\n\n"
        L"Über das Menü 'Wiedergabe' oben lassen sich RealVideo-Module und deren Sprache auch direkt per Maus steuern."
    },
    // 2: Español
    {
        {L"Carpetas y Sistema", L"Controles", L"Sonido", L"Vídeo", L"Tiempo y Diagnóstico", L"Atajos y Ayuda"},
        L"Archivo", L"Reproducción", L"Configuración",
        L"Cargar ROM...\tCtrl+O", L"Reiniciar juego\tCtrl+R", L"Detener juego", L"Salir",
        L"Reproducir / Pausa\tP / F10",
        L"Detener / Menú de episodios\tRetroceso / F9",
        L"|<< Anterior / Rebobinar\tB / F8",
        L">>I Siguiente / Avance rápido\tN / F12",
        L"<< Retroceder (Mantener |<<)",
        L">> Avanzar (Mantener >>I)",
        L"Idioma de cartucho / vídeo",
        {
            L"Automático (según idioma de interfaz)",
            L"Preguntar siempre en el cartucho (Menú de banderas)",
            L"English (EN)",
            L"Deutsch (DE)",
            L"Español (ES)",
            L"Italiano (IT)",
            L"Nederlands (NL)",
            L"Français (FR)",
            L"Türkçe (TR)",
            L"Mantener valor guardado en EEPROM"
        },
        L"OceanBlast — Ctrl+O: Abrir ROM o arrastrar ROM aquí",
        L"OceanBlast — Configuración",
        L"Cargar ROM: Ctrl+O · Arrastra una ROM a la ventana\nConfiguración: Sistema, Mando, Sonido, Vídeo y Tiempo",
        L"Cerrar", L"Los cambios se guardan localmente.",
        L"No se pudo guardar la configuración.",
        L"Selecciona primero un archivo ROM de digiBLAST válido (*.bin;*.rom).",
        L"No se pudo abrir el archivo de registro de sesión.",
        L"No se pudo iniciar el proceso del emulador.",
        L"Seleccionar carpeta predeterminada para ROMs de digiBLAST",
        L"ROM digiBLAST (*.bin;*.rom)\0*.bin;*.rom\0Todos los archivos (*.*)\0*.*\0",
        L"Idioma de interfaz",
        L"Carpeta de ROMs (directorio inicial para Ctrl+O)", L"Directorio predeterminado", L"Elegir...",
        L"ROM de digiBLAST seleccionada", L"Ninguna ROM seleccionada", L"Archivo...",
        L"Iniciar juego", L"Detener juego",
        L"Memoria persistente del dispositivo (EEPROM I2C de 2 KB)",
        L"Guardar ajustes del dispositivo y partidas",
        L"Consejo: Puedes arrastrar y soltar archivos ROM (*.bin; *.rom) directamente en la ventana principal o abrirlos con Ctrl+O.",
        L"Seleccionar mando", L"Solo teclado", L"Mando guardado (desconectado)", L"Actualizar",
        L"Perfil",
        {L"Stadia / Xbox — Posiciones", L"PlayStation — Posiciones", L"digiBLAST / USB — Personalizado"},
        L"digiBLAST  Mando                  Teclado",
        L"Haz clic en una asignación y pulsa un botón o tecla. Esc cancela.",
        L"Pulsa ahora un botón o dirección del mando … (Esc: Cancelar)",
        L"Pulsa ahora una tecla del teclado … (Esc: Cancelar)",
        L"Conectado · Las entradas se resaltan en vivo",
        L"Ningún dispositivo conectado",
        L"Sonido", L"Activar salida de audio", L"Volumen",
        L"Audio estéreo S3C2410 IIS + DMA2 · Frecuencia estándar de 22,05 kHz con remuestreo dinámico.",
        L"Modo de visualización",
        {L"Carcasa digiBLAST (Console Skin)", L"Pantalla completa (F11 alterna)", L"Ventana estándar (LCD en ventana principal)"},
        L"Relación de aspecto",
        {L"Proporciones originales digiBLAST (3:2)", L"4:3", L"Estirar a la ventana"},
        L"Escalado entero de píxeles (prioridad sobre relación de aspecto)",
        L"Escalado de imagen suave (GDI Halftone)",
        L"Factor de escala independiente (--gui)",
        {L"2× (480 × 320)", L"3× (720 × 480, Predeterminado)", L"4× (960 × 640)"},
        L"Decodificador de pantalla",
        {
            L"Automático (Predeterminado, LCD 12 bits / RGB565 16 bits)",
            L"RGB444, filas de 480 bytes (Diagnóstico)",
            L"RGB444 empaquetado, filas de 360 bytes (Diagnóstico)",
            L"RGB565, filas de 480 bytes (Diagnóstico)",
            L"Solo registros LCD (Diagnóstico)"
        },
        L"Sincronización de CPU y periféricos",
        {
            L"Ajustes automáticos de ROM (Predeterminado)",
            L"Estándar: 1× (base 20 MIPS)",
            L"Relojes de registro automáticos (experimental)",
            L"Más trabajo de CPU: 2× (40 MIPS)",
            L"Más trabajo de CPU: 4× (80 MIPS)",
            L"Más trabajo de CPU: 8× (160 MIPS, combate rápido en Wade Hixton)"
        },
        L"Registrar métricas de FPS y audio en sessions\\performance.csv",
        L"Activar registro de depuración extendido en sessions\\session.log",
        L"Nota: Los cambios en el modo de tiempo o decodificador se aplicarán al iniciar o reiniciar (Ctrl+R) la ROM.",
        L"Atajos y Controles",
        L"• Ctrl+O: Cargar ROM    ·    Ctrl+R: Reiniciar juego\n"
        L"• Arrastrar archivo ROM a la ventana: Iniciar juego directamente\n"
        L"• F11 o Alt+Intro: Pantalla completa    ·    Esc: Salir / Detener\n"
        L"• F5: Pausa / Continuar    ·    F6: Paso único de CPU    ·    F7: Guardar instantánea\n"
        L"• Vídeo Reproducir/Pausa: P / F10 / Espacio / Media Play\n"
        L"• Vídeo Detener / Menú de episodios: Retroceso / F9 / Intro / Media Stop\n"
        L"• Vídeo Anterior / Rebobinar |<<: B / F8 / RePág / , (mantener para retroceder)\n"
        L"• Vídeo Siguiente / Avance rápido >>|: N / F12 / AvPág / . (mantener para avanzar)\n\n"
        L"Usa el menú 'Reproducción' en la barra superior para controlar vídeos y elegir el idioma del cartucho."
    },
    // 3: Italiano
    {
        {L"Cartelle e Sistema", L"Controlli", L"Audio", L"Video", L"Timing e Diagnostica", L"Scorciatoie e Aiuto"},
        L"File", L"Riproduzione", L"Impostazioni",
        L"Carica ROM...\tCtrl+O", L"Riavvia gioco\tCtrl+R", L"Ferma gioco", L"Esci",
        L"Play / Pausa\tP / F10",
        L"Stop / Menu episodi\tBackspace / F9",
        L"|<< Precedente / Riavvolgi\tB / F8",
        L">>I Successivo / Avanti veloce\tN / F12",
        L"<< Riavvolgi (Tieni |<<)",
        L">> Avanti veloce (Tieni >>I)",
        L"Lingua cartuccia / video",
        {
            L"Automatico (come lingua interfaccia)",
            L"Chiedi sempre nella cartuccia (Menu bandiere)",
            L"English (EN)",
            L"Deutsch (DE)",
            L"Español (ES)",
            L"Italiano (IT)",
            L"Nederlands (NL)",
            L"Français (FR)",
            L"Türkçe (TR)",
            L"Mantieni valore EEPROM salvato"
        },
        L"OceanBlast — Ctrl+O: Apri ROM o trascina ROM qui",
        L"OceanBlast — Impostazioni",
        L"Carica ROM: Ctrl+O · Trascina una ROM nella finestra\nImpostazioni: Sistema, Controller, Audio, Video e Timing",
        L"Chiudi", L"Le modifiche vengono salvate localmente.",
        L"Impossibile salvare le impostazioni.",
        L"Seleziona prima un file ROM digiBLAST valido (*.bin;*.rom).",
        L"Impossibile aprire il file di log della sessione.",
        L"Impossibile avviare il processo dell'emulatore.",
        L"Seleziona la cartella predefinita per le ROM digiBLAST",
        L"ROM digiBLAST (*.bin;*.rom)\0*.bin;*.rom\0Tutti i file (*.*)\0*.*\0",
        L"Lingua interfaccia",
        L"Cartella ROM (directory iniziale per Ctrl+O)", L"Directory predefinita", L"Sfoglia...",
        L"ROM digiBLAST selezionata", L"Nessuna ROM selezionata", L"File...",
        L"Avvia gioco", L"Ferma gioco",
        L"Memoria persistente del dispositivo (EEPROM I2C da 2 KB)",
        L"Salva impostazioni del dispositivo e progressi",
        L"Suggerimento: Puoi trascinare i file ROM (*.bin; *.rom) direttamente nella finestra principale o aprirli con Ctrl+O.",
        L"Seleziona controller", L"Solo tastiera", L"Controller salvato (scollegato)", L"Aggiorna",
        L"Preset",
        {L"Stadia / Xbox — Posizioni", L"PlayStation — Posizioni", L"digiBLAST / USB — Personalizzato"},
        L"digiBLAST  Controller             Tastiera",
        L"Fai clic su un comando, poi premi un pulsante o tasto. Esc annulla.",
        L"Premi ora un pulsante o direzione del controller … (Esc: Annulla)",
        L"Premi ora un tasto della tastiera … (Esc: Annulla)",
        L"Connesso · Gli input sono evidenziati in tempo reale",
        L"Nessun dispositivo di input connesso",
        L"Audio", L"Attiva uscita audio", L"Volume",
        L"Audio stereo S3C2410 IIS + DMA2 · Frequenza standard 22,05 kHz con ricampionamento dinamico.",
        L"Modalità di visualizzazione",
        {L"Scocca digiBLAST (Console Skin)", L"Schermo intero (F11 commuta)", L"Finestra standard (LCD nella finestra principale)"},
        L"Proporzioni",
        {L"Proporzioni originali digiBLAST (3:2)", L"4:3", L"Adatta alla finestra"},
        L"Scala pixel intera (ha priorità sulle proporzioni)",
        L"Scala immagine morbida (GDI Halftone)",
        L"Fattore di scala autonomo (--gui)",
        {L"2× (480 × 320)", L"3× (720 × 480, Predefinito)", L"4× (960 × 640)"},
        L"Decoder display",
        {
            L"Automatico (Predefinito, LCD 12 bit / RGB565 16 bit)",
            L"RGB444, righe da 480 byte (Diagnostica)",
            L"RGB444 compatto, righe da 360 byte (Diagnostica)",
            L"RGB565, righe da 480 byte (Diagnostica)",
            L"Solo registri LCD (Diagnostica)"
        },
        L"Timing CPU e periferiche",
        {
            L"Impostazioni ROM automatiche (Predefinito)",
            L"Standard: 1× (base 20 MIPS)",
            L"Clock registri automatici (sperimentale)",
            L"Più lavoro CPU: 2× (40 MIPS)",
            L"Più lavoro CPU: 4× (80 MIPS)",
            L"Più lavoro CPU: 8× (160 MIPS, combattimento veloce in Wade Hixton)"
        },
        L"Registra metriche FPS e audio in sessions\\performance.csv",
        L"Attiva log di debug esteso in sessions\\session.log",
        L"Nota: Le modifiche al timing o al decoder avranno effetto al prossimo avvio o riavvio (Ctrl+R) della ROM.",
        L"Scorciatoie e Controlli",
        L"• Ctrl+O: Carica ROM    ·    Ctrl+R: Riavvia gioco\n"
        L"• Trascina file ROM nella finestra: Avvia direttamente con drag & drop\n"
        L"• F11 o Alt+Invio: Schermo intero    ·    Esc: Esci / Stop\n"
        L"• F5: Pausa / Riprendi    ·    F6: Singolo passo CPU    ·    F7: Salva snapshot\n"
        L"• Video Play/Pausa: P / F10 / Spazio / Media Play\n"
        L"• Video Stop / Menu episodi: Backspace / F9 / Invio / Media Stop\n"
        L"• Video Precedente / Riavvolgi |<<: B / F8 / PagSu / , (tieni premuto per riavvolgere)\n"
        L"• Video Successivo / Avanti veloce >>|: N / F12 / PagGiù / . (tieni premuto per avanzare)\n\n"
        L"Usa il menu 'Riproduzione' nella barra superiore per controllare i video e scegliere la lingua della cartuccia."
    },
    // 4: Nederlands
    {
        {L"Mappen & Systeem", L"Besturing", L"Geluid", L"Video", L"Timing & Diagnose", L"Sneltoetsen & Hulp"},
        L"Bestand", L"Afspelen", L"Instellingen",
        L"ROM laden...\tCtrl+O", L"Spel herstarten\tCtrl+R", L"Spel stoppen", L"Afsluiten",
        L"Afspelen / Pauze\tP / F10",
        L"Stop / Afleveringenmenu\tBackspace / F9",
        L"|<< Vorige / Terugspoelen\tB / F8",
        L">>I Volgende / Vooruitspoelen\tN / F12",
        L"<< Terugspoelen (Houd |<< ingedrukt)",
        L">> Vooruitspoelen (Houd >>I ingedrukt)",
        L"Cartridge- / videotaal",
        {
            L"Automatisch (zoals UI-taal)",
            L"Altijd in cartridge vragen (Vlaggenmenu)",
            L"English (EN)",
            L"Deutsch (DE)",
            L"Español (ES)",
            L"Italiano (IT)",
            L"Nederlands (NL)",
            L"Français (FR)",
            L"Türkçe (TR)",
            L"Opgeslagen EEPROM-waarde behouden"
        },
        L"OceanBlast — Ctrl+O: ROM openen of sleep ROM hierheen",
        L"OceanBlast — Instellingen",
        L"ROM laden: Ctrl+O · Sleep een ROM naar dit venster\nInstellingen: Systeem, Controller, Geluid, Video en Timing",
        L"Sluiten", L"Wijzigingen worden lokaal opgeslagen.",
        L"Instellingen konden niet worden opgeslagen.",
        L"Selecteer eerst een geldig digiBLAST-ROM-bestand (*.bin;*.rom).",
        L"Kon sessielogbestand niet openen.",
        L"Kon emulatorproces niet starten.",
        L"Standaardmap voor digiBLAST-ROMs selecteren",
        L"digiBLAST ROM (*.bin;*.rom)\0*.bin;*.rom\0Alle bestanden (*.*)\0*.*\0",
        L"Interfacetaal",
        L"ROM-map (startmap voor Ctrl+O)", L"Standaardmap", L"Bladeren...",
        L"Geselecteerde digiBLAST-ROM", L"Nog geen ROM geselecteerd", L"Bestand...",
        L"Spel starten", L"Spel stoppen",
        L"Permanent apparaatgeheugen (2 KB I2C-EEPROM)",
        L"Apparaatinstellingen en voortgang opslaan",
        L"Tip: Je kunt ROM-bestanden (*.bin; *.rom) altijd direct naar het hoofdvenster slepen of openen met Ctrl+O.",
        L"Controller selecteren", L"Alleen toetsenbord", L"Opgeslagen controller (niet verbonden)", L"Vernieuwen",
        L"Profiel",
        {L"Stadia / Xbox — Posities", L"PlayStation — Posities", L"digiBLAST / USB — Aangepast"},
        L"digiBLAST  Controller             Toetsenbord",
        L"Klik op een toewijzing en druk op een knop of toets. Esc annuleert.",
        L"Druk nu op een controllerknop of richting … (Esc: Annuleren)",
        L"Druk nu op een toetsenbordtoets … (Esc: Annuleren)",
        L"Verbonden · Invoer wordt live gemarkeerd",
        L"Geen verbonden invoerapparaat",
        L"Geluid", L"Audio-uitvoer inschakelen", L"Volume",
        L"S3C2410 IIS + DMA2 stereo-audio · 22,05 kHz standaardfrequentie met dynamische resampling.",
        L"Weergavemodus",
        {L"digiBLAST-behuizing (Console Skin)", L"Volledig scherm (F11 schakelt)", L"Standaardvenster (LCD in hoofdvenster)"},
        L"Beeldverhouding",
        {L"Originele digiBLAST-verhouding (3:2)", L"4:3", L"Uitrekken tot venster"},
        L"Gehele pixelschaling (heeft voorrang op beeldverhouding)",
        L"Vloeiende beeldschaling (GDI Halftone)",
        L"Standalone schaalfactor (--gui)",
        {L"2× (480 × 320)", L"3× (720 × 480, Standaard)", L"4× (960 × 640)"},
        L"Display-decoder",
        {
            L"Automatisch (Standaard, 12-bit LCD / 16-bit RGB565)",
            L"RGB444, 480-byte rijen (Diagnose)",
            L"RGB444 gepakt, 360-byte rijen (Diagnose)",
            L"RGB565, 480-byte rijen (Diagnose)",
            L"Alleen LCD-registers (Diagnose)"
        },
        L"CPU- & randapparatuur-timing",
        {
            L"Automatische ROM-instellingen (Standaard)",
            L"Standaard: 1× (20 MIPS basis)",
            L"Automatische registerklokken (experimenteel)",
            L"Meer CPU-werk: 2× (40 MIPS)",
            L"Meer CPU-werk: 4× (80 MIPS)",
            L"Meer CPU-werk: 8× (160 MIPS, snel gevecht in Wade Hixton)"
        },
        L"FPS- & audiogegevens loggen in sessions\\performance.csv",
        L"Uitgebreide debug-logging inschakelen in sessions\\session.log",
        L"Let op: Wijzigingen aan de timingmodus of display-decoder worden actief bij de volgende start of herstart (Ctrl+R).",
        L"Sneltoetsen & Besturing",
        L"• Ctrl+O: ROM laden    ·    Ctrl+R: Spel herstarten\n"
        L"• Sleep ROM-bestand naar venster: Spel direct starten via drag & drop\n"
        L"• F11 of Alt+Enter: Volledig scherm    ·    Esc: Verlaten / Stop\n"
        L"• F5: Pauze / Hervatten    ·    F6: Enkele CPU-stap    ·    F7: Snapshot opslaan\n"
        L"• Video Afspelen/Pauze: P / F10 / Spatie / Media Play\n"
        L"• Video Stop / Afleveringenmenu: Backspace / F9 / Enter / Media Stop\n"
        L"• Video Vorige / Terugspoelen |<<: B / F8 / PageUp / , (ingedrukt houden om te spoelen)\n"
        L"• Video Volgende / Vooruitspoelen >>|: N / F12 / PageDown / . (ingedrukt houden om te spoelen)\n\n"
        L"Gebruik het menu 'Afspelen' bovenin voor directe videobesturing en taalkeuze voor meertalige cartridges."
    },
    // 5: Français
    {
        {L"Dossiers & Système", L"Commandes", L"Son", L"Vidéo", L"Timing & Diagnostic", L"Raccourcis & Aide"},
        L"Fichier", L"Lecture", L"Paramètres",
        L"Charger une ROM...\tCtrl+O", L"Redémarrer le jeu\tCtrl+R", L"Arrêter le jeu", L"Quitter",
        L"Lecture / Pause\tP / F10",
        L"Arrêt / Menu des épisodes\tRetour arrière / F9",
        L"|<< Précédent / Retour rapide\tB / F8",
        L">>I Suivant / Avance rapide\tN / F12",
        L"<< Reculer (Maintenir |<<)",
        L">> Avancer (Maintenir >>I)",
        L"Langue cartouche / vidéo",
        {
            L"Automatique (selon la langue de l'interface)",
            L"Toujours demander dans la cartouche (Menu drapeaux)",
            L"English (EN)",
            L"Deutsch (DE)",
            L"Español (ES)",
            L"Italiano (IT)",
            L"Nederlands (NL)",
            L"Français (FR)",
            L"Türkçe (TR)",
            L"Conserver la valeur EEPROM enregistrée"
        },
        L"OceanBlast — Ctrl+O : Ouvrir une ROM ou glisser une ROM ici",
        L"OceanBlast — Paramètres",
        L"Charger une ROM : Ctrl+O · Glisser-déposer une ROM dans la fenêtre\nParamètres : Système, Manette, Son, Vidéo et Timing",
        L"Fermer", L"Les modifications sont enregistrées localement.",
        L"Impossible d'enregistrer les paramètres.",
        L"Veuillez d'abord sélectionner un fichier ROM digiBLAST valide (*.bin;*.rom).",
        L"Impossible d'ouvrir le fichier journal de session.",
        L"Impossible de démarrer le processus de l'émulateur.",
        L"Sélectionner le dossier par défaut pour les ROMs digiBLAST",
        L"ROM digiBLAST (*.bin;*.rom)\0*.bin;*.rom\0Tous les fichiers (*.*)\0*.*\0",
        L"Langue de l'interface",
        L"Dossier ROM (répertoire initial pour Ctrl+O)", L"Répertoire par défaut", L"Parcourir...",
        L"ROM digiBLAST sélectionnée", L"Aucune ROM sélectionnée", L"Fichier...",
        L"Démarrer le jeu", L"Arrêter le jeu",
        L"Mémoire persistante de l'appareil (EEPROM I2C de 2 Ko)",
        L"Enregistrer les réglages et la progression",
        L"Astuce : Vous pouvez glisser-déposer des fichiers ROM (*.bin; *.rom) directement dans la fenêtre principale ou les ouvrir avec Ctrl+O.",
        L"Sélectionner la manette", L"Clavier uniquement", L"Manette enregistrée (déconnectée)", L"Actualiser",
        L"Profil",
        {L"Stadia / Xbox — Positions", L"PlayStation — Positions", L"digiBLAST / USB — Personnalisé"},
        L"digiBLAST  Manette                Clavier",
        L"Cliquez sur une commande puis appuyez sur un bouton ou une touche. Échap annule.",
        L"Appuyez maintenant sur un bouton ou une direction de la manette … (Échap : Annuler)",
        L"Appuyez maintenant sur une touche du clavier … (Échap : Annuler)",
        L"Connecté · Les entrées sont mises en évidence en direct",
        L"Aucun périphérique d'entrée connecté",
        L"Son", L"Activer la sortie audio", L"Volume",
        L"Audio stéréo S3C2410 IIS + DMA2 · Fréquence standard de 22,05 kHz avec rééchantillonnage dynamique.",
        L"Mode d'affichage",
        {L"Boîtier digiBLAST (Console Skin)", L"Plein écran (F11 bascule)", L"Fenêtre standard (LCD dans la fenêtre principale)"},
        L"Format d'image",
        {L"Proportions digiBLAST d'origine (3:2)", L"4:3", L"Étirer à la fenêtre"},
        L"Mise à l'échelle entière des pixels (prioritaire sur le format)",
        L"Mise à l'échelle douce de l'image (GDI Halftone)",
        L"Facteur d'échelle autonome (--gui)",
        {L"2× (480 × 320)", L"3× (720 × 480, Par défaut)", L"4× (960 × 640)"},
        L"Décodeur d'affichage",
        {
            L"Automatique (Par défaut, LCD 12 bits / RGB565 16 bits)",
            L"RGB444, lignes de 480 octets (Diagnostic)",
            L"RGB444 compacté, lignes de 360 octets (Diagnostic)",
            L"RGB565, lignes de 480 octets (Diagnostic)",
            L"Registres LCD uniquement (Diagnostic)"
        },
        L"Timing CPU & périphériques",
        {
            L"Réglages ROM automatiques (Par défaut)",
            L"Standard : 1× (base 20 MIPS)",
            L"Horloges de registres automatiques (expérimental)",
            L"Plus de travail CPU : 2× (40 MIPS)",
            L"Plus de travail CPU : 4× (80 MIPS)",
            L"Plus de travail CPU : 8× (160 MIPS, combat rapide dans Wade Hixton)"
        },
        L"Enregistrer les mesures FPS et audio dans sessions\\performance.csv",
        L"Activer la journalisation de débogage étendue dans sessions\\session.log",
        L"Remarque : Les modifications du mode de timing ou du décodeur prennent effet au prochain démarrage ou redémarrage (Ctrl+R) de la ROM.",
        L"Raccourcis & Commandes",
        L"• Ctrl+O : Charger une ROM    ·    Ctrl+R : Redémarrer le jeu\n"
        L"• Glisser un fichier ROM dans la fenêtre : Lancer directement par glisser-déposer\n"
        L"• F11 ou Alt+Entrée : Plein écran    ·    Échap : Quitter / Arrêt\n"
        L"• F5 : Pause / Reprendre    ·    F6 : Pas CPU unique    ·    F7 : Enregistrer un instantané\n"
        L"• Vidéo Lecture/Pause : P / F10 / Espace / Media Play\n"
        L"• Vidéo Arrêt / Menu épisodes : Retour arrière / F9 / Entrée / Media Stop\n"
        L"• Vidéo Précédent / Retour rapide |<< : B / F8 / PagePréc / , (maintenir pour reculer)\n"
        L"• Vidéo Suivant / Avance rapide >>| : N / F12 / PageSuiv / . (maintenir pour avancer)\n\n"
        L"Utilisez le menu « Lecture » dans la barre supérieure pour contrôler la vidéo et choisir la langue de la cartouche."
    },
    // 6: Türkçe
    {
        {L"Klasörler ve Sistem", L"Kontroller", L"Ses", L"Görüntü", L"Zamanlama ve Tanılama", L"Kısayollar ve Yardım"},
        L"Dosya", L"Oynatma", L"Ayarlar",
        L"ROM Yükle...\tCtrl+O", L"Oyunu Yeniden Başlat\tCtrl+R", L"Oyunu Durdur", L"Çıkış",
        L"Oynat / Duraklat\tP / F10",
        L"Durdur / Bölüm Menüsü\tBackspace / F9",
        L"|<< Önceki / Geri Sar\tB / F8",
        L">>I Sonraki / İleri Sar\tN / F12",
        L"<< Geriye Sar (|<< Basılı Tut)",
        L">> İleriye Sar (>>I Basılı Tut)",
        L"Kartuş / Video Dili",
        {
            L"Otomatik (Arayüz Diliyle Aynı)",
            L"Her Zaman Kartuşta Sor (Bayrak Menüsü)",
            L"English (EN)",
            L"Deutsch (DE)",
            L"Español (ES)",
            L"Italiano (IT)",
            L"Nederlands (NL)",
            L"Français (FR)",
            L"Türkçe (TR)",
            L"Kayıtlı EEPROM Değerini Koru"
        },
        L"OceanBlast — Ctrl+O: ROM aç veya ROM dosyasını buraya sürükle",
        L"OceanBlast — Ayarlar",
        L"ROM Yükle: Ctrl+O · ROM dosyasını pencereye sürükleyin\nAyarlar: Sistem, Kontrolcü, Ses, Görüntü ve Zamanlama",
        L"Kapat", L"Değişiklikler yerel olarak kaydedilir.",
        L"Ayarlar kaydedilemedi.",
        L"Lütfen önce geçerli bir digiBLAST ROM dosyası (*.bin;*.rom) seçin.",
        L"Oturum günlük dosyası açılamadı.",
        L"Emülatör işlemi başlatılamadı.",
        L"digiBLAST ROM'ları için varsayılan klasörü seçin",
        L"digiBLAST ROM (*.bin;*.rom)\0*.bin;*.rom\0Tüm Dosyalar (*.*)\0*.*\0",
        L"Arayüz Dili",
        L"ROM Klasörü (Ctrl+O için başlangıç dizini)", L"Varsayılan dizin", L"Seç...",
        L"Seçilen digiBLAST ROM", L"Henüz ROM seçilmedi", L"Dosya...",
        L"Oyunu Başlat", L"Oyunu Durdur",
        L"Kalıcı Cihaz Belleği (2 KB I2C-EEPROM)",
        L"Cihaz ayarlarını ve oyun kayıtlarını sakla",
        L"İpucu: ROM dosyalarını (*.bin; *.rom) istediğiniz zaman doğrudan ana pencereye sürükleyip bırakabilir veya Ctrl+O ile açabilirsiniz.",
        L"Kontrolcü Seçin", L"Yalnızca Klavye", L"Kayıtlı Kontrolcü (bağlı değil)", L"Yenile",
        L"Profil",
        {L"Stadia / Xbox — Düzeni", L"PlayStation — Düzeni", L"digiBLAST / USB — Özel"},
        L"digiBLAST  Kontrolcü              Klavye",
        L"Bir atamaya tıklayın, ardından bir düğmeye veya tuşa basın. Esc iptal eder.",
        L"Şimdi bir kontrolcü düğmesine veya yönüne basın … (Esc: İptal)",
        L"Şimdi bir klavye tuşuna basın … (Esc: İptal)",
        L"Bağlı · Girişler canlı olarak vurgulanır",
        L"Bağlı giriş cihazı yok",
        L"Ses", L"Ses çıkışını etkinleştir", L"Ses Seviyesi",
        L"S3C2410 IIS + DMA2 stereo ses · Dinamik yeniden örnekleme ile 22,05 kHz standart hız.",
        L"Görünüm Modu",
        {L"digiBLAST Gövdesi (Console Skin)", L"Tam Ekran (F11 geçiş yapar)", L"Standart Pencere (Ana pencerede LCD)"},
        L"En-Boy Oranı",
        {L"Orijinal digiBLAST oranları (3:2)", L"4:3", L"Pencereye sığdır (uzatılmış)"},
        L"Tam sayı piksel ölçekleme (en-boy oranından önceliklidir)",
        L"Yumuşak görüntü ölçekleme (GDI Halftone)",
        L"Bağımsız ölçek faktörü (--gui)",
        {L"2× (480 × 320)", L"3× (720 × 480, Varsayılan)", L"4× (960 × 640)"},
        L"Ekran Kod Çözücü",
        {
            L"Otomatik (Varsayılan, 12-bit LCD / 16-bit RGB565)",
            L"RGB444, 480 bayt satırlar (Tanılama)",
            L"RGB444 sıkıştırılmış, 360 bayt satırlar (Tanılama)",
            L"RGB565, 480 bayt satırlar (Tanılama)",
            L"Yalnızca LCD yazmaçları (Tanılama)"
        },
        L"CPU ve Çevre Birimi Zamanlaması",
        {
            L"Otomatik ROM ayarları (Varsayılan)",
            L"Standart: 1× (20 MIPS temel)",
            L"Otomatik yazmaç saatleri (deneysel)",
            L"Daha fazla CPU işi: 2× (40 MIPS)",
            L"Daha fazla CPU işi: 4× (80 MIPS)",
            L"Daha fazla CPU işi: 8× (160 MIPS, Wade Hixton'da hızlı dövüş)"
        },
        L"FPS ve ses metriklerini sessions\\performance.csv dosyasına kaydet",
        L"Genişletilmiş hata ayıklama günlüğünü sessions\\session.log dosyasına kaydet",
        L"Not: Zamanlama modu veya ekran kod çözücü değişiklikleri ROM'u bir sonraki başlatmanızda veya yeniden başlatmanızda (Ctrl+R) etkinleşir.",
        L"Kısayollar ve Kontroller",
        L"• Ctrl+O: ROM yükle    ·    Ctrl+R: Oyunu yeniden başlat\n"
        L"• ROM dosyasını pencereye sürükle: Sürükle ve bırak ile doğrudan başlat\n"
        L"• F11 veya Alt+Enter: Tam ekran    ·    Esc: Çıkış / Durdur\n"
        L"• F5: Duraklat / Devam    ·    F6: Tek CPU adımı    ·    F7: Anlık görüntü kaydet\n"
        L"• Video Oynat/Duraklat: P / F10 / Boşluk / Media Play\n"
        L"• Video Durdur / Bölüm Menüsü: Backspace / F9 / Enter / Media Stop\n"
        L"• Video Önceki / Geri Sar |<<: B / F8 / PageUp / , (geri sarmak için basılı tutun)\n"
        L"• Video Sonraki / İleri Sar >>|: N / F12 / PageDown / . (ileri sarmak için basılı tutun)\n\n"
        L"Üst çubuktaki 'Oynatma' menüsünden videoları tek tıkla kontrol edebilir ve çok dilli kartuşların dilini seçebilirsiniz."
    }
};

static const UiText& tr() {
    return kUiText[std::min(6u, settings.language)];
}

static HWND findEmbeddedDisplay() {
    if (!window) return nullptr;
    return FindWindowExA(window, nullptr, "OceanBlastDisplayClass", nullptr);
}

static void syncCompatibilityControls() {
    if (!soundBox) return;
    SetWindowTextW(pathBox, settings.lastRom.c_str());
    SendMessageW(scaleBox, CB_SETCURSEL, settings.scaleIndex, 0);
    SendMessageW(soundBox, BM_SETCHECK, settings.soundEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(profileBox, BM_SETCHECK, settings.fpsLog ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(debugBox, BM_SETCHECK, settings.debugLog ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(formatBox, CB_SETCURSEL, settings.displayFormat, 0);
    SendMessageW(nvramBox, BM_SETCHECK, settings.keepNvram ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(timingBox, CB_SETCURSEL, settings.timingMode, 0);
    SendMessageW(windowModeBox, CB_SETCURSEL, settings.windowMode, 0);
}

static void pullCompatibilityControls() {
    if (!soundBox) return;
    wchar_t filename[32768]{};
    GetWindowTextW(pathBox, filename, 32768);
    if (filename[0]) settings.lastRom = filename;
    settings.scaleIndex = static_cast<unsigned>(std::max<LRESULT>(0, SendMessageW(scaleBox, CB_GETCURSEL, 0, 0)));
    settings.soundEnabled = SendMessageW(soundBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.fpsLog = SendMessageW(profileBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.debugLog = SendMessageW(debugBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.displayFormat = static_cast<unsigned>(std::max<LRESULT>(0, SendMessageW(formatBox, CB_GETCURSEL, 0, 0)));
    settings.keepNvram = SendMessageW(nvramBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.timingMode = static_cast<unsigned>(std::max<LRESULT>(0, SendMessageW(timingBox, CB_GETCURSEL, 0, 0)));
    settings.windowMode = static_cast<unsigned>(std::max<LRESULT>(0, SendMessageW(windowModeBox, CB_GETCURSEL, 0, 0)));
}

static void notifyRunningGameSettingsChanged() {
    syncCompatibilityControls();
    if (HWND child = findEmbeddedDisplay()) {
        PostMessageA(child, WM_APP + 1, 0, 0);
    }
}

static void saveSettings() {
    if (!settings.save(input.key())) {
        MessageBoxW(window, tr().saveError, L"OceanBlast", MB_ICONERROR);
    }
    notifyRunningGameSettingsChanged();
}

static void setIdleTitle() {
    if (!window) return;
    SetWindowTextW(window, tr().idleWindowTitle);
}

static BOOL CALLBACK closeChildWindow(HWND w, LPARAM p) {
    DWORD owner = 0;
    GetWindowThreadProcessId(w, &owner);
    if (owner == static_cast<DWORD>(p)) PostMessageW(w, WM_CLOSE, 0, 0);
    return TRUE;
}

static void stop() {
    if (process) {
        if (HWND child = findEmbeddedDisplay()) {
            SendMessageA(child, WM_CLOSE, 0, 0);
        }
        const DWORD pid = GetProcessId(process);
        EnumWindows(closeChildWindow, static_cast<LPARAM>(pid));
        if (WaitForSingleObject(process, 500) != WAIT_OBJECT_0) {
            TerminateProcess(process, 0);
            WaitForSingleObject(process, 200);
        }
        CloseHandle(process);
        process = nullptr;
    }
    if (startButton) EnableWindow(startButton, TRUE);
    if (stopButton) EnableWindow(stopButton, FALSE);
    setIdleTitle();
    if (window) InvalidateRect(window, nullptr, TRUE);
}

#include "mp3_manager_win32.h"

static void toggleMainFullscreen() {
    if (!window) return;
    LONG style = GetWindowLongW(window, GWL_STYLE);
    if (!mainFullscreen) {
        savedPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(window, &savedPlacement);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &mi);
        mainFullscreen = true;
        SetMenu(window, nullptr);
        SetWindowLongW(window, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN);
        SetWindowPos(window, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED);
    } else {
        mainFullscreen = false;
        SetMenu(window, mainMenu);
        SetWindowLongW(window, GWL_STYLE, style | WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN);
        SetWindowPlacement(window, &savedPlacement);
        SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    if (HWND child = findEmbeddedDisplay()) {
        RECT rc{};
        GetClientRect(window, &rc);
        SetWindowPos(child, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    InvalidateRect(window, nullptr, TRUE);
}

static bool launchRom(const std::wstring& romFile) {
    DWORD attrs = GetFileAttributesW(romFile.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        MessageBoxW(window, tr().invalidRomWarning, L"OceanBlast", MB_ICONWARNING);
        return false;
    }
    stop();
    const bool mp3 = isMp3Rom(romFile);
    if (mp3) {
        const bool existed = std::filesystem::exists(musicStoragePath());
        if (!createMusicStorage(window)) return false;
        try { oceanblast::Mp3Storage storage(musicStoragePath()); }
        catch (const std::exception& error) { musicError(window, error); return false; }
        if (!existed) importMusic(window);
    }
    activeRomPath = romFile;
    settings.lastRom = romFile;
    if (settings.romFolder.empty()) {
        std::filesystem::path p(romFile);
        if (p.has_parent_path()) settings.romFolder = p.parent_path().wstring();
    }
    saveSettings();

    const int scale = static_cast<int>(settings.scaleIndex) + 2;
    std::wstring command = L"\"" + executable + L"\" \"" + romFile + L"\" --gui --scale " + std::to_wstring(scale);
    command += L" --parent-hwnd " + std::to_wstring(reinterpret_cast<uintptr_t>(window));
    if (mp3) command += L" --usb-storage \"" + musicStoragePath().wstring() + L"\"";
    if (settings.windowMode == 0) command += L" --window-mode skin";
    else command += L" --window-mode plain";
    if (settings.windowMode == 1 && !mainFullscreen) toggleMainFullscreen();

    if (settings.soundEnabled) command += L" --sound";
    if (settings.fpsLog) command += L" --profile";
    if (settings.debugLog) command += L" --debug";

    if (settings.timingMode == 1) command += L" --preset off";
    else if (settings.timingMode == 2) command += L" --timing auto";
    else if (settings.timingMode == 3) command += L" --cpu-steps-per-tick 2";
    else if (settings.timingMode == 4) command += L" --cpu-steps-per-tick 4";
    else if (settings.timingMode == 5) command += L" --cpu-steps-per-tick 8";

    if (settings.displayFormat == 1) command += L" --display-format rgb444 --display-stride 480";
    else if (settings.displayFormat == 2) command += L" --display-format rgb444";
    else if (settings.displayFormat == 3) command += L" --display-format rgb565";
    else if (settings.displayFormat == 4) command += L" --display-format lcd";

    if (settings.keepNvram) {
        try {
            command += L" --nvram \"" + prepareCartridgeNvram(folder, romFile).wstring() + L"\"";
        } catch (const std::exception& error) {
            const std::string message=error.what();
            MessageBoxA(window,message.c_str(),"OceanBlast",MB_ICONERROR);
            return false;
        }
    }
    {
        static constexpr const wchar_t* kCartLangModes[10]{
            L"auto", L"ask", L"en", L"de", L"es", L"it", L"nl", L"fr", L"tr", L"keep"
        };
        command += L" --cart-language ";
        command += kCartLangModes[std::min(9u, settings.cartLanguage)];
    }

    SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, TRUE};
    HANDLE log = CreateFileW((folder + L"\\session.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE inHandle = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
    if (log == INVALID_HANDLE_VALUE || inHandle == INVALID_HANDLE_VALUE) {
        if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
        if (inHandle != INVALID_HANDLE_VALUE) CloseHandle(inHandle);
        MessageBoxW(window, tr().logOpenError, L"OceanBlast", MB_ICONERROR);
        return false;
    }

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = log;
    startup.hStdError = log;
    startup.hStdInput = inHandle;
    PROCESS_INFORMATION child = {};
    BOOL ok = CreateProcessW(executable.c_str(), &command[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, folder.c_str(), &startup, &child);
    CloseHandle(log);
    CloseHandle(inHandle);
    if (!ok) {
        MessageBoxW(window, tr().launchError, L"OceanBlast", MB_ICONERROR);
        return false;
    }
    process = child.hProcess;
    CloseHandle(child.hThread);
    if (startButton) EnableWindow(startButton, FALSE);
    if (stopButton) EnableWindow(stopButton, TRUE);
    if (statusBox) SetWindowTextW(statusBox, L"Emulator running. Details are logged in sessions\\session.log.");
    const auto romName = std::filesystem::path(romFile).filename().wstring();
    SetWindowTextW(window, (L"OceanBlast — " + romName).c_str());
    return true;
}

static bool browseRom(HWND owner, bool autoLaunch) {
    wchar_t filename[32768] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFile = filename;
    dialog.nMaxFile = 32768;
    dialog.lpstrFilter = tr().romFilter;
    dialog.lpstrInitialDir = settings.romFolder.empty() ? nullptr : settings.romFolder.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return false;
    settings.lastRom = filename;
    saveSettings();
    if (autoLaunch) return launchRom(filename);
    return true;
}

static void chooseRomFolder() {
    BROWSEINFOW info{};
    info.hwndOwner = settingsWindow ? settingsWindow : window;
    info.lpszTitle = tr().chooseFolderTitle;
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    if (auto item = SHBrowseForFolderW(&info)) {
        wchar_t path[MAX_PATH]{};
        if (SHGetPathFromIDListW(item, path)) {
            settings.romFolder = path;
            saveSettings();
        }
        CoTaskMemFree(item);
    }
}

static HMENU makeMenu() {
    const auto& t = tr();
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU playback = CreatePopupMenu();
    HMENU cartLang = CreatePopupMenu();
    HMENU cfg = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, OpenRom, t.menuOpenRom);
    AppendMenuW(file, MF_STRING, RestartRom, t.menuRestartRom);
    AppendMenuW(file, MF_STRING, StopRom, t.menuStopRom);
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ExitApp, t.menuExit);

    AppendMenuW(playback, MF_STRING, MediaPlayPause, t.menuPlayPause);
    AppendMenuW(playback, MF_STRING, MediaStop, t.menuStopMedia);
    AppendMenuW(playback, MF_STRING, MediaPrev, t.menuPrevTrack);
    AppendMenuW(playback, MF_STRING, MediaNext, t.menuNextTrack);
    AppendMenuW(playback, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(playback, MF_STRING, MediaSeekBack, t.menuSeekBack);
    AppendMenuW(playback, MF_STRING, MediaSeekFwd, t.menuSeekFwd);
    AppendMenuW(playback, MF_SEPARATOR, 0, nullptr);
    for (unsigned i = 0; i < 10; ++i) {
        const UINT flags = MF_STRING | (settings.cartLanguage == i ? MF_CHECKED : 0);
        AppendMenuW(cartLang, flags, CartLangBase + i, t.cartLanguageOptions[i]);
    }
    AppendMenuW(playback, MF_POPUP, reinterpret_cast<UINT_PTR>(cartLang), t.cartLanguageLabel);

    for (unsigned i = 0; i < 6; ++i) {
        std::wstring item = std::wstring(t.categories[i]) + L"...";
        AppendMenuW(cfg, MF_STRING, SettingsBase + i, item.c_str());
    }
    AppendMenuW(cfg, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(cfg, MF_STRING, ManageMp3Storage, L"MP3 Storage...");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), t.menuFile);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(playback), t.menuPlayback);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(cfg), t.menuSettings);
    return menu;
}

static void refreshMainMenuBar() {
    if (!window) return;
    HMENU newMenu = makeMenu();
    if (!mainFullscreen) {
        SetMenu(window, newMenu);
        DrawMenuBar(window);
    }
    if (mainMenu) DestroyMenu(mainMenu);
    mainMenu = newMenu;
}

static void sendMediaPulseToChild(uint32_t mask, uint32_t durationMs = 180) {
    if (HWND child = findEmbeddedDisplay()) {
        PostMessageA(child, WM_APP + 2, static_cast<WPARAM>(mask), static_cast<LPARAM>(durationMs));
    }
}

static void startScreen(HDC dc) {
    RECT r{};
    GetClientRect(window, &r);
    HBRUSH bg = CreateSolidBrush(RGB(6, 20, 36));
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    if (logo) {
        Gdiplus::Graphics g(dc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        float w = std::min(static_cast<float>(r.right) * 0.84f, 960.0f);
        float h = w * static_cast<float>(logo->GetHeight()) / static_cast<float>(logo->GetWidth());
        if (h > static_cast<float>(r.bottom) * 0.60f) {
            h = static_cast<float>(r.bottom) * 0.60f;
            w = h * static_cast<float>(logo->GetWidth()) / static_cast<float>(logo->GetHeight());
        }
        const float x = (r.right - w) / 2.0f;
        const float y = std::max(0.0f, (r.bottom - h) / 2.0f - 28.0f);
        g.DrawImage(logo.get(), x, y, w, h);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(180, 195, 213));
    HGDIOBJ oldFont = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    RECT text{10, r.bottom - 88, r.right - 10, r.bottom - 28};
    DrawTextW(dc, tr().startScreenText, -1, &text, DT_CENTER | DT_WORDBREAK);
    SelectObject(dc, oldFont);
}

static HWND settingsControl(const wchar_t* cls, const wchar_t* text, int id, int x, int y, int w, int h, DWORD style = 0) {
    HWND child = CreateWindowExW(
        0, cls, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, settingsWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    settingsChildren.push_back(child);
    return child;
}

static void settingsLabel(const wchar_t* text, int x, int y, int w = 620, int h = 24) {
    settingsControl(L"STATIC", text, 0, x, y, w, h);
}

static void comboItem(HWND h, const wchar_t* text) {
    SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}

static void rebuildSettings() {
    const auto& t = tr();
    learnAction = -1;
    deviceCombo = nullptr;
    learningLabel = nullptr;
    for (HWND child : settingsChildren) DestroyWindow(child);
    settingsChildren.clear();

    HWND nav = settingsControl(L"LISTBOX", L"", 900, 12, 14, 180, 535, LBS_NOTIFY | WS_BORDER | WS_TABSTOP);
    for (auto name : t.categories) SendMessageW(nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    SendMessageW(nav, LB_SETCURSEL, settingsPage, 0);

    settingsControl(L"BUTTON", t.closeBtn, 901, 720, 550, 110, 30, WS_TABSTOP);
    settingsLabel(t.savedLocallyNote, 210, 553, 500);

    if (settingsPage == 0) {
        settingsLabel(t.languageLabel, 210, 18, 280);
        HWND langCombo = settingsControl(L"COMBOBOX", L"", 915, 210, 42, 280, 200, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto langName : kLanguageNames) comboItem(langCombo, langName);
        SendMessageW(langCombo, CB_SETCURSEL, std::min(6u, settings.language), 0);

        settingsLabel(t.cartLanguageLabel, 510, 18, 320);
        HWND cartLangCombo = settingsControl(L"COMBOBOX", L"", 916, 510, 42, 320, 240, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto opt : t.cartLanguageOptions) comboItem(cartLangCombo, opt);
        SendMessageW(cartLangCombo, CB_SETCURSEL, std::min(9u, settings.cartLanguage), 0);

        settingsLabel(t.romFolderLabel, 210, 84);
        settingsControl(L"EDIT", settings.romFolder.empty() ? t.defaultFolderText : settings.romFolder.c_str(),
                        0, 210, 108, 490, 25, ES_READONLY | WS_BORDER);
        settingsControl(L"BUTTON", t.browseFolderBtn, 910, 710, 108, 120, 25, WS_TABSTOP);

        settingsLabel(t.selectedRomLabel, 210, 148);
        settingsControl(L"EDIT", settings.lastRom.empty() ? t.noRomSelectedText : settings.lastRom.c_str(),
                        0, 210, 172, 490, 25, ES_READONLY | WS_BORDER);
        settingsControl(L"BUTTON", t.browseFileBtn, 911, 710, 172, 120, 25, WS_TABSTOP);

        settingsControl(L"BUTTON", t.startGameBtn, 912, 210, 212, 150, 30, BS_DEFPUSHBUTTON | WS_TABSTOP);
        HWND stopBtn = settingsControl(L"BUTTON", t.stopGameBtn, 913, 372, 212, 150, 30, WS_TABSTOP);
        EnableWindow(stopBtn, process != nullptr);

        settingsLabel(t.nvramHeader, 210, 264);
        HWND nvram = settingsControl(L"BUTTON", t.nvramCheck,
                                     914, 210, 290, 610, 28, BS_AUTOCHECKBOX | WS_TABSTOP);
        SendMessageW(nvram, BM_SETCHECK, settings.keepNvram ? BST_CHECKED : BST_UNCHECKED, 0);

        settingsLabel(t.page0Tip, 210, 336, 620, 48);
    } else if (settingsPage == 1) {
        settingsLabel(t.selectControllerLabel, 210, 16);
        deviceCombo = settingsControl(L"COMBOBOX", L"", 960, 210, 42, 440, 180, CBS_DROPDOWNLIST | WS_TABSTOP);
        int selected = -1;
        for (unsigned i = 0; i < input.controllers.size(); ++i) {
            const wchar_t* itemLabel = (input.controllers[i].key == L"keyboard")
                ? t.keyboardOnlyItem
                : input.controllers[i].name.c_str();
            comboItem(deviceCombo, itemLabel);
            if (input.controllers[i].key == input.key()) selected = static_cast<int>(i);
        }
        if (selected < 0) {
            selected = static_cast<int>(input.controllers.size());
            comboItem(deviceCombo, t.disconnectedControllerItem);
        }
        SendMessageW(deviceCombo, CB_SETCURSEL, selected, 0);
        settingsControl(L"BUTTON", t.refreshBtn, 961, 666, 42, 164, 25, WS_TABSTOP);

        settingsLabel(t.presetLabel, 210, 80, 65);
        HWND preset = settingsControl(L"COMBOBOX", L"", 962, 280, 76, 370, 160, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto text : t.presets)
            comboItem(preset, text);
        SendMessageW(preset, CB_SETCURSEL, settings.controllerPreset, 0);

        settingsLabel(t.mappingColumnHeader, 210, 314, 300);
        settingsLabel(t.mappingColumnHeader, 525, 314, 305);
        for (unsigned i = 0; i < 12; ++i) {
            int y = 345 + static_cast<int>(i % 6) * 25;
            int x = 210 + static_cast<int>(i / 6) * 315;
            settingsLabel(win::actionName(i, settings.language), x, y, 48);
            settingsControl(L"BUTTON", win::controlLabel(settings.padMap[i], settings.language).c_str(),
                            980 + i, x + 50, y, 153, 23, WS_TABSTOP);
            settingsControl(L"BUTTON", win::keyboardLabel(settings.keyboardKeys[i], settings.language).c_str(),
                            1000 + i, x + 207, y, 98, 23, WS_TABSTOP);
        }
        learningLabel = settingsControl(
            L"STATIC", t.clickToBindHint,
            0, 210, 498, 620, 42);
    } else if (settingsPage == 2) {
        settingsLabel(t.soundHeader, 210, 20);
        HWND enabled = settingsControl(L"BUTTON", t.enableAudioCheck, 970, 210, 62, 400, 30, BS_AUTOCHECKBOX | WS_TABSTOP);
        SendMessageW(enabled, BM_SETCHECK, settings.soundEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

        settingsLabel(t.volumeLabel, 210, 110);
        HWND volume = settingsControl(TRACKBAR_CLASSW, L"", 971, 210, 140, 580, 45, TBS_AUTOTICKS | WS_TABSTOP);
        SendMessageW(volume, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessageW(volume, TBM_SETPOS, TRUE, settings.soundVolume);

        settingsLabel(t.soundInfoText, 210, 220, 620, 40);
    } else if (settingsPage == 3) {
        settingsLabel(t.displayModeLabel, 210, 20);
        HWND winMode = settingsControl(L"COMBOBOX", L"", 975, 210, 46, 500, 140, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto text : t.windowModes)
            comboItem(winMode, text);
        SendMessageW(winMode, CB_SETCURSEL, settings.windowMode, 0);

        settingsLabel(t.aspectRatioLabel, 210, 88);
        HWND aspect = settingsControl(L"COMBOBOX", L"", 972, 210, 114, 500, 140, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto text : t.aspectRatios)
            comboItem(aspect, text);
        SendMessageW(aspect, CB_SETCURSEL, settings.videoAspect, 0);

        HWND integer = settingsControl(L"BUTTON", t.integerScaleCheck,
                                       973, 210, 156, 600, 28, BS_AUTOCHECKBOX | WS_TABSTOP);
        SendMessageW(integer, BM_SETCHECK, settings.integerScale ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND smooth = settingsControl(L"BUTTON", t.smoothScaleCheck,
                                      974, 210, 190, 500, 28, BS_AUTOCHECKBOX | WS_TABSTOP);
        SendMessageW(smooth, BM_SETCHECK, settings.smoothVideo ? BST_CHECKED : BST_UNCHECKED, 0);

        settingsLabel(t.scaleFactorLabel, 210, 232);
        HWND scale = settingsControl(L"COMBOBOX", L"", 976, 210, 258, 300, 120, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto text : t.scaleFactors)
            comboItem(scale, text);
        SendMessageW(scale, CB_SETCURSEL, settings.scaleIndex, 0);

        settingsLabel(t.decoderLabel, 210, 300);
        HWND decoder = settingsControl(L"COMBOBOX", L"", 977, 210, 326, 460, 150, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto text : t.decoders) comboItem(decoder, text);
        SendMessageW(decoder, CB_SETCURSEL, settings.displayFormat, 0);
    } else if (settingsPage == 4) {
        settingsLabel(t.timingHeader, 210, 20);
        HWND timing = settingsControl(L"COMBOBOX", L"", 978, 210, 48, 560, 160, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto text : t.timingModes) comboItem(timing, text);
        SendMessageW(timing, CB_SETCURSEL, settings.timingMode, 0);

        HWND fps = settingsControl(L"BUTTON", t.fpsLogCheck,
                                   979, 210, 96, 600, 28, BS_AUTOCHECKBOX | WS_TABSTOP);
        SendMessageW(fps, BM_SETCHECK, settings.fpsLog ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND dbg = settingsControl(L"BUTTON", t.debugLogCheck,
                                   965, 210, 132, 600, 28, BS_AUTOCHECKBOX | WS_TABSTOP);
        SendMessageW(dbg, BM_SETCHECK, settings.debugLog ? BST_CHECKED : BST_UNCHECKED, 0);

        settingsLabel(t.timingNote, 210, 185, 620, 48);
    } else {
        settingsLabel(t.hotkeysHeader, 210, 20);
        settingsLabel(t.hotkeysBody, 210, 56, 620, 280);
    }
    InvalidateRect(settingsWindow, nullptr, TRUE);
}

static void drawController(HDC dc) {
    if (settingsPage != 1) return;
    using namespace Gdiplus;
    const auto* info = input.selected();
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeHighQuality);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // Exact coordinate space from Google_Stadia.svg (viewBox 0 0 1090 707, translate(-94, -404))
    // Uniform scale s preserves 100% of the original SVG proportions.
    constexpr float s = 0.232f;
    constexpr float ox = 520.0f - (640.0f * s);
    constexpr float oy = 128.0f - (406.0f * s);
    auto px = [&](float x) { return ox + x * s; };
    auto py = [&](float y) { return oy + y * s; };
    auto pr = [&](float r) { return r * s; };
    auto addL = [&](GraphicsPath& p, float x1, float y1, float x2, float y2) {
        p.AddLine(px(x1), py(y1), px(x2), py(y2));
    };
    auto addB = [&](GraphicsPath& p, float x1, float y1, float cx1, float cy1, float cx2, float cy2, float x2, float y2) {
        p.AddBezier(px(x1), py(y1), px(cx1), py(cy1), px(cx2), py(cy2), px(x2), py(y2));
    };
    auto circleRect = [&](float cx, float cy, float r) {
        return RectF(px(cx - r), py(cy - r), pr(r * 2.0f), pr(r * 2.0f));
    };
    auto addPill = [&](GraphicsPath& p, float cx, float cy, float hw, float hh) {
        const float left = px(cx - hw), top = py(cy - hh);
        const float w = pr(hw * 2.0f), h = pr(hh * 2.0f);
        p.AddArc(left, top, h, h, 90.0f, 180.0f);
        p.AddArc(left + w - h, top, h, h, 270.0f, 180.0f);
        p.CloseFigure();
    };

    // 1. Left & Right shoulder bumpers (id="L", id="R")
    GraphicsPath leftBumper, rightBumper;
    addL(leftBumper, 425.780f, 428.510f, 425.780f, 416.437f);
    addB(leftBumper, 425.780f, 416.437f, 425.780f, 412.277f, 422.615f, 408.835f, 418.458f, 408.666f);
    addB(leftBumper, 418.458f, 408.666f, 394.531f, 407.695f, 317.408f, 407.521f, 268.848f, 441.714f);
    addB(leftBumper, 268.848f, 441.714f, 266.800f, 443.157f, 265.638f, 445.530f, 265.638f, 448.036f);
    addL(leftBumper, 265.638f, 448.036f, 265.638f, 471.266f);

    addL(rightBumper, 854.890f, 428.510f, 854.890f, 416.437f);
    addB(rightBumper, 854.890f, 416.437f, 854.890f, 412.277f, 858.055f, 408.835f, 862.212f, 408.666f);
    addB(rightBumper, 862.212f, 408.666f, 886.140f, 407.695f, 963.262f, 407.521f, 1011.822f, 441.714f);
    addB(rightBumper, 1011.822f, 441.714f, 1013.872f, 443.157f, 1015.032f, 445.530f, 1015.032f, 448.036f);
    addL(rightBumper, 1015.032f, 448.036f, 1015.032f, 471.266f);

    const bool lPressed = win::hostPressed(input.current, 4) || win::hostPressed(input.current, 300);
    const bool rPressed = win::hostPressed(input.current, 5) || win::hostPressed(input.current, 301);

    Pen bumperPen(lPressed ? Color(255, 16, 188, 212) : Color(210, 64, 82, 104), lPressed ? 3.2f : 2.2f);
    Pen bumperPenR(rPressed ? Color(255, 16, 188, 212) : Color(210, 64, 82, 104), rPressed ? 3.2f : 2.2f);
    g.DrawPath(&bumperPen, &leftBumper);
    g.DrawPath(&bumperPenR, &rightBumper);

    // 2. Controller body contour (id="body" from Google_Stadia.svg)
    GraphicsPath body;
    addB(body, 1178.400f, 975.760f, 1158.030f, 821.080f, 1114.960f, 653.190f, 1076.580f, 552.920f);
    addB(body, 1076.580f, 552.920f, 1038.990f, 451.870f, 939.530f, 431.660f, 846.330f, 431.660f);
    addL(body, 846.330f, 431.660f, 433.590f, 431.660f);
    addB(body, 433.590f, 431.660f, 340.391f, 431.660f, 240.930f, 451.869f, 203.330f, 552.920f);
    addB(body, 203.330f, 552.920f, 165.738f, 653.970f, 122.662f, 821.080f, 101.520f, 975.760f);
    addB(body, 101.520f, 975.760f, 89.773f, 1052.712f, 146.162f, 1099.350f, 196.285f, 1104.790f);
    addB(body, 196.285f, 1104.790f, 246.409f, 1110.230f, 306.715f, 1091.570f, 336.475f, 1015.400f);
    addB(body, 336.475f, 1015.400f, 366.236f, 940.004f, 378.767f, 875.490f, 441.425f, 875.490f);
    addL(body, 441.425f, 875.490f, 837.715f, 875.490f);
    addB(body, 837.715f, 875.490f, 900.369f, 875.490f, 912.900f, 940.004f, 942.665f, 1015.400f);
    addB(body, 942.665f, 1015.400f, 972.426f, 1090.800f, 1032.736f, 1110.230f, 1082.855f, 1104.790f);
    addB(body, 1082.855f, 1104.790f, 1132.974f, 1099.350f, 1190.155f, 1052.710f, 1178.400f, 975.760f);
    body.CloseFigure();

    SolidBrush shadow(Color(28, 8, 22, 40));
    g.TranslateTransform(2.5f, 4.5f);
    g.FillPath(&shadow, &body);
    g.TranslateTransform(-2.5f, -4.5f);

    // Semi-transparent Ocean-Glass housing
    LinearGradientBrush housing(
        PointF(px(640.0f), py(425.0f)),
        PointF(px(640.0f), py(1110.0f)),
        Color(215, 240, 246, 252),
        Color(190, 202, 216, 232));
    Pen bodyOutline(Color(235, 96, 118, 142), 2.0f);
    g.FillPath(&housing, &body);
    g.DrawPath(&bodyOutline, &body);

    SolidBrush dark(Color(238, 34, 46, 60));
    SolidBrush darkSoft(Color(195, 52, 66, 84));
    SolidBrush wellFill(Color(45, 24, 42, 66));
    SolidBrush highlight(Color(250, 16, 188, 212));
    SolidBrush iconLight(Color(245, 238, 244, 250));
    Pen partOutline(Color(220, 108, 126, 146), 1.5f);
    Pen wellOutline(Color(150, 84, 104, 126), 1.2f);

    // 3. D-Pad (id="dpad" from Google_Stadia.svg)
    GraphicsPath dpad;
    addL(dpad, 405.610f, 582.850f, 374.392f, 582.850f);
    addB(dpad, 374.392f, 582.850f, 372.183f, 582.850f, 370.392f, 581.059f, 370.392f, 578.850f);
    addL(dpad, 370.392f, 578.850f, 370.392f, 547.632f);
    addB(dpad, 370.392f, 547.632f, 370.392f, 532.763f, 357.870f, 520.241f, 343.000f, 520.241f);
    addB(dpad, 343.000f, 520.241f, 328.131f, 520.241f, 315.609f, 532.763f, 315.609f, 547.632f);
    addL(dpad, 315.609f, 547.632f, 315.609f, 578.850f);
    addB(dpad, 315.609f, 578.850f, 315.609f, 581.059f, 313.818f, 582.850f, 311.609f, 582.850f);
    addL(dpad, 311.609f, 582.850f, 280.391f, 582.850f);
    addB(dpad, 280.391f, 582.850f, 265.521f, 582.850f, 252.999f, 595.371f, 252.999f, 610.241f);
    addB(dpad, 252.999f, 610.241f, 252.999f, 625.110f, 265.521f, 637.632f, 280.391f, 637.632f);
    addL(dpad, 280.391f, 637.632f, 311.609f, 637.632f);
    addB(dpad, 311.609f, 637.632f, 313.818f, 637.632f, 315.609f, 639.423f, 315.609f, 641.632f);
    addL(dpad, 315.609f, 641.632f, 315.609f, 672.850f);
    addB(dpad, 315.609f, 672.850f, 315.609f, 687.719f, 328.131f, 700.241f, 343.000f, 700.241f);
    addB(dpad, 343.000f, 700.241f, 357.870f, 700.241f, 370.392f, 687.719f, 370.392f, 672.850f);
    addL(dpad, 370.392f, 672.850f, 370.392f, 641.632f);
    addB(dpad, 370.392f, 641.632f, 370.392f, 639.423f, 372.183f, 637.632f, 374.392f, 637.632f);
    addL(dpad, 374.392f, 637.632f, 405.610f, 637.632f);
    addB(dpad, 405.610f, 637.632f, 420.480f, 637.632f, 433.002f, 625.110f, 433.002f, 610.241f);
    addB(dpad, 433.002f, 610.241f, 433.002f, 595.371f, 421.262f, 582.850f, 405.610f, 582.850f);
    dpad.CloseFigure();

    g.FillPath(&dark, &dpad);
    g.DrawPath(&partOutline, &dpad);

    // Directional highlights on D-Pad
    if (win::hostPressed(input.current, 200)) g.FillEllipse(&highlight, circleRect(343.0f, 552.0f, 22.0f));
    if (win::hostPressed(input.current, 201)) g.FillEllipse(&highlight, circleRect(343.0f, 668.5f, 22.0f));
    if (win::hostPressed(input.current, 202)) g.FillEllipse(&highlight, circleRect(285.0f, 610.24f, 22.0f));
    if (win::hostPressed(input.current, 203)) g.FillEllipse(&highlight, circleRect(401.0f, 610.24f, 22.0f));

    // 4. Left & Right Analog Sticks (id="leftcircle"/"leftstick", id="rightcircle"/"rightstick")
    const float stickCentersX[2] = {480.225f, 800.235f};
    const float stickCenterY = 765.40f;
    for (int i = 0; i < 2; ++i) {
        const float cx = stickCentersX[i];
        g.FillEllipse(&wellFill, circleRect(cx, stickCenterY, 64.0f));
        g.DrawEllipse(&wellOutline, circleRect(cx, stickCenterY, 64.0f));

        const auto& axes = input.current.axes;
        const float normX = std::clamp(axes[i * 2] / 32768.0f, -1.0f, 1.0f);
        const float normY = std::clamp(axes[i * 2 + 1] / 32768.0f, -1.0f, 1.0f);
        const float sx = cx + normX * 22.0f;
        const float sy = stickCenterY + normY * 22.0f;
        const bool stickActive = win::hostPressed(input.current, 8 + i) ||
                                 std::abs(axes[i * 2]) > 14000 || std::abs(axes[i * 2 + 1]) > 14000;

        g.FillEllipse(stickActive ? &highlight : &dark, circleRect(sx, sy, 48.0f));
        g.DrawEllipse(&partOutline, circleRect(sx, sy, 48.0f));
        g.FillEllipse(&darkSoft, circleRect(sx, sy, 31.0f));
        g.DrawEllipse(&wellOutline, circleRect(sx, sy, 31.0f));
    }

    // 5. Center buttons (optionsbutton, menubutton, assistantbutton, capturebutton, stadiabutton)
    const bool selectPressed = win::hostPressed(input.current, 6) || win::hostPressed(input.current, 10);
    const bool startPressed = win::hostPressed(input.current, 7) || win::hostPressed(input.current, 11);

    GraphicsPath optionsPill, menuPill;
    addPill(optionsPill, 518.0f, 532.58f, 34.0f, 20.0f);
    addPill(menuPill, 761.88f, 532.58f, 34.0f, 20.0f);
    g.FillPath(selectPressed ? &highlight : &dark, &optionsPill);
    g.DrawPath(&partOutline, &optionsPill);
    g.FillPath(startPressed ? &highlight : &dark, &menuPill);
    g.DrawPath(&partOutline, &menuPill);

    // 3 dots inside optionsbutton (506, 518, 530 at y=532.4)
    for (float dx : {506.0f, 518.0f, 530.0f}) {
        g.FillEllipse(&iconLight, circleRect(dx, 532.4f, 4.0f));
    }
    // 3 horizontal bars inside menubutton
    for (float by : {524.2f, 531.4f, 538.6f}) {
        g.FillRectangle(&iconLight, RectF(px(749.25f), py(by), pr(25.5f), pr(2.6f)));
    }

    // Assistant (564, 611.43, r=24) & Capture (715.37, 611.43, r=24) & Stadia Home (640, 765.4, r=36)
    g.FillEllipse(win::hostPressed(input.current, 12) ? &highlight : &darkSoft, circleRect(564.0f, 611.43f, 24.0f));
    g.DrawEllipse(&partOutline, circleRect(564.0f, 611.43f, 24.0f));
    g.FillEllipse(&iconLight, circleRect(558.2f, 609.5f, 6.2f));
    g.FillEllipse(&iconLight, circleRect(569.5f, 608.2f, 3.2f));
    g.FillEllipse(&iconLight, circleRect(569.5f, 616.5f, 3.8f));
    g.FillEllipse(&iconLight, circleRect(574.5f, 605.8f, 1.8f));

    g.FillEllipse(win::hostPressed(input.current, 13) ? &highlight : &darkSoft, circleRect(715.37f, 611.43f, 24.0f));
    g.DrawEllipse(&partOutline, circleRect(715.37f, 611.43f, 24.0f));
    Pen iconPen(Color(235, 238, 244, 250), 1.4f);
    g.DrawRectangle(&iconPen, px(706.5f), py(603.5f), pr(17.7f), pr(15.8f));

    g.FillEllipse(&dark, circleRect(640.0f, 765.40f, 36.0f));
    g.DrawEllipse(&partOutline, circleRect(640.0f, 765.40f, 36.0f));
    g.DrawEllipse(&iconPen, circleRect(640.0f, 765.40f, 16.0f));

    // 6. Face Buttons (Ybutton, Xbutton, Bbutton, Abutton — r=34 in Google_Stadia.svg)
    FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, 11.0f, FontStyleBold, UnitPixel);
    Gdiplus::Font smallFont(&family, 10.5f, FontStyleBold, UnitPixel);
    StringFormat centered;
    centered.SetAlignment(StringAlignmentCenter);
    centered.SetLineAlignment(StringAlignmentCenter);

    auto drawFaceButton = [&](float cx, float cy, const wchar_t* label, int token) {
        const RectF rc = circleRect(cx, cy, 34.0f);
        g.FillEllipse(win::hostPressed(input.current, token) ? &highlight : &dark, rc);
        g.DrawEllipse(&partOutline, rc);
        g.DrawString(label, -1, &font, rc, &centered, &iconLight);
    };

    const bool psSymbols = (settings.controllerPreset == 1);
    drawFaceButton(946.75f, 541.40f, psSymbols ? L"△" : L"Y", 3);
    drawFaceButton(879.00f, 609.32f, psSymbols ? L"□" : L"X", 2);
    drawFaceButton(1015.00f, 609.32f, psSymbols ? L"○" : L"B", 1);
    drawFaceButton(946.75f, 677.40f, psSymbols ? L"✕" : L"A", 0);

    SolidBrush labelBrush(Color(255, 42, 54, 70));
    g.DrawString(L"L / L2", -1, &smallFont, RectF(px(80.0f), py(410.0f), pr(175.0f), 18.0f), &centered, lPressed ? &highlight : &labelBrush);
    g.DrawString(L"R / R2", -1, &smallFont, RectF(px(1025.0f), py(410.0f), pr(175.0f), 18.0f), &centered, rPressed ? &highlight : &labelBrush);

    const wchar_t* name = (info && info->stadia)
        ? L"Google Stadia Controller"
        : (info ? (info->key == L"keyboard" ? tr().keyboardOnlyItem : info->name.c_str()) : L"Controller");
    g.DrawString(name, -1, &font, RectF(210.0f, 103.0f, 620.0f, 20.0f), &centered, &labelBrush);
    g.DrawString(input.connected ? tr().controllerConnectedStatus : tr().controllerDisconnectedStatus,
                 -1, &smallFont, RectF(210.0f, 293.0f, 620.0f, 19.0f), &centered, &labelBrush);
}

static LRESULT CALLBACK settingsProc(HWND h, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_COMMAND) {
        const int id = LOWORD(w), event = HIWORD(w);
        if (id == 900 && event == LBN_SELCHANGE) {
            settingsPage = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(l), LB_GETCURSEL, 0, 0));
            rebuildSettings();
            return 0;
        }
        if (id == 901 && event == BN_CLICKED) { DestroyWindow(h); return 0; }
        if (id == 910 && event == BN_CLICKED) { chooseRomFolder(); rebuildSettings(); return 0; }
        if (id == 911 && event == BN_CLICKED) { browseRom(h, false); rebuildSettings(); return 0; }
        if (id == 912 && event == BN_CLICKED) {
            if (settings.lastRom.empty()) browseRom(h, true);
            else launchRom(settings.lastRom);
            rebuildSettings();
            return 0;
        }
        if (id == 913 && event == BN_CLICKED) { stop(); rebuildSettings(); return 0; }
        if (id == 914 && event == BN_CLICKED) {
            settings.keepNvram = SendMessageW(reinterpret_cast<HWND>(l), BM_GETCHECK, 0, 0) == BST_CHECKED;
            saveSettings();
            return 0;
        }
        if (event == CBN_SELCHANGE) {
            const auto selection = SendMessageW(reinterpret_cast<HWND>(l), CB_GETCURSEL, 0, 0);
            const auto count = SendMessageW(reinterpret_cast<HWND>(l), CB_GETCOUNT, 0, 0);
            if (selection < 0 || selection >= count) return 0;
            const auto value = static_cast<unsigned>(selection);
            if (id == 915 && value < 7) {
                settings.language = value;
                saveSettings();
                refreshMainMenuBar();
                if (!process) setIdleTitle();
                if (window) InvalidateRect(window, nullptr, TRUE);
                SetWindowTextW(h, tr().settingsWindowTitle);
                rebuildSettings();
                return 0;
            }
            if (id == 916 && value < 10) {
                settings.cartLanguage = value;
                saveSettings();
                refreshMainMenuBar();
                return 0;
            }
            if (id == 960 && value < input.controllers.size()) {
                input.select(input.controllers[value].key);
                settings.savedController = input.key();
                settings.loadControllerFor(input.key());
                saveSettings();
                rebuildSettings();
                return 0;
            }
            if (id == 962) {
                settings.controllerPreset = value;
                settings.padMap = win::standardMap;
                if (value == 1) {
                    // PlayStation face button positions
                    settings.padMap[4] = 1;
                    settings.padMap[5] = 2;
                    settings.padMap[6] = 0;
                    settings.padMap[7] = 3;
                } else if (value == 2) {
                    // Alternative layout
                    settings.padMap[4] = 0;
                    settings.padMap[5] = 1;
                    settings.padMap[6] = 2;
                    settings.padMap[7] = 3;
                }
                saveSettings();
                rebuildSettings();
                return 0;
            }
            unsigned* setting = nullptr;
            switch (id) {
            case 972: setting = &settings.videoAspect; break;
            case 975: setting = &settings.windowMode; break;
            case 976: setting = &settings.scaleIndex; break;
            case 977: setting = &settings.displayFormat; break;
            case 978: setting = &settings.timingMode; break;
            }
            if (!setting || *setting == value) return 0;
            *setting = value;
            saveSettings();
            if (window) InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        // Focus, dropdown-open and other control notifications are not edits.
        if (event != BN_CLICKED) return 0;
        if (id == 961) {
            const auto key = input.key();
            input.refresh();
            input.select(key);
            rebuildSettings();
            return 0;
        }
        if (id >= 980 && id < 992) {
            learnAction = id - 980;
            learnKeyboard = false;
            previousPad = input.poll();
            SetWindowTextW(learningLabel, tr().pressControllerPrompt);
            SetFocus(h);
            return 0;
        }
        if (id >= 1000 && id < 1012) {
            learnAction = id - 1000;
            learnKeyboard = true;
            SetWindowTextW(learningLabel, tr().pressKeyboardPrompt);
            SetFocus(h);
            return 0;
        }
        bool* setting = nullptr;
        switch (id) {
        case 970: setting = &settings.soundEnabled; break;
        case 973: setting = &settings.integerScale; break;
        case 974: setting = &settings.smoothVideo; break;
        case 979: setting = &settings.fpsLog; break;
        case 965: setting = &settings.debugLog; break;
        }
        if (!setting) return 0;
        const bool checked = SendMessageW(reinterpret_cast<HWND>(l), BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (*setting == checked) return 0;
        *setting = checked;
        saveSettings();
        if (window) InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_HSCROLL) {
        HWND slider = reinterpret_cast<HWND>(l);
        if (!slider || GetParent(slider) != h || GetDlgCtrlID(slider) != 971) return 0;
        const auto volume = static_cast<unsigned>(SendMessageW(slider, TBM_GETPOS, 0, 0));
        if (volume > 100 || settings.soundVolume == volume) return 0;
        settings.soundVolume = volume;
        saveSettings();
        return 0;
    }
    if (message == WM_KEYDOWN && learnAction >= 0) {
        if (w == VK_ESCAPE) {
            rebuildSettings();
            return 0;
        }
        if (learnKeyboard) {
            settings.keyboardKeys[learnAction] = static_cast<unsigned>(w);
            saveSettings();
            rebuildSettings();
        }
        return 0;
    }
    if (message == WM_TIMER) {
        const auto now = input.poll();
        if (learnAction >= 0 && !learnKeyboard && GetForegroundWindow() == h) {
            const int token = win::firstNewControl(now, previousPad);
            if (token >= 0) {
                settings.padMap[learnAction] = token;
                saveSettings();
                rebuildSettings();
            }
        }
        previousPad = now;
        if (settingsPage == 1) {
            RECT r{210, 110, 835, 314};
            InvalidateRect(h, &r, FALSE);
        }
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT client{};
        GetClientRect(h, &client);
        HDC buffer = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
        if (buffer && bitmap) {
            HGDIOBJ previous = SelectObject(buffer, bitmap);
            FillRect(buffer, &client, GetSysColorBrush(COLOR_BTNFACE));
            drawController(buffer);
            BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top,
                   ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
                   buffer, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
            SelectObject(buffer, previous);
        } else {
            FillRect(dc, &ps.rcPaint, GetSysColorBrush(COLOR_BTNFACE));
            drawController(dc);
        }
        if (bitmap) DeleteObject(bitmap);
        if (buffer) DeleteDC(buffer);
        EndPaint(h, &ps);
        return 0;
    }
    if (message == WM_DESTROY) {
        KillTimer(h, 1);
        settingsWindow = nullptr;
        settingsChildren.clear();
        learnAction = -1;
        return 0;
    }
    return DefWindowProcW(h, message, w, l);
}

static void showSettings(int page = 0) {
    settingsPage = page;
    if (settingsWindow) {
        SetWindowTextW(settingsWindow, tr().settingsWindowTitle);
        rebuildSettings();
        SetForegroundWindow(settingsWindow);
        return;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = settingsProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"OceanBlastSettings";
    RegisterClassW(&wc);

    RECT rect{0, 0, 850, 595};
    AdjustWindowRect(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    settingsWindow = CreateWindowW(
        wc.lpszClassName, tr().settingsWindowTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        window, nullptr, wc.hInstance, nullptr);
    rebuildSettings();
    SetTimer(settingsWindow, 1, 30, nullptr);
    ShowWindow(settingsWindow, SW_SHOW);
}

static LRESULT CALLBACK procedure(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (id == OpenRom || id == 10) {
            browseRom(w, id == OpenRom);
        } else if (id == 11) {
            pullCompatibilityControls();
            saveSettings();
            if (!settings.lastRom.empty()) launchRom(settings.lastRom);
            else browseRom(w, true);
        } else if (id == RestartRom) {
            if (!activeRomPath.empty()) launchRom(activeRomPath);
            else if (!settings.lastRom.empty()) launchRom(settings.lastRom);
        } else if (id == ManageMp3Storage) {
            showMusicManager();
        } else if (id == StopRom || id == 12) {
            stop();
        } else if (id == ToggleFullscreenCmd) {
            toggleMainFullscreen();
        } else if (id == EscapeFromChildCmd) {
            if (mainFullscreen) toggleMainFullscreen();
            else stop();
        } else if (id == MediaPlayPause) {
            sendMediaPulseToChild(1u << 13, 180); // BTN_PLAY -> GPG9 (KEY_P)
        } else if (id == MediaStop) {
            sendMediaPulseToChild(1u << 14, 180); // BTN_STOP -> GPG10 (KEY_S)
        } else if (id == MediaPrev) {
            sendMediaPulseToChild(1u << 11, 180); // BTN_REWIND -> GPG0 (KEY_B)
        } else if (id == MediaNext) {
            sendMediaPulseToChild(1u << 12, 180); // BTN_FORWARD -> GPG13 (KEY_N)
        } else if (id == MediaSeekBack) {
            sendMediaPulseToChild(1u << 11, 1500); // Hold BTN_REWIND for 1.5s seek
        } else if (id == MediaSeekFwd) {
            sendMediaPulseToChild(1u << 12, 1500); // Hold BTN_FORWARD for 1.5s seek
        } else if (id >= CartLangBase && id < CartLangBase + 10) {
            settings.cartLanguage = static_cast<unsigned>(id - CartLangBase);
            saveSettings();
            refreshMainMenuBar();
            if (settingsWindow && settingsPage == 0) rebuildSettings();
            if (process && !activeRomPath.empty()) {
                launchRom(activeRomPath);
            }
        } else if (id == 13) {
            showSettings(1);
        } else if (id == ExitApp || id == 14) {
            PostMessageW(w, WM_CLOSE, 0, 0);
        } else if (id >= SettingsBase && id < SettingsBase + 6) {
            showSettings(id - SettingsBase);
        }
        return 0;
    }
    case WM_DEVICECHANGE: {
        const auto selected = input.key();
        input.refresh();
        input.select(selected);
        if (settingsWindow) rebuildSettings();
        notifyRunningGameSettingsChanged();
        return 0;
    }
    case WM_DROPFILES: {
        wchar_t dropped[MAX_PATH]{};
        if (DragQueryFileW(reinterpret_cast<HDROP>(wp), 0, dropped, MAX_PATH)) {
            DragFinish(reinterpret_cast<HDROP>(wp));
            launchRom(dropped);
        } else {
            DragFinish(reinterpret_cast<HDROP>(wp));
        }
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if ((GetKeyState(VK_CONTROL) & 0x8000) && !(lp & 0x40000000)) {
            if (wp == 'O') { browseRom(w, true); return 0; }
            if (wp == 'R') {
                if (!activeRomPath.empty()) launchRom(activeRomPath);
                else if (!settings.lastRom.empty()) launchRom(settings.lastRom);
                return 0;
            }
        }
        if ((wp == VK_F11 || (wp == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))) && !(lp & 0x40000000)) {
            toggleMainFullscreen();
            return 0;
        }
        if (wp == VK_ESCAPE && !(lp & 0x40000000) && mainFullscreen) {
            toggleMainFullscreen();
            return 0;
        }
        if (HWND child = findEmbeddedDisplay()) {
            PostMessageA(child, static_cast<UINT>(msg), wp, lp);
            return 0;
        }
        break;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        if (HWND child = findEmbeddedDisplay()) {
            PostMessageA(child, static_cast<UINT>(msg), wp, lp);
            return 0;
        }
        break;
    }
    case WM_SIZE: {
        if (HWND child = findEmbeddedDisplay()) {
            RECT rc{};
            GetClientRect(w, &rc);
            SetWindowPos(child, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        if (!findEmbeddedDisplay()) {
            startScreen(dc);
        } else {
            RECT r{};
            GetClientRect(w, &r);
            FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        }
        EndPaint(w, &ps);
        return 0;
    }
    case WM_TIMER:
        if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(process, &code);
            CloseHandle(process);
            process = nullptr;
            if (startButton) EnableWindow(startButton, TRUE);
            if (stopButton) EnableWindow(stopButton, FALSE);
            if (statusBox) {
                SetWindowTextW(statusBox, code
                    ? L"Emulator exited with error. Check session.log for details."
                    : L"Emulator stopped. Ready to launch another ROM.");
            }
            setIdleTitle();
            InvalidateRect(w, nullptr, TRUE);
            if (settingsWindow && settingsPage == 0) rebuildSettings();
        }
        return 0;
    case WM_CLOSE:
        stop();
        if (settingsWindow) DestroyWindow(settingsWindow);
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

static int run() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES};
    InitCommonControlsEx(&common);
    Gdiplus::GdiplusStartupInput gdipInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdipInput, nullptr);

    wchar_t module[32768] = {};
    GetModuleFileNameW(nullptr, module, 32768);
    executable = module;
    folder = executable.substr(0, executable.find_last_of(L"\\/")) + L"\\sessions";
    CreateDirectoryW(folder.c_str(), nullptr);

    settings.load();
    logo = win::loadImageAsset(101, L"oceanblast-logo.png", &logoStream);
    symbol = win::loadImageAsset(102, L"oceanblast-symbol.png", &symbolStream);

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls = {};
    cls.lpfnWndProc = procedure;
    cls.hInstance = instance;
    cls.lpszClassName = L"OceanBlastLauncher";
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.hbrBackground = nullptr;
    RegisterClassW(&cls);

    RECT r{0, 0, 1024, 768};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, TRUE);
    window = CreateWindowW(
        cls.lpszClassName,
        tr().idleWindowTitle,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, instance, nullptr);
    if (!window) return 1;

    DragAcceptFiles(window, TRUE);
    mainMenu = makeMenu();
    SetMenu(window, mainMenu);

    if (symbol) static_cast<Gdiplus::Bitmap*>(symbol.get())->GetHICON(&appIcon);
    if (appIcon) {
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIcon));
    }

    input.init(window);
    input.refresh();
    input.autoSelectPreferred(settings.savedController);
    settings.loadControllerFor(input.key());

    // Hidden compatibility controls for automated test inspection.
    auto hiddenControl = [&](const wchar_t* type, const wchar_t* title, DWORD style, int id) -> HWND {
        return CreateWindowW(type, title, WS_CHILD | style, 0, 0, 10, 10, window,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    };
    pathBox = hiddenControl(L"EDIT", L"", ES_AUTOHSCROLL, 0);
    scaleBox = hiddenControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST, 0);
    for (auto text : {L"2× (480 × 320)", L"3× (720 × 480)", L"4× (960 × 640)"})
        SendMessageW(scaleBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    soundBox = hiddenControl(L"BUTTON", L"Enable Audio", BS_AUTOCHECKBOX, 0);
    profileBox = hiddenControl(L"BUTTON", L"FPS Log", BS_AUTOCHECKBOX, 0);
    debugBox = hiddenControl(L"BUTTON", L"Debug Log", BS_AUTOCHECKBOX, 0);
    formatBox = hiddenControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST, 0);
    for (auto text : {L"Automatic (default)", L"RGB444, 480-byte rows (diagnostic)", L"RGB444 (diagnostic)", L"RGB565 (diagnostic)", L"LCD registers (diagnostic)"})
        SendMessageW(formatBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    nvramBox = hiddenControl(L"BUTTON", L"Keep Device Settings", BS_AUTOCHECKBOX, 0);
    timingBox = hiddenControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST, 0);
    for (auto text : {L"Automatic ROM settings (default)", L"Standard: 1x", L"Automatic register clocks (experimental)", L"More CPU work: 2× (experimental)", L"More CPU work: 4× (experimental)", L"More CPU work: 8×"})
        SendMessageW(timingBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    windowModeBox = hiddenControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST, 0);
    for (auto text : {L"Console Skin", L"Fullscreen", L"Plain Window (default)"})
        SendMessageW(windowModeBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    startButton = hiddenControl(L"BUTTON", L"Start Game", BS_DEFPUSHBUTTON, 11);
    stopButton = hiddenControl(L"BUTTON", L"Stop Game", 0, 12);
    EnableWindow(stopButton, FALSE);
    statusBox = hiddenControl(L"STATIC", L"Select a ROM file and click Start Game to begin emulation.", 0, 0);
    syncCompatibilityControls();

    SetTimer(window, 1, 200, nullptr);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (settingsWindow && learnAction < 0 && IsDialogMessageW(settingsWindow, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    input.shutdown();
    logo.reset();
    symbol.reset();
    if (logoStream) { logoStream->Release(); logoStream = nullptr; }
    if (symbolStream) { symbolStream->Release(); symbolStream = nullptr; }
    if (appIcon) { DestroyIcon(appIcon); appIcon = nullptr; }
    Gdiplus::GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    return 0;
}

} // namespace launcher
} // namespace oceanblast
#endif
