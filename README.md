<!DOCTYPE html>
<html lang="en" class="scroll-smooth">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>OmniQ-OS | Real-Time Quantum Linux (x86_64)</title>
  <meta name="description" content="A deterministic real-time Linux distribution built to control cryogenic quantum processors with sub-600ns feedback latency." />

  <!-- Google Fonts: Space Grotesk, Inter, JetBrains Mono -->
  <link rel="preconnect" href="https://fonts.googleapis.com" />
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin />
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500;600&family=Space+Grotesk:wght@600;700&display=swap" rel="stylesheet" />

  <!-- Tailwind CSS CDN -->
  <script src="https://cdn.tailwindcss.com"></script>
  <script>
    tailwind.config = {
      theme: {
        extend: {
          fontFamily: {
            sans: ['Inter', 'system-ui', 'sans-serif'],
            display: ['Space Grotesk', 'Inter', 'sans-serif'],
            mono: ['JetBrains Mono', 'monospace'],
          },
          colors: {
            obsidian: '#05080f',
            surface: '#080d1a',
            qcyan: '#00E5FF',
            qemerald: '#00FF9D',
            qviolet: '#8A4FFF',
          },
          boxShadow: {
            'cyan-glow': '0 0 28px -6px rgba(0, 229, 255, 0.35)',
            'emerald-glow': '0 0 28px -6px rgba(0, 255, 157, 0.3)',
          }
        }
      }
    };
  </script>

  <style>
    body {
      background-color: #05080f;
      color: #e2e8f0;
      overflow-x: hidden;
      background-image:
        radial-gradient(circle at 15% 15%, rgba(0, 229, 255, 0.09) 0%, transparent 40%),
        radial-gradient(circle at 85% 20%, rgba(138, 79, 255, 0.09) 0%, transparent 42%),
        radial-gradient(circle at 50% 85%, rgba(0, 255, 157, 0.05) 0%, transparent 45%);
    }

    .dot-backdrop {
      background-image: radial-gradient(rgba(0, 229, 255, 0.12) 1px, transparent 1px);
      background-size: 32px 32px;
    }

    .glass {
      background: linear-gradient(145deg, rgba(13, 21, 38, 0.72) 0%, rgba(7, 12, 24, 0.85) 100%);
      backdrop-filter: blur(16px);
      -webkit-backdrop-filter: blur(16px);
      border: 1px solid rgba(255, 255, 255, 0.08);
    }

    .glass-hover {
      transition: transform 0.25s ease, border-color 0.25s ease, box-shadow 0.25s ease;
    }

    .glass-hover:hover {
      transform: translateY(-3px);
      border-color: rgba(0, 229, 255, 0.38);
      box-shadow: 0 14px 34px -12px rgba(0, 229, 255, 0.2);
    }
  </style>
</head>
<body class="dot-backdrop antialiased selection:bg-qcyan/20 selection:text-qcyan">

  <!-- Toast Notification -->
  <div id="toast" class="fixed bottom-6 right-6 z-50 pointer-events-none opacity-0 translate-y-2 transition-all duration-300 glass px-4 py-2.5 rounded-xl border border-qemerald/50 text-qemerald font-mono text-xs shadow-lg">
    Copied to clipboard
  </div>

  <!-- Sticky Minimalist Glassmorphic Navbar -->
  <header class="sticky top-0 z-40 border-b border-white/5 bg-obsidian/80 backdrop-blur-xl">
    <div class="max-w-6xl mx-auto px-6 h-16 flex items-center justify-between">
      <a href="#" class="flex items-center gap-3 group">
        <div class="w-9 h-9 rounded-xl bg-surface border border-qcyan/35 flex items-center justify-center shadow-cyan-glow group-hover:border-qcyan transition">
          <svg viewBox="0 0 36 36" class="w-5 h-5" fill="none">
            <polygon points="18,3 31,10.5 31,25.5 18,33 5,25.5 5,10.5" stroke="#00E5FF" stroke-width="2" />
            <ellipse cx="18" cy="18" rx="10" ry="4.5" transform="rotate(-30 18 18)" stroke="#8A4FFF" stroke-width="1.5" />
            <circle cx="18" cy="18" r="2.5" fill="#00FF9D" />
          </svg>
        </div>
        <span class="font-display font-bold text-lg tracking-tight text-white">OmniQ-OS</span>
      </a>

      <nav class="hidden md:flex items-center gap-8 text-sm text-slate-300">
        <a href="#features" class="hover:text-qcyan transition">Features</a>
        <a href="#stack" class="hover:text-qcyan transition">Stack</a>
        <a href="#downloads" class="hover:text-qcyan transition">Downloads</a>
        <a href="#quickstart" class="hover:text-qcyan transition">Quick Start</a>
      </nav>

      <a href="https://github.com" target="_blank" rel="noopener noreferrer" class="inline-flex items-center gap-2 px-3.5 py-1.5 rounded-xl glass hover:border-qcyan/50 text-xs font-mono text-slate-200 transition">
        <svg class="w-4 h-4 fill-current" viewBox="0 0 24 24"><path d="M12 .5C5.65.5.5 5.65.5 12c0 5.09 3.29 9.4 7.86 10.92.58.1.79-.25.79-.56v-2c-3.2.7-3.88-1.38-3.88-1.38-.52-1.33-1.28-1.68-1.28-1.68-1.05-.72.08-.7.08-.7 1.16.08 1.77 1.2 1.77 1.2 1.03 1.76 2.7 1.25 3.36.96.1-.75.4-1.25.73-1.54-2.55-.29-5.24-1.28-5.24-5.68 0-1.26.45-2.28 1.19-3.09-.12-.29-.52-1.46.11-3.05 0 0 .97-.31 3.18 1.18a11.1 11.1 0 0 1 5.8 0c2.2-1.49 3.17-1.18 3.17-1.18.63 1.59.23 2.76.12 3.05.74.81 1.18 1.83 1.18 3.09 0 4.41-2.69 5.38-5.26 5.67.42.36.78 1.06.78 2.14v3.17c0 .31.21.67.8.56A11.51 11.51 0 0 0 23.5 12C23.5 5.65 18.35.5 12 .5Z"/></svg>
        <span>GitHub</span>
      </a>
    </div>
  </header>

  <main class="max-w-6xl mx-auto px-6">

    <!-- 1. HERO SECTION -->
    <section class="py-16 lg:py-24 grid grid-cols-1 lg:grid-cols-12 gap-12 items-center">
      <!-- Left Copy -->
      <div class="lg:col-span-7 space-y-7">
        <div class="inline-flex items-center gap-2.5 px-3.5 py-1.5 rounded-full bg-qcyan/10 border border-qcyan/30 text-qcyan text-xs font-mono">
          <span class="w-2 h-2 rounded-full bg-qemerald animate-pulse"></span>
          <span>Linux 7.0 PREEMPT_RT • CPU + GPU + QPU</span>
        </div>

        <div class="space-y-4">
          <h1 class="font-display text-5xl sm:text-6xl font-bold tracking-tight text-white">
            OmniQ-OS <span class="text-xl sm:text-2xl font-mono font-medium text-qcyan align-middle">x86_64</span>
          </h1>
          <p class="text-lg text-slate-300 leading-relaxed max-w-xl">
            A deterministic real-time Linux distribution built to control cryogenic quantum processors with sub-600ns feedback latency.
          </p>
        </div>

        <!-- CTAs -->
        <div class="flex flex-wrap items-center gap-4">
          <a href="#downloads" class="px-6 py-3 rounded-xl bg-gradient-to-r from-qcyan to-qemerald text-slate-950 font-display font-bold text-sm shadow-cyan-glow hover:opacity-95 transition">
            Download ISO
          </a>
          <a href="#quickstart" class="px-6 py-3 rounded-xl glass hover:border-qcyan/50 text-white font-mono text-sm transition">
            Quick Start &rarr;
          </a>
        </div>

        <!-- 3 Compact Stat Callouts -->
        <div class="grid grid-cols-3 gap-4 pt-4 max-w-lg border-t border-white/10">
          <div>
            <div class="font-mono text-lg sm:text-xl font-bold text-qemerald">&lt; 600 ns</div>
            <div class="text-xs text-slate-400">QEC Loop</div>
          </div>
          <div>
            <div class="font-mono text-lg sm:text-xl font-bold text-qcyan">&lt; 50 mK</div>
            <div class="text-xs text-slate-400">Cryo Interlock</div>
          </div>
          <div>
            <div class="font-mono text-lg sm:text-xl font-bold text-qviolet">FIPS 203</div>
            <div class="text-xs text-slate-400">ML-KEM PQC</div>
          </div>
        </div>
      </div>

      <!-- Right Interactive 3D Q-Sphere Canvas -->
      <div class="lg:col-span-5">
        <div class="glass rounded-2xl p-5 border border-qcyan/25 shadow-cyan-glow">
          <div class="flex items-center justify-between mb-3">
            <span id="state-formula" class="text-xs font-mono text-qcyan">|Φ⁺⟩ = (|00⟩ + |11⟩)/√2</span>
            <span class="text-[10px] font-mono text-slate-400 uppercase tracking-wider">Drag to Orbit</span>
          </div>

          <div class="relative h-[300px] rounded-xl bg-obsidian/90 border border-white/5 overflow-hidden cursor-grab active:cursor-grabbing">
            <canvas id="qSphereCanvas" class="w-full h-full block"></canvas>
          </div>

          <!-- 3-Button State Switcher -->
          <div class="grid grid-cols-3 gap-2 mt-3 font-mono text-xs">
            <button onclick="setQState('bell')" data-state="bell" class="qstate-btn py-2 rounded-lg bg-qcyan/20 text-qcyan border border-qcyan/50 font-medium transition">
              Bell |Φ⁺⟩
            </button>
            <button onclick="setQState('ghz')" data-state="ghz" class="qstate-btn py-2 rounded-lg bg-surface text-slate-400 border border-white/5 hover:text-white transition">
              GHZ |ψ⟩
            </button>
            <button onclick="setQState('plus')" data-state="plus" class="qstate-btn py-2 rounded-lg bg-surface text-slate-400 border border-white/5 hover:text-white transition">
              Superpos |+⟩
            </button>
          </div>
        </div>
      </div>
    </section>

    <!-- 2. CORE PILLARS (4 Clean Glassmorphic Cards) -->
    <section id="features" class="py-16 border-t border-white/5">
      <div class="mb-10">
        <span class="text-xs font-mono uppercase tracking-widest text-qcyan">Architecture Pillars</span>
        <h2 class="font-display text-3xl font-bold text-white mt-1">Engineered for Coherence</h2>
      </div>

      <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-5">
        <!-- Card 1 -->
        <div class="glass glass-hover rounded-2xl p-6 space-y-3">
          <div class="w-10 h-10 rounded-xl bg-qcyan/10 border border-qcyan/30 flex items-center justify-center text-qcyan">
            <svg class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 12h4l3-8 4 16 3-8h4" stroke-linecap="round" stroke-linejoin="round"/></svg>
          </div>
          <h3 class="font-display text-lg font-bold text-white">Sub-600ns RT Kernel</h3>
          <p class="text-sm text-slate-400 leading-relaxed">
            Isolated <code class="font-mono text-slate-200">PREEMPT_RT</code> cores eliminate OS jitter for real-time syndrome decoding.
          </p>
        </div>

        <!-- Card 2 -->
        <div class="glass glass-hover rounded-2xl p-6 space-y-3">
          <div class="w-10 h-10 rounded-xl bg-qviolet/15 border border-qviolet/35 flex items-center justify-center text-qviolet">
            <svg class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="12 2 20 7 20 17 12 22 4 17 4 7 12 2"/></svg>
          </div>
          <h3 class="font-display text-lg font-bold text-white">Spatial QMMU</h3>
          <p class="text-sm text-slate-400 leading-relaxed">
            Hardware-aware qubit lattice partitioning with crosstalk guard bands and no-cloning safety.
          </p>
        </div>

        <!-- Card 3 -->
        <div class="glass glass-hover rounded-2xl p-6 space-y-3">
          <div class="w-10 h-10 rounded-xl bg-qemerald/10 border border-qemerald/30 flex items-center justify-center text-qemerald">
            <svg class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 2v12m0 0a4 4 0 1 0 0 8 4 4 0 0 0 0-8z"/></svg>
          </div>
          <h3 class="font-display text-lg font-bold text-white">&lt; 50 mK Cryo Guard</h3>
          <p class="text-sm text-slate-400 leading-relaxed">
            Kernel watchdog immediately clamps RF DAC channels to <code class="font-mono text-slate-200">0x00</code> on thermal excursion.
          </p>
        </div>

        <!-- Card 4 -->
        <div class="glass glass-hover rounded-2xl p-6 space-y-3">
          <div class="w-10 h-10 rounded-xl bg-qcyan/10 border border-qcyan/30 flex items-center justify-center text-qcyan">
            <svg class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="4" width="18" height="14" rx="2"/><path d="M8 21h8M12 18v3"/></svg>
          </div>
          <h3 class="font-display text-lg font-bold text-white">Native Quantum Suite</h3>
          <p class="text-sm text-slate-400 leading-relaxed">
            Zero-web X11 glassmorphic desktop with Hilbert Explorer, Circuit IDE, and Cryo Lab bus.
          </p>
        </div>
      </div>
    </section>

    <!-- 3. MINIMAL 3-LAYER STACK VISUAL -->
    <section id="stack" class="py-16 border-t border-white/5">
      <div class="mb-10">
        <span class="text-xs font-mono uppercase tracking-widest text-qviolet">System Stack</span>
        <h2 class="font-display text-3xl font-bold text-white mt-1">Tri-Compute Native Stack</h2>
      </div>

      <div class="space-y-3">
        <!-- Layer 1 -->
        <div class="glass rounded-2xl p-5 sm:px-7 flex flex-col md:flex-row md:items-center justify-between gap-4 border-l-4 border-l-qcyan">
          <div>
            <span class="text-[11px] font-mono text-qcyan uppercase">Layer 01</span>
            <h3 class="font-display text-lg font-bold text-white">Desktop &amp; Studios</h3>
          </div>
          <div class="flex flex-wrap gap-2 font-mono text-xs text-slate-200">
            <span class="px-3 py-1.5 rounded-lg bg-obsidian/80 border border-white/10">omniq-hilbert-space</span>
            <span class="px-3 py-1.5 rounded-lg bg-obsidian/80 border border-white/10">omniq-circuit-maker</span>
            <span class="px-3 py-1.5 rounded-lg bg-obsidian/80 border border-white/10">omniq-gui</span>
            <span class="px-3 py-1.5 rounded-lg bg-obsidian/80 border border-white/10">omniq-cryo-lab</span>
          </div>
        </div>

        <!-- Layer 2 -->
        <div class="glass rounded-2xl p-5 sm:px-7 flex flex-col md:flex-row md:items-center justify-between gap-4 border-l-4 border-l-qviolet">
          <div>
            <span class="text-[11px] font-mono text-qviolet uppercase">Layer 02</span>
            <h3 class="font-display text-lg font-bold text-white">C++20 Core &amp; PQC</h3>
          </div>
          <div class="flex flex-wrap gap-2 font-mono text-xs text-slate-200">
            <span class="px-3 py-1.5 rounded-lg bg-obsidian/80 border border-white/10">omniqd</span>
            <span class="px-3 py-1.5 rounded-lg bg-obsidian/80 border border-white/10">1,024Q Clifford &amp; AVX2 Sim</span>
            <span class="px-3 py-1.5 rounded-lg bg-obsidian/80 border border-white/10">ML-KEM-768 Secret API</span>
            <span class="px-3 py-1.5 rounded-lg bg-obsidian/80 border border-white/10">Python SDK</span>
          </div>
        </div>

        <!-- Layer 3 -->
        <div class="glass rounded-2xl p-5 sm:px-7 flex flex-col md:flex-row md:items-center justify-between gap-4 border-l-4 border-l-qemerald">
          <div>
            <span class="text-[11px] font-mono text-qemerald uppercase">Layer 03</span>
            <h3 class="font-display text-lg font-bold text-white">RT Linux Kernel</h3>
          </div>
          <div class="flex flex-wrap gap-2 font-mono text-xs text-slate-200">
            <span class="px-3 py-1.5 rounded-lg bg-obsidian/80 border border-white/10">Linux 7.0.0-43</span>
            <span class="px-3 py-1.5 rounded-lg bg-obsidian/80 border border-white/10">omniq_qpu.ko</span>
            <span class="px-3 py-1.5 rounded-lg bg-obsidian/80 border border-white/10">Zero-Copy GPUDirect DMA</span>
            <span class="px-3 py-1.5 rounded-lg bg-obsidian/80 border border-white/10">&lt;50mK Interlock</span>
          </div>
        </div>
      </div>
    </section>

    <!-- 4. DUAL BOOTABLE x86_64 ISOs -->
    <section id="downloads" class="py-16 border-t border-white/5">
      <div class="mb-10">
        <span class="text-xs font-mono uppercase tracking-widest text-qemerald">Hybrid BIOS + UEFI</span>
        <h2 class="font-display text-3xl font-bold text-white mt-1">Bootable `x86_64` Releases</h2>
      </div>

      <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
        <!-- Full Workstation ISO -->
        <div class="glass glass-hover rounded-2xl p-7 border border-qcyan/30 flex flex-col justify-between space-y-6">
          <div class="space-y-3">
            <div class="flex items-center justify-between">
              <span class="px-2.5 py-1 rounded-md bg-qcyan/15 text-qcyan font-mono text-xs">Full Workstation</span>
              <span class="font-mono text-xl font-bold text-white">688 MB</span>
            </div>
            <h3 class="font-mono text-lg font-semibold text-white">omniq-os-full.iso</h3>
            <p class="text-sm text-slate-400 leading-relaxed">
              Glassmorphic X11 desktop, Hilbert &amp; Circuit studios, Cryo Lab protocols, and 1,024Q simulator.
            </p>
          </div>

          <div class="pt-4 border-t border-white/10 flex items-center justify-between gap-3">
            <div class="text-xs font-mono text-slate-400">
              SHA-256: <span class="text-slate-200">6541f048...72344319</span>
            </div>
            <button
              onclick="copyText('6541f04804dbd8292abab09bb4a2848502e1c3d75ae5a9ed32fdef4c72344319', 'Copied Full ISO SHA-256')"
              class="px-3 py-1.5 rounded-lg bg-qcyan/15 hover:bg-qcyan/25 text-qcyan font-mono text-xs font-medium transition"
            >
              Copy Hash
            </button>
          </div>
        </div>

        <!-- Cryo-Rack Minimal ISO -->
        <div class="glass glass-hover rounded-2xl p-7 border border-qemerald/25 flex flex-col justify-between space-y-6">
          <div class="space-y-3">
            <div class="flex items-center justify-between">
              <span class="px-2.5 py-1 rounded-md bg-qemerald/15 text-qemerald font-mono text-xs">Cryo-Rack Minimal</span>
              <span class="font-mono text-xl font-bold text-white">22 MB</span>
            </div>
            <h3 class="font-mono text-lg font-semibold text-white">omniq-os.iso</h3>
            <p class="text-sm text-slate-400 leading-relaxed">
              Pure RAM <code class="font-mono text-slate-200">initramfs</code> appliance with <code class="font-mono text-slate-200">omniq_qpu.ko</code> and <code class="font-mono text-slate-200">qpu-smi</code> for ultra-low-jitter rack controllers.
            </p>
          </div>

          <div class="pt-4 border-t border-white/10 flex items-center justify-between gap-3">
            <div class="text-xs font-mono text-slate-400">
              SHA-256: <span class="text-slate-200">c5e1247b...96570fc2</span>
            </div>
            <button
              onclick="copyText('c5e1247bf29ff4de080e6877ab97bd0705d233b3d24cc0b2432dd36696570fc2', 'Copied Minimal ISO SHA-256')"
              class="px-3 py-1.5 rounded-lg bg-qemerald/15 hover:bg-qemerald/25 text-qemerald font-mono text-xs font-medium transition"
            >
              Copy Hash
            </button>
          </div>
        </div>
      </div>
    </section>

    <!-- 5. QUICK START TERMINAL -->
    <section id="quickstart" class="py-16 border-t border-white/5">
      <div class="max-w-3xl mx-auto">
        <div class="flex items-center justify-between mb-5">
          <div>
            <span class="text-xs font-mono uppercase tracking-widest text-qcyan">Get Started</span>
            <h2 class="font-display text-2xl font-bold text-white mt-0.5">Quick Start</h2>
          </div>
          <button
            onclick="copyQuickStart()"
            class="px-3.5 py-1.5 rounded-xl glass hover:border-qcyan/50 text-xs font-mono text-qcyan transition"
          >
            Copy Commands
          </button>
        </div>

        <div class="glass rounded-2xl overflow-hidden border border-white/10">
          <div class="px-4 py-2.5 bg-obsidian/90 border-b border-white/5 flex items-center gap-2">
            <span class="w-2.5 h-2.5 rounded-full bg-rose-500/70"></span>
            <span class="w-2.5 h-2.5 rounded-full bg-amber-400/70"></span>
            <span class="w-2.5 h-2.5 rounded-full bg-emerald-400/70"></span>
            <span class="ml-2 text-[11px] font-mono text-slate-500">bash — Omni-QOS</span>
          </div>
          <pre class="p-5 font-mono text-xs sm:text-sm leading-relaxed overflow-x-auto text-slate-200"><code><span class="text-slate-500"># Launch native quantum studios locally</span>
<span class="text-qcyan">./scripts/run_local.sh all</span>

<span class="text-slate-500"># Build hybrid BIOS/UEFI workstation ISO</span>
<span class="text-qemerald">./scripts/build_full_distro_iso.sh</span>

<span class="text-slate-500"># Boot directly in VirtualBox</span>
<span class="text-qviolet">./scripts/create_vmbox_omniq.sh start</span></code></pre>
        </div>
      </div>
    </section>

  </main>

  <!-- Minimal 1-Line Footer -->
  <footer class="border-t border-white/5 py-8 mt-8 text-center text-xs font-mono text-slate-500 px-6">
    Dual License: <span class="text-slate-300">GPLv2</span> (Kernel) / <span class="text-slate-300">Apache 2.0</span> (Userland) &bull; <span class="text-qcyan">OmniQ-OS x86_64</span>
  </footer>

  <!-- Minimal Clipboard + Interactive 3D Q-Sphere Script -->
  <script>
    // Clipboard helper using execCommand for iframe compatibility
    let toastTimer = null;
    function copyText(text, label) {
      const el = document.createElement('textarea');
      el.value = text;
      el.style.position = 'fixed';
      el.style.opacity = '0';
      document.body.appendChild(el);
      el.select();
      try {
        document.execCommand('copy');
        const toast = document.getElementById('toast');
        toast.textContent = label || 'Copied to clipboard';
        toast.classList.remove('opacity-0', 'translate-y-2');
        clearTimeout(toastTimer);
        toastTimer = setTimeout(() => {
          toast.classList.add('opacity-0', 'translate-y-2');
        }, 2200);
      } catch (e) {}
      document.body.removeChild(el);
    }

    function copyQuickStart() {
      const cmd = `./scripts/run_local.sh all\n./scripts/build_full_distro_iso.sh\n./scripts/create_vmbox_omniq.sh start`;
      copyText(cmd, 'Copied Quick Start commands');
    }

    // Interactive 3D Bloch / Q-Sphere Visualization
    const states = {
      bell: {
        formula: '|Φ⁺⟩ = (|00⟩ + |11⟩)/√2',
        vectors: [
          { label: '|00⟩', x: 0, y: -1, z: 0, color: '#00E5FF', size: 7 },
          { label: '|11⟩', x: 0, y: 1, z: 0, color: '#00FF9D', size: 7 }
        ]
      },
      ghz: {
        formula: '|ψ⟩ = (|000⟩ + |111⟩)/√2',
        vectors: [
          { label: '|000⟩', x: 0, y: -1, z: 0, color: '#00E5FF', size: 7 },
          { label: '|111⟩', x: 0, y: 1, z: 0, color: '#8A4FFF', size: 7 }
        ]
      },
      plus: {
        formula: '|+⟩⊗³ = 1/√8 ∑ |x⟩',
        vectors: [
          { label: '|000⟩', x: 0, y: -1, z: 0, color: '#00E5FF', size: 5 },
          { label: '|001⟩', x: 0.94, y: -0.33, z: 0, color: '#00FF9D', size: 5 },
          { label: '|010⟩', x: -0.47, y: -0.33, z: 0.81, color: '#00FF9D', size: 5 },
          { label: '|100⟩', x: -0.47, y: -0.33, z: -0.81, color: '#00FF9D', size: 5 },
          { label: '|011⟩', x: 0.47, y: 0.33, z: 0.81, color: '#8A4FFF', size: 5 },
          { label: '|101⟩', x: 0.47, y: 0.33, z: -0.81, color: '#8A4FFF', size: 5 },
          { label: '|110⟩', x: -0.94, y: 0.33, z: 0, color: '#8A4FFF', size: 5 },
          { label: '|111⟩', x: 0, y: 1, z: 0, color: '#00E5FF', size: 5 }
        ]
      }
    };

    let activeState = 'bell';
    let rotY = 0.5;
    let rotX = 0.3;
    let dragging = false;
    let prevX = 0;
    let prevY = 0;

    function setQState(key) {
      activeState = key;
      document.getElementById('state-formula').textContent = states[key].formula;
      document.querySelectorAll('.qstate-btn').forEach(btn => {
        const isSelected = btn.dataset.state === key;
        btn.className = isSelected
          ? 'qstate-btn py-2 rounded-lg bg-qcyan/20 text-qcyan border border-qcyan/50 font-medium transition'
          : 'qstate-btn py-2 rounded-lg bg-surface text-slate-400 border border-white/5 hover:text-white transition';
      });
    }

    function project(x, y, z, cx, cy, r) {
      const cyRot = Math.cos(rotY), syRot = Math.sin(rotY);
      const x1 = x * cyRot - z * syRot;
      const z1 = x * syRot + z * cyRot;

      const cxRot = Math.cos(rotX), sxRot = Math.sin(rotX);
      const y2 = y * cxRot - z1 * sxRot;
      const z2 = y * sxRot + z1 * cxRot;

      const perspective = 3.4 / (3.4 + z2 * 0.6);
      return {
        x: cx + x1 * r * perspective,
        y: cy + y2 * r * perspective,
        scale: perspective
      };
    }

    window.onload = function () {
      const canvas = document.getElementById('qSphereCanvas');
      const ctx = canvas.getContext('2d');

      function resize() {
        const rect = canvas.parentElement.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        canvas.width = rect.width * dpr;
        canvas.height = rect.height * dpr;
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      }
      resize();
      window.addEventListener('resize', resize);

      canvas.addEventListener('mousedown', e => { dragging = true; prevX = e.clientX; prevY = e.clientY; });
      window.addEventListener('mouseup', () => { dragging = false; });
      window.addEventListener('mousemove', e => {
        if (!dragging) return;
        rotY += (e.clientX - prevX) * 0.01;
        rotX = Math.max(-1.1, Math.min(1.1, rotX + (e.clientY - prevY) * 0.01));
        prevX = e.clientX;
        prevY = e.clientY;
      });

      canvas.addEventListener('touchstart', e => {
        if (e.touches.length === 1) {
          dragging = true;
          prevX = e.touches[0].clientX;
          prevY = e.touches[0].clientY;
        }
      }, { passive: true });
      window.addEventListener('touchend', () => { dragging = false; });
      canvas.addEventListener('touchmove', e => {
        if (!dragging || e.touches.length !== 1) return;
        rotY += (e.touches[0].clientX - prevX) * 0.01;
        rotX = Math.max(-1.1, Math.min(1.1, rotX + (e.touches[0].clientY - prevY) * 0.01));
        prevX = e.touches[0].clientX;
        prevY = e.touches[0].clientY;
      }, { passive: true });

      function animate() {
        const w = canvas.clientWidth;
        const h = canvas.clientHeight;
        ctx.clearRect(0, 0, w, h);

        const cx = w / 2;
        const cy = h / 2;
        const r = Math.min(w, h) * 0.35;

        if (!dragging) rotY += 0.006;

        // Ambient core glow
        const glow = ctx.createRadialGradient(cx, cy, r * 0.1, cx, cy, r * 1.25);
        glow.addColorStop(0, 'rgba(0, 229, 255, 0.10)');
        glow.addColorStop(1, 'rgba(5, 8, 15, 0)');
        ctx.fillStyle = glow;
        ctx.beginPath();
        ctx.arc(cx, cy, r * 1.25, 0, Math.PI * 2);
        ctx.fill();

        // Latitude rings
        [-0.85, -0.33, 0, 0.33, 0.85].forEach((lat, idx) => {
          const ringRadius = Math.sqrt(Math.max(0.01, 1 - lat * lat));
          ctx.beginPath();
          for (let i = 0; i <= 48; i++) {
            const a = (i / 48) * Math.PI * 2;
            const p = project(Math.cos(a) * ringRadius, lat, Math.sin(a) * ringRadius, cx, cy, r);
            if (i === 0) ctx.moveTo(p.x, p.y);
            else ctx.lineTo(p.x, p.y);
          }
          ctx.strokeStyle = idx === 2 ? 'rgba(0, 229, 255, 0.28)' : 'rgba(148, 163, 184, 0.14)';
          ctx.lineWidth = 1;
          ctx.stroke();
        });

        // Longitude meridian
        ctx.beginPath();
        for (let i = 0; i <= 48; i++) {
          const a = (i / 48) * Math.PI * 2;
          const p = project(Math.cos(a), Math.sin(a), 0, cx, cy, r);
          if (i === 0) ctx.moveTo(p.x, p.y);
          else ctx.lineTo(p.x, p.y);
        }
        ctx.strokeStyle = 'rgba(138, 79, 255, 0.18)';
        ctx.stroke();

        // State vectors
        const center = project(0, 0, 0, cx, cy, r);
        states[activeState].vectors.forEach(v => {
          const tip = project(v.x, v.y, v.z, cx, cy, r);

          ctx.beginPath();
          ctx.moveTo(center.x, center.y);
          ctx.lineTo(tip.x, tip.y);
          ctx.strokeStyle = v.color;
          ctx.lineWidth = 2.2;
          ctx.stroke();

          ctx.beginPath();
          ctx.arc(tip.x, tip.y, v.size * tip.scale, 0, Math.PI * 2);
          ctx.fillStyle = v.color;
          ctx.shadowColor = v.color;
          ctx.shadowBlur = 12;
          ctx.fill();
          ctx.shadowBlur = 0;

          ctx.font = '11px "JetBrains Mono", monospace';
          ctx.fillStyle = '#e2e8f0';
          ctx.fillText(v.label, tip.x + 10, tip.y + 4);
        });

        requestAnimationFrame(animate);
      }

      animate();
    };
  </script>
</body>
</html>
