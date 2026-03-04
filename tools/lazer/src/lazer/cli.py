################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################


from shutil import get_terminal_size
from math import ceil
from rich.console import Console
from rich.table import Table
from gi.repository import Gst
from .elements import PRIMARY_ELEMENTS

from .pipelinecheck import run_gst_dmabuf_audit_io

# ---------------------------

import sys
import questionary
from prompt_toolkit.styles import Style

import gi
gi.require_version("Gst", "1.0")
Gst.init(sys.argv)

# ---------------------------


console = Console()

# ---------------------------


def list_all_elements():
    registry = Gst.Registry.get()
    factories = registry.get_feature_list(Gst.ElementFactory)

    for factory in sorted(factories, key=lambda f: f.get_name()):
        print(factory.get_name(), end=" ")
    print()


console = Console()


def element_has_static_support_dmabuf(element_name: str) -> bool:
    factory = Gst.ElementFactory.find(element_name)
    if not factory:
        return False

    for tmpl in factory.get_static_pad_templates() or []:
        caps = tmpl.get_caps()
        if not caps or caps.is_empty():
            continue
        for i in range(caps.get_size()):
            feats = caps.get_features(i)
            if feats and feats.contains("memory:DMABuf"):
                return True
            if feats:
                for j in range(feats.get_size()):
                    if feats.get_nth(j) == "memory:DMABuf":
                        return True
    return False


def list_dmabuf_elements_table() -> list[str]:
    registry = Gst.Registry.get()
    factories = registry.get_feature_list(Gst.ElementFactory)
    names = sorted(f.get_name() for f in factories if element_has_static_support_dmabuf(f.get_name()))

    if not names:
        console.print("[red]No elements advertise memory:DMABuf[/]")
        return []

    # terminal width and rough column sizing
    term_width = get_terminal_size((80, 20)).columns
    max_name_len = max(len(n) for n in names) + 2  # padding
    ncols = max(1, term_width // max_name_len)
    nrows = ceil(len(names) / ncols)

    table = Table(title="Elements with DMABuf support", show_header=False, box=None, pad_edge=False)
    for _ in range(ncols):
        table.add_column(justify="left", no_wrap=True)

    # fill table row by row
    for r in range(nrows):
        row = []
        for c in range(ncols):
            idx = r + c * nrows
            row.append(names[idx] if idx < len(names) else "")
        table.add_row(*row)

    console.print(table)
    return names


def list_primary_elements() -> list[str]:
    table = Table(title="Primary elements and DMABuf support")
    table.add_column("Element", style="cyan")
    table.add_column("DMABuf?", style="bold")

    supported = []
    for name in PRIMARY_ELEMENTS:
        if element_has_static_support_dmabuf(name):
            table.add_row(name, "[green]Yes[/]")
            supported.append(name)
        else:
            table.add_row(name, "[red]No[/]")

    console.print(table)
    return supported


def main():
    # Style overrides
    custom_style = Style([
        ("qmark", "fg:#ff0000"),
        ("pointer", "fg:#000000"),  # the little "›" pointer hidden
        ("selected", "reverse"),    # inverse bar for active choice
        ("highlighted", "reverse"),  # also inverse for search match
    ])

    while True:
        choice = questionary.select(
            "LAZER MENU",
            choices=[
                "1. List all elements",
                "2. List DMA-BUF elements",
                "3. List primary elements",
                "4. Pipeline check with Intel iGPU",
                "Quit",
            ],
            style=custom_style,
        ).ask()

        if choice == "Quit":
            print("Exiting..")
            break

        if choice.startswith("1."):
            list_all_elements()

        if choice.startswith("2."):
            list_dmabuf_elements_table()

        if choice.startswith("3."):
            list_primary_elements()

        if choice.startswith("4."):
            pipeline = (
                "videotestsrc is-live=true ! "
                "video/x-raw,format=NV12,width=1280,height=720,framerate=30/1 ! "
                "videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast ! "
                "mpegtsmux ! "
                "udpsink host=127.0.0.1 port=5000 sync=false async=false"
            )
            summary = run_gst_dmabuf_audit_io(pipeline, force_dmabuf_caps=False, run_seconds=3)
            # from pprint import pprint
            console.print(summary)
