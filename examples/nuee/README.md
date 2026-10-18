# Nuée — particules / 3 minutes

Une pièce audiovisuelle de 180 secondes pour un seul nuage de particules et une caméra qui le parcourt. La bande sonore est celle de [Champ de rupture](../champ-de-rupture/README.md) : la même synthèse FM, les mêmes repères. Ici, aucun grain n'est simulé par du bruit : jusqu'à 262 144 particules ont chacune une position, une vitesse et une durée de vie, calculées à chaque image sur le GPU (`isf/generators/Particles.fs` et `Particles.vs`).

Le projet demande la version de PINOL qui sait dessiner les passes `POINTS` des shaders ISF, avec l'onglet **Audio**.

## Jouer

1. Fabriquer le set (voir *Régénérer*), puis ouvrir `Nuee.pinol`. Garder les dossiers `audio` et `shaders` à côté.
2. Choisir la sortie audio et régler le niveau d'écoute.
3. Dans le séquenceur, sélectionner **Nuée · particules / 3 minutes**, puis déclencher **GO une seule fois**. Les 4 mouvements lents sont armés, puis la bande et les 13 cues s'enchaînent.
4. Pour rejouer, déclencher le premier step depuis son numéro. La dernière cue ramène l'image et le son au silence.

STOP arrête les enchaînements ; les sources déjà lancées continuent. Le blackout reste la commande de coupure immédiate.

## La caméra

La source a sa propre caméra, réglable et animable comme tous ses paramètres (inspecteur, OSC, snapshots, timelines) :

| Paramètre | Rôle |
|---|---|
| Camera Direction / Height / Distance | Où elle se tient autour de sa cible. |
| Camera Target | Le point qu'elle regarde (x, y). |
| Camera Lens | De grand-angle à téléobjectif. |
| Camera Path | Son mouvement : Fixed, Orbit, Figure eight, Spiral, Pendulum, Fly-through (elle entre dans le nuage et en ressort). |
| Camera Speed | Sa vitesse sur le chemin, de −1 (à rebours) à 1 (un tour en 20 s). |
| Camera Path Size | L'ampleur du mouvement. |

La position sur le chemin s'accumule d'une image à l'autre : changer la vitesse accélère ou ralentit la caméra sans la faire sauter. Un nouveau chemin est rejoint en 2 secondes depuis l'endroit où elle se trouve, en suivant la sphère qui entoure la cible.

## Partition

| Temps | Cue | Nuage | Caméra |
|---|---|---|---|
| 0:00 | Émergence / poussière lointaine | Peu de grains, froids. | Fixe, loin. |
| 0:12 | Premier corps / orbite lente | Le nuage se remplit. | Orbite lente. |
| 0:28 | La poussière se souvient | Traînées plus longues. | Huit. |
| 0:44 | Peaux de résonance | Teinte violette. | Spirale. |
| 1:00 | Première chambre / traversée | Le nuage se relâche. | Traversée rapide, coupée sur l'impact. |
| 1:16 | Compression | Forte attraction vers le centre. | Orbite proche. |
| 1:31 | Grêle métallique | Traînées courtes, lumière blanche. | Balancier rapide. |
| 1:44 | Fracture / silence | Les traces restent longtemps. | Arrêt net. |
| 1:56 | Mémoire oblique | Violet, mémoire des traces. | Spirale à rebours. |
| 2:08 | Masse critique | Densité maximale. | Traversée. |
| 2:24 | Le champ cède | Plus d'attraction : le nuage s'ouvre. | Huit à pleine vitesse. |
| 2:40 | Débris lents | Moins de grains, longues traînées. | Orbite très lente, loin. |
| 2:52–3:00 | Retour au noir | Extinction de l'image et de la bande. | Fixe. |

## Qui pilote quoi

- **Le son** (5 interactions Audio) : spectre complet → Energy (vitesse et lumière), 28–120 Hz → Bass (attraction), 700–3200 Hz → Metal (finesse du flux), 6–16 kHz → Air (turbulence), attaques → Hit (explosion et chaleur de la couleur).
- **Les timelines** (4 oscillateurs lents) : Turbulence, hauteur de la caméra, taille des grains, focale.
- **Les cues** : quantité, attraction, traînée, exposition, couleurs, chemin de la caméra, sa vitesse, son ampleur, sa distance, sa direction.

Aucun des trois ne réécrit les paramètres des deux autres.

## Régénérer

Python 3, NumPy et ffmpeg/ffprobe :

```sh
python3 ../champ-de-rupture/compose.py --out ./master
python3 build_show.py --audio ./master/Champ_de_rupture.mp3 --out ./nouveau-set
```

La composition est préparée en 1280×720 à 60 fps. Le nombre de particules suit **Amount** (262 144 au maximum) : valider la cadence sur la machine de diffusion et baisser Amount si nécessaire.
