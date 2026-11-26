import type { BackendState, StreamStatus } from './types'

// ---------------------------------------------------------------------------
// Optional remote host support (e.g. ?host=http://192.168.1.42 or http://mural.local)
// Lets you run the React UI from a PC browser and stream directly to HW-649
// even without uploading static files to LittleFS.
// ---------------------------------------------------------------------------
function resolveApiBase(): string {
  try {
    const params = new URLSearchParams(window.location.search)
    const hostParam = params.get('host')
    if (hostParam) {
      const cleaned = hostParam.replace(/\/+$/, '')
      sessionStorage.setItem('muralApiHost', cleaned)
      return cleaned
    }
    const saved = sessionStorage.getItem('muralApiHost')
    if (saved) return saved
  } catch {
    // ignore
  }
  return ''
}

const API_BASE = resolveApiBase()

function apiUrl(path: string): string {
  return `${API_BASE}${path}`
}

// ---------------------------------------------------------------------------
// Mock mode — activated automatically in dev when no backend is reachable
// ---------------------------------------------------------------------------
let _mockMode = false
let _mockState: BackendState = {
  phase: 'SvgSelect',
  topDistance: 1667,
  safeWidth: 1000,
  homeX: 500,
  homeY: 350,
  safeXFraction: 0.2,
  safeYFraction: 0.25,
  pulleyDiameter: 10.14926,
  streamingMode: true,
  unoConnected: true,
}
let _mockUploadedBlob: Blob | null = null
let _mockStreamNextSeq = 0
let _mockStreamTotal = 0
let _mockStreamDone = false
let _mockStreamState: StreamStatus['state'] = 'idle'

export function enableMockMode(state: BackendState) {
  _mockMode = true
  _mockState = state
}

export function isMockMode() { return _mockMode }

// ---------------------------------------------------------------------------

async function post<T = BackendState>(url: string, body: Record<string, unknown> = {}): Promise<T> {
  const res = await fetch(apiUrl(url), {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams(body as Record<string, string>).toString(),
  })
  const text = await res.text()
  if (!res.ok) throw new Error(text || `${url} failed: ${res.status}`)
  try {
    return JSON.parse(text) as T
  } catch {
    return text as unknown as T
  }
}

async function get<T = BackendState>(url: string): Promise<T> {
  const res = await fetch(apiUrl(url))
  const text = await res.text()
  if (!res.ok) throw new Error(text || `${url} failed: ${res.status}`)
  try {
    return JSON.parse(text) as T
  } catch {
    return text as unknown as T
  }
}

export const getState = (): Promise<BackendState> => get('/getState')

export const doneWithPhase = (): Promise<BackendState> => {
  if (_mockMode) {
    const nextMap: Record<string, BackendState['phase']> = {
      SvgSelect: 'RetractBelts',
      RetractBelts: 'ExtendToHome',
      BeginDrawing: 'SetTopDistance',
      Drawing: 'SetTopDistance',
    }
    _mockState = { ..._mockState, phase: nextMap[_mockState.phase] ?? 'SetTopDistance' }
    return Promise.resolve(_mockState)
  }
  return post('/doneWithPhase')
}

export const setTopDistance = (distance: number): Promise<BackendState> =>
  post('/setTopDistance', { distance: String(distance) })

export const setPulleyDiameter = (diameter: number): Promise<void> =>
  post<void>('/setPulleyDiameter', { diameter: String(diameter) })

export const extendToHome = (): Promise<number> => post<number>('/extendToHome')

export const setServo = (angle: number): Promise<void> =>
  post<void>('/setServo', { angle: String(angle) })

export const setPenDistance = (angle: number): Promise<BackendState> =>
  post('/setPenDistance', { angle: String(angle) })

export const estepsCalibration = (distanceMm: number): Promise<void> =>
  post<void>('/estepsCalibration', { distance: String(distanceMm) })

export const postMotorCommand = (command: string): Promise<void> =>
  post<void>('/command', { command })

export const run = (speed: number): Promise<void> => post<void>('/run', { speed: String(speed) })

// ---------------------------------------------------------------------------
// Browser streaming endpoints (HW-649 without SD card)
// ---------------------------------------------------------------------------

export class StreamSeqMismatchError extends Error {
  nextSeq: number
  constructor(nextSeq: number) {
    super(`Sequence mismatch; expected ${nextSeq}`)
    this.nextSeq = nextSeq
  }
}

export async function startStreamRun(
  speed: number,
  totalDistance: number,
  totalCommands: number,
): Promise<StreamStatus> {
  if (_mockMode) {
    _mockStreamNextSeq = 0
    _mockStreamTotal = totalCommands
    _mockStreamDone = false
    _mockStreamState = 'drawing'
    return {
      state: 'drawing',
      accepted: 0,
      nextSeq: 0,
      executed: 0,
      totalCommands,
      buffered: 0,
      capacity: 64,
      progress: 0,
      streamDone: false,
      unoConnected: true,
      unoFree: 10,
    }
  }
  return post<StreamStatus>('/run', {
    speed: String(speed),
    totalDistance: String(totalDistance),
    totalCommands: String(totalCommands),
  })
}

export async function sendStreamBatch(
  seq: number,
  cmds: string,
  done: boolean,
): Promise<StreamStatus> {
  if (_mockMode) {
    const lines = cmds ? cmds.split('\n').filter(Boolean).length : 0
    _mockStreamNextSeq = Math.max(_mockStreamNextSeq, seq + lines)
    if (done) {
      _mockStreamDone = true
      _mockStreamState = 'finished'
    }
    const pct = _mockStreamTotal > 0 ? Math.min(100, Math.round((_mockStreamNextSeq / _mockStreamTotal) * 100)) : 100
    return {
      state: _mockStreamState,
      accepted: lines,
      nextSeq: _mockStreamNextSeq,
      executed: _mockStreamNextSeq,
      totalCommands: _mockStreamTotal,
      buffered: 0,
      capacity: 64,
      progress: pct,
      streamDone: _mockStreamDone,
      unoConnected: true,
      unoFree: 10,
    }
  }

  const res = await fetch(apiUrl('/stream'), {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({
      seq: String(seq),
      cmds,
      done: done ? '1' : '0',
    }).toString(),
  })
  const text = await res.text()
  if (res.status === 409) {
    try {
      const parsed = JSON.parse(text) as { nextSeq?: number }
      if (typeof parsed.nextSeq === 'number') {
        throw new StreamSeqMismatchError(parsed.nextSeq)
      }
    } catch (e) {
      if (e instanceof StreamSeqMismatchError) throw e
    }
  }
  if (!res.ok) throw new Error(text || `/stream failed: ${res.status}`)
  return JSON.parse(text) as StreamStatus
}

export async function getStreamStatus(): Promise<StreamStatus> {
  if (_mockMode) {
    const pct = _mockStreamTotal > 0 ? Math.min(100, Math.round((_mockStreamNextSeq / _mockStreamTotal) * 100)) : 100
    return {
      state: _mockStreamState,
      accepted: 0,
      nextSeq: _mockStreamNextSeq,
      executed: _mockStreamNextSeq,
      totalCommands: _mockStreamTotal,
      buffered: 0,
      capacity: 64,
      progress: pct,
      streamDone: _mockStreamDone,
      unoConnected: true,
      unoFree: 10,
    }
  }
  return get<StreamStatus>('/streamStatus')
}

export const pauseStream = (): Promise<StreamStatus> => {
  if (_mockMode) {
    _mockStreamState = 'paused'
    return getStreamStatus()
  }
  return post<StreamStatus>('/pause')
}

export const resumeStream = (): Promise<StreamStatus> => {
  if (_mockMode) {
    _mockStreamState = 'drawing'
    return getStreamStatus()
  }
  return post<StreamStatus>('/resumeStream')
}

export const stopStream = (): Promise<StreamStatus> => {
  if (_mockMode) {
    _mockStreamState = 'stopped'
    return getStreamStatus()
  }
  return post<StreamStatus>('/stop')
}

// ---------------------------------------------------------------------------
// Legacy SD card upload / download (used by original ESP32 firmware)
// ---------------------------------------------------------------------------

const UPLOAD_CHUNK_SIZE = 32 * 1024
const UPLOAD_MAX_RETRIES = 3
const UPLOAD_RETRY_DELAY_MS = 500

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

function sendChunk(chunk: Blob, offset: number, totalBytes: number, final: boolean): Promise<BackendState> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest()
    const formData = new FormData()
    formData.append('commands', chunk)

    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        try {
          resolve(JSON.parse(xhr.responseText) as BackendState)
        } catch {
          reject(new Error('Invalid upload response'))
        }
      } else {
        reject(new Error(xhr.responseText || `Upload failed: ${xhr.status}`))
      }
    }

    xhr.onerror = () => reject(new Error('Upload network error'))
    const params = new URLSearchParams({
      offset: String(offset),
      totalBytes: String(totalBytes),
      final: String(final),
    })
    xhr.open('POST', apiUrl(`/uploadCommands?${params.toString()}`))
    xhr.send(formData)
  })
}

async function sendChunkWithRetry(
  chunk: Blob,
  offset: number,
  totalBytes: number,
  final: boolean,
): Promise<BackendState> {
  for (let attempt = 0; ; attempt++) {
    try {
      return await sendChunk(chunk, offset, totalBytes, final)
    } catch (err) {
      if (attempt >= UPLOAD_MAX_RETRIES) throw err
      await delay(UPLOAD_RETRY_DELAY_MS * (attempt + 1))
    }
  }
}

export async function uploadCommands(
  blob: Blob,
  onUploadProgress: (pct: number) => void,
): Promise<BackendState> {
  if (_mockMode) {
    _mockUploadedBlob = blob
    return new Promise((resolve) => {
      let pct = 0
      const iv = setInterval(() => {
        pct = Math.min(100, pct + 20)
        onUploadProgress(pct)
        if (pct === 100) { clearInterval(iv); resolve(_mockState) }
      }, 80)
    })
  }

  const totalBytes = blob.size
  let offset = 0
  let state: BackendState

  do {
    const end = Math.min(offset + UPLOAD_CHUNK_SIZE, totalBytes)
    const chunk = blob.slice(offset, end)
    const final = end === totalBytes
    state = await sendChunkWithRetry(chunk, offset, totalBytes, final)
    offset = end
    onUploadProgress(totalBytes === 0 ? 100 : Math.round((offset / totalBytes) * 100))
  } while (offset < totalBytes)

  return state
}

export function downloadCommands(
  onProgress: (pct: number) => void,
): Promise<string> {
  if (_mockMode) {
    return new Promise((resolve) => {
      let pct = 0
      const iv = setInterval(async () => {
        pct = Math.min(100, pct + 20)
        onProgress(pct)
        if (pct === 100) {
          clearInterval(iv)
          resolve(_mockUploadedBlob ? await _mockUploadedBlob.text() : '')
        }
      }, 80)
    })
  }
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest()

    xhr.addEventListener('progress', (evt) => {
      if (evt.lengthComputable) {
        onProgress(Math.round((evt.loaded / evt.total) * 100))
      }
    })

    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve(xhr.responseText)
      } else {
        reject(new Error(`Download failed: ${xhr.status}`))
      }
    }

    xhr.onerror = () => reject(new Error('Download network error'))
    xhr.open('GET', apiUrl('/downloadCommands'))
    xhr.send()
  })
}

// Motor helpers
export const raiseBot = (): Promise<number> => post<number>('/command', { command: 'raise' })
export const lowerBot = (): Promise<number> => post<number>('/command', { command: 'lower' })

export const leftRetractDown = () => postMotorCommand('l-ret')
export const leftExtendDown = () => postMotorCommand('l-ext')
export const leftStop = () => postMotorCommand('l-0')
export const rightRetractDown = () => postMotorCommand('r-ret')
export const rightExtendDown = () => postMotorCommand('r-ext')
export const rightStop = () => postMotorCommand('r-0')
