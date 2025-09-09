
from . import __appinfo__ 
from . import __version__

import sys
import typer

from rich import print
from rich.console import Console
from rich.panel import Panel
from rich.text import Text

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst

app = typer.Typer(help=__appinfo__)
console = Console()

Gst.init(sys.argv)

# ---

def element_supports_dmabuf(element_name: str) -> bool:
    factory = Gst.ElementFactory.find(element_name)
    if not factory:
        print(f"❌ Element '{element_name}' not found")
        return False

    found = False
    for tmpl in factory.get_static_pad_templates() or []:
        caps = tmpl.get_caps()
        if not caps or caps.is_empty():
            continue

        for i in range(caps.get_size()):
            feats = caps.get_features(i)  # Gst.CapsFeatures or None
            if not feats:
                continue

            # Easiest check:
            if feats.contains("memory:DMABuf"):
                s = caps.get_structure(i)
                print(f"✅ {element_name}: pad '{tmpl.name_template}' "
                      f"({tmpl.direction.value_nick}) supports DMABuf "
                      f"via {s.to_string()}")
                found = True
                continue

            # (Optional) explicit iteration:
            for j in range(feats.get_size()):
                if feats.get_nth(j) == "memory:DMABuf":
                    s = caps.get_structure(i)
                    print(f"✅ {element_name}: pad '{tmpl.name_template}' "
                          f"({tmpl.direction.value_nick}) supports DMABuf "
                          f"via {s.to_string()}")
                    found = True
                    break

    if not found:
        print(f"❌ {element_name} has no pads with memory:DMABuf")
    return found

# ---

def print_error(text: str):
        print(f"[red]Error:[/] {text}")


def print_banner():
    banner_text = Text.assemble(
        ("Lazer ", "bold magenta"),
        (__version__, "bold cyan"),
        (" - " + __appinfo__, "green"),
    )
    print(Panel(banner_text, expand=False, border_style="blue"))

# ---

@app.callback(invoke_without_command=True)
def main(ctx: typer.Context):
    print_banner()
    if ctx.invoked_subcommand is None:
        typer.echo(ctx.get_help())

@app.command()
def elements():
    registry = Gst.Registry.get()
    factories = registry.get_feature_list(Gst.ElementFactory)

    for factory in sorted(factories, key=lambda f: f.get_name()):
        print(factory.get_name())

@app.command()
def elementsdmabuf():
    registry = Gst.Registry.get()
    factories = registry.get_feature_list(Gst.ElementFactory)

    for factory in sorted(factories, key=lambda f: f.get_name()):
        element_supports_dmabuf(factory.get_name())

@app.command()
def dmabuf(element: str):
    element_supports_dmabuf(element)

@app.command()
def element(element: str):
    import subprocess
    r = subprocess.run(
        ["gst-inspect-1.0", element],
        capture_output=True,
        text=True,
    )

    if r.returncode != 0:
        msg = r.stderr.strip().splitlines()[0]
        print_error(msg)
        raise typer.Exit(r.returncode)
    console.print(f"[cyan]Info for [bold]{element}[/]:\n[/]")
    typer.echo(r.stdout)


if __name__ == "__main__":
    Gst(None)
    app()


