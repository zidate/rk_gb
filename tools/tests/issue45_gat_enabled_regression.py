#!/usr/bin/env python3
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
errors = []

protocol_header = (root / 'App/Protocol/config/ProtocolExternalConfig.h').read_text(encoding='utf-8', errors='ignore')
local_config = (root / 'App/Protocol/config/LocalConfigProvider.cpp').read_text(encoding='utf-8', errors='ignore')
protocol_manager = (root / 'App/Protocol/ProtocolManager.cpp').read_text(encoding='utf-8', errors='ignore')
gat_service = (root / 'App/Protocol/gat1400/GAT1400ClientService.cpp').read_text(encoding='utf-8', errors='ignore')

if 'struct GatRegisterParam' not in protocol_header or 'int enabled;' not in protocol_header.split('struct GatRegisterParam', 1)[1].split('};', 1)[0]:
    errors.append('GatRegisterParam 缺少 enabled 字段')

if 'GatRegisterParam()' in protocol_header and ': scheme("http")' in protocol_header and 'enabled(1)' not in protocol_header.split('GatRegisterParam()', 1)[1].split('{', 1)[0]:
    errors.append('GatRegisterParam 默认值未初始化 enabled(1)')

required_local_config_snippets = [
    'ReadIniInt(ini, kLocalGatConfigSection, "enable", path, ivalue)',
    'fprintf(fp, "enable=%d\\n", param.enabled != 0 ? 1 : 0);',
    'target.enabled = (source.enabled != 0) ? 1 : 0;',
    'if (param.enabled != 0 &&',
]
for snippet in required_local_config_snippets:
    if snippet not in local_config:
        errors.append(f'LocalConfigProvider.cpp 缺少片段: {snippet}')

if 'if (m_cfg.gat_register.enabled != 0)' not in protocol_manager:
    errors.append('ProtocolManager::Start 未按 gat_register.enabled 控制 GAT 启动')

if '(m_cfg.gat_register.enabled != latest.gat_register.enabled)' not in protocol_manager:
    errors.append('ReloadExternalConfig 未将 gat_register.enabled 纳入 reloadGat 判定')

required_gat_service_snippets = [
    'if (cfg.gat_register.enabled == 0)',
    'const bool nextEnabled = cfg.gat_register.enabled != 0;',
    'if (!nextEnabled) {',
]
for snippet in required_gat_service_snippets:
    if snippet not in gat_service:
        errors.append(f'GAT1400ClientService.cpp 缺少片段: {snippet}')

if errors:
    for error in errors:
        print(f'FAIL: {error}')
    sys.exit(1)

print('PASS: issue45 gat enabled regression checks')
