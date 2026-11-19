# SwitchCore iOS

SwitchCore esegue homebrew Nintendo Switch 1 in locale su un interprete ARM64. La libreria SwiftUI importa file NRO, legge titolo, autore e copertina dai metadati, conserva preferiti e profili per titolo. Rally, inclusa con i sorgenti, usa libnx per disegnare e leggere il controller. Si gioca con ↑ e ↓. Il pulsante A cambia il colore del campo e + chiude la partita.

## Motore e stato

Il core C++20 integra CPU scalare e NEON, RAM paginata, kernel e IPC Horizon, servizi HID/VI/FS/audio e comandi GPU Maxwell. Su iOS la pipeline accelerata traduce gli shader supportati in SPIR-V e usa MoltenVK per eseguirli in Metal. I draw non gestiti possono tornare al rasterizzatore software. La presentazione riceve i pixel del guest. L'app supporta touch, GameController, PCM stereo, limite FPS e sospensione sotto pressione di memoria o temperatura.

Da **Importa** si può scegliere una homebrew NRO oppure una cartella di applicazione Switch 1 estratta. Per la cartella selezionare la radice con `exefs/main`, gli eventuali moduli e `romfs.bin` o `romfs/`. Si può anche selezionare direttamente una cartella contenente `main`. L'app copia i file nel proprio archivio e riconosce le importazioni duplicate. Le cartelle di test contengono codice ARM64 del progetto. Per Mario Kart 8 Deluxe avvio, gameplay e FPS su iPhone 14 Pro sono da verificare. NSP e XCI rimangono formati sperimentali interni al core.

Un menu homebrew come nx-hbmenu può avviare le NRO presenti nella SD virtuale del titolo. Alla chiusura della NRO avviata si torna al menu. Il backend di ricerca Switch 2 restituisce un errore esplicito all'avvio. [Piano per l'esecuzione nativa su iPhone](docs/nativo.md)

## Compilazione e prove

Su Windows eseguire `cmake -S . -B build`, `cmake --build build --config Release` e `ctest --test-dir build -C Release --output-on-failure`. L'app si compila tramite il workflow iOS su macOS. L'IPA per dispositivi arm64 richiede firma valida per l'installazione. La procedura include test di input, cattura dei frame, confronto con nx-hbmenu e una prova di 6.000 frame. [Guida ai test](docs/test.md)

Licenza GPLv3. I sorgenti e le fixture derivati da NeXo 2 conservano la relativa licenza. libnx usa ISC, le macro di test deko3d usano zlib e MoltenVK usa Apache 2.0. [Analisi delle basi disponibili](docs/ricerca.md) · [Attribuzioni](third_party/nexo/NOTICE.md)

