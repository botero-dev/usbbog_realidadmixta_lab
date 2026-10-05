# Lectura de 4 digitales + 2 analogicas (Arduino Pro Micro)

Proyecto [PlatformIO](https://platformio.org/) para **Arduino Pro Micro** (ATmega32U4,
5 V / 16 MHz)


## Compilar y subir

Con PlatformIO CLI instalado:

```bash
pio run                 # compilar
pio run --target upload  # grabar en la placa
pio device monitor       # ver la salida serie a 115200 baudios
```

## Modos de salida

El formato de salida se elige en `src/main.cpp` descomentando uno de los
`#define SEND_STATE_*`:

| Define | Descripcion |
| ------ | ----------- |
| `SEND_STATE_TEXT`   | Texto plano por serie (`T.. A0 B1 X123 ...`) |
| `SEND_STATE_DEBUG`  | Formato para [Teleplot](https://teleplot.fr/) |
| `SEND_STATE_JSON`   | Objeto JSON por linea (`{"T":..,"A":0,"B":1,"X":520,"Y":509}`) |
| `SEND_STATE_BINARY` | Paquete binario compacto |
| `SEND_STATE_USB`    | Gamepad USB HID (ver abajo) |

## Tasa de envio (opcional)

Por defecto el firmware envia en **cada** vuelta del `loop`, asi que la tasa de
muestras la fija la velocidad del bucle (y puede variar entre protocolos). Para
comparar los protocolos en igualdad de condiciones, descomenta:

```c
#define SEND_EVERY_MS 5   // una muestra cada 5 ms (200 Hz)
```

Con esto el envio se hace a tasa fija con `millis()`, igual para todos los
protocolos, y la marca de tiempo `T` se toma en el instante del envio. Util junto
con el arnes de medicion de `tools/` (ver `tools/README.md`).

## Modo USB HID gamepad

Con `#define SEND_STATE_USB` activo, la placa (ATmega32U4) se enumera como un
dispositivo compuesto: **gamepad HID + puerto serie CDC**, usando el stack
`PluggableUSB`/`HID` del propio core de Arduino (sin librerias externas).

Mapeo de entradas -> gamepad:

| Entrada | Boton HID (evtest) |
| ------- | ------------------ |
| D6 (`BTN_FACE_NORTH`) | `BTN_NORTH` |
| D7 (`BTN_FACE_EAST`)  | `BTN_EAST`  |
| D9 (`BTN_FACE_SOUTH`) | `BTN_SOUTH` |
| D5 (`BTN_STICK_LEFT`) | `BTN_THUMBL` (click del stick) |
| A2 | Eje `ABS_X` (0..1023, valor crudo del ADC) |
| A3 | Eje `ABS_Y` (0..1023, valor crudo del ADC) |

Detalles:

- Report ID `0x03` (los IDs `0x01`/`0x02` los reserva el core para teclado/raton).
- El descriptor declara 16 botones (2 bytes); solo se usan los usages 1, 2, 4 y 14
  para obtener los codigos estandar `BTN_SOUTH`, `BTN_EAST`, `BTN_NORTH` y `BTN_THUMBL`.
- Ejes de 16 bits con el valor crudo del ADC (0..1023).
- Solo se envia un reporte cuando cambia el estado (`is_dirty`).
- Para activarlo: comentar `#define SEND_STATE_DEBUG` y descomentar
  `#define SEND_STATE_USB`.

