"""Validate reserved build identities shared by compilation and packaging."""
from datetime import datetime
import re


def validate_build_id(value):
    if not re.fullmatch(r'[0-9]{6}-[0-9]{6,}', value):
        raise ValueError('Build identity must be yymmdd-counter with at least six counter digits')
    datetime.strptime(value[:6], '%y%m%d')
    if int(value[7:]) == 0:
        raise ValueError('Build counter must be positive')
    return value
