// LUA Visuals Metal renderer: scene (generated MSL) or sand particles -> Lua canvas overlay -> feedback composite -> bloom -> final grade.
// Port of the web VizRenderer. All GPU work is on the UI thread; nothing here touches the audio thread.
import Metal
import MetalKit
import simd

struct VizQuality { let id: String; let label: String; let scale: Float; let q: Int; let sand: Int; let bloomDiv: Int; let aa: Bool
    static let table: [VizQuality] = [
        .init(id: "performance", label: "Performance", scale: 0.5, q: 0, sand: 200, bloomDiv: 4, aa: false),
        .init(id: "balanced", label: "Balanced", scale: 0.75, q: 1, sand: 320, bloomDiv: 4, aa: false),
        .init(id: "high", label: "High", scale: 1.0, q: 2, sand: 448, bloomDiv: 2, aa: true),
        .init(id: "ultra", label: "Ultra", scale: 1.35, q: 3, sand: 576, bloomDiv: 2, aa: true)] }

struct VizFrameInput {
    var shader: String?            // scene program name, nil = black
    var K: [Float]                 // 56-float uniform block (see VizScenes.metal)
    var post: [Float]              // 12 post slots
    var cmds: UnsafePointer<Float>?; var ncmd: Int
    var scriptW: Float, scriptH: Float
    var time: Float, dt: Float
    var aa: Bool
    var sand: (n: Int, size: Float, density: Float)?
}

final class VizMetalRenderer {
    let device: MTLDevice; let queue: MTLCommandQueue; let library: MTLLibrary
    private var pso: [String: MTLRenderPipelineState] = [:]
    private(set) var W = 0, H = 0, bloomDiv = 4
    private var scene: MTLTexture?, over: MTLTexture?, acc: [MTLTexture] = [], b0: MTLTexture?, b1: MTLTexture?, c0: MTLTexture?, c1: MTLTexture?
    private var accIdx = 0
    private var sandA: MTLTexture?, sandB: MTLTexture?, sandFlip = false, sandN = 0
    private var inst: MTLBuffer; private let instCap = 4096
    private var drawableFormat: MTLPixelFormat = .bgra8Unorm
    var lastError: String?
    let hdrFormat: MTLPixelFormat = .rgba16Float

    init?(device: MTLDevice? = MTLCreateSystemDefaultDevice()) {
        guard let d = device, let q = d.makeCommandQueue(), let lib = d.makeDefaultLibrary() else { return nil }
        self.device = d; queue = q; library = lib
        guard let b = d.makeBuffer(length: 4096 * 48, options: .storageModeShared) else { return nil }; inst = b
    }
    func setDrawableFormat(_ f: MTLPixelFormat) { if f != drawableFormat { drawableFormat = f; pso["final"] = nil } }

    // ---- pipelines
    private func pipe(_ key: String, vs: String, fs: String, format: MTLPixelFormat, blend: Int = 0) -> MTLRenderPipelineState? {
        if let p = pso[key] { return p }
        guard let v = library.makeFunction(name: vs), let f = library.makeFunction(name: fs) else { lastError = "missing shader function \(fs)"; return nil }
        let d = MTLRenderPipelineDescriptor(); d.vertexFunction = v; d.fragmentFunction = f; d.colorAttachments[0].pixelFormat = format
        let c = d.colorAttachments[0]!
        if blend == 1 { c.isBlendingEnabled = true; c.rgbBlendOperation = .add; c.alphaBlendOperation = .add; c.sourceRGBBlendFactor = .one; c.sourceAlphaBlendFactor = .one; c.destinationRGBBlendFactor = .oneMinusSourceAlpha; c.destinationAlphaBlendFactor = .oneMinusSourceAlpha }
        if blend == 2 { c.isBlendingEnabled = true; c.rgbBlendOperation = .add; c.alphaBlendOperation = .add; c.sourceRGBBlendFactor = .sourceAlpha; c.sourceAlphaBlendFactor = .sourceAlpha; c.destinationRGBBlendFactor = .oneMinusSourceAlpha; c.destinationAlphaBlendFactor = .oneMinusSourceAlpha }
        do { let p = try device.makeRenderPipelineState(descriptor: d); pso[key] = p; return p } catch { lastError = "pipeline \(key): \(error)"; return nil }
    }
    func hasScene(_ name: String) -> Bool { library.functionNames.contains("scene_" + name) }

    // ---- targets
    private func tex(_ w: Int, _ h: Int, _ f: MTLPixelFormat) -> MTLTexture? {
        let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: f, width: max(4, w), height: max(4, h), mipmapped: false); d.usage = [.renderTarget, .shaderRead]; d.storageMode = .private
        return device.makeTexture(descriptor: d)
    }
    func resize(w: Int, h: Int, bloomDiv bd: Int) {
        let w = max(16, w), h = max(16, h); if w == W && h == H && bd == bloomDiv && scene != nil { return }
        W = w; H = h; bloomDiv = bd; scene = tex(w, h, hdrFormat); over = tex(w, h, .rgba8Unorm); acc = [tex(w, h, hdrFormat)!, tex(w, h, hdrFormat)!]
        let bw = max(8, w / bd), bh = max(8, h / bd), cw = max(4, bw / 2), ch = max(4, bh / 2)
        b0 = tex(bw, bh, hdrFormat); b1 = tex(bw, bh, hdrFormat); c0 = tex(cw, ch, hdrFormat); c1 = tex(cw, ch, hdrFormat); accIdx = 0; wipe = true
    }
    private var wipe = true
    private func ensureSand(_ n: Int) { if sandN == n && sandA != nil { return }; sandN = n; sandA = tex(n, n, .rgba32Float); sandB = tex(n, n, .rgba32Float); sandFlip = false; sandNeedsClear = true }
    private var sandNeedsClear = false
    func resetSand() { sandNeedsClear = true }

    private func pass(_ cb: MTLCommandBuffer, _ target: MTLTexture, clear: Bool = false, load: Bool = false, _ body: (MTLRenderCommandEncoder) -> Void) {
        let rp = MTLRenderPassDescriptor(); rp.colorAttachments[0].texture = target; rp.colorAttachments[0].storeAction = .store
        rp.colorAttachments[0].loadAction = (load && !clear) ? .load : .clear; rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, clear ? 0 : 0)
        guard let e = cb.makeRenderCommandEncoder(descriptor: rp) else { return }; body(e); e.endEncoding()
    }
    private func quad(_ e: MTLRenderCommandEncoder) { e.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4) }

    /// Encode one frame into the drawable. Returns false if a pipeline failed (the caller keeps showing the previous frame).
    @discardableResult
    func render(_ f: VizFrameInput, into drawable: CAMetalDrawable) -> Bool {
        guard let scene = scene, let over = over, let b0 = b0, let b1 = b1, let c0 = c0, let c1 = c1, acc.count == 2, let cb = queue.makeCommandBuffer() else { return false }
        var K = f.K; if K.count < 56 { K += [Float](repeating: 0, count: 56 - K.count) }
        K[0] = Float(W); K[1] = Float(H); K[2] = f.time; K[4] = f.dt
        if wipe { for t in [over, acc[0], acc[1]] { pass(cb, t, clear: true) { _ in } }; wipe = false }
        // 1. scene
        if let name = f.shader, hasScene(name), let p = pipe("scene:" + name, vs: "quad_vs", fs: "scene_" + name, format: hdrFormat) {
            pass(cb, scene) { e in e.setRenderPipelineState(p); K.withUnsafeBytes { e.setFragmentBytes($0.baseAddress!, length: 56 * 4, index: 0) }; self.quad(e) }
        } else { pass(cb, scene) { _ in } }
        // 1b. sand
        if let s = f.sand, name(f) == "sand_plate" {
            ensureSand(s.n); guard let a = sandA, let b = sandB else { return false }
            if sandNeedsClear { pass(cb, a, clear: true) { _ in }; pass(cb, b, clear: true) { _ in }; sandNeedsClear = false }
            let steps = Int(K[3]) >= 2 ? 2 : 1
            for i in 0..<steps {
                guard let sim = pipe("sandsim", vs: "quad_vs", fs: "sand_sim", format: .rgba32Float) else { return false }
                var Ks = K; Ks[2] = f.time + Float(i) * 0.37; Ks[4] = min(f.dt, 1.0 / 20.0) / Float(steps); Ks[5] = Float(s.n); Ks[6] = Float(s.n); Ks[0] = Float(s.n); Ks[1] = Float(s.n)
                let src = sandFlip ? b : a, dst = sandFlip ? a : b
                pass(cb, dst) { e in e.setRenderPipelineState(sim); e.setFragmentTexture(src, index: 0); Ks.withUnsafeBytes { e.setFragmentBytes($0.baseAddress!, length: 56 * 4, index: 0) }; self.quad(e) }
                sandFlip.toggle()
            }
            let cur = sandFlip ? b : a
            if let dp = pipe("sanddraw", vs: "sand_vs", fs: "sand_fs", format: hdrFormat, blend: 2) {
                var Kd = K; Kd[5] = Float(s.n); Kd[6] = Float(s.n); Kd[7] = max(1, s.size * Float(H) / 760)
                let count = max(1000, min(s.n * s.n, Int(Float(W * H) * 0.4 * s.density)))
                pass(cb, scene, load: true) { e in e.setRenderPipelineState(dp); e.setVertexTexture(cur, index: 0); Kd.withUnsafeBytes { e.setVertexBytes($0.baseAddress!, length: 56 * 4, index: 0); e.setFragmentBytes($0.baseAddress!, length: 56 * 4, index: 0) }; e.drawPrimitives(type: .point, vertexStart: 0, vertexCount: count) }
            }
        }
        // 2. Lua canvas overlay
        if f.ncmd > 0, let cm = f.cmds { drawCmds(cb, over, cm, f.ncmd, f.scriptW, f.scriptH) }
        // 3. composite + feedback
        let prev = acc[accIdx], next = acc[1 - accIdx]; let post = f.post
        guard let comp = pipe("comp", vs: "quad_vs", fs: "comp_fs", format: hdrFormat) else { return false }
        var cp: [Float] = [max(0, min(0.985, post[2])), post[3] == 0 ? 1 : post[3], post[4], post[5], f.time, Float(W) / Float(H)]
        pass(cb, next) { e in e.setRenderPipelineState(comp); e.setFragmentTexture(scene, index: 0); e.setFragmentTexture(over, index: 1); e.setFragmentTexture(prev, index: 2); cp.withUnsafeBytes { e.setFragmentBytes($0.baseAddress!, length: 24, index: 0) }; self.quad(e) }
        accIdx = 1 - accIdx
        // 4. bloom
        let bloomOn = post[0] > 0.004
        if bloomOn, let br = pipe("bright", vs: "quad_vs", fs: "bright_fs", format: hdrFormat), let bl = pipe("blur", vs: "quad_vs", fs: "blur_fs", format: hdrFormat) {
            var bp: [Float] = [1 / Float(W), 1 / Float(H), 0.55, 0]
            pass(cb, b0) { e in e.setRenderPipelineState(br); e.setFragmentTexture(next, index: 0); bp.withUnsafeBytes { e.setFragmentBytes($0.baseAddress!, length: 16, index: 0) }; self.quad(e) }
            func blur(_ src: MTLTexture, _ tmp: MTLTexture, _ dst: MTLTexture, _ w: Int, _ h: Int, _ scale: Float = 1) {
                var d1: [Float] = [scale / Float(w), 0, 0, 0], d2: [Float] = [0, 1 / Float(h), 0, 0]
                pass(cb, tmp) { e in e.setRenderPipelineState(bl); e.setFragmentTexture(src, index: 0); d1.withUnsafeBytes { e.setFragmentBytes($0.baseAddress!, length: 16, index: 0) }; self.quad(e) }
                pass(cb, dst) { e in e.setRenderPipelineState(bl); e.setFragmentTexture(tmp, index: 0); d2.withUnsafeBytes { e.setFragmentBytes($0.baseAddress!, length: 16, index: 0) }; self.quad(e) }
            }
            blur(b0, b1, b0, b0.width, b0.height)
            var d3: [Float] = [1.5 / Float(b0.width), 0, 0, 0]
            pass(cb, c0) { e in e.setRenderPipelineState(bl); e.setFragmentTexture(b0, index: 0); d3.withUnsafeBytes { e.setFragmentBytes($0.baseAddress!, length: 16, index: 0) }; self.quad(e) }
            blur(c0, c1, c0, c0.width, c0.height); blur(c0, c1, c0, c0.width, c0.height)
        }
        // 5. final grade to the drawable
        guard let fin = pipe("final", vs: "quad_vs", fs: "final_fs", format: drawable.texture.pixelFormat) else { return false }
        var fp: [Float] = [1 / Float(W), 1 / Float(H), f.time, bloomOn ? post[0] : 0, post[1], post[5], post[6], post[7], post[8], post[9], post[10], post[11], f.aa ? 1 : 0, 0, Float(drawable.texture.width), Float(drawable.texture.height)]
        let rp = MTLRenderPassDescriptor(); rp.colorAttachments[0].texture = drawable.texture; rp.colorAttachments[0].loadAction = .dontCare; rp.colorAttachments[0].storeAction = .store
        if let e = cb.makeRenderCommandEncoder(descriptor: rp) { e.setRenderPipelineState(fin); e.setFragmentTexture(next, index: 0); e.setFragmentTexture(bloomOn ? b0 : next, index: 1); e.setFragmentTexture(bloomOn ? c0 : next, index: 2); fp.withUnsafeBytes { e.setFragmentBytes($0.baseAddress!, length: 64, index: 0) }; quad(e); e.endEncoding() }
        cb.present(drawable); cb.commit(); return true
    }
    private func name(_ f: VizFrameInput) -> String { f.shader ?? "" }

    // Lua canvas command list -> instanced quads into the persistent overlay texture
    private func drawCmds(_ cb: MTLCommandBuffer, _ target: MTLTexture, _ d: UnsafePointer<Float>, _ n: Int, _ sw: Float, _ sh: Float) {
        guard let p = pipe("batch", vs: "batch_vs", fs: "batch_fs", format: .rgba8Unorm, blend: 1) else { return }
        let sx = Float(W) / max(1, sw), sy = Float(H) / max(1, sh), sm = (sx + sy) * 0.5
        var r: Float = 0, g: Float = 0, b: Float = 0, a: Float = 1, add: Float = 0
        // gather into batches (and honour op 10 = wipe by flushing then clearing)
        struct Seg { var start: Int; var count: Int; var clear: Bool }
        var segs: [Seg] = []; var cnt = 0; let ptr = inst.contents().bindMemory(to: Float.self, capacity: instCap * 12); var segStart = 0
        func put(_ ax: Float, _ ay: Float, _ az: Float, _ aw: Float, _ bx: Float, _ by: Float, _ ty: Float) {
            if cnt >= instCap { return }                      // fixed buffer: extra shapes in one frame are dropped (the script cap is 14,000; typical scripts stay far below)
            let o = cnt * 12; ptr[o] = ax; ptr[o + 1] = ay; ptr[o + 2] = az; ptr[o + 3] = aw; ptr[o + 4] = bx; ptr[o + 5] = by; ptr[o + 6] = add; ptr[o + 7] = ty; ptr[o + 8] = r; ptr[o + 9] = g; ptr[o + 10] = b; ptr[o + 11] = a; cnt += 1 }
        var pendingClear = false
        for i in 0..<n {
            let o = i * 8; let op = Int(d[o])
            switch op {
            case 1: let sr = r, sg = g, sb = b, sa = a, sadd = add; a = d[o + 4]; r = d[o + 1]; g = d[o + 2]; b = d[o + 3]; add = 0; put(0, 0, Float(W), Float(H), 0, 0, 3); r = sr; g = sg; b = sb; a = sa; add = sadd
            case 2: r = d[o + 1]; g = d[o + 2]; b = d[o + 3]; a = d[o + 4]
            case 3: add = d[o + 1] > 0.5 ? 1 : 0
            case 4: put(d[o + 1] * sx, d[o + 2] * sy, d[o + 3] * sx, d[o + 4] * sy, max(0.5, d[o + 5] * sm), 0, 0)
            case 5: put(d[o + 1] * sx, d[o + 2] * sy, d[o + 3] * sm, 0, 0, 0, 1)
            case 6: put(d[o + 1] * sx, d[o + 2] * sy, d[o + 3] * sm, max(0.5, d[o + 4] * sm), 0, 0, 2)
            case 7: put(d[o + 1] * sx, d[o + 2] * sy, d[o + 3] * sx, d[o + 4] * sy, 0, 0, 3)
            case 8: put(d[o + 1] * sx, d[o + 2] * sy, d[o + 3] * sm, 0, 0, 0, 4)
            case 9: put(d[o + 1] * sx, d[o + 2] * sy, d[o + 3] * sx, d[o + 4] * sy, d[o + 5] * sx, d[o + 6] * sy, 5)
            case 10: if cnt > segStart { segs.append(Seg(start: segStart, count: cnt - segStart, clear: pendingClear)); pendingClear = false; segStart = cnt }; pendingClear = true
            default: break
            }
        }
        if cnt > segStart || pendingClear { segs.append(Seg(start: segStart, count: cnt - segStart, clear: pendingClear)) }
        var res: [Float] = [Float(W), Float(H)]
        // one render pass per clear boundary; shapes keep their order
        for seg in segs {
            let rp = MTLRenderPassDescriptor(); rp.colorAttachments[0].texture = target; rp.colorAttachments[0].storeAction = .store; rp.colorAttachments[0].loadAction = seg.clear ? .clear : .load; rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0)
            guard let e = cb.makeRenderCommandEncoder(descriptor: rp) else { continue }
            if seg.count > 0 { e.setRenderPipelineState(p); e.setVertexBuffer(inst, offset: seg.start * 48, index: 0); res.withUnsafeBytes { e.setVertexBytes($0.baseAddress!, length: 8, index: 1) }; e.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4, instanceCount: seg.count) }
            e.endEncoding()
        }
    }
}
