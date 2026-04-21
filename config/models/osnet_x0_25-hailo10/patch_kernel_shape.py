################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

import onnx
from onnx import helper

inp = "osnet_x0_25_msmt17_opset21.onnx"
out = "osnet_x0_25_msmt17_opset21_patched.onnx"

model = onnx.load(inp)
init_map = {init.name: init for init in model.graph.initializer}

patched = 0
missing = []

for i, node in enumerate(model.graph.node):
    if node.op_type != "Conv":
        continue

    has_kernel = any(attr.name == "kernel_shape" for attr in node.attribute)
    if has_kernel:
        continue

    if len(node.input) < 2:
        missing.append((i, node.name, "no_weight_input"))
        continue

    w_name = node.input[1]
    w = init_map.get(w_name)
    if w is None:
        missing.append((i, node.name, f"weight_not_initializer:{w_name}"))
        continue

    dims = list(w.dims)
    if len(dims) < 3:
        missing.append((i, node.name, f"bad_weight_dims:{dims}"))
        continue

    kernel_shape = dims[2:]
    node.attribute.append(helper.make_attribute("kernel_shape", kernel_shape))
    patched += 1

onnx.save(model, out)
print("patched conv nodes:", patched)
print("unpatched cases:", missing[:20])
print("saved:", out)
