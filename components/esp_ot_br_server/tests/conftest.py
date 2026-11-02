# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0


def pytest_configure(config):
    config.addinivalue_line('markers', 'webui: Web UI board tests, run in CI only when Web UI files change')
