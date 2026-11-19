import SwiftUI
import UniformTypeIdentifiers

@main
struct SwitchCoreApp: App {
    @StateObject private var library = Library()
    @StateObject private var diagnostics = Diagnostics()
    var body: some Scene { WindowGroup { LibraryView().environmentObject(library).environmentObject(diagnostics).tint(.teal) } }
}

struct LibraryView: View {
    @EnvironmentObject private var library: Library
    @EnvironmentObject private var diagnostics: Diagnostics
    @State private var importing = false
    @State private var credits = false
    @State private var query = ""
    @State private var filter = 0
    @State private var playing: Title?
    @State private var configuring: Title?
    private var visible: [Title] {
        library.titles.filter {
            (query.isEmpty || $0.name.localizedCaseInsensitiveContains(query)) &&
            (filter != 1 || $0.favorite) && (filter != 2 || $0.lastPlayed != nil)
        }.sorted { a, b in
            filter == 2 ? (a.lastPlayed ?? .distantPast) > (b.lastPlayed ?? .distantPast) :
                a.name.localizedCaseInsensitiveCompare(b.name) == .orderedAscending
        }
    }
    var body: some View {
        NavigationStack {
            List {
                Picker("Filtro", selection: $filter) {
                    Text("Tutti").tag(0); Text("Preferiti").tag(1); Text("Recenti").tag(2)
                }.pickerStyle(.segmented).listRowBackground(Color.clear).listRowInsets(EdgeInsets())
                ForEach(visible) { title in
                    HStack(spacing: 14) {
                        Button {
                            library.mark(title.id, favorite: false)
                            playing = title
                        } label: {
                            HStack(spacing: 14) {
                                Cover(title: title)
                                VStack(alignment: .leading, spacing: 5) {
                                    Text(title.name).font(.headline).foregroundStyle(.primary)
                                    Text(title.author.isEmpty ? "Homebrew ARM64" : title.author).font(.subheadline).foregroundStyle(.secondary)
                                }
                                Spacer()
                            }.contentShape(Rectangle())
                        }.buttonStyle(.plain).accessibilityIdentifier("play-" + title.name)
                        Menu {
                            Button(title.favorite ? "Rimuovi dai preferiti" : "Aggiungi ai preferiti",
                                   systemImage: "star") { library.mark(title.id, favorite: true) }
                            Button("Profilo e diagnostica", systemImage: "slider.horizontal.3") { configuring = title }
                        } label: { Image(systemName: title.favorite ? "star.fill" : "ellipsis.circle").padding(8) }
                        .accessibilityLabel("Opzioni " + title.name)
                    }
                }
                if visible.isEmpty {
                    ContentUnavailableView("Nessun titolo", systemImage: "square.stack", description: Text("Importa una homebrew .nro o cambia filtro."))
                }
            }.navigationTitle("Libreria")
            .searchable(text: $query, prompt: "Cerca un titolo")
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button("Informazioni", systemImage: "info.circle") { credits = true }
                }
                ToolbarItem(placement: .topBarTrailing) { Button("Importa", systemImage: "plus") { importing = true } }
            }
            .sheet(isPresented: $credits) {
                NavigationStack {
                    List {
                        Text("SwitchCore 0.1.0 · GPLv3")
                        Link("Sorgenti del progetto", destination: URL(string: "https://github.com/sminikiller/SwitchCore-iOS")!)
                        Text("Core ARM64, Horizon e Maxwell derivati da NeXo 2. Rally usa libnx. MoltenVK traduce SPIR-V per Metal.")
                        if !diagnostics.reports.isEmpty {
                            Section("Rapporti iOS") {
                                ForEach(diagnostics.reports, id: \.self) { url in
                                    ShareLink(item: url, label: { Text(url.lastPathComponent) })
                                }
                            }
                        }
                        ForEach(["GPLv3", "libnx", "MoltenVK"], id: \.self) { name in
                            if let url = Bundle.main.url(forResource: name, withExtension: "txt") {
                                NavigationLink(name) {
                                    ScrollView { Text((try? String(contentsOf: url, encoding: .utf8)) ?? "").padding() }
                                        .navigationTitle(name)
                                }
                            }
                        }
                    }.navigationTitle("Informazioni").toolbar { Button("Fine") { credits = false } }
                }
            }
            .fileImporter(isPresented: $importing, allowedContentTypes: [UTType(filenameExtension: "nro") ?? .data]) { result in
                do { try library.importFile(result.get()) }
                catch { library.error = error.localizedDescription }
            }
            .alert("Libreria", isPresented: Binding(get: { library.error != nil }, set: { if !$0 { library.error = nil } })) {
                Button("Chiudi") { library.error = nil }
            } message: { Text(library.error ?? "") }
            .fullScreenCover(item: $playing) { GameView(title: $0) }
            .sheet(item: $configuring) { ProfileView(title: $0) }
        }
    }
}

struct Cover: View {
    let title: Title
    var body: some View {
        Group {
            if let image = UIImage(contentsOfFile: Library.dir(title.id).appendingPathComponent("cover.jpg").path) {
                Image(uiImage: image).resizable().scaledToFill()
            } else {
                ZStack {
                    LinearGradient(colors: [.teal, Color(red: 0.04, green: 0.18, blue: 0.25)], startPoint: .topLeading, endPoint: .bottomTrailing)
                    Image(systemName: "gamecontroller").font(.title2).foregroundStyle(.white)
                }
            }
        }.frame(width: 64, height: 64).clipShape(RoundedRectangle(cornerRadius: 12))
    }
}

struct ProfileView: View {
    let title: Title
    @Environment(\.dismiss) private var dismiss
    @AppStorage private var fps: Int
    @AppStorage private var scale: Double
    @AppStorage private var audio: Bool
    @AppStorage private var accelerated: Bool
    @AppStorage private var cache: Bool
    @AppStorage private var swap: Bool
    @AppStorage private var haptics: Bool
    init(title: Title) {
        self.title = title
        _fps = AppStorage(wrappedValue: 60, title.id + ".fps")
        _scale = AppStorage(wrappedValue: 1, title.id + ".scale")
        _audio = AppStorage(wrappedValue: true, title.id + ".audio")
        _accelerated = AppStorage(wrappedValue: true, title.id + ".metal")
        _cache = AppStorage(wrappedValue: true, title.id + ".cache")
        _swap = AppStorage(wrappedValue: false, title.id + ".swap")
        _haptics = AppStorage(wrappedValue: true, title.id + ".haptics")
    }
    var body: some View {
        NavigationStack {
            Form {
                Section("Grafica e prestazioni") {
                    Picker("Limite FPS", selection: $fps) { ForEach([15,30,60], id: \.self) { Text("\($0)").tag($0) } }
                    Picker("Scala di presentazione", selection: $scale) { Text("50%").tag(0.5); Text("100%").tag(1.0) }
                    Toggle("Accelera shader con Metal", isOn: $accelerated)
                    Toggle("Cache istruzioni", isOn: $cache)
                }
                Section("Audio e controller") {
                    Toggle("Audio", isOn: $audio)
                    Toggle("Scambia A e B", isOn: $swap)
                    Toggle("Feedback tattile", isOn: $haptics)
                }
                Section("Diagnostica") {
                    LabeledContent("CPU", value: "Interprete ARM64")
                    LabeledContent("GPU", value: accelerated ? "MoltenVK con fallback software" : "Maxwell software")
                    let log = Library.dir(title.id).appendingPathComponent("last-session.txt")
                    if let text = try? String(contentsOf: log, encoding: .utf8) {
                        Text(text).font(.caption.monospaced()).textSelection(.enabled)
                        ShareLink(item: log, label: { Text("Esporta log") })
                    }
                }
            }.navigationTitle(title.name).toolbar { Button("Fine") { dismiss() } }
        }
    }
}

