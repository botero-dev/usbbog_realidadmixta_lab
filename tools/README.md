# Arnés de medición

Scripts para medir el rendimiento de la comunicación entre el Arduino Pro Micro y el
host, **sin depender del render de Godot** (la medición se hace fuera del bucle de dibujo).

## Instalación

```bash
pip install -r tools/requirements.txt
# en Linux, para HID quizá necesites: sudo usermod -aG input $USER   (y reabrir sesión)
```

## ¿Qué se mide?

Unidad de **muestra** = un estado completo del controlador (todos los botones y ejes).
Así la comparación entre protocolos es justa: en Teleplot una muestra ocupa varias
líneas, mientras que en JSON/TEXT/​HID ocupa una sola.

- **bytes por muestra**
- **muestras/segundo** (ancho de banda)
- **Δt entre muestras** (host)
- **Δt entre marcas de tiempo del firmware** (`T`, microsegundos) → independiente del render
- **bytes/segundo**
- **retardo extra de un solo sentido** (mediana, p95, p99, max)

## Serie (Teleplot / JSON / TEXT)

```bash
# Teleplot
python tools/medir_serie.py --port /dev/ttyACM0 --format debug --duration 10 \
    --label debug --summary-csv mediciones.csv

# JSON
python tools/medir_serie.py --port /dev/ttyACM0 --format json --duration 10 \
    --label json --summary-csv mediciones.csv --out json_samples.csv

# Texto plano
python tools/medir_serie.py --port /dev/ttyACM0 --format text --duration 10 --label text

# Analizar una captura ya guardada
python tools/medir_serie.py --replay captura.bin --format auto
```

Opciones útiles:

| Flag | Descripción |
|------|-------------|
| `--format` | `auto` (default), `debug`, `json`, `text` |
| `--duration` | segundos a medir (default 10) |
| `--warmup` | segundos descartados al inicio (default 1) |
| `--out` | CSV con una fila por muestra |
| `--summary-csv` | CSV acumulativo: una fila por corrida (útil para la Tabla II) |
| `--label` | etiqueta de la fila en el resumen |

> El CSV de resumen (`--summary-csv`) se puede ir llenando con las tres corridas y
> usarlo directamente para completar la tabla cuantitativa del informe.

## HID (gamepad)

```bash
python tools/medir_hid.py --list                       # ver dispositivos
sudo python tools/medir_hid.py --match Arduino --duration 10 \
     --label hid --summary-csv mediciones.csv
```

En HID no se puede medir el tamaño desde el bus; se usa el tamaño fijo del reporte
declarado en el firmware (`--report-bytes`, por defecto 6 B).

## Interpretación: `dt host` vs `dt firmware`

El arnés toma **dos** marcas de tiempo por muestra:

- **`t_host`** — `time.perf_counter()` en el momento de leer la línea (reloj del host).
  Está afectado por la entrega en ráfagas del bus USB: la mediana puede salir ~0 y
  aparecer picos de decenas de ms. Sirve como referencia, no como medida fina.
- **`t_fw`** — el campo `T` del firmware (µs del Arduino). Su intervalo refleja el
  ritmo **real del emisor** y es **inmune al render** de Godot. El arnés corrige el
  desbordamiento de `micros()` (2³² µs ≈ 71.6 min).

**Retardo de un solo sentido.** `d_i = t_host_i - t_fw_i` incluye un desfase de reloj
constante, así que no es una latencia absoluta. Sin embargo, `min(d)` es el mejor caso
y `d - min(d)` es el **retardo adicional** (cola de latencia); se reportan su mediana,
p95, p99 y máximo. Es una medida **independiente del render**.

## Notas

- Para una comparación justa, fija la **misma tasa** en los tres protocolos (ver el
  `#define SEND_EVERY_MS` sugerido en `src/main.cpp`).
- El `dt` del firmware (a partir de `T`) es la medida **más robusta** frente al render,
  porque su ritmo lo marca el Arduino, no el motor.
