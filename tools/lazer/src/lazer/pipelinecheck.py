#!/usr/bin/env python3
import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst, GLib

# Initialize GStreamer once
if not Gst.is_initialized():
    Gst.init(None)

def run_gst_dmabuf_audit_io(
    pipeline_str: str,
    *,
    force_dmabuf_caps: bool = False,
    run_seconds: int = 5
):
    """
    Build and run a GStreamer pipeline, counting DMA-Buf vs non-DMA-Buf buffers
    on BOTH sink (incoming) and src (outgoing) pads of every element.

    Returns:
        {
          "pads": {
            "elem:pad": {"dir":"sink|src", "dmabuf":int, "sysmem":int}
          },
          "pipeline_str_effective": str
        }
    """

    def _force_caps(s: str) -> str:
        # best-effort; only touch raw video caps
        return s.replace("video/x-raw", "video/x-raw(memory:DMABuf)")

    desc = _force_caps(pipeline_str) if force_dmabuf_caps else pipeline_str
    pipeline = Gst.parse_launch(desc)

    pads = {}  # key -> {dir, dmabuf, sysmem}

    def _pad_key(pad: Gst.Pad) -> str:
        parent = pad.get_parent_element()
        return f"{parent.get_name()}:{pad.get_name()}"

    def _buffer_has_dmabuf(buf: Gst.Buffer) -> bool:
        if not buf:
            return False
        n = buf.n_memory()
        for i in range(n):
            mem = buf.peek_memory(i)
            # GI exposes gst_memory_is_type() as Memory.is_type("DMABuf")
            try:
                if mem.is_type("DMABuf"):
                    return True
            except Exception:
                pass
        return False

    def _attach_buffer_probe(pad: Gst.Pad, direction: str):
        key = _pad_key(pad)
        pads.setdefault(key, {"dir": direction, "dmabuf": 0, "sysmem": 0})

        def _probe(_pad, info):
            buf = info.get_buffer()
            if buf is not None:
                if _buffer_has_dmabuf(buf):
                    pads[key]["dmabuf"] += 1
                else:
                    pads[key]["sysmem"] += 1
            return Gst.PadProbeReturn.OK

        pad.add_probe(Gst.PadProbeType.BUFFER, _probe)

    # Iterate elements/pads and attach probes to ALL sink/src pads
    it_elems = pipeline.iterate_elements()
    while True:
        res, elem = it_elems.next()
        if res != Gst.IteratorResult.OK:
            break

        it_pads = elem.iterate_pads()
        while True:
            r2, pad = it_pads.next()
            if r2 != Gst.IteratorResult.OK:
                break
            if pad.get_direction() == Gst.PadDirection.SINK:
                _attach_buffer_probe(pad, "sink")
            elif pad.get_direction() == Gst.PadDirection.SRC:
                _attach_buffer_probe(pad, "src")

    # Run the pipeline for N seconds
    loop = GLib.MainLoop()

    def _stop():
        pipeline.set_state(Gst.State.NULL)
        if loop.is_running():
            loop.quit()
        return False

    pipeline.set_state(Gst.State.PLAYING)
    GLib.timeout_add_seconds(max(1, int(run_seconds)), _stop)
    try:
        loop.run()
    finally:
        pipeline.set_state(Gst.State.NULL)

    return {
        "pads": pads,
        "pipeline_str_effective": desc,
    }