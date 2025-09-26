PRIMARY_ELEMENTS = [
    # sources (capture)
    "v4l2src",              # camera capture (can negotiate DMABuf with io-mode=dmabuf)
    "v4l2jpegdec",          # hardware JPEG decoder

    # processing / filters
    "glupload",             # upload buffers into GL textures
    "glcolorconvert",       # color space conversion on GPU
    "glshader",             # apply custom GLSL shaders
    "gldownload",           # pull buffers back out of GL as dmabuf

    # sinks (display/output)
    "kmssink",              # direct DRM/KMS display sink
    "waylandsink",          # Wayland compositor sink

    "videotestsrc",
    "videoconvert",
    "mpegtsmux",
]

