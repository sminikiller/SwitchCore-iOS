# Esecuzione nativa ARM64

## Obiettivo

Portare titoli 3D pesanti, estratti dalla propria console, su iPhone 14 Pro (A16, 6 GB di RAM) con iOS 26 e firma tramite account developer. Le build iOS nascono nella CI GitHub, perché lo sviluppo avviene da Windows. L'interprete NeXo resta il percorso senza JIT. La memoria del dispositivo è il limite principale per i titoli più grandi e va misurata prima di qualsiasi stima di compatibilità.

Switch e iPhone usano entrambi AArch64. Il codice del guest può quindi girare direttamente sulla CPU dell'iPhone, a tre condizioni:

- la memoria del guest occupa gli stessi indirizzi nel processo host;
- le istruzioni di sistema (SVC, MRS/MSR su TPIDRRO_EL0 e TPIDR_EL0, contatori) diventano salti verso il kernel HLE;
- iOS concede pagine eseguibili.

Il backend NCE di yuzu segue lo stesso schema su Android ARM64. [Native code execution in yuzu](https://yuzu-mirror.github.io/entry/yuzu-progress-report-nov-2023)

## JIT su iOS 26

Le guide di StikDebug indicano che sui dispositivi con TXM (A15 e successivi) non basta collegare un debugger. Lo script di StikDebug deve preparare le regioni eseguibili seguendo un protocollo. [StikJIT INTEGRATION.md](https://github.com/StikDebug/StikJIT/blob/main/INTEGRATION.md)

- L'app firmata per sviluppo ha `get-task-allow` e apre `stikdebug://enable-jit` con `bundle-id`, `pid` e `script-name=universal.js`.
- Quando il processo risulta `CS_DEBUGGED`, l'app chiama `mov x16, #1` seguito da `brk #0xf00d`, con x0 = indirizzo e x1 = lunghezza. Lo script rende eseguibile la regione a pagine da 16 KiB.
- L'app crea poi l'alias scrivibile della regione e chiude con `mov x16, #0` seguito da `brk #0xf00d`, che stacca il debugger.
- Dopo il distacco non si preparano altre regioni. Il pool eseguibile per il codice del gioco, i moduli caricati durante l'esecuzione e i trampolini va riservato tutto all'avvio.
- Lo script trasmette indirizzi a 36 bit, quindi il pool resta sotto 64 GiB.
- Una segnalazione del 5 ottobre 2026 riporta il JIT non funzionante su iOS 27.0. Un aggiornamento di iOS può quindi disattivare la modalità nativa.

Il protocollo viene implementato nel progetto. Lo script e la libreria StikJIT (MPL-2.0) restano esterni.

## Verifiche prima del backend

| Verifica | Motivo | Ambiente |
| --- | --- | --- |
| x18 dopo i context switch | Darwin riserva x18, mentre il codice compilato per Switch lo usa come registro ordinario. Le fonti disponibili sono discussioni informali e non concordano. Se il kernel lo altera, ogni uso di x18 va riscritto in un trampolino. | Runner macOS arm64 della CI, iPhone |
| TPIDR_EL0 dopo i context switch | Il codice compilato con l'SDK Nintendo usa TPIDR_EL0 come thread pointer. | Runner macOS arm64, iPhone |
| Riserva di indirizzi fissi tra 4 e 64 GiB | La memoria guest deve avere gli stessi indirizzi nell'host. I primi 4 GiB appartengono a `__PAGEZERO`. | iPhone con entitlement `extended-virtual-addressing` |
| Memoria disponibile | Va misurata con l'entitlement `increased-memory-limit`. | iPhone |
| Pool eseguibile con alias scrivibile | Il codice patchato si scrive dall'alias e si esegue dal pool dopo l'invalidazione della cache istruzioni. | Runner macOS arm64, iPhone con StikDebug |

## Architettura

1. **Spazio di indirizzi.** Layout guest a 36 bit sopra i primi 4 GiB e sotto 64 GiB. Il kernel HLE comunica queste regioni con `svcGetInfo`. Il codice NSO/NRO risiede nel pool eseguibile; heap, stack e regione alias sono mappati in lettura e scrittura agli stessi indirizzi.
2. **Caricatore.** I moduli vengono copiati nell'alias scrivibile e le istruzioni di sistema sostituite con salti verso trampolini nel pool. Segue l'invalidazione della cache istruzioni.
3. **Thread.** Ogni thread guest ha un thread host. I trampolini salvano registri e TPIDR nel contesto del thread. Lo scheduler HLE interrompe i thread con segnali.
4. **Eccezioni.** Gli accessi non consentiti arrivano come eccezioni Mach o SIGSEGV e seguono gli stessi controlli di permesso dell'interprete.
5. **Ripiego.** Senza JIT la sessione usa l'interprete.

## Tappe

| Tappa | Risultato verificabile |
| --- | --- |
| M0 Sonde | Test sul runner macOS arm64 e pannello diagnostico nell'app: x18 e TPIDR_EL0 dopo i context switch, riserva di indirizzi fissi, pool eseguibile con funzione chiamata, memoria disponibile, stato `CS_DEBUGGED` e protocollo StikDebug. Report copiabile dall'iPhone. |
| M1 CPU nativa su macOS | Programmi di test e Rally eseguiti in nativo sul runner macOS arm64 con gli stessi criteri del probe. Confronto delle istruzioni al secondo con l'interprete. |
| M2 iPhone | Collegamento con StikDebug, entitlement e ripiego automatico. Rally nativo su iPhone 14 Pro, con FPS e memoria misurati. |
| M3 GPU | Vertex shader tradotto funzionante, cache e compilazione asincrona degli shader, texture ASTC native su A16. |
| M4 Titoli dai propri dump | Caricamento di NSP e XCI con le chiavi della propria console, esposto nell'app. Livello registrato per ciascun titolo sulla [scala di compatibilità](test.md#scala-di-compatibilità). |

File di gioco, chiavi e firmware provengono solo dalla console dell'utente e non entrano nel repository né nella CI.
