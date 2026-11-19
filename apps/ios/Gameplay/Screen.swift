import MetalKit
import SwiftUI

struct GuestFrame {
    let data: Data
    let width: Int
    let height: Int
    let serial: UInt64
}

struct Screen: UIViewRepresentable {
    var frame: GuestFrame?
    var scale: Double

    final class Renderer: NSObject, MTKViewDelegate {
        var frame: GuestFrame?
        var queue: MTLCommandQueue?
        var pipeline: MTLRenderPipelineState?
        // Pooled textures stay immutable while the GPU samples them. MTKView draws on demand
        // on the main thread; the completion callback returns the slot on the same thread.
        private struct Slot {
            var texture: MTLTexture?
            var serial: UInt64?
            var busy = false
        }
        private var slots = Array(repeating: Slot(), count: 3)
        private var waiting = false

        func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) { view.setNeedsDisplay() }
        func draw(in view: MTKView) {
            guard let f = frame, let device = view.device, let queue, let pipeline else { return }
            guard let index = slots.firstIndex(where: { !$0.busy && $0.serial == f.serial })
                    ?? slots.firstIndex(where: { !$0.busy }) else {
                waiting = true
                return
            }
            guard
                  let drawable = view.currentDrawable, let descriptor = view.currentRenderPassDescriptor,
                  let command = queue.makeCommandBuffer() else { return }
            if slots[index].texture?.width != f.width || slots[index].texture?.height != f.height {
                let desc = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: f.width, height: f.height, mipmapped: false)
                desc.usage = .shaderRead
                slots[index].texture = device.makeTexture(descriptor: desc)
                slots[index].serial = nil
            }
            guard let texture = slots[index].texture else { return }
            if slots[index].serial != f.serial {
                f.data.withUnsafeBytes { bytes in
                    texture.replace(region: MTLRegionMake2D(0, 0, f.width, f.height), mipmapLevel: 0,
                                    withBytes: bytes.baseAddress!, bytesPerRow: f.width * 4)
                }
                slots[index].serial = f.serial
            }
            guard let encoder = command.makeRenderCommandEncoder(descriptor: descriptor) else { return }
            encoder.setRenderPipelineState(pipeline)
            encoder.setFragmentTexture(texture, index: 0)
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
            encoder.endEncoding()
            command.present(drawable)
            slots[index].busy = true
            waiting = false
            command.addCompletedHandler { [weak self, weak view] buffer in
                let message = buffer.status == .completed ? "Frame Metal" : "Errore Metal"
                DispatchQueue.main.async {
                    guard let self else { return }
                    self.slots[index].busy = false
                    if self.waiting { view?.setNeedsDisplay() }
                    view?.accessibilityValue = message
                }
            }
            command.commit()
        }
    }
    func makeCoordinator() -> Renderer { Renderer() }
    func makeUIView(context: Context) -> MTKView {
        let device = MTLCreateSystemDefaultDevice()
        let view = MTKView(frame: .zero, device: device)
        view.isPaused = true
        view.enableSetNeedsDisplay = true
        view.autoResizeDrawable = false
        view.isAccessibilityElement = true
        view.accessibilityLabel = "Schermo emulato"
        view.accessibilityIdentifier = "guest-screen"
        view.delegate = context.coordinator
        let r = context.coordinator
        r.queue = device?.makeCommandQueue()
        let d = MTLRenderPipelineDescriptor()
        let library = device?.makeDefaultLibrary()
        d.vertexFunction = library?.makeFunction(name: "scanoutVertex")
        d.fragmentFunction = library?.makeFunction(name: "scanoutPixel")
        d.colorAttachments[0].pixelFormat = view.colorPixelFormat
        do { r.pipeline = try device?.makeRenderPipelineState(descriptor: d) }
        catch { view.accessibilityValue = error.localizedDescription }
        return view
    }
    func updateUIView(_ view: MTKView, context: Context) {
        guard let frame else { return }
        let size = CGSize(width: max(1, Double(frame.width) * scale), height: max(1, Double(frame.height) * scale))
        if view.drawableSize != size { view.drawableSize = size }
        if context.coordinator.frame?.serial != frame.serial {
            context.coordinator.frame = frame
            view.setNeedsDisplay()
        }
    }
}

