# Notiz an mich: Erst das Leistungsziel planen

17. Oktober 2026

Ich hätte früher festlegen müssen, welche Ausführungsarchitektur zu meinem eigentlichen Ziel passt: korrekte Geschwindigkeit und sauberer Ton auch auf schwacher Hardware, mit ausreichend Reserve.

Wir haben mehrere Tage den vorhandenen Interpreter ausgebaut. Die Verbesserungen waren messbar und nicht nutzlos. Trotzdem haben wir zu lange einzelne Abläufe optimiert, ohne früh genug zu klären, ob dieser Ansatz das Gesamtziel tragen kann. Jetzt muss der CPU-Ausführungsweg grundlegend neu aufgebaut werden. Das fühlt sich an, als würde ich wieder von vorne anfangen.

Ein vollständiger Neustart ist es nicht: CPU-Semantik, Geräte, Speicherverwaltung, Spielstände und Prüfungen bleiben wichtige Grundlagen. Aber die Architekturentscheidung hätte vor der langen Reihe kleiner Optimierungen stehen müssen.

Für die nächsten Schritte halte ich deshalb fest:

* Vor größeren Arbeiten Zielhardware, Leistungsreserve und Abnahmekriterien festlegen.
* Architekturentscheidungen am Gesamtziel ausrichten und schriftlich begründen.
* Messbare Teilverbesserungen nicht mit einem erreichten Produktziel verwechseln.
* Zeitbudgets einhalten und mit kurzen, aussagekräftigen Prüfungen entscheiden.
* Die neue JIT-Architektur konsequent aufbauen, statt wieder in einzelne Interpreter-Sonderfälle abzubiegen.

Die verbindliche Richtung steht in der [Architekturentscheidung](92_jit_architecture_decision.md). Diese Notiz soll mir bei späteren Projekten helfen, dieselbe Planungsentscheidung früher zu treffen.
