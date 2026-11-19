# SwitchCore su iPhone

Questa guida porta la build della CI su un iPhone con iOS 26, partendo da un PC Windows e da un account Apple Developer a pagamento. Serve per la tappa M0 del [piano nativo](nativo.md): le sonde girano sul telefono e producono un rapporto di testo da inviare.

## L'IPA della CI

Il workflow iOS pubblica l'artefatto `ios-evidence` (GitHub → Actions → esecuzione del workflow iOS → Artifacts). Contiene due IPA:

- `SwitchCore.ipa` è firmata ad hoc dalla CI con gli entitlement di [`apps/ios/SwitchCore.entitlements`](../apps/ios/SwitchCore.entitlements), in formato XML e DER. Il passo «Sign the IPA ad hoc» controlla entrambi i blocchi della firma e stampa l'esito in `entitlements.txt`.
- `SwitchCore-unsigned.ipa` è la stessa app senza firma.

Nessuna delle due si installa così com'è. La firma con il proprio account sostituisce sempre quella della CI.

| Entitlement | A cosa serve | Nella firma finale arriva da |
| --- | --- | --- |
| `get-task-allow` | StikDebug si collega solo alle app che lo hanno | Profilo di tipo Development |
| `com.apple.developer.kernel.extended-virtual-addressing` | Indirizzi alti per la memoria guest, sonda [b] | Capability «Extended Virtual Address Space» dell'App ID |
| `com.apple.developer.kernel.increased-memory-limit` | Limite di memoria più alto, sonda [c] | Capability «Increased Memory Limit» dell'App ID |

Gli strumenti di firma si comportano in due modi. Alcuni copiano nella nuova firma gli entitlement del profilo di provisioning. Altri ripartono dagli entitlement già presenti nell'app, che trovano nella firma ad hoc di `SwitchCore.ipa`, e li accettano solo se il profilo li consente. In entrambi i casi servono un App ID con le due capability e un profilo Development. Il rapporto M0 mostra cosa è arrivato davvero sul telefono, quindi conferma il risultato qualunque strumento si usi.

## Account sviluppatore

Su [developer.apple.com](https://developer.apple.com/account/resources), sezione Certificates, Identifiers & Profiles:

1. **Identifiers.** Creare un App ID esplicito, ad esempio `io.github.sminikiller.SwitchCore` o un identificatore proprio. Se lo strumento di firma ne ha già creato uno, modificare quello.
2. **Capabilities** dello stesso App ID: attivare «Extended Virtual Address Space» e «Increased Memory Limit», poi salvare.
3. **Devices.** Registrare l'UDID dell'iPhone, se manca.
4. **Certificates.** Serve un certificato «Apple Development» con la sua chiave privata in un file `.p12`. Da Windows la richiesta si crea con l'OpenSSL incluso in Git per Windows, da PowerShell:

   ```powershell
   & "C:\Program Files\Git\usr\bin\openssl.exe" req -new -newkey rsa:2048 -nodes -keyout sviluppo.key -out sviluppo.csr -subj "/CN=SwitchCore/C=IT"
   ```

   Caricare `sviluppo.csr`, scaricare il certificato (`development.cer`) e unirlo alla chiave:

   ```powershell
   & "C:\Program Files\Git\usr\bin\openssl.exe" x509 -inform DER -in development.cer -out development.pem
   ```

   ```powershell
   & "C:\Program Files\Git\usr\bin\openssl.exe" pkcs12 -export -inkey sviluppo.key -in development.pem -out sviluppo.p12
   ```

   Se lo strumento di firma non legge il `.p12`, ripetere l'ultimo comando aggiungendo `-legacy`.

5. **Profiles.** Creare un profilo «iOS App Development» per l'App ID, il certificato e l'iPhone, poi scaricare il `.mobileprovision`. I profili Ad Hoc e App Store non hanno `get-task-allow` e StikDebug non potrebbe collegarsi. Dopo ogni modifica alle capability il profilo va rigenerato.

## Firma e installazione da Windows

- Con uno strumento che accetta certificato e profilo, ad esempio [zsign](https://github.com/zhlynn/zsign) (compilabile con Visual Studio 2022 o usabile in WSL con la build Linux), firmare `SwitchCore.ipa`. Se l'App ID ha un altro identificatore, aggiungere `-b` con quell'identificatore.

  ```powershell
  zsign -k sviluppo.p12 -p PASSWORD -m SwitchCore.mobileprovision -o SwitchCore-firmata.ipa SwitchCore.ipa
  ```

  L'IPA firmata si installa con uno strumento che installa app già firmate, ad esempio iMazing o `ideviceinstaller` di libimobiledevice.
- Uno strumento che firma con l'Apple ID, come Sideloadly, crea da solo App ID e profilo. Se il rapporto mostra entitlement assenti, attivare le due capability sull'App ID creato dallo strumento e reinstallare.
- Sull'iPhone attivare Impostazioni → Privacy e sicurezza → Modalità sviluppatore. Se iOS chiede di autorizzare lo sviluppatore, la voce è in Impostazioni → Generali → VPN e gestione dispositivi.

## StikDebug

Su iOS 26 gli iPhone con TXM, tra cui iPhone 14 Pro, rendono eseguibile la memoria solo tramite un debugger che esegue uno script. SwitchCore usa lo script `universal.js` di [StikDebug](https://github.com/StikDebug/StikDebug) e ne implementa il protocollo descritto in [INTEGRATION.md](https://github.com/StikDebug/StikJIT/blob/main/INTEGRATION.md). Requisiti indicati dal progetto StikDebug a novembre 2026:

1. StikDebug non è più sull'App Store. Si installa dall'IPA delle [release](https://github.com/StikDebug/StikDebug/releases/latest), firmata come SwitchCore, oppure dalla sua AltSource con SideStore o AltStore.
2. Installare [LocalDevVPN](https://apps.apple.com/us/app/localdevvpn/id6755608044) dall'App Store e attivarne la VPN prima di usare il JIT, con il Wi-Fi acceso.
3. Su iOS 26 il file di abbinamento si crea dal PC. Su Windows installare iTunes (versione MSI consigliata) e [iloader](https://iloader.app), collegare l'iPhone via USB, scegliere «Manage Pairing File» e poi «Place» accanto a StikDebug. Cambiare il codice di sblocco invalida il file.
4. Tenere attiva la Modalità sviluppatore. Se il collegamento fallisce più volte, riavviare l'iPhone, riattivare la VPN e riprovare.

## Rapporto M0

1. Aprire SwitchCore e toccare l'icona del processore in alto a sinistra («Diagnostica nativa»). Le sonde [a]–[d] partono da sole e durano pochi secondi.
2. Con LocalDevVPN attiva, toccare «Attiva JIT con StikDebug». Si apre StikDebug, che si collega a SwitchCore con `universal.js`. Se StikDebug non riporta da solo a SwitchCore, tornarci a mano. Il pannello attende il debugger fino a 3 minuti e poi esegue il test.
3. Toccare «Copia rapporto» e incollarlo nella chat, oppure «Condividi rapporto» per inviarlo con Mail, Note, Messaggi o File.
4. Se l'app si chiude durante il test JIT, riaprirla, aprire di nuovo «Diagnostica nativa» e inviare il rapporto. La sezione [e] riporta i passi registrati prima della chiusura.
5. Il test JIT gira una volta per avvio. Per ripeterlo chiudere SwitchCore dal multitasking e riaprirla.

Contenuto del rapporto:

- intestazione con modello, versione di iOS, identificatore dell'app, entitlement trovati nella firma dell'eseguibile (XML e DER) e nel profilo incorporato;
- [a] variazioni di x18 e TPIDR_EL0 durante context switch e segnali;
- [b] mappa delle riserve a indirizzo fisso tra 4 e 64 GiB a blocchi di 256 MiB, più alcuni indirizzi oltre 64 GiB;
- [c] memoria disponibile (`os_proc_available_memory`), footprint e limite stimato;
- [d] flag di firma del processo, tra cui `CS_DEBUGGED` e `GET_TASK_ALLOW`, e presenza del debugger;
- [e] test JIT: una pagina da 16 KiB sotto 2^36 preparata con `brk #0xf00d`, alias scrivibile con `vm_remap`, funzione che restituisce 42 prima e dopo il distacco del debugger e 43 dopo la riscrittura.

L'app esegue `brk` solo se `CS_DEBUGGED` è attivo e il debugger risulta collegato, perché senza lo script un `brk` chiude l'app. Il rapporto non contiene UDID né identificativo del team.

## SD virtuale per nx-hbmenu

Ogni titolo ha una SD virtuale. Un menu homebrew come nx-hbmenu elenca le NRO presenti in `sdmc:/switch/` e le avvia con A. Quando la NRO avviata termina, si torna al menu.

1. Importare `hbmenu.nro` dallo zip delle [release di nx-hbmenu](https://github.com/switchbrew/nx-hbmenu/releases) con Importa → Homebrew NRO.
2. Sul PC creare una cartella chiamata `switch` con dentro i file `.nro` (fino a 128 MiB ciascuno) e copiarla sull'iPhone, in iCloud Drive o in «Sul mio iPhone» dell'app File.
3. In SwitchCore aprire il menu ⋯ di nx-hbmenu, scegliere «Aggiungi cartella alla SD» e selezionare la cartella `switch`. I file finiscono in `sdmc:/switch/`.
4. Avviare nx-hbmenu: le NRO compaiono nell'elenco.

Una seconda cartella `switch` aggiunge i file nuovi a quelli presenti. Un file con lo stesso nome di uno già presente fa annullare l'importazione: i file esistenti non vengono sostituiti. Lo stesso comando copia nella radice della SD anche le cartelle di dati di una homebrew, ad esempio `stk-data`. nx-hbmenu crea `switch/` vuota al primo avvio, quindi la cartella si può aggiungere anche dopo averlo già aperto.
