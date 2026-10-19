---
paths:
  - "src/**"
  - "inc/**"
  - "port_f10x/**"
  - "tools/**"
---

# Firmware AOCI : perimetre et revue

- La commande moteur et les protections de l'etage de puissance sont du code normal. Aucune partie
  n'est hors perimetre pour motif de puissance.
- Methode par portes obligatoire : aucune puissance au depart, `MOE = 0`, alimentation limitee,
  preuve mesuree a chaque etape. Les valeurs non mesurees sont marquees provisoires.
- Yanni developpe le portage en autonomie. Fangyan (ingenieure systemes embarques, equipe Syklo,
  bancs propres, premiere versions des firmwares Syklo) relit les points critiques et valide au
  banc, sans reprendre le developpement.
- StanCoke (auteur d'EBiCS) est une source d'informations sur la bibliotheque, pas un editeur du
  portage.
- Rien n'est flashe sans accord explicite de Yanni.
- EN 15194 : 25 km/h par defaut, pas de mise en service d'un mode hors limite pour la vente.
