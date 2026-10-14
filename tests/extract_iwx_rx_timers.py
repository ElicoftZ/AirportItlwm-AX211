"""Compile the actual driver timer methods against the host timer/mbuf fixture."""
import re
import sys
from pathlib import Path

source = (Path(__file__).resolve().parents[1] / 'itlwm/hal_iwx/ItlIwx.cpp').read_text()
names = ['iwx_rx_ba_session_expired', 'iwx_reorder_timer_expired',
         'iwx_is_sn_less', 'iwx_release_frames', 'iwx_init_reorder_buffer',
         'iwx_clear_reorder_buffer']
parts = []
helper = re.search(r'static uint64_t\s+iwx_uptime_usec\(', source)
if helper:
    end = source.index('\n}', helper.start()) + 2
    parts.append(source[helper.start():end])
for name in names:
    match = re.search(r'(?:void|int) ItlIwx::\s*' + name + r'\(', source)
    if not match:
        raise RuntimeError(f'Missing driver method: {name}')
    end = source.index('\n}', match.start()) + 2
    parts.append(source[match.start():end])
Path(sys.argv[1]).write_text('\n\n'.join(parts) + '\n')
