#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from gi.repository import Gst, GLib
import gi
gi.require_version("Gst", "1.0")

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
    Build and run a GStreamer pipeline, counting DMA-Buf, GLMemory (GLImage/EGLImage), and system memory buffers
    on BOTH sink (incoming) and src (outgoing) pads of every element.

    Returns:
        {
            "pads": {
                "elem:pad": {
                    "dir": "sink" or "src",
                    "dmabuf": int,
                    "glmem": int,
                    "sysmem": int
                }
            },
            "pipeline_str_effective": str
        }
    """

    def _force_caps(s: str) -> str:
        # best-effort; only touch raw video caps
        return s.replace("video/x-raw", "video/x-raw(memory:DMABuf)")

    desc = _force_caps(pipeline_str) if force_dmabuf_caps else pipeline_str
    pipeline = Gst.parse_launch(desc)

    pads = {}  # key -> {dir, dmabuf, glmem, sysmem}

    def _pad_key(pad: Gst.Pad) -> str:
        parent = pad.get_parent_element()
        return f"{parent.get_name()}:{pad.get_name()}"

    def _buffer_memory_types(buf: Gst.Buffer) -> set:
        types = set()
        if not buf:
            return types
        for i in range(buf.n_memory()):
            mem = buf.peek_memory(i)
            try:
                if mem.is_type("DMABuf"):
                    types.add("DMABuf")
                if mem.is_type("GLMemory"):
                    types.add("GLMemory")
                if mem.is_type("EGLImage"):
                    types.add("EGLImage")
                if mem.is_type("GstMemoryGL"):
                    types.add("GstMemoryGL")
            except Exception:
                continue
        return types

    def _attach_buffer_probe(pad: Gst.Pad, direction: str):
        key = _pad_key(pad)
        pads.setdefault(key, {"dir": direction, "dmabuf": 0, "glmem": 0, "sysmem": 0})

        def _probe(_pad, info):
            buf = info.get_buffer()
            if buf is not None:
                types = _buffer_memory_types(buf)

                if "DMABuf" in types:
                    pads[key]["dmabuf"] += 1
                elif {"GLMemory", "EGLImage", "GstMemoryGL"} & types:
                    pads[key]["glmem"] += 1
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
