## Componentes
- El transformador [Clase sobre transformadores monofásicos](https://youtu.be/fHOQ986yyZ0?si=3Nx_2Lxxf4w6tcXB)

## Esquemas
- Wall wart +/-12V PS [Fuente simétrica de 12V](https://musicfromouterspace.com/analogsynth_new/WALLWARTSUPPLY/WALLWARTSUPPLY.php)

## Introducción

Vamos a construir una fuente de alimentación simétrica de 12 voltios basándonos en el proyecto de la web  [MFOS](https://musicfromouterspace.com/analogsynth_new/WALLWARTSUPPLY/WALLWARTSUPPLY.php)

Esta fuente es bastante sencilla, y sus componentes principales son dos reguladores lineales el [LM7812](https://www.alldatasheet.com/datasheet-pdf/pdf/838008/TI1/LM7812.html) y el [LM7912](https://www.alldatasheet.com/datasheet-pdf/pdf/545533/TI/LM7912.html). 

La ventaja de usar una fuente de alimentación lineal, frente a una fuente conmutada es que es más sencilla de construir y generan menos ruido, algo fundamental en circuitos de audio. 

El principal problema de las fuentes lineales, como la que vamos a construir, es que es menos eficiente y cara. 

## Por qué queremos usar una fuente de alimentación

La fuente de alimentación nos va a permitir alimentar nuestros circuitos sin depender de baterías o pilas.

Lo que hace la fuente de alimentación es convertir el voltaje alterno que nos dan los enchufes de casa en un voltaje continuo determinado. Por tanto, realiza dos tareas:
- Convertir el voltaje alterno en continuo
- Reducir o aumentar el voltaje

## Qué es el voltaje alterno

La corriente continua o voltaje continuo mantiene una diferencia de potencial constante y sin cambios de polaridad, mientras que la corriente alterna, mantiene la diferencia de potencial constante pero sí cambios de polaridad. 

![[Pasted image 20260403113239.png]]

Las características principales de la corriente alterna son las siguientes:
- Frecuencia (F): número de veces que la corriente cambia de polaridad en un segundo. La unidad es el hertz (HZ). La tensión de casa en España tiene una frecuencia de 50 HZ, por lo que cambia de polaridad 50 veces en un segundo. Los cambios de polaridad están referidos a ciclos (cuando se repite el mismo punto de la onda)
- Periodo (T): es el tiempo que tarda en producirse un ciclo de voltaje alterno, su fórmula es T = 1/F. Por lo que en nuestro caso, cada ciclo tiene una duración de 20 milisegundos. **Es el tiempo que tarda en repetirse algo**
- Valor instantáneo: valor que toma la tensión en cada instante de tiempo.
- Valor máximo o pico: valor que la tensión alcanza en su punto máximo. **Este valor es bastante importante para saber qué condensadores de filtrado usar**
- Valor pico a pico: la distancia entre el valle y el pico. En una onda senoidal, es igual a dos veces el valor pico. 
- Valor medio: media aritmética de todos los valores instantáneos de la señal en un periodo dado. 
- Valor eficaz o RMS (el que mide el polímetro): es más o menos el 70 % del valor pico o el valor pico dividido por raíz de dos. Es el valor de tensión continua que produciría el mismo efecto térmico (calor) en una resistencia que la tensión alterna dada.

## Cómo funciona una fuente lineal 

Para entender el funcionamiento de una fuente de alimentación lineal recorreremos el circuito examinando cada uno de sus componentes y realizaremos simulaciones sencillas con LTSpice.


**SINE**: Offset 0V, Amplitud **17V** (para un adaptador de 12V AC RMS)
**Frecuencia**:  50Hz o 60Hz.

![[Diodos.asc]]

![[Pasted image 20260403105112.png]]