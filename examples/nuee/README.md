# Nuée — particules / 3 minutes

Une pièce audiovisuelle de 180 secondes pour un seul nuage de particules et une caméra qui le parcourt. La bande sonore est celle de [Champ de rupture](../champ-de-rupture/README.md) : la même synthèse FM, les mêmes repères. Ici, aucun grain n'est simulé par du bruit : jusqu'à 262 144 particules ont chacune une position, une vitesse et une durée de vie, calculées à chaque image sur le GPU (`isf/generators/Particles.fs` et `Particles.vs`).

Le projet demande la version de NYXMAP qui sait dessiner les passes `POINTS` des shaders ISF, avec l'onglet **Audio**.

## Jouer

1. Fabriquer le set (voir *Régénérer*), puis ouvrir `Nuee.nyxmap`. Garder les dossiers `audio` et `shaders` à côté.
2. Choisir la sortie audio et régler le niveau d'écoute.
3. Dans le séquenceur, sélectionner **Nuée · particules / 3 minutes**, puis déclencher **GO une seule fois**. Les 4 mouvements lents sont armés, puis la bande et les 13 cues s'enchaînent.
4. Pour rejouer, déclencher le premier step depuis son numéro. La dernière cue ramène l'image et le son au silence.

STOP arrête les enchaînements ; les sources déjà lancées continuent. Le blackout reste la commande de coupure immédiate.

## Les contrôles des particules

Les paramètres reprennent le vocabulaire des moteurs de particules courants (Trapcode Particular, particlesGPU de TouchDesigner, POPs de Houdini, VFX Graph d'Unity, Niagara) : un émetteur, des forces, des attracteurs, un aspect qui évolue sur la durée de vie.

**Émetteur**

| Paramètre | Rôle |
|---|---|
| Particles | Combien de grains (262 144 au maximum). Avec Life, il fixe le débit : Particles ÷ Life grains naissent chaque seconde. |
| Emitter Shape | Sphere (le volume), Shell (la surface), Ring (un anneau horizontal), Point, Line (en travers de l'image), Layer (les zones claires du calque choisi dans Form Layer). |
| Emitter Position / Size | Où se trouve l'émetteur, et sa taille. |
| Life (s) / Life Random | Durée de vie d'un grain, et l'écart aléatoire autour d'elle. |
| Emit Direction | Vers où partent les grains : Outward (vers l'extérieur de la forme), Up, Down, Left, Right (une fontaine, une pluie). |
| Initial Speed / Direction Spread | La vitesse de départ, et l'ouverture du jet : 0 suit la direction, 1 part dans tous les sens. |
| Burst | Une poussée vers l'extérieur de l'émetteur, tant qu'elle est levée (pour les attaques). |

**Forces**

| Paramètre | Rôle |
|---|---|
| Time Factor | Ralentit ou accélère toute la simulation (0 la fige). |
| Gravity / Wind | Une accélération vers le bas (négative : vers le haut) ; un vent dans le plan de l'image. |
| Drag | La résistance de l'air, proportionnelle à la vitesse : les grains freinent. |
| Turbulence / Scale / Evolution | Un champ de bruit « curl », sans puits : les grains tourbillonnent sans s'agglutiner. Scale grandit les tourbillons, Evolution les fait changer plus vite. |
| Vortex | Une rotation autour de l'axe vertical qui passe par l'émetteur (négatif : dans l'autre sens). |

**Attracteurs** (deux, placés dans le plan de l'image)

| Paramètre | Rôle |
|---|---|
| Attractor 1 / 2 Position | Où il se trouve. |
| Strength | Positif : il attire. Négatif : il repousse, c'est un disperseur. Une attraction s'adoucit près du centre, comme un ressort, pour que les grains forment un nuage autour du point au lieu de s'y écraser. Une répulsion est la plus forte en son centre. |
| Radius | Sa portée ; l'effet s'éteint en douceur sur la moitié extérieure. |
| Swirl | Une traction de côté : les grains tournent autour au lieu de tomber dedans. |

Par défaut, l'attracteur 1 tient le nuage au centre et l'attracteur 2 creuse un vide à droite.

**Aspect** : Size, Size Random (surtout de la poussière fine, quelques grains plus gros), Size over Life (négatif : rétrécit en vieillissant, positif : grandit), Color at Birth → Color at Death sur la vie du grain, Color when Fast mêlée selon la vitesse (Color by Speed), Brightness, Trail.

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

## Le rendu et les images

Chaque grain laisse une traînée de mouvement (**Motion Streaks**). Hors du plan net, il devient un disque flou (**Depth of Field**). La lumière s'additionne puis passe par un halo (**Bloom**) et une courbe filmique. **Size Random** mêle à la poussière fine quelques grains plus gros et plus brillants.

Deux calques peuvent servir d'image, choisis dans l'inspecteur de la source :

- **Grain Layer** : chaque particule est dessinée avec l'image de ce calque (une étoile, une tache d'encre…), sur sa luminosité.
- **Form Layer**, avec **Emitter Shape** sur *Layer* : les particules prennent leur place sur les zones claires de ce calque, et le nuage le dessine. Le calque est suivi en direct : une vidéo, un texte ou un générateur qui bouge entraîne le nuage, qui se remet en place en une seconde environ. **Form Hold** règle la fermeté du rappel : les forces et les attaques déforment la forme, puis elle revient. **Emitter Size** et **Emitter Position** règlent sa taille et sa place.

Ces deux entrées ne prennent qu'un calque, jamais un fichier : pour une image fixe, la charger dans un calque (masqué si besoin, il est rendu quand même) puis le choisir avec le bouton **Layer**.

## Partition

| Temps | Cue | Nuage | Caméra |
|---|---|---|---|
| 0:00 | Émergence / poussière lointaine | Peu de grains, froids. | Fixe, loin. |
| 0:12 | Premier corps / orbite lente | Le nuage se remplit. | Orbite lente. |
| 0:28 | La poussière se souvient | Traînées plus longues. | Huit. |
| 0:44 | Peaux de résonance | Teinte violette. | Spirale. |
| 1:00 | Première chambre / traversée | Le nuage se relâche, un disperseur l'ouvre. | Traversée rapide, coupée sur l'impact. |
| 1:16 | Compression | Forte attraction vers le centre. | Orbite proche. |
| 1:31 | Grêle métallique | Traînées courtes, lumière blanche. | Balancier rapide. |
| 1:44 | Fracture / silence | Un disperseur fend le nuage ; les traces restent longtemps. | Arrêt net. |
| 1:56 | Mémoire oblique | Violet, mémoire des traces. | Spirale à rebours. |
| 2:08 | Masse critique | Densité maximale. | Spirale proche. |
| 2:24 | Le champ cède | L'attracteur s'inverse : le nuage explose. | Huit à pleine vitesse. |
| 2:40 | Débris lents | Moins de grains, longues traînées. | Orbite très lente, loin. |
| 2:52–3:00 | Retour au noir | Extinction de l'image et de la bande. | Fixe. |

## Qui pilote quoi

- **Le son** (5 interactions Audio) : spectre complet → Time Factor (la vitesse de toute la simulation), 28–120 Hz → Vortex, 700–3200 Hz → Color by Speed, 6–16 kHz → Turbulence, attaques → Burst.
- **Les timelines** (4 oscillateurs lents) : Turbulence Evolution, hauteur de la caméra, taille des grains, focale.
- **Les cues** : quantité, attraction (Attractor 1) et dispersion (Attractor 2), traînée, luminosité, couleurs, chemin de la caméra, sa vitesse, son ampleur, sa distance, sa direction.

Aucun des trois ne réécrit les paramètres des deux autres.

## Régénérer

Python 3, NumPy et ffmpeg/ffprobe :

```sh
python3 ../champ-de-rupture/compose.py --out ./master
python3 build_show.py --audio ./master/Champ_de_rupture.mp3 --out ./nouveau-set
```

La composition est préparée en 1280×720 à 60 fps. Le nombre de particules suit **Particles** (262 144 au maximum) : valider la cadence sur la machine de diffusion et baisser Particles si nécessaire.
