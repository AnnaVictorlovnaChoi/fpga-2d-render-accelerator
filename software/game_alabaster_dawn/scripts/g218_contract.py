"""Compatibility import for inherited tests; all release authority is G219.

This module must never resolve a G218 stage or inherited aggregate in this copy.
The original G2.18 project is not modified.
"""
from g219_contract import *

if __name__ == '__main__':
    import sys
    aggregate(Path(sys.argv[1]))
    print('G219 functional aggregate PASS; CPU budget 5000us board NOT_VERIFIED')
