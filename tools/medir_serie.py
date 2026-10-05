#!/usr/bin/env python3
# Author: Andrés Botero
"""
Arnes de medicion de la comunicacion serie (Arduino Pro Micro -> host).

Mide, SIN depender del render de Godot:
  * bytes por muestra
  * muestras/segundo (ancho de banda)
  * intervalo (dt) entre muestras medido en el host
  * intervalo (dt) entre las marcas de tiempo del firmware (T, microsegundos)
  * bytes/segundo

Unidad de "muestra" = un estado completo del controlador (todos los botones y ejes),
que en Teleplot ocupa varias lineas y en JSON/TEXT una sola. Asi la comparacion
entre protocolos es justa.

Formatos soportados (los del firmware src/main.cpp):
  - debug : Teleplot  ->  '>T:123' '>A:0' ... '>Y:509'   (varias lineas por muestra, CRLF)
  - json  : 1 linea   ->  '{"T":123,"A":0,...,"Y":509}'  (CRLF)
  - text  : 1 linea   ->  'T123 A0 B1 C0 D1 X234 Y221'   (LF)
  - auto  : detecta el formato linea por linea

Ejemplos:
  python tools/medir_serie.py --port /dev/ttyACM0 --format debug --duration 10
  python tools/medir_serie.py --port /dev/ttyACM0 --format json  --duration 10 \\
         --label json --summary-csv mediciones.csv --out json_samples.csv
  python tools/medir_serie.py --replay captura.bin --format auto
"""
from __future__ import annotations

import argparse
import csv
import os
import re
import statistics
import sys
import time
from dataclasses import dataclass

try:
    import serial  # pyserial
except Exception:  # permite usar --replay sin pyserial instalado
    serial = None


_US_PER_S = 1_000_000.0
_RE_JSON_T = re.compile(r'"T"\s*:\s*(\d+)')
_RE_TEXT = re.compile(r'^T(\d+)\b')


@dataclass
class Sample:
    t_host: float                    # instante del host al recibir la 1ra linea
    n_bytes: int = 0                 # bytes crudos de la muestra (incluye CRLF)
    t_fw: float | None = None        # marca de tiempo del firmware (segundos)


def _to_seconds(value):
    try:
        return int(value) / _US_PER_S
    except (TypeError, ValueError):
        return None


# micros() del ATmega32U4 es de 32 bits y desborda cada 2^32 us (~4294.967 s).
_MICROS_WRAP = (2 ** 32) / _US_PER_S


def _dt_fw(a, b):
    """Intervalo b-a entre marcas T, corrigiendo el desbordamiento de micros()."""
    d = b - a
    if d < 0:
        d += _MICROS_WRAP
    return d


def parse_line(line: bytes):
    """Devuelve (kind, key, value); kind in {'debug','json','text',None}."""
    s = line.decode('ascii', errors='replace').strip()
    if not s:
        return (None, None, None)
    if s.startswith('>'):
        body = s[1:]
        if ':' in body:
            key, value = body.split(':', 1)
            return ('debug', key, value)
        return (None, None, None)
    if s.startswith('{') and s.endswith('}'):
        m = _RE_JSON_T.search(s)
        return ('json', m.group(1) if m else None, s)
    m = _RE_TEXT.match(s)
    if m:
        return ('text', m.group(1), s)
    return (None, None, None)


def iter_samples(fh, fmt, unknown=None):
    """Genera Sample a partir de un stream binario (puerto serie o fichero)."""
    buf = bytearray()
    cur = None
    while True:
        # Drenar solo lo que ya llego (evita acumular hasta 4096 B y mejora la
        # resolucion del timestamp del host). En un fichero se lee por bloques.
        if hasattr(fh, 'in_waiting'):
            n = fh.in_waiting
            chunk = fh.read(n) if n else fh.read(1)
        else:
            chunk = fh.read(4096)
        if not chunk:
            break
        buf += chunk
        while b'\n' in buf:
            raw, _, rest = buf.partition(b'\n')
            buf = bytearray(rest)
            n_bytes = len(raw) + 1  # incluye el '\n'
            kind, key, value = parse_line(raw)
            now = time.perf_counter()

            if kind == 'debug' and fmt in ('auto', 'debug'):
                if key == 'T':
                    if cur is not None:
                        yield cur
                    cur = Sample(t_host=now)
                    cur.t_fw = _to_seconds(value)
                if cur is None:
                    cur = Sample(t_host=now)
                cur.n_bytes += n_bytes
            elif kind == 'json' and fmt in ('auto', 'json'):
                s = Sample(t_host=now, n_bytes=n_bytes)
                s.t_fw = _to_seconds(key) if key is not None else None
                yield s
            elif kind == 'text' and fmt in ('auto', 'text'):
                s = Sample(t_host=now, n_bytes=n_bytes)
                s.t_fw = _to_seconds(key) if key is not None else None
                yield s
            elif unknown is not None:
                unknown(raw)
    if cur is not None:
        yield cur


def collect(fh, fmt, duration=None, warmup=0.0):
    """Recolecta muestras; descarta las del calentamiento y respeta la duracion."""
    t0 = time.perf_counter()
    out = []
    for s in iter_samples(fh, fmt):
        elapsed = s.t_host - t0
        if duration is not None and elapsed > duration:
            break
        if elapsed >= warmup:
            out.append(s)
    return out


def _stats(values):
    if not values:
        return None
    ordered = sorted(values)
    n = len(ordered)

    def pct(p):
        idx = min(n - 1, max(0, int(round((p / 100.0) * (n - 1)))))
        return ordered[idx]

    return {
        'mean': statistics.mean(values),
        'median': statistics.median(values),
        'min': ordered[0],
        'max': ordered[-1],
        'p95': pct(95),
        'p99': pct(99),
        'std': statistics.pstdev(values),
        'n': n,
    }


def analyze(samples, live=True):
    """Calcula el resumen agregado a partir de la lista de muestras."""
    if not samples:
        return None

    n = len(samples)
    duration = samples[-1].t_host - samples[0].t_host
    bytes_list = [s.n_bytes for s in samples]
    total_bytes = sum(bytes_list)

    dt_host = [b.t_host - a.t_host
               for a, b in zip(samples, samples[1:])
               if live]
    dt_fw = [_dt_fw(a.t_fw, b.t_fw)
             for a, b in zip(samples, samples[1:])
             if a.t_fw is not None and b.t_fw is not None]

    # Latencia de un solo sentido estimada: d_i = t_host - t_fw. El desfase entre
    # los relojes es constante, asi que min(d) es el mejor caso y d - min(d) es el
    # retardo adicional (cola de latencia), independiente del render de Godot.
    latency = None
    delays = [s.t_host - s.t_fw for s in samples if s.t_fw is not None]
    if live and len(delays) >= 2:
        offset = min(delays)
        latency = _stats([d - offset for d in delays])

    res = {
        'n': n,
        'duration': duration,
        'rate': (n / duration) if (live and duration > 0) else None,
        'bytes_mean': statistics.mean(bytes_list),
        'bytes_min': min(bytes_list),
        'bytes_max': max(bytes_list),
        'bytes_per_s': (total_bytes / duration) if (live and duration > 0) else None,
        'dt_host': _stats(dt_host),
        'dt_fw': _stats(dt_fw),
        'latency': latency,
    }
    return res


def _ms(value, digits=3):
    return 'n/a' if value is None else f'{value * 1000:.{digits}f}'


def print_summary(res, args):
    if res is None:
        print('No se recibieron muestras. Revisa el puerto/formato.')
        return
    print()
    print('================ RESUMEN ================')
    print(f'fuente:            {args.port or args.replay}')
    print(f'formato:           {args.format}')
    print(f'muestras:          {res["n"]}')
    print(f'duracion:          {res["duration"]:.3f} s')
    if res['rate'] is not None:
        print(f'muestras/s:        {res["rate"]:.1f}')
        print(f'bytes/s:           {res["bytes_per_s"]:.0f}')
    else:
        print('muestras/s:        n/a (modo replay)')
    print(f'bytes/muestra:     {res["bytes_mean"]:.1f} '
          f'(min {res["bytes_min"]}, max {res["bytes_max"]})')

    dh = res['dt_host']
    if dh is None:
        print('dt host (ms):      n/a')
    else:
        print(f'dt host (ms):      media {_ms(dh["mean"])} | mediana {_ms(dh["median"])} | '
              f'min {_ms(dh["min"])} | max {_ms(dh["max"])} | desv {_ms(dh["std"])}')

    df = res['dt_fw']
    if df is None:
        print('dt firmware (ms):  n/a (sin campo T)')
    else:
        print(f'dt firmware (ms):  media {_ms(df["mean"])} | mediana {_ms(df["median"])} | '
              f'min {_ms(df["min"])} | p95 {_ms(df["p95"])} | max {_ms(df["max"])} | '
              f'desv {_ms(df["std"])}')

    lat = res.get('latency')
    if lat is None:
        print('retardo extra 1-via (ms): n/a (sin campo T / modo replay)')
    else:
        print(f'retardo extra 1-via (ms): mediana {_ms(lat["median"])} | '
              f'p95 {_ms(lat["p95"])} | p99 {_ms(lat["p99"])} | max {_ms(lat["max"])} | '
              f'desv {_ms(lat["std"])}')
    print('========================================')
    print()


def write_samples_csv(path, samples):
    with open(path, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['idx', 't_host_s', 'dt_host_ms', 't_fw_ms', 'dt_fw_ms', 'bytes'])
        t0 = samples[0].t_host
        prev = None
        prev_fw = None
        for i, s in enumerate(samples):
            dt_host = '' if prev is None else f'{(s.t_host - prev.t_host) * 1000:.3f}'
            dt_fw = ''
            if prev_fw is not None and s.t_fw is not None:
                dt_fw = f'{(s.t_fw - prev_fw) * 1000:.3f}'
            prev = s
            if s.t_fw is not None:
                prev_fw = s.t_fw
            w.writerow([
                i,
                f'{s.t_host - t0:.6f}',
                dt_host,
                '' if s.t_fw is None else f'{s.t_fw * 1000:.3f}',
                dt_fw,
                s.n_bytes,
            ])


def append_summary_csv(path, args, res):
    if res is None:
        return
    header = (not os.path.exists(path)) or os.path.getsize(path) == 0
    dh = res['dt_host'] or {}
    df = res['dt_fw'] or {}
    with open(path, 'a', newline='') as f:
        w = csv.writer(f)
        if header:
            w.writerow([
                'label', 'format', 'samples', 'duration_s', 'rate_samples_s',
                'bytes_mean', 'bytes_min', 'bytes_max', 'bytes_per_s',
                'dt_host_mean_ms', 'dt_host_median_ms', 'dt_host_min_ms',
                'dt_host_max_ms', 'dt_host_std_ms',
                'dt_fw_mean_ms', 'dt_fw_median_ms', 'dt_fw_min_ms',
                'dt_fw_max_ms', 'dt_fw_std_ms',
                'lat_median_ms', 'lat_p95_ms', 'lat_max_ms',
            ])
        lat = res.get('latency') or {}
        w.writerow([
            args.label or args.format,
            args.format,
            res['n'],
            f'{res["duration"]:.3f}',
            '' if res['rate'] is None else f'{res["rate"]:.2f}',
            f'{res["bytes_mean"]:.2f}', res['bytes_min'], res['bytes_max'],
            '' if res['bytes_per_s'] is None else f'{res["bytes_per_s"]:.1f}',
            _ms(dh.get('mean')), _ms(dh.get('median')), _ms(dh.get('min')),
            _ms(dh.get('max')), _ms(dh.get('std')),
            _ms(df.get('mean')), _ms(df.get('median')), _ms(df.get('min')),
            _ms(df.get('max')), _ms(df.get('std')),
            _ms(lat.get('median')), _ms(lat.get('p95')), _ms(lat.get('max')),
        ])


def make_reader(args):
    """Devuelve (fh, close_fn, live)."""
    if args.replay:
        fh = open(args.replay, 'rb')
        return fh, fh.close, False
    if serial is None:
        sys.exit('pyserial no esta instalado. Instala con: pip install pyserial')
    ser = serial.Serial(args.port, args.baud, timeout=0.02)
    try:  # agrandar el buffer de recepcion en Linux para reducir rafagas
        ser.set_buffer_size(rx_size=1 << 20, tx_size=1 << 16)
    except Exception:
        pass
    ser.reset_input_buffer()
    return ser, ser.close, True


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    src = p.add_mutually_exclusive_group(required=True)
    src.add_argument('--port', help='puerto serie (ej. /dev/ttyACM0, COM3)')
    src.add_argument('--replay', help='analiza un fichero de captura binario en vez del puerto')
    p.add_argument('--baud', type=int, default=115200, help='velocidad (default 115200)')
    p.add_argument('--format', choices=['auto', 'debug', 'json', 'text'], default='auto')
    p.add_argument('--duration', type=float, default=10.0, help='segundos a medir (default 10)')
    p.add_argument('--warmup', type=float, default=1.0, help='segundos a descartar al inicio')
    p.add_argument('--label', help='etiqueta para la fila del resumen (default: formato)')
    p.add_argument('--out', help='CSV con una fila por muestra')
    p.add_argument('--summary-csv', help='CSV acumulativo con una fila resumen por corrida')
    args = p.parse_args(argv)

    fh, close_fn, live = make_reader(args)
    try:
        samples = collect(fh, args.format,
                          duration=args.duration if live else None,
                          warmup=args.warmup if live else 0.0)
    except KeyboardInterrupt:
        samples = []
    finally:
        close_fn()

    res = analyze(samples, live=live)
    print_summary(res, args)

    if args.out and samples:
        write_samples_csv(args.out, samples)
        print(f'CSV de muestras -> {args.out}')
    if args.summary_csv:
        append_summary_csv(args.summary_csv, args, res)
        print(f'Resumen agregado -> {args.summary_csv}')

    return 0 if res else 1


if __name__ == '__main__':
    raise SystemExit(main())
