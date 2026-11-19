# Esecuzione nativa ARM64

## Obiettivo

Portare titoli 3D pesanti, estratti dalla propria console, su iPhone 14 Pro (A16, 6 GB di RAM) con iOS 26 e firma tramite account developer. Le build iOS nascono nella CI GitHub, perché lo sviluppo avviene da Windows. L'interprete NeXo resta il percorso senza JIT. La memoria del dispositivo è il limite principale per i titoli più grandi e va misurata prima di qualsiasi stima di compatibilità.

Switch e iPhone usano entrambi AArch64, quindi la traduzione del codice guest è quasi uno a uno. Il backend NCE di yuzu su Android esegue il codice del gioco senza traduzione. Servono la memoria guest agli stessi indirizzi dell'host e i salti verso il kernel HLE al posto delle istruzioni di sistema. [Native code execution in yuzu](https://yuzu-mirror.github.io/entry/yuzu-progress-report-nov-2023) Sotto Darwin questo non basta, come mostrano i risultati M0 più avanti. In ogni caso iOS deve concedere pagine eseguibili.

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

## Risultati M0

Runner macOS 15 arm64 della CI, 19 novembre 2026, programma `native-probes`:

- Con 6 thread e 214 segnali, x18 e TPIDR_EL0 cambiano 531 volte su 600.000 controlli. Le due modifiche coincidono: Darwin le applica a ogni context switch. Il codice guest non può quindi usare questi registri direttamente.
- Si riservano a indirizzi fissi 51 GiB contigui tra 12 e 63 GiB. Tra 4 e 12 GiB restano solo frammenti, occupati dall'immagine del processo e dalla cache condivisa di dyld.
- Una funzione scritta tramite un alias `vm_remap` e chiamata dalla mappatura eseguibile restituisce il valore atteso. Lo stesso avviene con `MAP_JIT`.

Su iPhone i valori vanno misurati di nuovo. Se x18 e TPIDR_EL0 si comportano allo stesso modo, il codice del gioco non può girare senza modifiche.

## Architettura

1. **CPU.** Un ricompilatore ARM64→ARM64 traduce i blocchi guest e assegna x18 e TPIDR_EL0 a registri o memoria dell'host. NeXo 2 a monte integra già Dynarmic come alternativa a `Interpreter::Run()` (`jit_dynarmic.cpp`, oggi escluso dalla build). Dynarmic dispone di un backend ARM64 che yuzu usava su Android. Lo stato dei registri resta in `CPUState`, quindi kernel HLE e scheduler non cambiano.
2. **Memoria eseguibile.** La cache del codice tradotto occupa il pool preparato con StikDebug all'avvio e viene scritta tramite l'alias scrivibile, seguita dall'invalidazione della cache istruzioni. Il pool ha dimensione fissa, perché dopo il distacco del debugger non si aggiungono regioni.
3. **Memoria guest.** Si parte dalle tabelle di pagine attuali. Il ricompilatore genera accessi tramite tabella con controllo dei permessi. La mappatura diretta guest = host, nella fascia libera misurata dalle sonde, è un'ottimizzazione successiva.
4. **Ripiego.** Senza JIT la sessione usa l'interprete.

## Tappe

| Tappa | Risultato verificabile |
| --- | --- |
| M0 Sonde | Runner macOS arm64 completato (sezione precedente). Resta il pannello diagnostico nell'app: x18 e TPIDR_EL0 dopo i context switch, riserva di indirizzi fissi, pool eseguibile con funzione chiamata, memoria disponibile, stato `CS_DEBUGGED` e protocollo StikDebug. Report copiabile dall'iPhone. |
| M1 Ricompilatore su macOS | Dynarmic con backend ARM64 nella build di SwitchCore. Suite CPU, programmi di test e Rally eseguiti con il ricompilatore sul runner macOS arm64, con gli stessi criteri del probe. Confronto delle istruzioni al secondo con l'interprete. |
| M2 iPhone | Collegamento con StikDebug, pool eseguibile, entitlement e ripiego automatico. Rally con il ricompilatore su iPhone 14 Pro, con FPS e memoria misurati. |
| M3 GPU | Vertex shader tradotto funzionante, cache e compilazione asincrona degli shader, texture ASTC native su A16. |
| M4 Titoli dai propri dump | Caricamento di NSP e XCI con le chiavi della propria console, esposto nell'app. Livello registrato per ciascun titolo sulla [scala di compatibilità](test.md#scala-di-compatibilità). |

File di gioco, chiavi e firmware provengono solo dalla console dell'utente e non entrano nel repository né nella CI.
