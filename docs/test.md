# Riprodurre le verifiche

Il frontend e il programma switchcore-probe usano lo stesso bridge C, lo stesso caricatore NRO e lo stesso interprete. La cartella passata al probe ospita SD virtuale e salvataggi di un solo titolo.

## Windows

Da PowerShell con CMake 3.25+ e un compilatore C++20 disponibili:

```powershell
cmake -S . -B build
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
build/Release/switchcore-probe.exe apps/ios/Resources/rally.nro build/rally-title rally build/rally.ppm
build/Release/switchcore-probe.exe apps/ios/Resources/rally.nro build/soak-title soak
python tools/verify-independent.py build/Release/switchcore-probe.exe build/independent
```

Con un generatore a configurazione singola usare `-DCMAKE_BUILD_TYPE=Release` e l'eseguibile in `build/`. Ogni esecuzione del probe stampa istruzioni, byte delle pagine guest, frame, dimensioni, PC e tempo trascorso. I file PPM sono catture del framebuffer restituito dal guest.

| Prova | Criterio controllato |
| --- | --- |
| Suite del core | CPU scalare e NEON, confronto differenziale, mapping, IPC, sincronizzazione, audio, loader e GPU. Vulkan desktop escluso dalla configurazione portatile. |
| Contratto del bridge | Esecuzione a budget, protezione R/W/X e confini di pagina, rifiuto Switch 2, riavvio dopo errore, NRO richieste dal guest confinate nella SD del titolo. |
| Rally | NRO compilata con libnx. Il guest presenta un campo 320 × 180 attraverso VI. Premere A e rilasciarlo deve cambiare e ripristinare il pixel di fondo. Il test intercetta campioni PCM non nulli da audout. Il tasto + deve salvare il record nella SD virtuale del titolo e terminare il processo. |
| Rally prolungato | Almeno 6.000 frame con lo stesso processo, seguito da uscita tramite HID. Il probe gira senza limitatore di velocità. |
| nx-hbmenu 3.6.1 | Download dalla release Switchbrew con verifica SHA-256. Almeno un framebuffer 1280 × 720 con contenuto non uniforme. Con Rally nella SD virtuale, A avvia Rally tramite `envSetNextLoad`. Rally riceve A, produce PCM e salva il record. Con + si torna a hbmenu, che termina con un secondo +. |
| Test iOS | Xcode crea il binario iphoneos arm64. XCTest avvia il programma di triangoli con il fragment shader Maxwell tradotto in Metal e i vertici calcolati dalla CPU, poi controlla draw accelerati e pixel. Il programma legge il render target con la CPU, quindi il test imposta `NEXO2_VK_SYNC=1` e ogni invio alla GPU torna nella memoria del guest. Un secondo caso traduce anche il vertex shader, che legge attributi e viewport da storage buffer, e richiede gli stessi pixel. Sul simulatore MoltenVK lega i descrittori senza argument buffer Metal, perché sulle macchine virtuali CI il servizio SimMetalHost termina mentre li scrive. Il workflow esporta i crash report del test. XCUITest avvia Rally dalla libreria, attende un frame, verifica pausa, ripresa e uscita. |

## iOS

Su macOS con Xcode, Metal Toolchain e XcodeGen installati:

```sh
bash tools/prepare-metal.sh
python3 tools/verify-independent.py --fetch-only build-fixtures
xcodegen generate
xcodebuild -scheme SwitchCore -configuration Release -destination 'generic/platform=iOS' CODE_SIGNING_ALLOWED=NO build
```

GitHub Actions conserva l'IPA senza firma, il bundle XCTest e i log. L'installazione su un iPhone fisico richiede la firma con un team Apple autorizzato. Per riprodurre una misura su A16 selezionare il proprio team in Xcode, installare, aprire Rally, premere A, muovere la racchetta con ↑/↓ e chiudere con +. Registrare build, versione iOS, profilo, durata, FPS guest e memoria app. Una misura del simulatore o del probe Windows non vale come misura A16.

## Presentazione RGBA8 a 720p

`scanout-bench` misura `Display::PresentAt` su un framebuffer da 1280 × 720 già presente nella memoria guest. Scarta 10 fotogrammi di riscaldamento e registra i successivi 200 per layout. Su Windows x64, Ryzen 7 5800X, build Release MSVC, confronto con il core del commit `0ab12f5` sulla stessa macchina:

| Layout | Mediana prima | Mediana dopo | 95º percentile prima | 95º percentile dopo |
| --- | ---: | ---: | ---: | ---: |
| Lineare | 2,080 ms | 0,062 ms | 2,128 ms | 0,077 ms |
| Tegra block-linear | 3,616 ms | 0,301 ms | 4,002 ms | 0,316 ms |

I checksum dei pixel coincidono per ciascun layout. La misura riguarda la preparazione del framebuffer sulla CPU del PC. La durata di un frame completo di gioco comprende anche esecuzione ARM64, servizi, draw GPU e presentazione sul dispositivo. Per ripetere la misura usare `build/Release/scanout-bench.exe` dopo la compilazione. Per avviare un'applicazione estratta con il bridge usare `switchcore-probe CARTELLA_GIOCO CARTELLA_SALVATAGGI frame`.

## Giochi di corse homebrew

Per riprodurre una sequenza utilizzare `switchcore-replay PROGRAMMA CARTELLA_TITOLO PIANO.txt CARTELLA_CATTURE [TIMEOUT_SECONDI]`. Ogni riga del piano contiene il numero di frame guest da attendere, la maschera decimale dei tasti HID e la posizione orizzontale dello stick sinistro. Lo strumento registra istruzioni, tempo host, RAM guest, PC e immagini PPM. I tempi includono il backend software desktop e possono variare con la macchina. Il test segnala separatamente un arresto del guest e un checkpoint scaduto. Il limite predefinito è di 120 secondi per checkpoint, estendibile per il primo caricamento di un titolo.

- **Need for Madness v1.2.3** distribuisce una NRO con i dati incorporati. Sul core Windows, il piano in `tests/integration/nfm-race.txt` parte da una SD vuota, invia A ai menu, seleziona auto e pista, poi aziona ZR e lo stick. L'esecuzione ha mostrato la griglia di partenza in 1280 × 720. Nell'app si può importare la NRO da File. Il gioco resta una fixture scaricata separatamente. [Release del port](https://github.com/HopeAero/nfm-switch-port/releases/tag/v1.2.3)
- **SuperTuxKart 1.5** distribuisce `switch/stk.nro` e `stk-data/` in un unico ZIP. Per l'app, estrarre lo ZIP in File, importare `stk.nro` e scegliere **Aggiungi cartella alla SD** nelle opzioni del titolo per copiare `stk-data`. Per il probe desktop estrarre lo ZIP nella sottocartella `sdmc` del titolo e avviare la NRO al suo interno. Su Windows il guest ha letto modelli e shader, visualizzato il caricamento e risposto ad A sulla prima schermata di configurazione. [Distribuzione ufficiale](https://supertuxkart.net/Download)

Sul Ryzen 7 5800X con interprete e rasterizzatore software, Need for Madness percorre il rettilineo e sterza. Negli ultimi 210 frame del piano (1188–1398), due esecuzioni consecutive con SD vuota hanno impiegato 42,73 s con il vecchio scanout e 45,11 s con quello nuovo, pari a 4,91 e 4,66 FPS. Il confronto usa lo stesso eseguibile di test ricompilato con le due versioni di `display.cpp`. Le quattro catture ai checkpoint coincidono byte per byte. Una singola coppia di prove non stabilisce una variazione stabile delle prestazioni complessive. Per riprodurre il percorso attivare la modalità software nel profilo iOS. L'XCTest legge lo stesso piano e allega il framebuffer finale.

## Scala di compatibilità

Registrare per ciascun binario il più alto livello osservato con le prove intermedie disponibili. I dieci livelli sono riconoscimento, caricamento, creazione del processo, esecuzione CPU, servizi HLE, primo frame, input, interazione, stabilità prolungata e misura sul dispositivo fisico. nx-hbmenu documenta il livello 8 con input e avvio di un'altra NRO dalla propria SD. L'esito di Rally non si estende ad altri eseguibili.

La homebrew inclusa viene ricostruita da `tests/homebrew/rally/main.c` nel container devkitA64 identificato dal digest nel workflow Homebrew. Il tarball ufficiale di MoltenVK è verificato contro SHA-256 prima della compilazione. La sorgente di NeXo 2 è fissata in `third_party/nexo/NOTICE.md`.

