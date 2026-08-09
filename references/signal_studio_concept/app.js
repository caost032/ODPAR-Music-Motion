
const DEFAULT_WAVEFORM = Object.freeze([0.5419,0.6282,0.5654,0.6326,0.6626,0.6259,0.6092,0.3618,0.3409,0.3787,0.317,0.6247,0.5559,0.5734,0.5925,0.5979,0.624,0.4621,0.3451,0.3693,0.6405,0.7587,0.755,0.8005,0.8025,0.7736,0.8384,0.7377,0.7201,0.6895,0.5679,0.7543,0.7889,0.7592,0.8858,0.8749,0.6034,0.5734,0.5445,0.2199,1.0,0.929,0.6668,1.0,0.6517,0.643,0.6182,1.0,1.0,0.3651,0.513,1.0,0.6658,1.0,0.5658,0.6199,0.4983,1.0,1.0,0.5402,0.3448,1.0,0.8156,1.0,0.6956,0.7759,0.7024,0.4634,1.0,1.0,1.0,1.0,0.8526,0.7042,1.0,0.7099,0.7468,0.8456,0.621,0.8967,0.5031,1.0,0.9573,0.8827,1.0,0.624,0.7448,0.6261,1.0,0.9516,1.0,1.0,1.0,0.9476,1.0,0.6942,1.0,1.0,1.0,1.0,0.7002,0.7607,0.9949,0.7209,0.9912,0.6275,0.683,0.669,0.5026,1.0,0.7046,0.3691,0.9844,0.6122,0.8351,0.8067,0.8901,0.8196,0.4632,1.0,0.8242,0.781,1.0,0.7795,0.6964,0.8773,0.7579,0.7976,0.6261,1.0,0.8484,0.9619,1.0,1.0,0.9325,1.0,0.9448,0.951,1.0,1.0,1.0,0.6864,0.5661,1.0,0.9842,1.0,0.5409,0.567,0.5162,1.0,1.0,1.0,1.0,1.0,0.9256,1.0,0.561,0.5386,0.5609,1.0,1.0,0.9435,0.8134,1.0,0.8455,1.0,0.9059,0.5993,0.4621,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.8107,1.0,0.6151,1.0,1.0,0.8835,1.0,0.1986,0.075,1.0,1.0,1.0,1.0,1.0,1.0,0.831,1.0,1.0,1.0,1.0,0.947,1.0,0.4508,0.4158,1.0,0.804,1.0,0.7254,0.7975,0.7551,0.4999,1.0,1.0,0.9856,1.0,0.7458,0.9275,0.9987,0.7232,1.0,1.0,1.0,1.0,0.6907,1.0,1.0,0.8486,1.0,0.4758,0.7087,0.6608,1.0,1.0,1.0,0.9357,1.0,0.7976,1.0,0.5927,0.6091,1.0,1.0,0.9889,0.6305,0.7313,1.0,0.6209,0.8783,0.7592,0.8103,0.6481,0.4137,1.0,0.727,0.974,1.0,0.8213,1.0,0.8988,0.9332,1.0,0.8321,1.0,1.0,0.9814,1.0,0.816,0.946,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.8577,0.7586,0.1886,0.5529,1.0,1.0,1.0,0.5277,0.5507,0.5682,1.0,1.0,1.0,1.0,1.0,0.9037,1.0,0.6882,0.5086,0.5014,1.0,1.0,1.0,0.7875,1.0,0.897,1.0,0.8421,0.553,0.4766,1.0,1.0,1.0,1.0,1.0,1.0,0.9681,1.0,1.0,1.0,1.0,0.8857,1.0,0.6404,1.0,1.0,1.0,1.0,0.5874,0.5157,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.5551,0.586,0.5345,1.0,1.0,0.82,1.0,1.0,0.9165,1.0,0.9544,0.5285,0.9629,1.0,1.0,1.0,1.0,1.0,0.9921,0.9931,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.86,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.4873,1.0,0.9368,0.8253,0.7218,0.4014,0.764,0.4941,0.4759,0.2821,0.2736,0.1455,0.0677]);
const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => [...document.querySelectorAll(selector)];
const STORAGE_KEY = 'kaost032_signal_studio_definitive_state_v1';
const CONFIG_VERSION = 1;
const STYLE_PROFILES = {
  obsidian: { bg:'#000000', primary:'#f4f7fb', accentA:'#2f7cff', accentB:'#8bb7ff', descriptor:'BLACK / WHITE / BLUE' },
  monochrome: { bg:'#000000', primary:'#ffffff', accentA:'#dfe5ef', accentB:'#9ca6b7', descriptor:'MONOCHROME EDITORIAL' },
  blueprint: { bg:'#000000', primary:'#f2f6ff', accentA:'#4e92ff', accentB:'#9bc2ff', descriptor:'BLUEPRINT PULSE' },
  frost: { bg:'#000000', primary:'#f7fbff', accentA:'#7ab0ff', accentB:'#cfe0ff', descriptor:'FROST SIGNAL' },
  museum: { bg:'#000000', primary:'#f1f1f1', accentA:'#5a85d6', accentB:'#aab8d8', descriptor:'MUSEUM MONO' },
  afterblue: { bg:'#000000', primary:'#ffffff', accentA:'#1f69ff', accentB:'#7da9ff', descriptor:'AFTERBLUE LUXE' },
  cobalt: { bg:'#000000', primary:'#fbfdff', accentA:'#3a86ff', accentB:'#a5c8ff', descriptor:'COBALT CATHEDRAL' },
  ink: { bg:'#000000', primary:'#ffffff', accentA:'#d8e4ff', accentB:'#7ea4ff', descriptor:'INK WHITE' },
  deep: { bg:'#000000', primary:'#f3f7ff', accentA:'#2d6bff', accentB:'#7eacff', descriptor:'DEEP FOCUS' },
};
const DEFAULTS = Object.freeze({
  styleProfile:'obsidian', preset:'halo', shape:'circle', intensity:96, sensitivity:128, smoothing:84, density:164, zoom:38,
  bloom:64, trails:28, chroma:12, particles:62, vignette:60, directorStrength:62, typeScale:102,
  showGrid:true, showNoise:false, showOrbit:false, showStats:true, showRibbon:true, showFlash:true,
  autoDirector:false, showIntro:true, showOutro:true, cleanMode:true,
  bg:'#000000', primary:'#f4f7fb', accentA:'#2f7cff', accentB:'#8bb7ff'
});

class SignalStudioObsidian {
  constructor() {
    this.canvas = $('#visualizerCanvas');
    this.ctx = this.canvas.getContext('2d', { alpha: false, desynchronized: true });
    this.waveformCanvas = $('#waveformCanvas');
    this.waveformCtx = this.waveformCanvas.getContext('2d');
    this.audio = $('#audioElement');
    this.video = $('#videoElement');
    this.image = $('#imageElement');
    this.mediaType = 'video';
    this.mediaReady = false;
    this.audioContext = null; this.sourceNode = null; this.analyser = null; this.monitorGain = null; this.captureGain = null; this.captureDestination = null;
    this.freqData = new Uint8Array(1024); this.timeData = new Uint8Array(2048);
    this.waveformPeaks = [];
    this.objectUrls = { visual:null, audio:null };
    this.isExporting = false; this.cancelledExport = false; this.mediaRecorder = null; this.exportChunks = []; this.exportMime = ''; this.downloadUrl = null; this.wakeLock = null;
    this.lastTimestamp = performance.now(); this.lastBeat = 0; this.beatPulse = 0; this.energyAverage = .13; this.glitchSeed = 0; this.frameCount = 0;
    this.metrics = { bass:0, lowMid:0, mid:0, high:0, rms:0 };
    this.smoothMetrics = { bass:0, lowMid:0, mid:0, high:0, rms:0 };
    this.settings = { ...DEFAULTS };
    this.format = 'square'; this.quality = 1080; this.activeTab = 'media'; this.persistTimer = null;
    this.particles = this.createParticles(42); this.shockwaves = []; this.impactBursts = []; this.gridBursts = []; this.progressSparks = [];
    this.sceneNames = { intro:'QUIET INTRO', frame:'PRECISION FRAME', halo:'HALO RING', haloPlus:'HALO CHAMBER', bars:'MONOLITH BARS', line:'WAVE LINE', minimal:'MINIMAL FOCUS', outro:'FINAL FADE' };
    this.hadFirstFrame = false;
    this.bindEvents(); this.detectExportSupport(); this.resizeCanvas(false); this.applyStyleProfile(DEFAULTS.styleProfile, true); this.loadDefaultMedia(); this.restorePersistedConfig(false); this.syncConfigBox(); this.render(performance.now());
  }

  bindEvents() {
    $('#playButton').addEventListener('click', () => this.togglePlayback());
    $('#seekRange').addEventListener('input', (event) => { if (this.isExporting) return; if (Number.isFinite(this.audio.duration)) this.audio.currentTime = (Number(event.target.value) / 1000) * this.audio.duration; });
    $('#fullscreenButton').addEventListener('click', () => this.toggleFullscreen());
    this.audio.addEventListener('loadedmetadata', () => this.updateDuration());
    this.audio.addEventListener('timeupdate', () => this.updateTransport());
    this.audio.addEventListener('play', () => $('#playButton').classList.add('playing'));
    this.audio.addEventListener('pause', () => $('#playButton').classList.remove('playing'));
    this.audio.addEventListener('ended', () => { $('#playButton').classList.remove('playing'); if (!this.isExporting) this.audio.currentTime = 0; });
    this.video.addEventListener('loadeddata', () => { this.mediaReady = true; this.video.play().catch(() => {}); });
    this.image.addEventListener('load', () => { this.mediaReady = true; });
    $('#visualInput').addEventListener('change', (event) => this.loadVisualFile(event.target.files?.[0]));
    $('#audioInput').addEventListener('change', (event) => this.loadAudioFile(event.target.files?.[0]));
    [['#titleInput','title'], ['#artistInput','artist'], ['#descriptorInput','descriptor'], ['#creditInput','credit'], ['#rightsInput','rights']].forEach(([s,k]) => $(s).addEventListener('input', (e) => { this.settings[k] = e.target.value; }));
    $('#styleProfileSelect').addEventListener('change', (event) => this.applyStyleProfile(event.target.value, true));
    $('#presetSelect').addEventListener('change', (event) => { this.settings.preset = event.target.value; });
    $('#shapeSelect').addEventListener('change', (event) => { this.settings.shape = event.target.value; });

    const rangeBindings = [
      ['#intensityRange','#intensityOutput','intensity', v=>`${v}%`], ['#sensitivityRange','#sensitivityOutput','sensitivity', v=>`${v}%`], ['#smoothingRange','#smoothingOutput','smoothing', v=>`${v}%`], ['#densityRange','#densityOutput','density', v=>`${v}`], ['#zoomRange','#zoomOutput','zoom', v=>`${v}%`], ['#bloomRange','#bloomOutput','bloom', v=>`${v}%`], ['#trailsRange','#trailsOutput','trails', v=>`${v}%`], ['#chromaRange','#chromaOutput','chroma', v=>`${v}%`], ['#particlesRange','#particlesOutput','particles', v=>`${v}%`], ['#vignetteRange','#vignetteOutput','vignette', v=>`${v}%`], ['#directorStrengthRange','#directorStrengthOutput','directorStrength', v=>`${v}%`], ['#typeScaleRange','#typeScaleOutput','typeScale', v=>`${v}%`]
    ];
    for (const [inputSel, outputSel, key, formatter] of rangeBindings) {
      $(inputSel).addEventListener('input', (event) => { const value = Number(event.target.value); this.settings[key] = value; $(outputSel).value = formatter(value); if (key === 'smoothing' && this.analyser) this.analyser.smoothingTimeConstant = value / 100; });
    }

    [['#bgColor','bg'], ['#primaryColor','primary'], ['#accentAColor','accentA'], ['#accentBColor','accentB']].forEach(([s,k]) => $(s).addEventListener('input', (e) => { this.settings[k] = e.target.value; }));
    $('#resetDesignButton').addEventListener('click', () => this.resetDesign());

    this.bindToggle('#gridToggle','showGrid'); this.bindToggle('#noiseToggle','showNoise'); this.bindToggle('#orbitToggle','showOrbit'); this.bindToggle('#statsToggle','showStats'); this.bindToggle('#ribbonToggle','showRibbon'); this.bindToggle('#flashToggle','showFlash'); this.bindToggle('#autoDirectorToggle','autoDirector'); this.bindToggle('#introToggle','showIntro'); this.bindToggle('#outroToggle','showOutro'); this.bindToggle('#cleanModeToggle','cleanMode');
    $$('.tab').forEach((tab) => tab.addEventListener('click', () => this.activateTab(tab.dataset.tab)));
    $$('.format-option').forEach((button) => button.addEventListener('click', () => this.setFormat(button.dataset.format)));
    $('#qualitySelect').addEventListener('change', (event) => { this.quality = Number(event.target.value); this.updateResolutionLabel(); });
    $('#exportButton').addEventListener('click', () => this.startExport());
    $('#cancelExportButton').addEventListener('click', () => this.cancelExport());
    $('#copyConfigButton').addEventListener('click', () => this.copyConfigToClipboard());
    $('#pasteConfigButton').addEventListener('click', () => this.pasteConfigFromClipboard());
    $('#applyConfigButton').addEventListener('click', () => this.applyConfigFromBox());
    $('#downloadConfigButton').addEventListener('click', () => this.downloadConfigFile());
    $('#restoreAutosaveButton').addEventListener('click', () => this.restorePersistedConfig(true));
    $('#clearAutosaveButton').addEventListener('click', () => this.clearPersistedConfig());
    $('#loadConfigFileButton').addEventListener('click', () => $('#configFileInput').click());
    $('#configFileInput').addEventListener('change', (event) => this.loadConfigFile(event.target.files?.[0]));
    $('#configBox').addEventListener('input', () => this.setConfigStatus('Config editada. Usa “Aplicar desde caja” para restaurarla.', false));
    window.addEventListener('resize', () => this.fitStage());
    document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'visible' && this.wakeLock === null && this.isExporting) this.requestWakeLock(); });
    window.addEventListener('keydown', (event) => this.handleKeydown(event));
    const controlPanel = $('.control-panel');
    ['input','change','click'].forEach((type) => controlPanel.addEventListener(type, () => { clearTimeout(this.persistTimer); this.persistTimer = setTimeout(() => this.persistState('Autosave actualizado.'), 140); }));
  }

  bindToggle(selector, key) { const button = $(selector); button.addEventListener('click', () => { this.settings[key] = !this.settings[key]; button.classList.toggle('active', this.settings[key]); button.setAttribute('aria-pressed', String(this.settings[key])); this.syncConfigBox(); }); }
  handleKeydown(event) { if (event.repeat) return; const tag = document.activeElement?.tagName; if (tag === 'INPUT' || tag === 'SELECT' || document.activeElement?.isContentEditable) return; if (event.code === 'Space') { event.preventDefault(); this.togglePlayback(); } if (event.key === 'f' || event.key === 'F') { event.preventDefault(); this.toggleFullscreen(); } if (event.key === 'e' || event.key === 'E') { event.preventDefault(); this.startExport(); } }

  async loadDefaultMedia() { this.waveformPeaks = [...DEFAULT_WAVEFORM]; this.drawWaveform(); this.settings.title = $('#titleInput').value; this.settings.artist = $('#artistInput').value; this.settings.descriptor = $('#descriptorInput').value; this.settings.credit = $('#creditInput').value; this.settings.rights = $('#rightsInput').value; this.video.play().catch(() => {}); try { const response = await fetch(this.audio.src); if (!response.ok) throw new Error('No se pudo cargar el audio por defecto'); const arrayBuffer = await response.arrayBuffer(); await this.buildWaveform(arrayBuffer); } catch (error) { console.warn(error); this.waveformPeaks = [...DEFAULT_WAVEFORM]; this.drawWaveform(); } }
  async ensureAudioGraph() { if (this.audioContext) { if (this.audioContext.state === 'suspended') await this.audioContext.resume(); return; } const AC = window.AudioContext || window.webkitAudioContext; if (!AC) throw new Error('Este navegador no admite Web Audio API.'); this.audioContext = new AC({ latencyHint:'playback' }); this.sourceNode = this.audioContext.createMediaElementSource(this.audio); this.analyser = this.audioContext.createAnalyser(); this.analyser.fftSize = 2048; this.analyser.smoothingTimeConstant = this.settings.smoothing / 100; this.analyser.minDecibels = -92; this.analyser.maxDecibels = -12; this.monitorGain = this.audioContext.createGain(); this.captureGain = this.audioContext.createGain(); this.captureDestination = this.audioContext.createMediaStreamDestination(); this.sourceNode.connect(this.analyser); this.analyser.connect(this.monitorGain); this.analyser.connect(this.captureGain); this.monitorGain.connect(this.audioContext.destination); this.captureGain.connect(this.captureDestination); this.freqData = new Uint8Array(this.analyser.frequencyBinCount); this.timeData = new Uint8Array(this.analyser.fftSize); await this.audioContext.resume(); }
  async togglePlayback() { if (this.isExporting) return; try { await this.ensureAudioGraph(); if (this.audio.paused) { await this.audio.play(); if (this.mediaType === 'video') this.video.play().catch(() => {}); } else this.audio.pause(); } catch (error) { this.showCompatibilityError(error.message); } }

  async loadVisualFile(file) { if (!file) return; if (!file.type.startsWith('image/') && !file.type.startsWith('video/')) { this.showCompatibilityError('El archivo central debe ser una imagen o un video.'); return; } if (this.objectUrls.visual) URL.revokeObjectURL(this.objectUrls.visual); this.objectUrls.visual = URL.createObjectURL(file); $('#visualFilename').textContent = file.name; this.mediaReady = false; if (file.type.startsWith('video/')) { this.mediaType = 'video'; this.image.hidden = true; this.video.hidden = false; this.video.src = this.objectUrls.visual; this.video.load(); this.video.play().catch(() => {}); } else { this.mediaType = 'image'; this.video.pause(); this.video.hidden = true; this.image.hidden = false; this.image.src = this.objectUrls.visual; } this.persistState('Archivo visual enlazado.'); }
  async loadAudioFile(file) { if (!file) return; if (!file.type.startsWith('audio/')) { this.showCompatibilityError('Selecciona un archivo de audio válido.'); return; } this.audio.pause(); if (this.objectUrls.audio) URL.revokeObjectURL(this.objectUrls.audio); this.objectUrls.audio = URL.createObjectURL(file); this.audio.src = this.objectUrls.audio; this.audio.load(); $('#audioFilename').textContent = file.name; const inferredTitle = file.name.replace(/\.[^.]+$/, '').replace(/[_-]+/g, ' ').trim().toUpperCase(); if (inferredTitle) { $('#titleInput').value = inferredTitle; this.settings.title = inferredTitle; } try { await this.buildWaveform(await file.arrayBuffer()); } catch (error) { console.warn(error); } this.persistState('Archivo de audio enlazado.'); }

  async buildWaveform(arrayBuffer) { const AC = window.AudioContext || window.webkitAudioContext; const context = this.audioContext || new AC(); const buffer = await context.decodeAudioData(arrayBuffer.slice(0)); const channels = Array.from({ length: buffer.numberOfChannels }, (_, i) => buffer.getChannelData(i)); const samples = 420; const blockSize = Math.max(1, Math.floor(buffer.length / samples)); const peaks = []; for (let i = 0; i < samples; i++) { const start = i * blockSize; const end = Math.min(buffer.length, start + blockSize); let peak = 0; for (let j = start; j < end; j += Math.max(1, Math.floor(blockSize / 90))) { let sum = 0; for (const channel of channels) sum += Math.abs(channel[j] || 0); peak = Math.max(peak, sum / channels.length); } peaks.push(Math.min(1, Math.pow(peak, .68))); } this.waveformPeaks = peaks; if (!this.audioContext) context.close().catch(() => {}); this.drawWaveform(); }
  drawWaveform() { const canvas = this.waveformCanvas; const rect = canvas.getBoundingClientRect(); const dpr = Math.min(2, window.devicePixelRatio || 1); const width = Math.max(300, Math.round(rect.width * dpr)); const height = Math.max(60, Math.round(rect.height * dpr)); if (canvas.width !== width || canvas.height !== height) { canvas.width = width; canvas.height = height; } const ctx = this.waveformCtx; const impact = this.getImpactLevel ? this.getImpactLevel() : 0; const progress = Number.isFinite(this.audio.duration) && this.audio.duration > 0 ? this.audio.currentTime / this.audio.duration : 0; ctx.clearRect(0, 0, width, height); ctx.fillStyle = '#000'; ctx.fillRect(0,0,width,height); const bg = ctx.createLinearGradient(0,0,0,height); bg.addColorStop(0, 'rgba(255,255,255,.04)'); bg.addColorStop(1, 'rgba(255,255,255,.012)'); ctx.fillStyle = bg; ctx.fillRect(0,0,width,height); const peaks = this.waveformPeaks.length ? this.waveformPeaks : Array.from({ length:300 }, (_,i) => .14 + .08 * Math.sin(i * .32)); const center = height / 2; const barWidth = width / peaks.length; ctx.lineCap = 'round'; for (let i = 0; i < peaks.length; i++) { const x = i * barWidth + barWidth / 2; const nearProgress = Math.abs(i / peaks.length - progress); const ampBoost = nearProgress < .035 ? 1 + impact * .16 : 1; const amp = Math.max(2, peaks[i] * height * .42 * ampBoost); const done = i / peaks.length <= progress; ctx.strokeStyle = done ? (nearProgress < .024 ? this.settings.accentA : '#f4f7fb') : 'rgba(244,247,251,.14)'; ctx.lineWidth = Math.max(1, barWidth * (nearProgress < .024 ? .58 : .36)); ctx.beginPath(); ctx.moveTo(x, center - amp); ctx.lineTo(x, center + amp); ctx.stroke(); } const markerX = progress * width; ctx.strokeStyle = this.settings.accentA; ctx.shadowColor = this.settings.accentA; ctx.shadowBlur = 10 + impact * 16; ctx.lineWidth = Math.max(1.4, dpr * (1.3 + impact * .9)); ctx.beginPath(); ctx.moveTo(markerX, 6); ctx.lineTo(markerX, height - 6); ctx.stroke(); ctx.shadowBlur = 0; ctx.fillStyle = '#ffffff'; ctx.beginPath(); ctx.arc(markerX, center, Math.max(3, dpr * (2.8 + impact * 1.8)), 0, Math.PI * 2); ctx.fill(); this.drawProgressSparks(ctx, width, height, markerX, center, impact); ctx.strokeStyle = 'rgba(255,255,255,.10)'; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(0, center); ctx.lineTo(width, center); ctx.stroke(); }
  updateDuration() { $('#durationTime').textContent = this.formatTime(this.audio.duration || 0); $('#exportHint').textContent = `Duración aproximada: ${this.formatTime(this.audio.duration || 0)}`; }
  updateTransport() { const duration = this.audio.duration || 0; const progress = duration ? this.audio.currentTime / duration : 0; $('#seekRange').value = String(Math.round(progress * 1000)); $('#currentTime').textContent = this.formatTime(this.audio.currentTime || 0); this.drawWaveform(); if (this.isExporting) this.updateExportProgress(progress); }
  activateTab(name) { this.activeTab = name; $$('.tab').forEach((tab) => tab.classList.toggle('active', tab.dataset.tab === name)); $$('.tab-content').forEach((panel) => panel.classList.toggle('active', panel.dataset.panel === name)); }
  setFormat(format) { if (this.isExporting) return; this.format = format; $$('.format-option').forEach((button) => button.classList.toggle('active', button.dataset.format === format)); this.resizeCanvas(false); this.updateResolutionLabel(); this.syncConfigBox(); }
  resetDesign() { this.settings = { ...this.settings, ...DEFAULTS }; $('#styleProfileSelect').value = DEFAULTS.styleProfile; this.applyStyleProfile(DEFAULTS.styleProfile, true); $('#presetSelect').value = DEFAULTS.preset; $('#shapeSelect').value = DEFAULTS.shape; [['#intensityRange','#intensityOutput','intensity',true],['#sensitivityRange','#sensitivityOutput','sensitivity',true],['#smoothingRange','#smoothingOutput','smoothing',true],['#densityRange','#densityOutput','density',false],['#zoomRange','#zoomOutput','zoom',true],['#bloomRange','#bloomOutput','bloom',true],['#trailsRange','#trailsOutput','trails',true],['#chromaRange','#chromaOutput','chroma',true],['#particlesRange','#particlesOutput','particles',true],['#vignetteRange','#vignetteOutput','vignette',true],['#directorStrengthRange','#directorStrengthOutput','directorStrength',true],['#typeScaleRange','#typeScaleOutput','typeScale',true]].forEach(([i,o,k,p]) => { $(i).value = this.settings[k]; $(o).value = p ? `${this.settings[k]}%` : `${this.settings[k]}`; }); [['#gridToggle','showGrid'],['#noiseToggle','showNoise'],['#orbitToggle','showOrbit'],['#statsToggle','showStats'],['#ribbonToggle','showRibbon'],['#flashToggle','showFlash'],['#autoDirectorToggle','autoDirector'],['#introToggle','showIntro'],['#outroToggle','showOutro'],['#cleanModeToggle','cleanMode']].forEach(([selector,key]) => { $(selector).classList.toggle('active', this.settings[key]); $(selector).setAttribute('aria-pressed', String(this.settings[key])); }); if (this.analyser) this.analyser.smoothingTimeConstant = this.settings.smoothing / 100; this.syncConfigBox(); }

  applyStyleProfile(name, updateDescriptor=false) { const profile = STYLE_PROFILES[name] || STYLE_PROFILES.obsidian; this.settings.styleProfile = name; this.settings.bg = profile.bg; this.settings.primary = profile.primary; this.settings.accentA = profile.accentA; this.settings.accentB = profile.accentB; $('#bgColor').value = profile.bg; $('#primaryColor').value = profile.primary; $('#accentAColor').value = profile.accentA; $('#accentBColor').value = profile.accentB; if (updateDescriptor) { this.settings.descriptor = profile.descriptor; $('#descriptorInput').value = profile.descriptor; } this.syncConfigBox(); }


  serializeProjectState() { return { version:CONFIG_VERSION, app:'KAOST032 Signal Studio Definitive', savedAt:new Date().toISOString(), format:this.format, quality:this.quality, activeTab:this.activeTab || 'media', settings:{ ...this.settings }, labels:{ visualFilename:$('#visualFilename')?.textContent || '', audioFilename:$('#audioFilename')?.textContent || '', mediaType:this.mediaType } }; }
  buildConfigText() { return JSON.stringify(this.serializeProjectState(), null, 2); }
  syncConfigBox() { const box = $('#configBox'); if (!box) return; if (document.activeElement === box) return; box.value = this.buildConfigText(); }
  setConfigStatus(message, isError=false) { const el = $('#configStatus'); if (!el) return; el.textContent = message; el.classList.toggle('is-error', !!isError); }
  persistState(message='Autosave actualizado.') { try { const text = this.buildConfigText(); localStorage.setItem(STORAGE_KEY, text); this.syncConfigBox(); this.setConfigStatus(message, false); } catch (error) { console.warn(error); this.setConfigStatus('No se pudo guardar automáticamente en este navegador.', true); } }
  syncUiFromSettings() {
    $('#titleInput').value = this.settings.title || '';
    $('#artistInput').value = this.settings.artist || '';
    $('#descriptorInput').value = this.settings.descriptor || '';
    $('#creditInput').value = this.settings.credit || '';
    $('#rightsInput').value = this.settings.rights || '';
    if ([...$('#styleProfileSelect').options].some((o) => o.value === this.settings.styleProfile)) $('#styleProfileSelect').value = this.settings.styleProfile;
    if ([...$('#presetSelect').options].some((o) => o.value === this.settings.preset)) $('#presetSelect').value = this.settings.preset;
    if ([...$('#shapeSelect').options].some((o) => o.value === this.settings.shape)) $('#shapeSelect').value = this.settings.shape;
    const rangeSpecs = [['#intensityRange','#intensityOutput','intensity',true],['#sensitivityRange','#sensitivityOutput','sensitivity',true],['#smoothingRange','#smoothingOutput','smoothing',true],['#densityRange','#densityOutput','density',false],['#zoomRange','#zoomOutput','zoom',true],['#bloomRange','#bloomOutput','bloom',true],['#trailsRange','#trailsOutput','trails',true],['#chromaRange','#chromaOutput','chroma',true],['#particlesRange','#particlesOutput','particles',true],['#vignetteRange','#vignetteOutput','vignette',true],['#directorStrengthRange','#directorStrengthOutput','directorStrength',true],['#typeScaleRange','#typeScaleOutput','typeScale',true]];
    rangeSpecs.forEach(([input, output, key, percent]) => { $(input).value = this.settings[key]; $(output).value = percent ? `${this.settings[key]}%` : `${this.settings[key]}`; });
    [['#bgColor','bg'], ['#primaryColor','primary'], ['#accentAColor','accentA'], ['#accentBColor','accentB']].forEach(([s,k]) => $(s).value = this.settings[k]);
    [['#gridToggle','showGrid'],['#noiseToggle','showNoise'],['#orbitToggle','showOrbit'],['#statsToggle','showStats'],['#ribbonToggle','showRibbon'],['#flashToggle','showFlash'],['#autoDirectorToggle','autoDirector'],['#introToggle','showIntro'],['#outroToggle','showOutro'],['#cleanModeToggle','cleanMode']].forEach(([selector,key]) => { $(selector).classList.toggle('active', !!this.settings[key]); $(selector).setAttribute('aria-pressed', String(!!this.settings[key])); });
    $('#qualitySelect').value = String(this.quality);
    this.setFormat(this.format);
    this.activateTab(this.activeTab || 'media');
    if (this.analyser) this.analyser.smoothingTimeConstant = this.settings.smoothing / 100;
    this.updateResolutionLabel();
    this.syncConfigBox();
  }
  applySerializedConfig(payload, options={}) {
    if (!payload || typeof payload !== 'object') throw new Error('La configuración no es válida.');
    const incomingSettings = payload.settings && typeof payload.settings === 'object' ? payload.settings : {};
    this.settings = { ...this.settings, ...DEFAULTS, ...incomingSettings };
    this.format = ['square','vertical','landscape'].includes(payload.format) ? payload.format : this.format;
    this.quality = [720,1080].includes(Number(payload.quality)) ? Number(payload.quality) : this.quality;
    this.activeTab = typeof payload.activeTab === 'string' ? payload.activeTab : (this.activeTab || 'media');
    this.mediaType = payload.labels?.mediaType === 'image' ? 'image' : this.mediaType;
    if (payload.labels?.visualFilename) $('#visualFilename').textContent = payload.labels.visualFilename;
    if (payload.labels?.audioFilename) $('#audioFilename').textContent = payload.labels.audioFilename;
    this.syncUiFromSettings();
    if (!options.skipSave) this.persistState(options.statusMessage || 'Configuración aplicada y guardada.');
  }
  applyConfigText(text, options={}) { const payload = JSON.parse(text); this.applySerializedConfig(payload, options); return payload; }
  restorePersistedConfig(showStatus=true) { try { const raw = localStorage.getItem(STORAGE_KEY); if (!raw) { this.syncConfigBox(); if (showStatus) this.setConfigStatus('No se encontró autosave previo.', false); return false; } this.applyConfigText(raw, { skipSave:true }); if (showStatus) this.setConfigStatus('Última configuración restaurada.', false); return true; } catch (error) { console.warn(error); this.setConfigStatus('El autosave existe, pero no se pudo restaurar.', true); return false; } }
  async copyConfigToClipboard() { try { const text = this.buildConfigText(); if (navigator.clipboard?.writeText) await navigator.clipboard.writeText(text); else { const box = $('#configBox'); box.value = text; box.focus(); box.select(); document.execCommand('copy'); } this.syncConfigBox(); this.setConfigStatus('Configuración copiada al portapapeles.', false); } catch (error) { console.warn(error); this.setConfigStatus('No se pudo copiar automáticamente. Copia el texto desde la caja.', true); } }
  async pasteConfigFromClipboard() { try { if (!navigator.clipboard?.readText) throw new Error('Clipboard API no disponible'); const text = await navigator.clipboard.readText(); $('#configBox').value = text; this.applyConfigText(text); this.setConfigStatus('Configuración pegada y aplicada.', false); } catch (error) { console.warn(error); this.setConfigStatus('No se pudo leer el portapapeles. Pega manualmente en la caja.', true); } }
  applyConfigFromBox() { try { const text = $('#configBox').value.trim(); if (!text) throw new Error('Caja vacía'); this.applyConfigText(text); this.setConfigStatus('Configuración aplicada desde la caja.', false); } catch (error) { console.warn(error); this.setConfigStatus('La configuración escrita no es válida.', true); } }
  downloadConfigFile() { try { const blob = new Blob([this.buildConfigText()], { type:'application/json' }); const url = URL.createObjectURL(blob); const a = document.createElement('a'); a.href = url; a.download = `${this.slugify(this.settings.title || 'signal-studio')}-config.json`; document.body.appendChild(a); a.click(); a.remove(); URL.revokeObjectURL(url); this.setConfigStatus('Archivo .json descargado.', false); } catch (error) { console.warn(error); this.setConfigStatus('No se pudo descargar el archivo de configuración.', true); } }
  async loadConfigFile(file) { if (!file) return; try { const text = await file.text(); $('#configBox').value = text; this.applyConfigText(text); this.setConfigStatus('Configuración cargada desde archivo.', false); } catch (error) { console.warn(error); this.setConfigStatus('No se pudo leer el archivo de configuración.', true); } finally { $('#configFileInput').value = ''; } }
  clearPersistedConfig() { try { localStorage.removeItem(STORAGE_KEY); this.setConfigStatus('Autosave eliminado. El diseño actual sigue abierto, pero ya no se restaurará automáticamente.', false); } catch (error) { console.warn(error); this.setConfigStatus('No se pudo limpiar el autosave.', true); } }

  getOutputDimensions() { const high = this.quality === 1080; if (this.format === 'vertical') return high ? { width:1080, height:1920 } : { width:720, height:1280 }; if (this.format === 'landscape') return high ? { width:1920, height:1080 } : { width:1280, height:720 }; return high ? { width:1080, height:1080 } : { width:720, height:720 }; }
  getPreviewDimensions() { if (this.format === 'vertical') return { width:405, height:720 }; if (this.format === 'landscape') return { width:720, height:405 }; return { width:720, height:720 }; }
  resizeCanvas(forExport) { const d = forExport ? this.getOutputDimensions() : this.getPreviewDimensions(); this.canvas.width = d.width; this.canvas.height = d.height; this.fitStage(); this.hadFirstFrame = false; }
  fitStage() { const stage = $('#stageShell'); const ratio = this.canvas.width / this.canvas.height; if (window.innerWidth <= 1180) { stage.style.aspectRatio = `${this.canvas.width} / ${this.canvas.height}`; stage.style.height = 'auto'; stage.style.minHeight = '0'; this.canvas.style.width = '100%'; this.canvas.style.height = '100%'; } else { stage.style.aspectRatio = 'auto'; stage.style.height = 'min(76vh, 880px)'; stage.style.minHeight = ratio > 1.35 ? '430px' : '620px'; if (ratio > 1.35) { this.canvas.style.width = '100%'; this.canvas.style.height = 'auto'; } else { this.canvas.style.width = 'auto'; this.canvas.style.height = '100%'; } } }
  updateResolutionLabel() { const { width, height } = this.getOutputDimensions(); $('#resolutionLabel').textContent = `${width} × ${height}`; }
  detectExportSupport() { if (!window.MediaRecorder || !HTMLCanvasElement.prototype.captureStream) { $('#exportButton').disabled = true; $('#compatibilityNote').textContent = 'Este navegador no ofrece las APIs necesarias para exportar video. Usa Chrome, Edge o Safari actualizado.'; this.setEngineStatus('PREVIEW ONLY', false); return; } const mime = this.pickMimeType(); this.exportMime = mime; $('#containerLabel').textContent = mime.includes('mp4') ? 'MP4' : 'WEBM'; }
  pickMimeType() { const candidates = ['video/mp4;codecs=avc1.42E01E,mp4a.40.2','video/mp4','video/webm;codecs=vp9,opus','video/webm;codecs=vp8,opus','video/webm']; return candidates.find((type) => MediaRecorder.isTypeSupported(type)) || ''; }
  createMediaRecorder(stream, videoBitsPerSecond) { const candidates = ['video/mp4;codecs=avc1.42E01E,mp4a.40.2','video/mp4','video/webm;codecs=vp9,opus','video/webm;codecs=vp8,opus','video/webm'].filter((type) => MediaRecorder.isTypeSupported(type)); for (const mimeType of candidates) { try { const recorder = new MediaRecorder(stream, { mimeType, videoBitsPerSecond, audioBitsPerSecond:192000 }); this.exportMime = mimeType; $('#containerLabel').textContent = mimeType.includes('mp4') ? 'MP4' : 'WEBM'; return recorder; } catch (error) { console.warn('MediaRecorder rechazó', mimeType, error); } } try { this.exportMime = 'video/webm'; $('#containerLabel').textContent = 'WEBM'; return new MediaRecorder(stream, { videoBitsPerSecond, audioBitsPerSecond:192000 }); } catch (_) { throw new Error('El navegador no pudo iniciar un codificador de video compatible.'); } }

  async startExport() { if (this.isExporting) return; if (!window.MediaRecorder || !this.canvas.captureStream) { this.showCompatibilityError('Tu navegador no permite generar video desde Canvas.'); return; } try { await this.ensureAudioGraph(); this.exportMime = this.pickMimeType(); if (!this.exportMime) throw new Error('No se encontró un formato de video compatible.'); this.isExporting = true; document.body.classList.add('is-exporting'); this.cancelledExport = false; this.exportChunks = []; $('#downloadCard').hidden = true; $('#exportProgress').hidden = false; $('#exportButton').disabled = true; this.setEngineStatus('RECORDING', true); this.activateTab('export'); this.resizeCanvas(true); await this.requestWakeLock(); this.audio.pause(); this.audio.currentTime = 0; if (this.mediaType === 'video') { this.video.currentTime = 0; await this.video.play().catch(() => {}); } await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve))); const canvasStream = this.canvas.captureStream(30); const combinedStream = new MediaStream([...canvasStream.getVideoTracks(), ...this.captureDestination.stream.getAudioTracks()]); const bits = this.quality === 1080 ? 8500000 : 4500000; this.mediaRecorder = this.createMediaRecorder(combinedStream, bits); this.mediaRecorder.addEventListener('dataavailable', (event) => { if (event.data && event.data.size > 0) this.exportChunks.push(event.data); }); this.mediaRecorder.addEventListener('stop', () => this.finishExport(combinedStream)); this.mediaRecorder.addEventListener('error', (event) => { console.error(event.error); this.cancelledExport = true; this.mediaRecorder?.stop(); }); this.audio.addEventListener('ended', () => { if (this.isExporting && this.mediaRecorder?.state === 'recording') setTimeout(() => this.mediaRecorder.stop(), 260); }, { once:true }); this.mediaRecorder.start(1000); await this.audio.play(); this.updateExportProgress(0); } catch (error) { console.error(error); this.isExporting = false; document.body.classList.remove('is-exporting'); $('#exportButton').disabled = false; $('#exportProgress').hidden = true; this.resizeCanvas(false); this.setEngineStatus('READY', false); await this.releaseWakeLock(); this.showCompatibilityError(error.message || 'No se pudo iniciar la exportación.'); } }
  cancelExport() { if (!this.isExporting) return; this.cancelledExport = true; this.audio.pause(); if (this.mediaRecorder && this.mediaRecorder.state !== 'inactive') this.mediaRecorder.stop(); }
  async finishExport(stream) { stream.getTracks().forEach((track) => track.stop()); this.audio.pause(); this.isExporting = false; document.body.classList.remove('is-exporting'); $('#exportButton').disabled = false; $('#exportProgress').hidden = true; this.resizeCanvas(false); this.setEngineStatus('READY', false); await this.releaseWakeLock(); if (this.cancelledExport || !this.exportChunks.length) return; const blob = new Blob(this.exportChunks, { type:this.exportMime }); if (this.downloadUrl) URL.revokeObjectURL(this.downloadUrl); this.downloadUrl = URL.createObjectURL(blob); const extension = this.exportMime.includes('mp4') ? 'mp4' : 'webm'; const filename = `${this.slugify(this.settings.title || 'signal-video')}-${this.slugify(this.settings.artist || 'KAOST032')}.${extension}`; const link = $('#downloadLink'); link.href = this.downloadUrl; link.download = filename; $('#downloadFilename').textContent = filename; $('#downloadCard').hidden = false; $('#downloadCard').scrollIntoView({ behavior:'smooth', block:'nearest' }); }
  updateExportProgress(progress) { const percent = Math.max(0, Math.min(100, Math.round(progress * 100))); $('#exportPercent').textContent = `${percent}%`; $('#exportProgressBar').style.width = `${percent}%`; $('#exportStatus').textContent = `RENDER ${this.formatTime(this.audio.currentTime)} / ${this.formatTime(this.audio.duration || 0)}`; }
  async requestWakeLock() { try { if ('wakeLock' in navigator) this.wakeLock = await navigator.wakeLock.request('screen'); } catch (error) { console.warn('Wake Lock no disponible:', error); } }
  async releaseWakeLock() { try { await this.wakeLock?.release(); } catch (_) {} this.wakeLock = null; }
  toggleFullscreen() { const stage = $('#stageShell'); if (!document.fullscreenElement) stage.requestFullscreen?.().catch(() => {}); else document.exitFullscreen?.(); }
  setEngineStatus(text, recording) { const status = $('#engineStatus'); status.lastChild.textContent = ` ${text}`; status.classList.toggle('recording', recording); }
  showCompatibilityError(message) { $('#compatibilityNote').textContent = message; this.activateTab('export'); }

  render(timestamp) { const dt = Math.min(.05, (timestamp - this.lastTimestamp) / 1000 || .016); this.lastTimestamp = timestamp; this.frameCount++; this.readAudio(timestamp, dt); this.drawScene(timestamp / 1000, dt); requestAnimationFrame((next) => this.render(next)); }
  readAudio(timestamp, dt) { const active = this.analyser && !this.audio.paused; if (active) { this.analyser.getByteFrequencyData(this.freqData); this.analyser.getByteTimeDomainData(this.timeData); const sr = this.audioContext.sampleRate; this.metrics.bass = this.bandAverage(24, 170, sr); this.metrics.lowMid = this.bandAverage(170, 620, sr); this.metrics.mid = this.bandAverage(620, 2700, sr); this.metrics.high = this.bandAverage(2700, 14500, sr); let sum = 0; for (let i = 0; i < this.timeData.length; i += 4) { const v = (this.timeData[i] - 128) / 128; sum += v * v; } this.metrics.rms = Math.sqrt(sum / (this.timeData.length / 4)); } else { const t = timestamp * .001; this.metrics.bass = .055 + Math.sin(t * 1.2) * .012; this.metrics.lowMid = .06 + Math.sin(t * .8 + 1) * .012; this.metrics.mid = .055 + Math.sin(t * 1.7 + 2) * .01; this.metrics.high = .04 + Math.sin(t * 2.2 + 3) * .008; this.metrics.rms = .025; } const follow = 1 - Math.pow(.002, dt); for (const key of Object.keys(this.smoothMetrics)) this.smoothMetrics[key] += (this.metrics[key] - this.smoothMetrics[key]) * follow; this.energyAverage = this.energyAverage * .965 + this.metrics.bass * .035; const beatThreshold = Math.max(.19, this.energyAverage * 1.34); if (active && this.metrics.bass > beatThreshold && timestamp - this.lastBeat > 145) { this.lastBeat = timestamp; this.beatPulse = Math.min(1.35, .42 + this.metrics.bass * 1.25); this.glitchSeed = Math.random(); this.spawnShockwave(); } this.beatPulse *= Math.pow(.055, dt); }
  bandAverage(minHz, maxHz, sampleRate) { const nyquist = sampleRate / 2; const start = Math.max(0, Math.floor((minHz / nyquist) * this.freqData.length)); const end = Math.min(this.freqData.length - 1, Math.ceil((maxHz / nyquist) * this.freqData.length)); let sum = 0; for (let i = start; i <= end; i++) sum += this.freqData[i]; return (sum / Math.max(1, end - start + 1)) / 255; }
  getImpactLevel() { return Math.min(1.85, this.smoothMetrics.bass * 1.2 + this.smoothMetrics.mid * .34 + this.beatPulse * 1.95); }
  getCenterPulseScale() { const impact = this.getImpactLevel(); return 1 + this.beatPulse * (.018 + this.settings.zoom / 3600) + this.smoothMetrics.rms * (.028 + this.settings.zoom / 2400) + impact * .008; }
  frequencyValue(t) { if (!this.analyser || this.audio.paused) { const idle = .05 + .025 * Math.sin(performance.now() * .0018 + t * 26); return Math.max(0, idle); } const minHz = 34; const maxHz = 15000; const hz = minHz * Math.pow(maxHz / minHz, Math.min(1, Math.max(0, t))); const bin = Math.min(this.freqData.length - 1, Math.floor((hz / (this.audioContext.sampleRate / 2)) * this.freqData.length)); const raw = this.freqData[bin] / 255; const neighbor = this.freqData[Math.min(this.freqData.length - 1, bin + 2)] / 255; return Math.pow((raw * .72 + neighbor * .28) * (this.settings.sensitivity / 100), 1.28); }
  spawnShockwave() { this.shockwaves.push({ age:0, life:1.05, radius:.18 + Math.random() * .04 }); if (this.shockwaves.length > 5) this.shockwaves.shift(); this.impactBursts.push({ age:0, life:.42, rays:10 + Math.floor(Math.random()*6), seed:Math.random()*Math.PI*2 }); if (this.impactBursts.length > 8) this.impactBursts.shift(); this.gridBursts.push({ age:0, life:.58, strength:.4 + Math.random()*.45 }); if (this.gridBursts.length > 6) this.gridBursts.shift(); }

  getActiveScene() { if (!this.settings.autoDirector || !this.audio.duration) return this.settings.preset; const p = this.audio.currentTime / this.audio.duration; const strength = this.settings.directorStrength / 100; if (p < 0.12) return 'minimal'; if (p < 0.30) return strength > .25 ? 'frame' : this.settings.preset; if (p < 0.50) return strength > .45 ? 'halo' : 'frame'; if (p < 0.72) return strength > .62 ? 'haloPlus' : 'halo'; if (p < 0.9) return strength > .5 ? 'line' : 'frame'; return 'minimal'; }

  drawScene(time, dt) {
    const ctx = this.ctx; const w = this.canvas.width; const h = this.canvas.height; const min = Math.min(w,h);
    if (!this.hadFirstFrame || this.settings.trails < 1) { ctx.clearRect(0,0,w,h); this.hadFirstFrame = true; }
    else { const fade = this.settings.cleanMode ? .24 + (100 - this.settings.trails) / 190 : .3 + (100 - this.settings.trails) / 145; ctx.fillStyle = `rgba(0,0,0,${Math.min(.82, fade)})`; ctx.fillRect(0,0,w,h); }
    ctx.fillStyle = this.settings.bg; ctx.fillRect(0,0,w,h);
    const layout = this.getLayout(w,h);
    const scene = this.getActiveScene();
    $('#directorStatus').textContent = `Scene: ${this.sceneNames[scene] || scene.toUpperCase()}`;
    this.drawBackground(ctx, w, h, time);
    if (this.settings.showGrid) this.drawGrid(ctx, w, h, time);
    if (this.settings.showOrbit && !this.settings.cleanMode) this.drawOrbit(ctx, layout, time);
    this.drawShockwaves(ctx, layout, dt);
    this.drawCenterMedia(ctx, layout, time);
    this.drawImpactBursts(ctx, layout, dt, time);
    if (scene === 'frame') this.drawPrecisionFrame(ctx, layout, time);
    else if (scene === 'halo') this.drawHalo(ctx, layout, time);
    else if (scene === 'haloPlus') this.drawHaloPlus(ctx, layout, time);
    else if (scene === 'bars') this.drawMonolithBars(ctx, layout, time);
    else if (scene === 'line') this.drawWaveLine(ctx, layout, time);
    else this.drawMinimalFocus(ctx, layout, time);
    if (this.settings.showRibbon) this.drawRibbon(ctx, layout);
    this.drawMetadata(ctx, layout);
    this.drawCinemaCards(ctx, w, h);
    this.drawBeatFlash(ctx, w, h);
    this.drawParticles(ctx, w, h, time, dt);
    this.drawVignette(ctx, w, h);
    if (this.settings.showNoise && !this.settings.cleanMode) this.drawNoise(ctx, w, h, time);
  }

  getLayout(w,h) { const min = Math.min(w,h); const vertical = h / w > 1.35; const landscape = w / h > 1.35; const size = vertical ? min * .58 : landscape ? min * .56 : min * .48; return { w, h, min, vertical, landscape, centerX:w*.5, centerY: vertical ? h*.42 : landscape ? h*.45 : h*.455, size }; }

  drawBackground(ctx, w, h, time) {
    const source = this.currentVisualSource();
    if (source && this.mediaReady) {
      ctx.save();
      const blur = this.settings.cleanMode ? Math.round(Math.min(w,h) * .022) : Math.round(Math.min(w,h) * .03);
      ctx.filter = `blur(${blur}px) saturate(1.02) brightness(.12) contrast(1.02)`;
      ctx.globalAlpha = .34;
      this.drawCover(ctx, source, -w*.05, -h*.05, w*1.10, h*1.10);
      ctx.restore();
    }
    const glowAlpha = .02 + this.smoothMetrics.bass * .05 + (this.settings.bloom / 100) * .05;
    const a = this.hexToRgb(this.settings.accentA); const b = this.hexToRgb(this.settings.accentB);
    ctx.globalCompositeOperation = 'screen';
    const g1 = ctx.createRadialGradient(w*.2, h*.22, 0, w*.2, h*.22, Math.max(w,h)*.34);
    g1.addColorStop(0, `rgba(${a.r},${a.g},${a.b},${glowAlpha})`); g1.addColorStop(1, 'rgba(0,0,0,0)'); ctx.fillStyle = g1; ctx.fillRect(0,0,w,h);
    const g2 = ctx.createRadialGradient(w*.82, h*.72, 0, w*.82, h*.72, Math.max(w,h)*.36);
    g2.addColorStop(0, `rgba(${b.r},${b.g},${b.b},${glowAlpha*.9})`); g2.addColorStop(1, 'rgba(0,0,0,0)'); ctx.fillStyle = g2; ctx.fillRect(0,0,w,h);
    ctx.globalCompositeOperation = 'source-over';
  }

  drawGrid(ctx, w, h, time) {
    const min = Math.min(w, h); const cx = w * .5; const cy = h * .455; const impact = this.getImpactLevel(); const spacing = Math.max(46, min * .082); const amplitude = min * (.006 + impact * .02); const influenceRadius = min * .52; const lineBoost = 1 + impact * .95; const burstEnergy = this.gridBursts.reduce((sum, b) => sum + Math.max(0, 1 - b.age / b.life) * b.strength, 0);
    ctx.save(); ctx.strokeStyle = 'rgba(255,255,255,.11)'; ctx.lineWidth = Math.max(1, 1.05 * lineBoost + burstEnergy * .45); ctx.globalCompositeOperation = 'screen';
    const xShift = (time * 18) % spacing; const yShift = (time * 8) % spacing;
    for (let x0 = -spacing + xShift; x0 < w + spacing; x0 += spacing) {
      ctx.beginPath();
      for (let y = -spacing; y <= h + spacing; y += Math.max(14, spacing * .18)) {
        const dxCenter = x0 - cx; const dyCenter = y - cy; const dist = Math.hypot(dxCenter, dyCenter); const influence = Math.max(0, 1 - dist / influenceRadius); const wave = Math.sin((y / h) * Math.PI * 4 + time * 2.2 + x0 * .012); const burstWarp = Math.sin(time * 5.2 + dist * .014) * burstEnergy * min * .008 * influence; const offset = wave * amplitude * influence + Math.sign(dxCenter || 1) * influence * impact * min * .012 + burstWarp;
        const x = x0 + offset; if (y <= -spacing) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }
    for (let y0 = -spacing + yShift; y0 < h + spacing; y0 += spacing) {
      ctx.beginPath();
      for (let x = -spacing; x <= w + spacing; x += Math.max(14, spacing * .18)) {
        const dxCenter = x - cx; const dyCenter = y0 - cy; const dist = Math.hypot(dxCenter, dyCenter); const influence = Math.max(0, 1 - dist / influenceRadius); const wave = Math.sin((x / w) * Math.PI * 4 + time * 2.6 + y0 * .012); const burstWarp = Math.cos(time * 5.6 + dist * .013) * burstEnergy * min * .008 * influence; const offset = wave * amplitude * influence + Math.sign(dyCenter || 1) * influence * impact * min * .012 + burstWarp;
        const y = y0 + offset; if (x <= -spacing) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }
    this.gridBursts = this.gridBursts.filter((b) => { b.age += 1/60; return b.age < b.life; });
    ctx.restore();
  }


  drawOrbit(ctx, layout, time) {
    const { centerX:cx, centerY:cy, min } = layout; ctx.save(); ctx.globalCompositeOperation = 'screen'; ctx.strokeStyle = 'rgba(255,255,255,.035)'; ctx.lineWidth = Math.max(1, min*.001);
    for (let i = 0; i < 2; i++) { const rx = min*(.33 + i*.06); const ry = min*(.20 + i*.03); ctx.beginPath(); for (let a=0; a<=Math.PI*2+.08; a+=.08) { const x = cx + Math.cos(a + time*(.08 + i*.01))*rx; const y = cy + Math.sin(a + time*(.06 + i*.01))*ry; if (a===0) ctx.moveTo(x,y); else ctx.lineTo(x,y); } ctx.stroke(); }
    ctx.restore();
  }

  drawShockwaves(ctx, layout, dt) {
    if (this.settings.cleanMode && this.settings.directorStrength < 30) { this.shockwaves = []; return; }
    const { centerX:cx, centerY:cy, size, min } = layout; ctx.save(); ctx.globalCompositeOperation = 'screen';
    this.shockwaves = this.shockwaves.filter((wave) => {
      wave.age += dt; const t = wave.age / wave.life; if (t >= 1) return false;
      const r = size * (.70 + wave.radius + t*.28); const alpha = (1 - t) * .14;
      ctx.strokeStyle = this.withAlpha(this.settings.accentA, alpha); ctx.lineWidth = Math.max(1, min*.0014*(1 - t*.3)); ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI*2); ctx.stroke();
      return true;
    }); ctx.restore();
  }

  drawCenterMedia(ctx, layout, time) {
    const { centerX:cx, centerY:cy, size, min } = layout; const pulseScale = this.getCenterPulseScale(); const drawSize = size * pulseScale; const x = cx - drawSize/2; const y = cy - drawSize/2; const radius = this.settings.shape === 'circle' ? drawSize/2 : this.settings.shape === 'square' ? min*.006 : min*.024; const high = this.smoothMetrics.high; const offset = this.settings.chroma > 0 ? (high * min * .0018 + this.beatPulse * min * .0016) : 0;
    ctx.save(); this.roundedPath(ctx, x, y, drawSize, drawSize, radius); ctx.clip(); ctx.fillStyle = '#000'; ctx.fillRect(x,y,drawSize,drawSize);
    const source = this.currentVisualSource(); const zoom = 1.04 + this.smoothMetrics.bass * .05 + this.beatPulse * .03;
    if (source && this.mediaReady) {
      if (this.settings.chroma > 0 && !this.settings.cleanMode) {
        ctx.save(); ctx.globalAlpha = .10; this.drawTintedCover(ctx, source, x - offset, y, drawSize * zoom, drawSize * zoom, this.settings.accentA); this.drawTintedCover(ctx, source, x + offset, y, drawSize * zoom, drawSize * zoom, this.settings.accentB); ctx.restore();
      }
      this.drawCover(ctx, source, x - (drawSize*(zoom-1)/2), y - (drawSize*(zoom-1)/2), drawSize*zoom, drawSize*zoom);
    }
    const shade = ctx.createLinearGradient(x, y, x, y + drawSize); shade.addColorStop(0, 'rgba(0,0,0,.02)'); shade.addColorStop(.78, 'rgba(0,0,0,.04)'); shade.addColorStop(1, 'rgba(0,0,0,.16)'); ctx.fillStyle = shade; ctx.fillRect(x,y,drawSize,drawSize); ctx.restore();
    ctx.save(); ctx.lineWidth = Math.max(1.6, min*.0024); ctx.strokeStyle = this.withAlpha(this.settings.primary, .92); this.roundedPath(ctx, x, y, drawSize, drawSize, radius); ctx.stroke(); ctx.strokeStyle = this.withAlpha(this.settings.accentA, .32 + this.beatPulse*.18); ctx.lineWidth = Math.max(1, min*.0011); this.roundedPath(ctx, x + min*.006, y + min*.006, drawSize - min*.012, drawSize - min*.012, Math.max(0, radius - min*.005)); ctx.stroke(); ctx.restore();
    if (this.settings.shape !== 'circle') this.drawCornerMarks(ctx, x, y, drawSize, min);
  }

  drawPrecisionFrame(ctx, layout, time) {
    const { centerX:cx, centerY:cy, size, min } = layout; const margin = min*.038; const half = size/2 + margin; const count = Math.max(40, Math.round(this.settings.density * .62)); const maxLength = min*.08 * (this.settings.intensity / 82); ctx.save(); ctx.globalCompositeOperation = 'screen'; ctx.lineCap = 'round';
    for (let i=0; i<count; i++) { const p = i / count; const edge = Math.floor(p*4); const local = (p*4) % 1; let x,y,nx,ny; if (edge===0) { x = cx - half + local*half*2; y = cy - half; nx=0; ny=-1; } else if (edge===1) { x = cx + half; y = cy - half + local*half*2; nx=1; ny=0; } else if (edge===2) { x = cx + half - local*half*2; y = cy + half; nx=0; ny=1; } else { x = cx - half; y = cy + half - local*half*2; nx=-1; ny=0; } const value = this.frequencyValue((p*.88 + .04)%1); const len = min*.005 + value*maxLength; const tint = i % 17 === 0 ? this.settings.accentA : this.settings.primary; ctx.strokeStyle = this.withAlpha(tint, .18 + value*.62); ctx.lineWidth = Math.max(1, min*(.0012 + value*.0014)); ctx.beginPath(); ctx.moveTo(x + nx*min*.005, y + ny*min*.005); ctx.lineTo(x + nx*len, y + ny*len); ctx.stroke(); }
    ctx.strokeStyle = this.withAlpha(this.settings.primary, .18); ctx.lineWidth = Math.max(1, min*.001); ctx.setLineDash([min*.02, min*.012]); ctx.lineDashOffset = time * min*.004; ctx.strokeRect(cx - half, cy - half, half*2, half*2); ctx.setLineDash([]); ctx.restore();
  }

  drawHalo(ctx, layout, time) {
    const { centerX:cx, centerY:cy, size, min } = layout; const pulseScale = this.getCenterPulseScale(); const mediaRadius = (size * pulseScale) / 2; const innerRadius = mediaRadius + min * .014; const count = Math.max(84, Math.round(this.settings.density * .8)); const impact = this.getImpactLevel(); const maxLength = min * (.064 + (this.settings.intensity / 100) * .056) * (1 + impact * .16); const base = min * .008; ctx.save(); ctx.globalCompositeOperation = 'screen'; ctx.lineCap = 'round';
    ctx.shadowColor = this.withAlpha(this.settings.accentA, .52); ctx.shadowBlur = 18 + impact * 16;
    for (let i = 0; i < count; i++) { const t = i / count; const angle = t * Math.PI * 2 - Math.PI / 2; const value = this.frequencyValue((t * .96 + .02) % 1); const len = base + value * maxLength; const x1 = cx + Math.cos(angle) * innerRadius; const y1 = cy + Math.sin(angle) * innerRadius; const x2 = cx + Math.cos(angle) * (innerRadius + len); const y2 = cy + Math.sin(angle) * (innerRadius + len); const tint = i % 11 === 0 ? this.settings.accentA : this.settings.primary; ctx.strokeStyle = this.withAlpha(tint, .24 + value * .76 + this.beatPulse * .08); ctx.lineWidth = Math.max(1.15, min * (.00115 + value * .002 + impact * .00035)); ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke(); }
    ctx.shadowBlur = 0;
    ctx.strokeStyle = this.withAlpha(this.settings.primary, .42); ctx.lineWidth = Math.max(1.25, min * .00135); ctx.beginPath(); ctx.arc(cx, cy, innerRadius - min * .006, 0, Math.PI * 2); ctx.stroke();
    ctx.strokeStyle = this.withAlpha(this.settings.accentA, .32 + this.beatPulse * .12); ctx.lineWidth = Math.max(1, min * .00105); ctx.setLineDash([min * .018, min * .015]); ctx.lineDashOffset = -time * min * .02; ctx.beginPath(); ctx.arc(cx, cy, innerRadius + min * .011 + impact * min * .005, 0, Math.PI * 2); ctx.stroke(); ctx.setLineDash([]);
    for (let i = 0; i < 20; i++) { const a = (i / 20) * Math.PI * 2 + time * .35; const r = innerRadius + min * .024 + Math.sin(time * 1.6 + i) * min * .004 + impact * min * .006; const px = cx + Math.cos(a) * r; const py = cy + Math.sin(a) * r; ctx.fillStyle = i % 4 === 0 ? this.withAlpha(this.settings.accentA, .7) : this.withAlpha(this.settings.primary, .5); ctx.beginPath(); ctx.arc(px, py, min * .0022 + (i % 3 === 0 ? min * .0011 : 0), 0, Math.PI * 2); ctx.fill(); }
    ctx.restore();
  }

  drawHaloPlus(ctx, layout, time) {
    this.drawHalo(ctx, layout, time);
    const { centerX:cx, centerY:cy, size, min } = layout; const pulseScale = this.getCenterPulseScale(); const mediaRadius = (size * pulseScale) / 2; const impact = this.getImpactLevel(); ctx.save(); ctx.globalCompositeOperation = 'screen';
    for (let band = 0; band < 2; band++) { const radius = mediaRadius + min * (.05 + band * .026) + impact * min * .006; ctx.strokeStyle = this.withAlpha(band === 0 ? this.settings.primary : this.settings.accentA, .14 + impact * .08); ctx.lineWidth = Math.max(1, min * (.0009 + band * .0002)); ctx.beginPath(); for (let a = 0; a <= Math.PI * 2 + .06; a += .06) { const mod = 1 + Math.sin(a * (10 + band * 2) + time * (1.9 + band * .4)) * (.018 + impact * .02); const x = cx + Math.cos(a) * radius * mod; const y = cy + Math.sin(a) * radius * mod; if (a === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y); } ctx.closePath(); ctx.stroke(); }
    ctx.restore();
  }


  drawMonolithBars(ctx, layout, time) {
    const { centerX:cx, centerY:cy, size, min } = layout; const count = Math.max(18, Math.round(this.settings.density * .18)); const width = size * .96; const step = width / count; const left = cx - width / 2; const top = cy - size*.72; ctx.save(); ctx.globalCompositeOperation = 'screen';
    for (let i=0; i<count; i++) { const t = i / (count - 1); const value = this.frequencyValue(t); const h = min*(.05 + value*.13); const x = left + i*step; const barW = Math.max(2, step*.46); const tint = i % 6 === 0 ? this.settings.accentA : this.settings.primary; ctx.fillStyle = this.withAlpha(tint, .12 + value*.42); ctx.fillRect(x, top + size*.18 - h, barW, h); ctx.fillRect(x, cy + size*.50, barW, h*.33); }
    ctx.restore();
  }

  drawWaveLine(ctx, layout, time) {
    const { w,h,min } = layout; const left = w*.12; const right = w*.88; const centerY = h*.48; const count = Math.max(96, Math.round(this.settings.density*.7)); const width = right - left; ctx.save(); ctx.globalCompositeOperation = 'screen'; ctx.strokeStyle = this.withAlpha(this.settings.primary, .86); ctx.lineWidth = Math.max(1.2, min*.0018); ctx.beginPath();
    for (let i=0; i<count; i++) { const t = i/(count-1); const x = left + t*width; const value = this.frequencyValue(t); const y = centerY + Math.sin(t*Math.PI*4 + time*.55)*min*.008 + (value - .12)*min*.08; if (i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y); }
    ctx.stroke(); ctx.strokeStyle = this.withAlpha(this.settings.accentA, .28); ctx.lineWidth = Math.max(1, min*.0012); ctx.beginPath(); ctx.moveTo(left, centerY); ctx.lineTo(right, centerY); ctx.stroke(); ctx.restore();
  }

  drawMinimalFocus(ctx, layout, time) {
    const { centerX:cx, centerY:cy, size, min } = layout; const pulse = this.smoothMetrics.bass*min*.022 + this.beatPulse*min*.028; ctx.save(); ctx.globalCompositeOperation = 'screen'; for (let i=0; i<2; i++) { ctx.strokeStyle = this.withAlpha(i===0 ? this.settings.primary : this.settings.accentA, .22 - i*.04 + this.beatPulse*.05); ctx.lineWidth = Math.max(1, min*.0012); ctx.beginPath(); ctx.arc(cx, cy, size*(.68 + i*.06) + pulse*(i+1)*.22, 0, Math.PI*2); ctx.stroke(); } ctx.restore(); }

  drawRibbon(ctx, layout) {
    const { w,h,min,vertical } = layout; const barCount = Math.max(36, Math.round(this.settings.density*.26)); const baseY = vertical ? h*.71 : h*.77; const left = w*.13; const right = w*.87; const width = right - left; const step = width / barCount; ctx.save(); ctx.globalCompositeOperation = 'screen'; ctx.strokeStyle = 'rgba(255,255,255,.08)'; ctx.lineWidth = Math.max(1, min*.001); ctx.beginPath(); ctx.moveTo(left, baseY); ctx.lineTo(right, baseY); ctx.stroke(); for (let i=0; i<barCount; i++) { const t = i / (barCount-1); const value = this.frequencyValue((t*.92)%1); const amp = value * min*.033 * (this.settings.intensity/82); const x = left + i*step; const tint = i % 10 === 0 ? this.settings.accentA : this.settings.primary; ctx.strokeStyle = this.withAlpha(tint, .16 + value*.36); ctx.lineWidth = Math.max(1, step*.28); ctx.beginPath(); ctx.moveTo(x, baseY - amp*.55); ctx.lineTo(x, baseY + amp*.55); ctx.stroke(); } ctx.restore(); }

  drawMetadata(ctx, layout) {
    const { w,h,min,vertical } = layout; const safe = vertical ? w*.075 : min*.055; const title = (this.settings.title || 'UNTITLED').trim(); const artist = (this.settings.artist || 'KAOST032').trim(); const descriptor = (this.settings.descriptor || '').trim(); const credit = (this.settings.credit || '').trim(); const rights = (this.settings.rights || '').trim(); const progress = Number.isFinite(this.audio.duration) && this.audio.duration > 0 ? this.audio.currentTime / this.audio.duration : 0; const typeScale = this.settings.typeScale / 100; ctx.save(); ctx.textBaseline = 'alphabetic'; ctx.fillStyle = this.withAlpha(this.settings.primary, .62); ctx.font = `700 ${Math.max(9,min*.011)}px Inter, Arial, sans-serif`; ctx.textAlign = 'left'; ctx.fillText('KAOST032 / SIGNAL DEFINITIVE', safe, safe*.82); ctx.textAlign = 'right'; ctx.fillText(descriptor.toUpperCase(), w - safe, safe*.82); if (this.settings.showStats) { ctx.textAlign = 'left'; ctx.font = `600 ${Math.max(8,min*.009)}px Inter, Arial, sans-serif`; ctx.fillStyle = this.withAlpha(this.settings.primary, .36); const statsY = safe*1.2; ctx.fillText(`BASS ${this.percent(this.smoothMetrics.bass)}   MID ${this.percent(this.smoothMetrics.mid)}   HIGH ${this.percent(this.smoothMetrics.high)}`, safe, statsY + min*.022); ctx.textAlign = 'right'; ctx.fillText(`${(this.sceneNames[this.getActiveScene()]||'MANUAL')}   RMS ${this.percent(this.smoothMetrics.rms)}`, w - safe, statsY + min*.022); }
    const titleY = vertical ? h*.81 : h - safe*2.12; const titleSize = this.fitFontSize(ctx, title, w - safe*2, Math.max(26,min*.048*typeScale), Math.max(17,min*.027), 780); ctx.textAlign = 'center'; ctx.font = `780 ${titleSize}px Inter, Arial, sans-serif`; ctx.fillStyle = this.settings.primary; ctx.fillText(title, w/2, titleY); ctx.font = `650 ${Math.max(10,min*.014*typeScale)}px Inter, Arial, sans-serif`; ctx.fillStyle = this.withAlpha(this.settings.primary, .62); ctx.fillText(artist, w/2, titleY + min*.031); const lineY = vertical ? h - safe*1.18 : h - safe*.75; ctx.strokeStyle = 'rgba(255,255,255,.14)'; ctx.lineWidth = Math.max(1, min*.0008); ctx.beginPath(); ctx.moveTo(safe, lineY - min*.029); ctx.lineTo(w - safe, lineY - min*.029); ctx.stroke(); ctx.strokeStyle = this.settings.accentA; ctx.beginPath(); ctx.moveTo(safe, lineY - min*.029); ctx.lineTo(safe + (w - safe*2)*progress, lineY - min*.029); ctx.stroke(); const footerSize = Math.max(8,min*.0092); ctx.font = `600 ${footerSize}px Inter, Arial, sans-serif`; ctx.fillStyle = this.withAlpha(this.settings.primary, .52); if (vertical) { ctx.textAlign = 'center'; ctx.fillText(credit, w/2, lineY); ctx.fillText(rights, w/2, lineY + min*.019); } else { ctx.textAlign = 'left'; ctx.fillText(this.truncateText(ctx, credit, w*.43), safe, lineY); ctx.textAlign = 'right'; ctx.fillText(this.truncateText(ctx, rights, w*.43), w - safe, lineY); } ctx.restore(); }

  drawCinemaCards(ctx, w, h) {
    const duration = this.audio.duration || 0; const t = this.audio.currentTime || 0; const startWindow = Math.min(4.2, duration*.18); const endWindow = Math.min(4.5, duration*.18); let introAlpha = 0; let outroAlpha = 0; if (this.settings.showIntro && t < startWindow) introAlpha = Math.sin((t/startWindow) * Math.PI) ** 1.55; if (this.settings.showOutro && duration > 0 && t > duration - endWindow) { const local = (t - (duration - endWindow))/endWindow; outroAlpha = Math.sin(local * Math.PI) ** 1.55; } if (introAlpha <= 0 && outroAlpha <= 0) return; ctx.save(); ctx.fillStyle = `rgba(0,0,0,${Math.max(introAlpha, outroAlpha) * .52})`; ctx.fillRect(0,0,w,h); const min = Math.min(w,h); const title = (this.settings.title || 'UNTITLED').trim(); const artist = (this.settings.artist || 'KAOST032').trim(); const credit = (this.settings.credit || '').trim(); const rights = (this.settings.rights || '').trim(); ctx.textAlign='center'; ctx.textBaseline='middle'; if (introAlpha > 0) { ctx.fillStyle = this.withAlpha(this.settings.primary, .22 * introAlpha); ctx.font = `700 ${Math.max(10, min*.012)}px Inter, Arial, sans-serif`; ctx.fillText('KAOST032 PRESENTS', w/2, h*.39); ctx.fillStyle = this.withAlpha(this.settings.primary, .98 * introAlpha); ctx.font = `800 ${Math.max(28, min*.062)}px Inter, Arial, sans-serif`; ctx.fillText(title, w/2, h*.47); ctx.fillStyle = this.withAlpha(this.settings.accentA, .5 * introAlpha); ctx.font = `650 ${Math.max(12, min*.018)}px Inter, Arial, sans-serif`; ctx.fillText(artist, w/2, h*.53); } if (outroAlpha > 0) { ctx.fillStyle = this.withAlpha(this.settings.primary, .96 * outroAlpha); ctx.font = `760 ${Math.max(18, min*.036)}px Inter, Arial, sans-serif`; ctx.fillText(title, w/2, h*.46); ctx.fillStyle = this.withAlpha(this.settings.primary, .6 * outroAlpha); ctx.font = `650 ${Math.max(10, min*.014)}px Inter, Arial, sans-serif`; ctx.fillText(credit, w/2, h*.53); ctx.fillText(rights, w/2, h*.57); } ctx.restore(); }
  drawBeatFlash(ctx, w, h) { if (!this.settings.showFlash) return; const alpha = Math.min(.08, this.beatPulse*.03); if (alpha <= .0015) return; ctx.save(); ctx.fillStyle = `rgba(255,255,255,${alpha})`; ctx.fillRect(0,0,w,h); ctx.restore(); }
  drawParticles(ctx, w, h, time, dt) { const density = this.settings.cleanMode ? this.settings.particles * .76 : this.settings.particles; const target = Math.round(32 + (density / 100) * 126); const impact = this.getImpactLevel(); while (this.particles.length < target) this.particles.push(...this.createParticles(Math.min(12, target - this.particles.length))); if (this.particles.length > target) this.particles.length = target; const cx = w * .5; const cy = (h / w > 1.35) ? h * .42 : (w / h > 1.35 ? h * .45 : h * .455); const min = Math.min(w, h); ctx.save(); ctx.globalCompositeOperation = 'screen'; for (const p of this.particles) { if (p.mode === 'orbit') { p.angle += dt * p.orbitSpeed * (1.3 + impact * 2.7); const r = min * (p.orbitRadius + Math.sin(time * p.drift + p.phase) * .006 + impact * .011); const x = cx + Math.cos(p.angle) * r; const y = cy + Math.sin(p.angle * 1.08 + p.phase) * r * .74; const alpha = p.alpha * (.24 + impact * .3) * (density / 100); ctx.fillStyle = p.tint === 1 ? this.withAlpha(this.settings.accentA, alpha) : `rgba(255,255,255,${alpha})`; ctx.beginPath(); ctx.arc(x, y, p.size * min * (1 + impact * .24), 0, Math.PI * 2); ctx.fill(); } else if (p.mode === 'spark') { p.y -= p.speed * dt * (1.2 + impact * 1.6); p.x += Math.cos(time * p.drift + p.phase) * dt * p.sway * 1.6; if (p.y < -.08) { p.y = 1.08; p.x = Math.random(); } const alpha = p.alpha * (.18 + impact * .18) * (density / 100); ctx.fillStyle = p.tint === 1 ? this.withAlpha(this.settings.accentA, alpha) : `rgba(255,255,255,${alpha})`; ctx.fillRect(p.x * w, p.y * h, p.size * min * (1.2 + impact * .2), p.size * min * (2.2 + impact * .5)); } else { p.y -= p.speed * dt * (1 + this.smoothMetrics.high * 1.8 + impact * .6); p.x += Math.sin(time * p.drift + p.phase) * dt * p.sway; if (p.y < -.05) { p.y = 1.05; p.x = Math.random(); } const alpha = p.alpha * (.22 + this.smoothMetrics.high * .52 + impact * .12) * (density / 100); ctx.fillStyle = p.tint === 1 ? this.withAlpha(this.settings.accentA, alpha) : `rgba(255,255,255,${alpha})`; ctx.fillRect(p.x * w, p.y * h, p.size * min * (1 + impact * .18), p.size * min * (1 + impact * .18)); } } ctx.restore(); }

  drawImpactBursts(ctx, layout, dt, time) { const { centerX:cx, centerY:cy, size, min } = layout; ctx.save(); ctx.globalCompositeOperation = 'screen'; this.impactBursts = this.impactBursts.filter((burst) => { burst.age += dt; const p = burst.age / burst.life; if (p >= 1) return false; const alpha = (1 - p) * .22; for (let i=0; i<burst.rays; i++) { const a = burst.seed + (i / burst.rays) * Math.PI * 2 + time * .2; const inner = size * .52 + p * min * .02; const outer = inner + min * (.035 + p * .05); const x1 = cx + Math.cos(a) * inner; const y1 = cy + Math.sin(a) * inner; const x2 = cx + Math.cos(a) * outer; const y2 = cy + Math.sin(a) * outer; ctx.strokeStyle = i % 4 === 0 ? this.withAlpha(this.settings.accentA, alpha) : this.withAlpha(this.settings.primary, alpha * .9); ctx.lineWidth = Math.max(1, min * (.0008 + (1-p) * .0013)); ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke(); } return true; }); ctx.restore(); }

  drawProgressSparks(ctx, width, height, markerX, centerY, impact) { while (this.progressSparks.length < 14) this.progressSparks.push({ angle:Math.random()*Math.PI*2, radius:8+Math.random()*18, speed:.8+Math.random()*2, life:.5+Math.random()*.5, age:Math.random()*.5, size:1+Math.random()*1.5 }); ctx.save(); ctx.globalCompositeOperation = 'screen'; this.progressSparks = this.progressSparks.filter((s) => { s.age += 1/60; if (s.age > s.life) { s.age = 0; s.angle = Math.random()*Math.PI*2; s.radius = 8+Math.random()*18; } const p = s.age / s.life; const x = markerX + Math.cos(s.angle + this.audio.currentTime * s.speed) * (s.radius + impact * 12 * p); const y = centerY + Math.sin(s.angle + this.audio.currentTime * (s.speed + .4)) * (s.radius * .45 + impact * 8 * p); ctx.fillStyle = this.withAlpha(p > .5 ? this.settings.primary : this.settings.accentA, .35 * (1-p)); ctx.fillRect(x, y, s.size, s.size); return true; }); ctx.restore(); }

  drawVignette(ctx, w, h) { const strength = this.settings.vignette / 100; const g = ctx.createRadialGradient(w/2, h/2, Math.min(w,h)*.18, w/2, h/2, Math.max(w,h)*.72); g.addColorStop(0, 'rgba(0,0,0,0)'); g.addColorStop(.72, `rgba(0,0,0,${.1 + strength*.12})`); g.addColorStop(1, `rgba(0,0,0,${.34 + strength*.5})`); ctx.fillStyle = g; ctx.fillRect(0,0,w,h); }
  drawNoise(ctx, w, h, time) { const count = Math.max(80, Math.floor((w*h)/18000)); ctx.save(); ctx.globalAlpha = .03; ctx.fillStyle = '#fff'; let seed = Math.floor(time*12)*48271 + 17; for (let i=0; i<count; i++) { seed = (seed*16807)%2147483647; const x = (seed/2147483647)*w; seed = (seed*16807)%2147483647; const y = (seed/2147483647)*h; ctx.fillRect(x,y,1,1); } ctx.restore(); }

  currentVisualSource() { if (this.mediaType === 'image') return this.image.complete ? this.image : null; return this.video.readyState >= 2 ? this.video : null; }
  drawCover(ctx, source, x, y, width, height) { const sourceWidth = source.videoWidth || source.naturalWidth || source.width; const sourceHeight = source.videoHeight || source.naturalHeight || source.height; if (!sourceWidth || !sourceHeight) return; const sourceRatio = sourceWidth / sourceHeight; const targetRatio = width / height; let sx=0, sy=0, sw=sourceWidth, sh=sourceHeight; if (sourceRatio > targetRatio) { sw = sourceHeight * targetRatio; sx = (sourceWidth - sw) / 2; } else { sh = sourceWidth / targetRatio; sy = (sourceHeight - sh) / 2; } try { ctx.drawImage(source, sx, sy, sw, sh, x, y, width, height); } catch (_) {} }
  drawTintedCover(ctx, source, x, y, width, height, tint) { ctx.save(); ctx.globalCompositeOperation='screen'; this.drawCover(ctx, source, x, y, width, height); ctx.fillStyle = tint; ctx.globalAlpha *= .18; ctx.fillRect(x, y, width, height); ctx.restore(); }
  roundedPath(ctx, x, y, width, height, radius) { const r = Math.max(0, Math.min(radius, width/2, height/2)); ctx.beginPath(); ctx.moveTo(x + r, y); ctx.arcTo(x + width, y, x + width, y + height, r); ctx.arcTo(x + width, y + height, x, y + height, r); ctx.arcTo(x, y + height, x, y, r); ctx.arcTo(x, y, x + width, y, r); ctx.closePath(); }
  drawCornerMarks(ctx, x, y, size, min) { const length = min*.034; const gap = min*.013; ctx.save(); ctx.strokeStyle = this.withAlpha(this.settings.primary, .46); ctx.lineWidth = Math.max(1, min*.0013); const corners = [[x-gap,y-gap,1,1],[x+size+gap,y-gap,-1,1],[x-gap,y+size+gap,1,-1],[x+size+gap,y+size+gap,-1,-1]]; for (const [cx,cy,sx,sy] of corners) { ctx.beginPath(); ctx.moveTo(cx, cy + sy*length); ctx.lineTo(cx,cy); ctx.lineTo(cx + sx*length, cy); ctx.stroke(); } ctx.restore(); }
  createParticles(count) { return Array.from({ length:count }, () => ({ mode: Math.random() < .26 ? 'orbit' : (Math.random() < .28 ? 'spark' : 'drift'), x:Math.random(), y:Math.random(), speed:.008 + Math.random()*.024, sway:.003 + Math.random()*.008, drift:.5 + Math.random()*1.3, phase:Math.random()*Math.PI*2, angle:Math.random()*Math.PI*2, orbitRadius:.22 + Math.random()*.18, orbitSpeed:.45 + Math.random()*1.45, size:.001 + Math.random()*.0019, alpha:.08 + Math.random()*.2, tint: Math.random() < .26 ? 1 : 0 })); }
  fitFontSize(ctx, text, maxWidth, initialSize, minimumSize, weight) { let size = initialSize; while (size > minimumSize) { ctx.font = `${weight} ${size}px Inter, Arial, sans-serif`; if (ctx.measureText(text).width <= maxWidth) break; size -= 1; } return size; }
  truncateText(ctx, text, maxWidth) { if (ctx.measureText(text).width <= maxWidth) return text; let value = text; while (value.length > 4 && ctx.measureText(`${value}…`).width > maxWidth) value = value.slice(0,-1); return `${value}…`; }
  withAlpha(hex, alpha) { const {r,g,b} = this.hexToRgb(hex); return `rgba(${r},${g},${b},${Math.max(0, Math.min(1, alpha))})`; }
  hexToRgb(hex) { const normalized = hex.replace('#',''); const value = normalized.length === 3 ? normalized.split('').map((c)=>c+c).join('') : normalized; const number = Number.parseInt(value, 16); return { r:(number >> 16) & 255, g:(number >> 8) & 255, b:number & 255 }; }
  percent(value) { return `${Math.round(Math.max(0, Math.min(1, value))*100)}`; }
  formatTime(seconds) { if (!Number.isFinite(seconds)) return '00:00'; const total = Math.max(0, Math.floor(seconds)); const minutes = Math.floor(total / 60); const remaining = total % 60; return `${String(minutes).padStart(2,'0')}:${String(remaining).padStart(2,'0')}`; }
  slugify(value) { return String(value).normalize('NFD').replace(/[\u0300-\u036f]/g,'').replace(/[^a-zA-Z0-9]+/g,'-').replace(/^-|-$/g,'').toUpperCase() || 'VIDEO'; }
}
new SignalStudioObsidian();
