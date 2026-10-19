---
paths:
  - "src/**"
  - "inc/**"
  - "port_f10x/**"
  - "tools/**"
---

# Firmware AOCI : perimetre et revue

- Le travail couvre la commande moteur et les protections, avec la methode par portes.
- Strategie : portage EBiCS + constantes AOCI. AOCI = reference/temoin, pas de patch binaire AOCI
  pour le produit.
- Validation minimale : ne mesurer que ce qui peut detruire le materiel ou invalider la boucle.
  Le reste est reporte avec un risque ecrit. Timebox 2 h par tache.
- Banc : alim 60 V / 5 A, WCH-Link, pas d'analyseur, run direct en walk assist 24 V / 0,5-1 A.
- Methode par portes obligatoire : aucune puissance au depart, `MOE = 0`, alimentation limitee,
  preuve mesuree a chaque etape. Les valeurs non mesurees sont marquees provisoires.
- Yanni developpe le portage en autonomie. Fangyan (ingenieure systemes embarques, equipe Syklo,
  bancs propres, premiere versions des firmwares Syklo) relit les points critiques et valide au
  banc, sans reprendre le developpement.
- StanCoke (auteur d'EBiCS) est une source d'informations sur la bibliotheque, pas un editeur du
  portage.
- Rien n'est flashe sans accord explicite de Yanni.
- EN 15194 : 25 km/h par defaut, pas de mise en service d'un mode hors limite pour la vente.
