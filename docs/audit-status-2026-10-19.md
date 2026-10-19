# Audit des chantiers documentés — 19 octobre 2026

Base examinée : `b2e2d17b7413d1ac3d507fd0b2164c659772cff8` (`main`).
Les 15 documents présents dans `docs/` ont été comparés au code et aux tests.
Les audits des 15 et 16 octobre décrivent une révision historique : leurs
constats ne doivent pas être interprétés comme une liste de bugs tous encore ouverts.

## Corrections livrées

| Défaut confirmé | Correction | Régression |
|---|---|---|
| Les snapshots omettaient les quatre contrôles de source Audio, contrairement à la documentation : entrée, mesure, bornes basse/haute. Un rappel pouvait donc restaurer une route différente de celle sauvegardée. | Ajout des quatre adresses à `Engine::interactionKeys()`, utilisé pour capturer et ordonner les rappels. | `audio_snapshot` échouait sur capture, rappel et recréation d'une route supprimée ; vérifie maintenant ces trois parcours. |
| A3 : un tick dépassant le pre-wait pouvait déclencher AutoFollow alors que la timeline venait juste de démarrer. | La durée initiale d'un Play prend son départ effectif comme référence ; le suivi de la durée restante et le post-wait sont conservés. Les waits purs gardent leur report de dépassement. | `show_clock` reproduit un retard de 1,9 s, puis vérifie attente de la fin réelle, post-wait et rappel unique. Les régressions GL existantes de `cue_lifecycle` restent activées en CI. |
| A7 : changer Loop/PingPong rebâtissait le compteur de passe utilisé aussi comme phase des oscillateurs. | Décalage de temps joué indépendant du compteur de passe, préservé pendant les éditions et remis à zéro par Seek/Rewind/Stop/nouveau Play. Aucune nouvelle donnée persistée. | `show_clock` vérifie continuité, poursuite, pause/reprise, édition, reset et animation par paramètre. Les six premières assertions de conduite/phase échouaient avant correction. |
| A5 : l'audit matériel avait établi une perte d'alpha ProRes 4444 avec VideoToolbox, y compris avec copie forcée. | Sur macOS, sélection conservatrice du décodage logiciel pour ProRes 4444/XQ ou ProRes explicitement alpha (profil, tag ou format). Les autres codecs/profils restent éligibles au matériel. | `decoder_alpha` compare trois points de la rampe alpha réelle avec matériel demandé puis désactivé. Vérifie sur macOS le choix logiciel. Coût CPU supplémentaire à mesurer pour les shows comportant ces médias. |
| A12 : l'analyseur cherchait `snapshot`, mais le moteur journalise `snapshot_recall`. Il continuait aussi à annoncer un compteur de partage défectueux alors que le recorder v2 est corrigé. | Corrélation avec le nom réel, recherche temporelle triée, compteurs de partage exportés, avertissements limités aux anciens schémas et aux événements perdus. | Trois tests Python : schéma réel, compteurs, fenêtre ±250 ms, ordre des événements, fuseaux, JSON invalide et pertes. |
| A13 : tests vidéo dépendant d'un format logiciel ou d'un budget inapplicable au zero copy. | Accepter NV12 pour la sortie matérielle H.264 ; forcer le décodage logiciel uniquement dans le test du budget CPU et restaurer le réglage ensuite. | Les comparaisons RGBA et les seuils de fidélité sont conservés. Validation GL multiplateforme requise. |
| A14 : le benchmark abaissait son objectif à `min(sourceFPS, achievedRenderFPS)`. | Comparer séparément le rendu au FPS demandé et chaque couche à `min(sourceFPS, requestedFPS)`, avec la tolérance existante de 5 %. Rejeter les durées/FPS/dimensions invalides. | `benchmark_rates` protège notamment les faux succès 60 Hz demandé / 30 Hz obtenu, et les sources à 24 fps sur un rendu à 60 Hz. |

La modification ProRes est une politique de préservation de l'image fondée sur
la reproduction matérielle documentée le 15 octobre. Dans l'environnement local
de cet audit, VideoToolbox refusait l'initialisation et retombait en logiciel :
aucune nouvelle reproduction matérielle de la perte d'alpha n'est revendiquée.

## État des 15 documents

| Document / chantier | État actuel vérifié | Suite utile |
|---|---|---|
| [COLOR_PIPELINE_AUDIT](COLOR_PIPELINE_AUDIT.md) | RGBA16 UNORM interne, FLOAT ISF et test de rampe présents. Les images dans `Layers.cpp` et `Isf.cpp` sont encore converties en RGBA8888 ; readbacks et sorties CPU restent 8 bits. | Conservation des images 16 bits, vraie négociation 10 bits, dithering final et gestion colorimétrique restent des chantiers distincts. Aucun HDR/10 bits bout en bout validé. |
| [CUE_LATENCY](CUE_LATENCY.md) | Histogrammes capture/recall/exécution/retard et remise à zéro présents dans `Sequencer.cpp` et `Render.cpp`. | Mesurer les cues sous charge GPU, avec médias froids puis préchauffés. Les GO/recalls dispatchés au cue clock sont inclus dans les mesures de travail. |
| [architecture-debug-audit](architecture-debug-audit-2026-10-16.md) | Les allocations FFmpeg, seeks échoués et snapshots spectraux ont été corrigés depuis l'audit. `decoder_allocations` injecte les échecs sur Linux ; `audio_analysis_concurrency` vérifie la publication. Les métriques callback/verrou/pertes existent désormais. | Contention réelle, budgets souples, coût FFT, réclamation du mix audio et indexation restent à mesurer. Aucun besoin établi de réécrire globalement le moteur. |
| [audio-interactions](audio-interactions.md) | Analyse stéréo, migration, entrée live/programme, filtre par route, lecture sans attente et cache des bandes répétées présents. Capture snapshot complétée ici. | Vérifier latence et routage avec le périphérique de spectacle. Le premier spectre complet demande 4096 échantillons. |
| [ci-validation-policy](ci-validation-policy.md) | Routage conservateur, résultat Validation, ASan/TSan/fuzz, nightly Valgrind et échec autoritaire au premier essai présents. | Commentaires CMake et ancien plan de performance corrigés : un rerun diagnostique ne rend jamais un échec vert. |
| [endurance-telemetry](endurance-telemetry.md) | CSV v2, SHA régénéré, RSS, événements, compteurs de pertes et métriques lifetime présents. `EnduranceLogger` lit déjà `gpu_shares_skipped` correctement. Analyseur aligné ici. | Effectuer la session 8 h ; une hausse de RSS ou une corrélation temporelle ne prouve pas à elle seule une fuite ou une causalité. |
| [gpu-share-validation](gpu-share-validation.md) | Triple buffering à la demande et tests de rotation présents. Les contextes Syphon distincts gardent la limite explicitement décrite dans le document. | Captures chez les vrais récepteurs, sous charge et reconnexions. Une marge d'une frame ne prouve pas l'achèvement de la copie Syphon. |
| [macos-release-signing](macos-release-signing.md) | Packaging, signature ad hoc imbriquée, `codesign --verify --deep --strict` et contrôle des chemins Homebrew présents dans le workflow. | Notarisation/Developer ID restent absents par choix de distribution, sans promesse de lancement universel. |
| [mask-and-snapshot-editing](mask-and-snapshot-editing.md) | Géométrie Bézier, insertion exacte, suppression bornée et édition partielle undoable couvertes par les tests dédiés. | Validation visuelle des interactions GL sur le poste ; géométrie et mise à jour partielle vérifiées localement. |
| [per-parameter-delay](per-parameter-delay.md) | File ordonnée, index d'adresses, annulation par nouveau rappel et Escape, découpage des ticks aux deadlines et tests `recall_delay` présents. | Les opérations structurelles ne disposent toujours pas d'un délai indépendant : limite déjà annoncée, pas un contrôle factice à ajouter. |
| [performance-work-plan](performance-work-plan.md) | Décodage séparé par responsabilité, PBO, budgets souples, métriques, pacing absolu et sorties différées présents. | Budgets de threads/RAM non stricts ; anciens décodeurs et minimums par flux peuvent dépasser le seuil. Clarification du commentaire trompeur dans le code. Comparaisons matérielles à refaire avec le benchmark corrigé. |
| [realtime-acceptance](realtime-acceptance.md) | Instrumentation callback/verrous/pertes et seuils proposés présents ; compteurs lifetime clairement distingués. | Les seuils restent des objectifs : session 18 couches/60 Hz, audio physique, 8 h et vingt pertes/reprises de sorties ne sont pas attestés ici. |
| [timeline-repeat-validation](timeline-repeat-validation.md) | Gel de durée après fin de timeline et régression GL 60/120 Hz présents. AutoFollow tardif corrigé ici par une régression sans GL supplémentaire. | Vérifier l'affichage chez les récepteurs ; suppression de l'avertissement obsolète sur la clé CSV. |
| [ui-performance](ui-performance.md) | Dispatch cues, captures limitées aux couches concernées, télémétrie sans copie de clés, tryLock des meters et cache de 32 bandes présents. | Les tests UI sans GL passent localement ; aucune mesure physique de FPS ou de blocage GPU extrapolée du microbenchmark. |
| [video-timing-audit](video-timing-audit-2026-10-15.md) | A1/A2/A6 corrigés par tokens d'annulation et identités stables ; A3/A5/A7/A12/A13/A14 traités dans cet audit. | Voir les points encore ouverts ci-dessous ; préserver les preuves historiques du document original. |

## Constats historiques encore ouverts ou partiels

- **A4 : Escape et shaders ISF.** `stopShowPlayback()` arrête transports,
  fades et transitions, mais les instances ISF continuent leurs passes et
  `FRAMEINDEX`. Geler TIME seul laisserait évoluer feedback, DATE et entrées
  externes. Un gel/reprise explicite de l'image et de l'état des passes nécessite
  un contrat et des tests spécifiques ; ce correctif n'est pas simulé par un
  changement global de vitesse.
- **A8/A9 : politique après stall.** Le rendu borne encore son dt shader à
  250 ms et son dt de conduite à 2 s (`Render.cpp::nextDt`). Ces horloges peuvent
  diverger après un long blocage. Définir rattrapage, gel ou resynchronisation
  avant de supprimer les bornes : des simulations à feedback ne peuvent pas
  recevoir arbitrairement plusieurs secondes de dt sans validation.
- **A10 : fondus de composition et vitesse globale.** Les rampes de sortie
  utilisent le dt de rendu alors que la conduite utilise le temps de show.
  Séparer la sémantique du blackout de sécurité de celle d'un fondu de show
  reste une décision de comportement à tester.
- **A11 : comparaison/undo des presets.** Le comparateur UI a évolué depuis
  l'audit et les mises à jour partielles sont testées. Une preuve exhaustive
  d'undo/concordance couvrant définition d'animation, géométrie et tous les délais
  n'est pas établie par cet audit ; ne pas déclarer ce chantier fermé.
- **Architecture temps réel.** Le verrou de rendu reste long ; les budgets
  threads/mémoire restent souples ; la file d'interactions perd les nouveaux
  événements au-delà de 8192 et le routage reste proportionnel au produit
  événements × interactions. Les pertes sont désormais comptées. Les seuils de
  [realtime-acceptance](realtime-acceptance.md) doivent guider la priorité d'une
  refonte, sans coalescer les press/release ou triggers au risque de les perdre.
- **Publication GPU et IO.** Les attentes de readback sont bornées par sortie,
  pas par une deadline globale. L'arrêt peut encore dépendre d'une IO de média
  lente. Les comportements sous pilote défaillant, hot-unplug et réception
  Syphon/Spout/NDI restent à tester matériellement.

## Validation de cet audit

Compilation native macOS : Qt 6.12, FFmpeg 9, AppleClang 21 ; application,
moteur, tests d'intégration et benchmark compilés. Les régressions sans contexte
GL et les tests UI compatibles `offscreen` sont exécutés localement. Les
commandes et résultats finaux sont consignés dans la PR.

La plateforme Qt `offscreen` locale ne crée pas de contexte OpenGL ;
`recall_delay_ui` échoue avec « This plugin does not support
createPlatformOpenGLContext! ». Cela ne valide ni n'invalide son rendu : les
aires GL, ASan/TSan et les trois plateformes restent des validations CI requises.
Aucun seuil de fidélité, de précision ou de timing n'a été relâché pour faire
passer les nouveaux tests.

La CI du commit de code de référence `3a137c3` était verte sur Linux, macOS et
Windows ([run](https://github.com/PixelStereo/Pinol/actions/runs/39220085402)).
Le run de `b2e2d17`, uniquement documentaire, n'a pas recompilé le moteur ;
il ne constitue donc pas une nouvelle preuve de compatibilité multiplateforme.

Vérification locale Release : **18/18 CTest passent**, dont les cinq nouvelles
aires `show_clock`, `audio_snapshot`, `decoder_alpha`, `benchmark_rates` et
`endurance_analysis` (trois cas Python). Commande reproductible :

```sh
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure \
  -R '^(show_clock|audio_snapshot|decoder_alpha|benchmark_rates|endurance_analysis|ci_scope|champ_score|matieres_score|nuee_score|mask_geometry|realtime_timing|audio_analysis|audio_analysis_concurrency|snapshot_update_ui|timeline_activity|sequence_ui|number_widgets|color_picker_ui)$'
```

Le benchmark compilé refuse aussi `--seconds 0` avant d'ouvrir le média ou de
créer un contexte GL. `git diff --check` est propre.
