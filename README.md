# ZINZIN

Un firmware complet pour le **M-VAVE FM-1** : un groovebox à 4 pistes qui sonne comme un Buchla fabriqué par
Nintendo et qui ressemble à une page perso de 1999. Pas de vocabulaire de synthé : des potards qui sont des paquets
de réglages, qui dérivent tout seuls, et un écran qui réagit à tout ce qui se passe.

Il remplace la couche d'origine du FM-1. Il est bâti sur [fm1-base](https://github.com/charlesvestal/fm1-base)
(la plateforme : écran, panneau, LED, audio, MIDI, stockage, mise à jour par USB, garde au démarrage), elle-même sur
[felucca-bsp](https://github.com/hugelton/felucca-bsp). ZINZIN n'est que l'app par-dessus : `zinzin/`.

> **État : écrit et testé sur ordinateur, pas encore lancé sur un vrai FM-1.** Le moteur sonore et l'écran tournent
> dans un banc d'essai (`make test`) qui produit des captures, des GIF et du son. Le premier flash sur la machine est
> le vrai test : voir « À vérifier sur la machine » plus bas.

## Les commandes

| Commande | Ce que ça fait |
| --- | --- |
| **K1 K2 K3 K4** | BIDOCHE, CHATOUILLE, MOISI, FLEMME de la piste choisie |
| **ARP** tenu + K1..K4 | BOUCAN (le volume) de BOUM, MATOU, MAMMOUTH, BAZAR |
| **ALGO** | HUMEUR : sept humeurs, de l'euphorie à la déprime (la gamme, le beat, l'écran) |
| **PRESET** | APÉRO : à quel point tout dérive tout seul |
| **SELECT** | CAFÉINE : le tempo (70 à 180), et des roulements quand ça monte |
| **MASTER** | le volume général |
| **FX SEL ENV LFO** | choisir BOUM, MATOU, MAMMOUTH, BAZAR |
| **PLAY** | ZOU : lance et arrête le séquenceur (le MIDI start/stop aussi) |
| **EDIT** | PAS : les 16 touches blanches sont les 16 pas ; les noires choisissent l'élément (le fût, la bêtise, la note) |
| **GLO** | AU PIF : un nouveau motif pour la piste choisie (**SEQ** : pour les quatre) |
| **HOME** | MUET : coupe la piste choisie |
| **REC** | ENREGISTRE : pendant que ça joue, ce que tu joues s'écrit dans le motif |
| **SAVE** | garde le morceau (potards, motifs, mutes) ; la LED SAVE s'allume quand il y a du neuf |
| **OCT- / OCT+** | CAVE / GRENIER : le clavier une octave plus bas / plus haut |
| **Le clavier** | joue la piste choisie (BOUM : les 4 fûts ; BAZAR : les 6 bêtises) ; le MIDI entrant aussi |

En mode PAS, les LED du clavier montrent le motif (accent fort, note faible) et la tête de lecture qui clignote à
travers. En lecture, la touche blanche du pas en cours luit.

## Les quatre pistes

- **BOUM** : la batterie. Kick en FM qui se change en triangle 4 bits, caisse claire, charley et « bing » métallique,
  bruit LFSR 15 bits façon NES.
- **MATOU** : le synthé. Oscillateur complexe façon côte Ouest (modulation de phase par un triangle 4 bits / une
  impulsion, wavefolder), filtre façon vactrol, copies désaccordées, sub, chorus.
- **MAMMOUTH** : la basse. Impulsion / triangle replié, sub, glissés quand FLEMME est haut.
- **BAZAR** : les bêtises, sur tout le mix (BÉGAIE, RECULE, FIGE, COULE, PLONGE, ÉCRASE), plus l'espace partagé
  (écho ping-pong, une pièce vide), la pourriture de bande et la colle.

Chaque piste a les mêmes quatre macros, qui bougent chacune plusieurs choses à la fois : **BIDOCHE** (le poids),
**CHATOUILLE** (le caractère, avec des crans qui basculent d'un coup), **MOISI** (la pourriture : bits, échantillonnage,
bande), **FLEMME** (la longueur et l'espace).

## Les humeurs

Les mélodies sont gardées en degrés de gamme : tourner HUMEUR recolore le même air.

| Humeur | Mode | Monde à l'écran |
| --- | --- | --- |
| euphorie | lydien | kawaii : prairie rose, arc-en-ciel, confettis, cœurs dans les yeux du chat |
| joie | ionien (majeur) | kawaii |
| insouciance | mixolydien | cosmos : étoiles, soleil rayé, grille vers l'horizon |
| rêverie | dorien | cosmos |
| mélancolie | éolien (mineur) | cosmos, la pluie commence |
| colère | phrygien | l'enfer : ciel rouge, lune rouge, le feu de Doom, lasers dans les yeux du chat |
| déprime | locrien | gris : l'écran passe en noir et blanc, il pleut, tout le monde pleure |

Les mondes se mélangent par un tramage (jamais un fondu : gratuit sur le FM-1). La batterie suit l'humeur tant que
personne ne l'a éditée.

## Construire et installer

Il faut la chaîne de compilation de JieLi (Linux x86-64 ; sur Mac elle tourne dans Docker) et trois fichiers du SDK
AC79 de JieLi. Tout est expliqué dans `base/README.md` ; en bref :

    git clone --recursive <ce dépôt> zinzin-fm1 && cd zinzin-fm1   # ou, depuis le zip : sh tools/get_base.sh
    sh base/tools/get_toolchain.sh ~/.jieli && export JIELI_TOOLCHAIN=~/.jieli/toolchain
    git clone https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK ~/fw-AC79_AIoT_SDK   # (AC79_SDK si ailleurs)
    make firmware                       # -> zinzin/build/zinzin.fwsc
    python3 base/tools/fm1_install.py zinzin/build/zinzin.fwsc

Sur un PC Windows (une Surface, par exemple), dans WSL (Linux dans Windows ; une fois, dans PowerShell en
administrateur : `wsl --install -d Debian`, puis redémarrer) :

    cp -r "/mnt/c/Users/<toi>/Documents/ZINZIN FM1" ~/zinzin-fm1 && cd ~/zinzin-fm1
    sh tools/build_wsl.sh               # tout ce qu'il faut, puis la construction

Le script installe ce qui manque, va chercher fm1-base, la chaîne de JieLi et les trois fichiers du SDK, fait une
version publiée (`--release 0.1`) et sa page d'installation, puis la sert sur la machine. Dans Edge ou Chrome, ouvrir
**http://localhost:8000/install/**, cocher **m-bootloader** (écrit juste après ZINZIN : PLAY à l'allumage = menu de
secours, REC = mode téléchargement) et appuyer sur Install. Le paquet est aussi copié dans `Téléchargements\zinzin`.

Si le FM-1 ne répond plus après une installation : REC tenu à l'allumage le met en mode téléchargement (Windows voit
un disque « WL82 UBOOT1.00 » : ne pas le formater), et [M-Flasher](https://github.com/jvitkauskas/m-flasher)
(Windows) y remet le firmware d'origine. Le Transporter reste le dernier recours.

Sans rien installer : le workflow GitHub Actions (`.github/workflows/firmware.yml`) fait la même chose et donne le
`.fwsc` en artefact (pas encore essayé). Pour revenir au firmware d'origine : M-UPGRADE, l'outil de M-VAVE.

### Pour ne pas bricker la machine

ZINZIN ne touche jamais au démarrage : il remplace l'app, pas ce qui est en dessous.

1. **Le mode mise à jour de la puce** (dans sa ROM, donc intouchable) : **REC tenu à l'allumage**. De là, l'installeur
   remet n'importe quel firmware, ZINZIN ou celui de M-VAVE.
2. **La garde de fm1-base** : deux démarrages ratés de suite et le FM-1 s'arrête en **mode sans échec** (USB actif :
   PLAY réessaie, REC passe en mode mise à jour) ; quatre et il passe tout seul en mode mise à jour.
3. **m-bootloader** (proposé par l'installeur web de fm1-base, recommandé) : REC à l'allumage = mode téléchargement,
   PLAY = menu de secours.
4. En tout dernier recours, le **FM-1 Transporter** (RP2040 sur les lignes USB) parle directement à la puce.

## Le banc d'essai

    make test

- `build/engine_test` : le moteur seul, toutes les configurations (douceur, masse, à fond, à zéro, chaque bêtise,
  chaque humeur) ; niveaux, pas de NaN, et un WAV de chacune dans `build/wav/`.
- `build/host` : toute l'app sur un script d'appuis et de tours de potards, sur une horloge simulée. Captures d'écran
  et GIF dans `build/host_out/` (`planche.png` les rassemble), le son de la session, et la vérification que le
  morceau sauvé se relit pareil.

C'est notre propre banc : il imite les quelques appels de la plateforme que ZINZIN fait (`hostsim/plat.h`).
`make host-base` construit en plus le simulateur de fm1-base, avec le vrai code de la plateforme.

## Ce que ça coûte

| | |
| --- | --- |
| RAM du son (lignes à retard en int16) | 182,6 Ko dans le pool (sur ~336 Ko) |
| Écran : deux bandes de 240 × 40 | 37,5 Ko (le canvas de fm1-base) |
| Particules de l'écran | ~14 Ko dans le pool |
| Images (16 couleurs) | 65 Ko de flash |
| Polices, table de sinus | ~31 Ko de flash |
| Images par seconde | 12,5, chacune dessinée en quelques passes de la boucle (≤ 9 ms par passe) |

L'écran n'a pas de mémoire d'image : chaque ligne est recalculée à la demande, ce qui rend gratuits les effets
plein écran (ondulations, miroir, pixellisation, tramage noir et blanc). Si le son prend trop de temps, le moteur
retire ses copies désaccordées et son chorus, puis les remet après 15 s de calme.

## À vérifier sur la machine

- **La charge du processeur** : mesurée ici, le son prend ~4 % d'un Xeon ; sur le cœur à 360 MHz du FM-1 on attend
  30 à 55 % selon les réglages. À regarder avec `plat_cpu_pct()`. Plan B prévu par fm1-base : le second cœur.
- Les couleurs de l'écran, le sens des potards, les LED de PLAY (rouge + vert), le niveau de sortie.
- Le déchirement de l'écran (pas de ligne TE sur le FM-1 ; les bandes de 40 lignes partent en ~3,5 ms chacune).

## Les fichiers

| | |
| --- | --- |
| `zinzin/app.c`, `app.h` | l'app : le panneau, les LED, le contrat avec fm1-base |
| `zinzin/zz_song.c` | le morceau : humeurs, gammes, motifs au pif, sauvegarde |
| `zinzin/zz_draw.c`, `.h` | l'écran (unité à part, en -O2) |
| `zinzin/dsp/zz_engine.c`, `.h` | le son (unité à part, en -O2) |
| `zinzin/gen/` | images, polices, table de sinus, générées par `tools/` depuis `assets/` (`make gen`) |
| `zinzin/fm1.toml`, `words.tsv` | réglages de construction ; mots pour un lecteur d'écran |
| `tests/`, `hostsim/` | le banc d'essai |
| `base/` | fm1-base (sous-module, ou `sh tools/get_base.sh`) |

## Licences

- **ZINZIN : MPL-2.0** (`LICENSE`), comme fm1-base et felucca-bsp : tout le firmware sous une seule licence. C'est
  une copyleft par fichier : qui modifie un fichier de ZINZIN et le distribue doit en publier la version modifiée,
  mais chacun peut combiner ZINZIN avec son propre code, sous la licence qu'il veut.
- Polices : Press Start 2P (CodeMan38) et Terminus (Dimitar Zhekov), SIL OFL 1.1, licences dans `assets/fonts/`.
- Images : `assets/png/`. Les photos (le chat, le cacatoès, le pendule de Newton, la tasse, la pièce, la lune, le
  cheval, la prairie, l'espace) viennent des images d'exemple d'imageio / scikit-image (domaine public d'après leur
  documentation), réduites à 16 couleurs ; les sprites sont dessinés pour ZINZIN.
