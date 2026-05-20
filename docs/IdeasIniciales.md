## Dudas y decisiones
- Hardware base: Teensy 4.1 (~30€) + [Teensy Audio Library](https://www.pjrc.com/teensy/td_libs_Audio.html) o ESP32 + [AMY Library](https://github.com/shorepine/amy) ([Ejemplos](https://shorepine.github.io/amy/tutorial.html))
	- Ventaja del ESP32: precio
	- Ventaja del Teensy: más potente, mayor post-procesado, más todo en uno
- MIDI input: conector MIDI (gasta pines) o USB Host (necesita ser soportado). Cómo de fácil es leer y procesar los valores?
- Salida e audio: line out con algo tipo PCM5102A?
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
  
[Generador de acordes basado en Teensy](https://minichord.com/)
- Es open source, están colgados hasta los *stl para imprimir las carcasas.
### Esquemas por partes

#### Fuente de alimentación
[Fuente de MFOS](https://musicfromouterspace.com/analogsynth_new/WALLWARTSUPPLY/WALLWARTSUPPLY.php)
