"""Compile the exact production callbacks, without Arduino setup/network services.

Not a reimplementation: function bodies are copied verbatim into a temporary
translation unit. The fake radio/clock/FS are confined to tests/host.
"""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
text = (root / 'src/main.cpp').read_text()
functions=[]
for name in ('pollForStatus', 'mqttCallback', 'loop'):
    m = re.search(r'^void '+name+r'\([^;\n]*\)\s*\{', text, re.M)
    if not m:
        raise RuntimeError('Production callback not found: '+name)
    # Balanced braces, ignoring comments and string literals.
    start=m.end()-1
    token=re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',re.S)
    depth=0
    for t in token.finditer(text,start):
        if t.group()=='{': depth+=1
        elif t.group()=='}':
            depth-=1
            if depth==0:
                functions.append(text[m.start():t.end()]);break
    else: raise RuntimeError('Unbalanced function '+name)
(root/'tests/build/callbacks.cpp').write_text('#include "globals.h"\n#include "reliability.h"\n#include "RF/irqManager.h"\n#include "postStopPolling.h"\n'+ '\n'.join(functions) if (root/'include/reliability.h').exists() else '#include "globals.h"\n'+'\n'.join(functions))
