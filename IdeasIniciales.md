## Dudas y decisiones
- Hardware base: Teensy 4.1 (~30€)+ o ESP32
	- Ventaja del ESP32: precio
## Link a proyectos interesantes

### Proyectos completos

[Sintetizador basado en Teensy](https://github.com/NickCulbertson/Mini-Teensy-Synth)
Características remarcables del proyecto:
- Usa Teensy 4.1 (~30€)
- Utiliza la librería propia de Teensy para la sintetización de audio
- En este proyecto se utiliza el único puerto USB del Teensy 4.1 para 3 funciones:
	- Entrada MIDI usando el USB en modo Host
	- Salida de audio hacia el ordenador
	- Alimentación
- Al utilizar el USB para todo ello, quedan libres todos los pines para poder conectar los potenciómetros y pantalla.
### Esquemas por partes

#### Fuente de alimentación
[Fuente de MFOS](https://musicfromouterspace.com/analogsynth_new/WALLWARTSUPPLY/WALLWARTSUPPLY.php)
