# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Border Router board tests.

The runner provides RUNNER_PC_NIC, RUNNER_AP_SSID, RUNNER_AP_PASSWORD, and the
serial port in ESPPORT1. Firmware is built with CONFIG_OPENTHREAD_BR_START_WEB=y.
"""
import os
import re
import socket
import time

import pexpect
import pytest
import requests
from requests.adapters import HTTPAdapter

IP_ADDRESS = re.compile(r'Got IPv4 event: Interface ".*?" address: (\d+\.\d+\.\d+\.\d+)')

interface = os.getenv('RUNNER_PC_NIC')
ssid = os.getenv('RUNNER_AP_SSID')
password = os.getenv('RUNNER_AP_PASSWORD')
ESPPORT1 = os.getenv('ESPPORT1') or os.getenv('ESPPORT')
APP_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../../examples/basic_thread_border_router'))

WEB_PAGES = {
    '/': 'Thread Border Router - Dashboard',
    '/index.html': 'Thread Border Router - Dashboard',
    '/network.html': 'Thread Border Router - Network',
    '/commission.html': 'Thread Border Router - Commissioner',
    '/addresses.html': 'Thread Border Router - Addresses',
    '/tools.html': 'Thread Border Router - Tools',
    '/topology.html': 'Thread Border Router - Topology',
    '/about.html': 'Thread Border Router - About',
}

br_board = pytest.mark.parametrize(
    'target, port, app_path',
    [pytest.param('esp32s3', ESPPORT1, APP_PATH, id='s3')],
    indirect=True,
)


def _interface_exists(name):
    return bool(name) and os.path.isdir(f'/sys/class/net/{name}')


class _BindInterfaceAdapter(HTTPAdapter):
    def __init__(self, source_interface, **kwargs):
        self._source_interface = source_interface if _interface_exists(source_interface) else None
        if source_interface and self._source_interface is None:
            print(f'{source_interface} is not in this network namespace, connecting without SO_BINDTODEVICE')
        super().__init__(**kwargs)

    def init_poolmanager(self, connections, maxsize, block=False, **pool_kwargs):
        if self._source_interface:
            options = list(pool_kwargs.get('socket_options') or [])
            options.append((socket.SOL_SOCKET, socket.SO_BINDTODEVICE, self._source_interface.encode()))
            pool_kwargs['socket_options'] = options
        super().init_poolmanager(connections, maxsize, block, **pool_kwargs)


def _text(value):
    if isinstance(value, bytes):
        return value.decode()
    return value


def _flush_output(dut, quiet=0.3):
    while True:
        try:
            dut.expect(re.compile(r'.+', re.DOTALL), timeout=quiet)
        except pexpect.TIMEOUT:
            return


def _expect_ot_result(dut, command, timeout):
    # OpenThread prints exactly one of these after the command returns. Matching either
    # one consumes this command's result so a later command cannot accept it as its own.
    matched = dut.expect(re.compile(r'(Done|Error.*)'), timeout=timeout)
    result = _text(matched.group(1)).strip()
    if result != 'Done':
        pytest.fail(f'ot {command} failed: {result}')


def _ot_command_and_check(dut, command, timeout=10):
    dut.write(f'ot {command}')
    _expect_ot_result(dut, command, timeout)


def _wait_for_cli(dut):
    # esp_cli_custom_command_init() runs after the RCP version check. A version
    # mismatch updates the RCP and reboots, so this line is the boot where the
    # wifi command exists.
    dut.expect('Returned from app_main()', timeout=180)


def _factory_reset(dut):
    _wait_for_cli(dut)
    # factoryreset reboots inside the command, so the CLI does not print Done.
    dut.write('ot factoryreset')
    dut.expect('Returned from app_main()', timeout=180)


def _connect_wifi(dut, web=False):
    command = f'wifi connect -s {ssid} -p {password}'
    dut.write(f'ot {command}')
    address = None
    if web:
        # The server starts from the GOT_IP handler and logs its address there.
        # connect_handler()'s "Start the web server..." line is not on this path.
        ip_match = dut.expect(IP_ADDRESS, timeout=90)
        address = _text(ip_match.group(1))
    matched = dut.expect(
        re.compile(r'wifi sta is connected successfully|wifi sta connection is failed|Error \d+: [^\r\n]*'),
        timeout=90,
    )
    outcome = _text(matched.group(0))
    if outcome != 'wifi sta is connected successfully':
        pytest.fail(f'wifi connect failed: {outcome}')
    # The connect command still prints Done. Drop it so the next command cannot accept it.
    _flush_output(dut)
    return address


def _form_thread_network(dut):
    _ot_command_and_check(dut, 'dataset init new')
    channel = os.getenv('CHANNEL')
    networkkey = os.getenv('NETWORKKEY')
    if channel:
        _ot_command_and_check(dut, f'dataset channel {channel}')
    if networkkey:
        _ot_command_and_check(dut, f'dataset networkkey {networkkey}')
    _ot_command_and_check(dut, 'dataset commit active')
    _ot_command_and_check(dut, 'ifconfig up')
    _ot_command_and_check(dut, 'thread start')
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        dut.write('ot state')
        try:
            matched = dut.expect(
                re.compile(r'(?:^|\n)(leader|router|child|detached|disabled)\r*\n|Error \d+: [^\r\n]*'),
                timeout=3,
            )
        except pexpect.TIMEOUT:
            continue
        role = matched.group(1)
        if role is None:
            pytest.fail(f'ot state failed: {_text(matched.group(0))}')
        _expect_ot_result(dut, 'state', 3)
        if _text(role) == 'leader':
            return
    pytest.fail('Border router did not become the Thread leader')


@br_board
def test_br_form_network(dut):
    _factory_reset(dut)
    _connect_wifi(dut)
    _form_thread_network(dut)


@br_board
@pytest.mark.webui
def test_br_webui(dut):
    _factory_reset(dut)
    address = _connect_wifi(dut, web=True)
    session = requests.Session()
    session.mount('http://', _BindInterfaceAdapter(interface))
    for path, title in WEB_PAGES.items():
        page = session.get(f'http://{address}{path}', timeout=20)
        assert page.status_code == 200, page.text
        assert title in page.text
    stylesheet = session.get(f'http://{address}/static/style.css', timeout=20)
    assert stylesheet.status_code == 200
    script = session.get(f'http://{address}/static/api.js', timeout=20)
    assert script.status_code == 200
    assert 'function apiGet' in script.text
