# Scelta del motore

| Base esaminata | Evidenza e licenza | Decisione per SwitchCore |
| --- | --- | --- |
| [nxnative](https://github.com/seedds/nxnative) | MIT per il codice del progetto, licenze distinte per le dipendenze. Riscrive NSO prima del packaging e genera un'app per titolo. | Valutare il traduttore NVN/Metal separatamente. Il flusso di importazione NRO richiede esecuzione dinamica del guest. |
| [Eden iOS](https://github.com/kiddreads/eden-ios) | Port comunitario GPL-3.0-or-later. Usa Dynarmic, Vulkan e MoltenVK. | Tenere come confronto per la compatibilità. Il percorso accelerato richiede condizioni diverse dall'interprete su iOS standard. |
| [MeloNX, fork con CI](https://github.com/nurtrino/MeloNX) | Il README dichiara GPLv3, mentre LICENSE.txt è identificata come MIT. Core derivato da Ryujinx, stack .NET. | Nessun sorgente importato. Per un riuso occorrerebbe chiarire le licenze dei singoli componenti e verificare il percorso senza JIT. |
| [Dynarmic](https://github.com/emuall/dynarmic) | Ricompilatore dinamico, licenza 0BSD alla revisione esaminata. | Possibile confronto futuro. La baseline usa un interprete e funziona senza generare codice eseguibile. |
| [NeXo 2](https://github.com/NeXoNetwork/NeXo2-emu/tree/d329d28bfeb3bfab175c4c380aa926679446c1c9) | Core GPLv3, CPU interprete, caricatore NRO, IPC Horizon, HID, display VI, GPU Maxwell software e test. | Portare i moduli homebrew in C++20. Verificare i servizi con NRO, adattare clock e configurazione Switch 1, scrivere integrazione Swift e Metal. |

I permessi di esecuzione delle pagine su iOS sono controllati dal kernel. Il progetto non usa entitlement JIT. L'IPA senza firma è un prodotto di compilazione. L'installazione sul dispositivo richiede una firma e un provisioning validi. [Apple Platform Security](https://help.apple.com/pdf/security/en_US/apple-platform-security-guide.pdf)

## Ricerca Switch 2

NVIDIA documenta una nuova GPU con RT Core, Tensor Core e DLSS. L'interprete, la diagnostica e le strutture per le pagine possono essere valutati per il riuso. I profili CPU, l'ABI di sistema, i comandi GPU e il budget di memoria richiedono convalida indipendente. Il nome del motore upstream non certifica il supporto alla console. Il test del backend sperimentale deve rifiutare il lancio finché manca un percorso riproducibile con un eseguibile autorizzato. [NVIDIA](https://blogs.nvidia.com/blog/nintendo-switch-2-leveled-up-with-nvidia-ai-powered-dlss-and-4k-gaming/)

## Fonti dei formati

La convenzione di ingresso NRO e le strutture del contenitore sono confrontabili con [Homebrew ABI](https://switchbrew.org/wiki/Homebrew_ABI) e [elf2nro](https://github.com/switchbrew/switch-tools/blob/master/src/elf2nro.c).

