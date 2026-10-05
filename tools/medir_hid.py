#!/usr/bin/env python3
# Author: Andrés Botero
"""
Arnes de medicion del modo USB HID (gamepad) usando evdev.

Cuenta "muestras" como reportes HID: cada grupo de eventos termina en SYN_REPORT.
Mide reportes/segundo y el intervalo (dt) entre reportes. El tamano del reporte es
fijo (6 bytes segun el descriptor del firmware), no se mide desde el bus.

Requiere Linux + permiso de lectura sobre /dev/input/* (usa sudo o el grupo 'input').

Ejemplos:
  python tools/medir_hid.py --list
  sudo python tools/medir_hid.py --match Arduino --duration 10 \\
       --label hid --summary-csv mediciones.csv
  sudo python tools/medir_hid.py --device /dev/input/event7 --duration 10
"""
from __future__ import annotations

import argparse
import csv
import os
import statistics
import sys
import time

try:
    import select
    import evdev
    from evdev import ecodes
except Exception:
    evdev = None
    ecodes = None
    select = None


def list_devices():
    if evdev is None:
        sys.exit('evdev no esta instalado. Instala con: pip install evdev')
    for path in evdev.list_devices():
        dev = evdev.InputDevice(path)
        print(f'{path:24s} {dev.name}')
        dev.close()


def find_device(match=None, path=None):
    if path:
        return evdev.InputDevice(path)
    if not match:
        sys.exit('Indica --match <texto> o --device <ruta> (o usa --list).')
    match = match.lower()
    for p in evdev.list_devices():
        dev = evdev.InputDevice(p)
        if match in dev.name.lower():
            return dev
        dev.close()
    sys.exit(f'No se encontro un dispositivo cuyo nombre contenga "{match}".')


def measure(dev, duration, warmup):
    """Devuelve los instantes (timestamp del kernel) de cada reporte.

    Se usa `ev.timestamp()` (reloj del kernel) para medir el intervalo entre
    reportes, y `perf_counter()` solo para controlar duracion/calentamiento.
    """
    t0 = time.perf_counter()
    times = []
    while True:
        r, _, _ = select.select([dev.fd], [], [], 0.1)
        if r:
            for ev in dev.read():
                if ecodes and ev.type == ecodes.EV_SYN and ev.code == ecodes.SYN_REPORT:
                    elapsed = time.perf_counter() - t0
                    if elapsed > duration:
                        return times
                    if elapsed >= warmup:
                        times.append(ev.timestamp())
        else:
            if time.perf_counter() - t0 > duration:
                return times


def stats(values):
    if not values:
        return None
    return {
        'mean': statistics.mean(values), 'median': statistics.median(values),
        'min': min(values), 'max': max(values), 'std': statistics.pstdev(values),
        'n': len(values),
    }


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--list', action='store_true', help='lista dispositivos de entrada')
    p.add_argument('--match', help='subcadena del nombre del dispositivo')
    p.add_argument('--device', help='ruta del dispositivo (ej. /dev/input/event7)')
    p.add_argument('--duration', type=float, default=10.0)
    p.add_argument('--warmup', type=float, default=1.0)
    p.add_argument('--report-bytes', type=int, default=6,
                   help='bytes por reporte segun el descriptor (default 6)')
    p.add_argument('--label', default='hid')
    p.add_argument('--out', help='CSV con una fila por reporte')
    p.add_argument('--summary-csv', help='CSV acumulativo de resumen')
    args = p.parse_args(argv)

    if args.list:
        list_devices()
        return 0

    if evdev is None:
        sys.exit('evdev no esta instalado. Instala con: pip install evdev')

    dev = find_device(args.match, args.device)
    try:
        times = measure(dev, args.duration, args.warmup)
    finally:
        dev.close()

    if not times:
        print('No se recibieron reportes. Revisa el dispositivo/permisos.')
        return 1

    dt = [b - a for a, b in zip(times, times[1:])]
    dur = times[-1] - times[0]
    rate = (len(times) - 1) / dur if dur > 0 else None
    d = stats(dt)

    print()
    print('================ RESUMEN (HID) ================')
    print(f'dispositivo:       {dev.name} ({dev.path})')
    print(f'reportes:          {len(times)}')
    print(f'duracion:          {dur:.3f} s')
    if rate:
        print(f'reportes/s:        {rate:.1f}')
    print(f'bytes/reporte:     {args.report_bytes}')
    if d:
        print(f'dt (ms):           media {d["mean"]*1000:.3f} | mediana {d["median"]*1000:.3f} | '
              f'min {d["min"]*1000:.3f} | max {d["max"]*1000:.3f} | desv {d["std"]*1000:.3f}')
    print('==============================================')
    print()

    if args.out:
        with open(args.out, 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['idx', 't_s', 'dt_ms', 'bytes'])
            t0 = times[0]
            prev = None
            for i, t in enumerate(times):
                row_dt = '' if prev is None else f'{(t - prev) * 1000:.3f}'
                prev = t
                w.writerow([i, f'{t - t0:.6f}', row_dt, args.report_bytes])
        print(f'CSV de reportes -> {args.out}')

    if args.summary_csv:
        header = (not os.path.exists(args.summary_csv)) or os.path.getsize(args.summary_csv) == 0
        with open(args.summary_csv, 'a', newline='') as f:
            w = csv.writer(f)
            if header:
                w.writerow(['label', 'format', 'samples', 'duration_s', 'rate_samples_s',
                            'bytes_mean', 'bytes_min', 'bytes_max', 'bytes_per_s',
                            'dt_host_mean_ms', 'dt_host_median_ms', 'dt_host_min_ms',
                            'dt_host_max_ms', 'dt_host_std_ms',
                            'dt_fw_mean_ms', 'dt_fw_median_ms', 'dt_fw_min_ms',
                            'dt_fw_max_ms', 'dt_fw_std_ms'])
            w.writerow([
                args.label, 'hid', len(times), f'{dur:.3f}',
                '' if rate is None else f'{rate:.2f}',
                args.report_bytes, args.report_bytes, args.report_bytes,
                '' if rate is None else f'{rate * args.report_bytes:.1f}',
                '' if d is None else f'{d["mean"]*1000:.3f}',
                '' if d is None else f'{d["median"]*1000:.3f}',
                '' if d is None else f'{d["min"]*1000:.3f}',
                '' if d is None else f'{d["max"]*1000:.3f}',
                '' if d is None else f'{d["std"]*1000:.3f}',
                '', '', '', '', '',
            ])
        print(f'Resumen agregado -> {args.summary_csv}')

    return 0


if __name__ == '__main__':
    raise SystemExit(main())
