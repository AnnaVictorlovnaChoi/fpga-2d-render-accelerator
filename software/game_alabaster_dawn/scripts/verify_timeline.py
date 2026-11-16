"""Standalone G2.5 authoritative timing verification."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).resolve().parents[1]/'tests/verify_authoritative.py'),run_name='__main__')
