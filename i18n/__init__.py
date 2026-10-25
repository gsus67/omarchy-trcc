"""Shared system-language selection and messages for the native plugin."""
from functools import lru_cache
import json
import os
from pathlib import Path


def language():
    value = os.environ.get("LC_ALL") or os.environ.get("LC_MESSAGES") or os.environ.get("LANG") or "en"
    return "es" if value.lower().replace("-", "_").split("_")[0].split(".")[0] == "es" else "en"


@lru_cache(maxsize=1)
def catalog():
    return json.loads(Path(__file__).with_name("messages.json").read_text())


def translate(source, *values):
    result = source if language() == "es" else catalog().get("en", {}).get(source, source)
    for index, value in enumerate(values, 1):
        result = result.replace(f"%{index}", str(value))
    return result
