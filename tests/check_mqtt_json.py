"""Validate actual JSON produced by native production-code tests.

Requires Jinja2==3.1.6. This exercises the discovery template, not a live Home
Assistant instance. Run bash tests/run_native.sh first to generate the inputs.
"""
import json
from pathlib import Path
from jinja2 import StrictUndefined, Template

build = Path(__file__).resolve().parent / 'build'
config = json.loads((build / 'shutter-discovery.json').read_text())
detail = json.loads((build / 'shutter-detail.json').read_text())
assert config['optimistic'] is True
assert config['json_attr_t'] == '~tele/DETAIL'
assert config['cmd_t'] == '~cmnd/POWER'
assert config['payload_open'] == 'ON'
assert config['payload_close'] == 'OFF'
assert config['payload_stop'] == 'PAUSE'
template = Template(config['val_tpl'], undefined=StrictUndefined)
for value, expected in [('stopped', 'None'), ('open', 'open'),
                        ('closed', 'closed'), ('opening', 'opening'),
                        ('closing', 'closing'), ('None', 'None')]:
    assert template.render(value_json={'POWER': value}) == expected
assert detail['yokis_state'] == 'stopped'
assert detail['state_source'] == 'command_stop_estimate'
assert detail['state_estimated'] is True
assert detail['raw_response'] == '00 00'
assert detail['stop_check'] == 'stopped_observed'
assert detail['stop_check_attempts'] == 1
print('MQTT JSON valid; 6 Home Assistant template translations passed')

# Validate all generated entity kinds, including maximum supported names.
for path in [build / 'shutter-discovery.json', *sorted(build.glob('network-discovery-*.json'))]:
    item = json.loads(path.read_text())
    assert item['avty_mode'] == 'all'
    assert 'avty_t' not in item
    assert len(item['avty']) == 2
    assert item['avty'][0]['topic'] == '~tele/LWT'
    assert item['avty'][1]['topic'].startswith('yokis/YokisHack-')
    assert item['avty'][1]['topic'].endswith('/availability')
    for availability in item['avty']:
        assert availability['pl_avail'] == 'Online'
        assert availability['pl_not_avail'] == 'Offline'
    assert item['stat_t'] == '~tele/STATE'
    assert item['dev']['ids'] == [item['uniq_id']]
    assert item['cmd_t'] == '~cmnd/POWER'
    assert item['dev']['name'] + '/' == item['~']
assert detail['state_replayed'] is False
assert isinstance(detail['status_age_ms'], int)
print('Network discovery: 5 JSON configurations valid; dual availability and identifiers preserved')
