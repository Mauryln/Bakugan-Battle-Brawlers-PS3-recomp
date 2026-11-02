"""rdoc_dump.py -- dump a RenderDoc capture per draw: RTs, textures, constant blocks.

Runs inside qrenderdoc (the only host that exposes the renderdoc module):

    RDOC_CAPTURE=diag.rdc RDOC_OUT=out_dir qrenderdoc.exe --python tools/rdoc_dump.py

Writes out_dir/index.json, out_dir/tex/<id>.png and out_dir/cb/<event>_<stage>_<i>.bin.
Textures and constant blocks are comparable across APIs, so the same dump from
the recomp (D3D12) and from RPCS3 (Vulkan) can be diffed with no normalisation
beyond the resource ids.

The script must run before the UI (qrenderdoc --python), so it ends by writing
out_dir/done.txt; the caller kills qrenderdoc afterwards.
"""
import json
import os

import renderdoc as rd

cap_path = os.environ["RDOC_CAPTURE"]
out_dir = os.environ["RDOC_OUT"]
tex_dir = os.path.join(out_dir, "tex")
cb_dir = os.path.join(out_dir, "cb")
for d in (out_dir, tex_dir, cb_dir):
    os.makedirs(d, exist_ok=True)

done = os.path.join(out_dir, "done.txt")
error = os.path.join(out_dir, "error.txt")


def walk(actions, out):
    for a in actions:
        out.append(a)
        walk(a.children, out)
    return out


def tex_info(textures, rid):
    if rid == rd.ResourceId.Null():
        return None
    t = textures.get(str(rid))
    if t is None:
        return None
    return {
        "id": str(rid),
        "width": t.width,
        "height": t.height,
        "depth": t.depth,
        "mips": t.mips,
        "format": str(t.format.Name()),
        "type": str(t.type),
    }


def floats(raw, count=256):
    """First `count` floats of a constant block, for cheap diffing."""
    import struct
    n = min(count, len(raw) // 4)
    return [round(v, 6) for v in struct.unpack_from("<%df" % n, raw, 0)]


def main():
    cap = rd.OpenCaptureFile()
    res = cap.OpenFile(cap_path, "", None)
    if not res.OK():
        raise RuntimeError("OpenFile: %s" % res.Message())

    res, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
    if not res.OK():
        raise RuntimeError("OpenCapture: %s" % res.Message())

    api = ctrl.GetAPIProperties()

    # Targeted mode: RDOC_SAVE_EVENTS="ev,ev,..." saves the render targets bound
    # at those events and exits (~seconds) instead of walking the whole frame.
    only = os.environ.get("RDOC_SAVE_EVENTS", "")
    if only:
        textures = {str(t.resourceId): t for t in ctrl.GetTextures()}
        saved = set()
        for ev in [int(x) for x in only.split(",") if x.strip()]:
            ctrl.SetFrameEvent(ev, False)
            state = ctrl.GetPipelineState()
            for d in state.GetOutputTargets():
                if d.resource != rd.ResourceId.Null():  # Vulkan reports unused slots
                    save_tex(ctrl, d.resource, saved, force=True)
            depth = state.GetDepthTarget()
            if depth.resource != rd.ResourceId.Null():
                save_tex(ctrl, depth.resource, saved, force=True)
        ctrl.Shutdown()
        with open(done, "w") as f:
            f.write("saved RTs at %d events\n" % len([x for x in only.split(",") if x.strip()]))
        return

    index = {
        "capture": os.path.basename(cap_path),
        "api": str(api.pipelineType),
        "draws": [],
    }

    textures = {str(t.resourceId): t for t in ctrl.GetTextures()}
    saved_tex = set()
    rt_last = {}
    cb_cache = {}
    progress = os.path.join(out_dir, "progress.txt")

    # GetStructuredFile() is expensive; calling it per draw made the dump
    # quadratic on a 1700-draw frame (1 draw/s instead of ~200).
    sdf = ctrl.GetStructuredFile()
    actions = [a for a in walk(ctrl.GetRootActions(), []) if a.flags & rd.ActionFlags.Drawcall]

    for i, act in enumerate(actions):
        if i % 50 == 0:
            with open(progress, "w") as f:
                f.write("%d/%d\n" % (i, len(actions)))
        ctrl.SetFrameEvent(act.eventId, False)
        state = ctrl.GetPipelineState()

        vp = state.GetViewport(0)
        entry = {
            "event": act.eventId,
            "name": act.GetName(sdf),
            "indices": act.numIndices,
            "instances": act.numInstances,
            "topology": str(state.GetPrimitiveTopology()),
            "viewport": [vp.x, vp.y, vp.width, vp.height],
            "rts": [],
            "depth": None,
            "textures": [],
            "constant_blocks": [],
        }

        for i, d in enumerate(state.GetOutputTargets()):
            info = tex_info(textures, d.resource)
            if info:
                info["slot"] = i
                entry["rts"].append(info)
                # an RT is stale until the pass that writes it finishes, so the
                # content is saved at its last writer once the loop is done
                rt_last[str(d.resource)] = (act.eventId, d.resource)
        depth = state.GetDepthTarget()
        if depth.resource != rd.ResourceId.Null():
            entry["depth"] = tex_info(textures, depth.resource)

        for stage in (rd.ShaderStage.Vertex, rd.ShaderStage.Pixel):
            for used in state.GetReadOnlyResources(stage, True):
                d = used.descriptor
                if d.resource == rd.ResourceId.Null():
                    continue
                if d.type in (rd.DescriptorType.Image, rd.DescriptorType.ImageSampler,
                              rd.DescriptorType.Buffer,
                              rd.DescriptorType.TypedBuffer):
                    info = tex_info(textures, d.resource)
                    if info:
                        info.update({"stage": str(stage), "slot": used.access.index})
                        entry["textures"].append(info)
                        if d.type in (rd.DescriptorType.Image, rd.DescriptorType.ImageSampler):
                            save_tex(ctrl, d.resource, saved_tex)

            for used in state.GetConstantBlocks(stage, True):
                d = used.descriptor
                if d.resource == rd.ResourceId.Null():
                    continue
                # the bound view can span megabytes (whole arena blocks); the
                # matrices we compare live at the start of the block
                size = min(d.byteSize or 0, 65536)
                key = (str(d.resource), d.byteOffset, size)
                cached = cb_cache.get(key)
                if cached is None:
                    name = "%d_%s_%d.bin" % (act.eventId, str(stage).split(".")[-1],
                                             used.access.index)
                    raw = ctrl.GetBufferData(d.resource, d.byteOffset, size)
                    with open(os.path.join(cb_dir, name), "wb") as f:
                        f.write(raw)
                    cached = (name, len(raw), floats(raw))
                    cb_cache[key] = cached
                entry["constant_blocks"].append({
                    "stage": str(stage),
                    "index": used.access.index,
                    "file": cached[0],
                    "bytes": cached[1],
                    "floats": cached[2],
                })

        index["draws"].append(entry)

    # Render targets only hold the pass result after its last writer runs.
    for key, (ev, rid) in rt_last.items():
        ctrl.SetFrameEvent(ev, False)
        save_tex(ctrl, rid, saved_tex, force=True)

    with open(os.path.join(out_dir, "index.json"), "w") as f:
        json.dump(index, f, indent=1)

    ctrl.Shutdown()
    with open(done, "w") as f:
        f.write("%d draws, %d textures\n" % (len(index["draws"]), len(saved_tex)))


def save_tex(ctrl, rid, saved, force=False):
    key = str(rid)
    if key in saved and not force:
        return
    saved.add(key)
    ts = rd.TextureSave()
    ts.resourceId = rid
    ts.destType = rd.FileType.PNG
    # ':' is illegal in Windows paths and ResourceId str() is "ResourceId::282"
    res = ctrl.SaveTexture(ts, os.path.join(tex_dir, key.replace(":", "_") + ".png"))
    if not res.OK():
        raise RuntimeError("SaveTexture %s: %s" % (key, res.Message()))


try:
    main()
except Exception as e:  # noqa: BLE001 - the host has no console, report to disk
    import traceback
    with open(error, "w") as f:
        f.write(traceback.format_exc())
