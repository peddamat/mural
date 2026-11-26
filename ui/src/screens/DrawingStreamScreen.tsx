import { useEffect, useRef, useState } from 'react'
import {
  startStreamRun,
  sendStreamBatch,
  getStreamStatus,
  pauseStream,
  resumeStream,
  stopStream,
  doneWithPhase,
  StreamSeqMismatchError,
} from '../api'
import type { BackendState, StreamStatus } from '../types'
import { Card } from '../components/Card'
import { Button } from '../components/Button'
import { ProgressBar } from '../components/ProgressBar'

interface Props {
  commands: string | null
  speed: number
  /** True when re-attaching to a drawing already running on the device */
  attachOnly?: boolean
  onReset: (state: BackendState) => void
}

const BATCH_SIZE = 16
const BUFFER_LOW_WATER_FREE = 16

function parseDrawingCommands(raw: string): { totalDistance: number; lines: string[] } {
  const rawLines = raw
    .split('\n')
    .map((l) => l.trim())
    .filter((l) => l.length > 0)

  let totalDistance = 0
  const lines: string[] = []

  for (const line of rawLines) {
    if (line.startsWith('d') || line.startsWith('D')) {
      const parsed = parseFloat(line.slice(1))
      if (!isNaN(parsed)) totalDistance = parsed
      continue
    }
    if (line.startsWith('h') || line.startsWith('H')) {
      continue
    }
    lines.push(line)
  }

  return { totalDistance, lines }
}

function formatDuration(ms: number): string {
  const totalSec = Math.max(0, Math.floor(ms / 1000))
  const h = Math.floor(totalSec / 3600)
  const m = Math.floor((totalSec % 3600) / 60)
  const s = totalSec % 60
  if (h > 0) return `${h}h ${String(m).padStart(2, '0')}m ${String(s).padStart(2, '0')}s`
  return `${m}m ${String(s).padStart(2, '0')}s`
}

function sleep(ms: number): Promise<void> {
  return new Promise((r) => setTimeout(r, ms))
}

export function DrawingStreamScreen({ commands, speed, attachOnly = false, onReset }: Props) {
  const [status, setStatus] = useState<StreamStatus | null>(null)
  const [linkStatus, setLinkStatus] = useState<'starting' | 'streaming' | 'reconnecting' | 'done' | 'error'>('starting')
  const [errorMsg, setErrorMsg] = useState<string | null>(null)
  const [sentCount, setSentCount] = useState(0)
  const [totalCount, setTotalCount] = useState(0)
  const [elapsedMs, setElapsedMs] = useState(0)
  const [actionBusy, setActionBusy] = useState(false)
  const [confirmStop, setConfirmStop] = useState(false)

  const cancelledRef = useRef(false)
  const startTimeRef = useRef<number>(Date.now())

  // Keep screen awake on phones/laptops while streaming
  useEffect(() => {
    let wakeLock: { release: () => Promise<void> } | null = null
    let active = true

    async function acquireWakeLock() {
      try {
        const nav = navigator as Navigator & {
          wakeLock?: { request: (type: 'screen') => Promise<{ release: () => Promise<void> }> }
        }
        if (nav.wakeLock && active) {
          wakeLock = await nav.wakeLock.request('screen')
        }
      } catch {
        // Wake Lock not supported or denied — continue normally
      }
    }

    acquireWakeLock()
    const onVis = () => {
      if (document.visibilityState === 'visible') acquireWakeLock()
    }
    document.addEventListener('visibilitychange', onVis)

    return () => {
      active = false
      document.removeEventListener('visibilitychange', onVis)
      wakeLock?.release().catch(() => {})
    }
  }, [])

  // Elapsed timer
  useEffect(() => {
    const iv = setInterval(() => {
      if (status?.state === 'finished' || status?.state === 'stopped') return
      setElapsedMs(Date.now() - startTimeRef.current)
    }, 1000)
    return () => clearInterval(iv)
  }, [status?.state])

  // Main browser-to-ESP8266 streaming loop
  useEffect(() => {
    cancelledRef.current = false
    startTimeRef.current = Date.now()

    async function runStreamLoop() {
      const parsed = commands ? parseDrawingCommands(commands) : { totalDistance: 0, lines: [] }
      const allLines = parsed.lines
      setTotalCount(allLines.length)

      let currentStatus: StreamStatus | null = null
      let seq = 0

      // 1. Initialize or attach to the drawing session
      while (!cancelledRef.current) {
        try {
          if (attachOnly || !commands) {
            currentStatus = await getStreamStatus()
            seq = currentStatus.nextSeq
            if (currentStatus.totalCommands > 0) {
              setTotalCount(currentStatus.totalCommands)
            }
          } else {
            currentStatus = await startStreamRun(speed, parsed.totalDistance, allLines.length)
            seq = currentStatus.nextSeq
          }
          setStatus(currentStatus)
          setSentCount(seq)
          setLinkStatus('streaming')
          setErrorMsg(null)
          break
        } catch (err) {
          setLinkStatus('reconnecting')
          setErrorMsg(`Connecting to Mural… (${err instanceof Error ? err.message : String(err)})`)
          await sleep(1000)
        }
      }

      if (cancelledRef.current || !currentStatus) return

      // 2. Stream command batches with backpressure & idempotent retry
      let retryDelayMs = 300
      let lastState: StreamStatus['state'] = currentStatus.state
      while (!cancelledRef.current) {
        try {
          const st = await getLatestAndSend(allLines, seq, lastState === 'paused')
          currentStatus = st.status
          seq = st.nextSeq
          lastState = currentStatus.state
          retryDelayMs = 300

          setStatus(currentStatus)
          setSentCount(seq)
          setErrorMsg(null)

          if (currentStatus.state === 'finished' || currentStatus.state === 'stopped') {
            setLinkStatus('done')
            return
          }

          setLinkStatus('streaming')

          // Backpressure pacing
          const freeSlots = currentStatus.capacity - currentStatus.buffered
          if (currentStatus.state === 'paused' || currentStatus.streamDone || !commands) {
            await sleep(500)
          } else if (freeSlots < BUFFER_LOW_WATER_FREE) {
            await sleep(180)
          } else {
            await sleep(25)
          }
        } catch (err) {
          if (err instanceof StreamSeqMismatchError) {
            seq = err.nextSeq
            setSentCount(seq)
            continue
          }
          setLinkStatus('reconnecting')
          setErrorMsg(`Connection interrupted — retrying… (${err instanceof Error ? err.message : String(err)})`)
          await sleep(retryDelayMs)
          retryDelayMs = Math.min(3000, Math.round(retryDelayMs * 1.5))
        }
      }
    }

    async function getLatestAndSend(
      allLines: string[],
      seq: number,
      paused: boolean,
    ): Promise<{ status: StreamStatus; nextSeq: number }> {
      // If paused, attached without local commands, or finished streaming, just poll status
      if (paused || !commands || seq >= allLines.length) {
        const st = await getStreamStatus()
        if (!paused && commands && allLines.length > 0 && st.nextSeq < allLines.length && !st.streamDone) {
          return { status: st, nextSeq: st.nextSeq }
        }
        return { status: st, nextSeq: seq }
      }

      const end = Math.min(seq + BATCH_SIZE, allLines.length)
      const chunkText = allLines.slice(seq, end).join('\n')
      const isFinal = end >= allLines.length
      const st = await sendStreamBatch(seq, chunkText, isFinal)
      return { status: st, nextSeq: st.nextSeq }
    }

    runStreamLoop()

    return () => {
      cancelledRef.current = true
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  async function handlePauseToggle() {
    if (!status) return
    setActionBusy(true)
    try {
      const updated = status.state === 'paused' ? await resumeStream() : await pauseStream()
      setStatus(updated)
    } finally {
      setActionBusy(false)
    }
  }

  async function handleStop() {
    setActionBusy(true)
    try {
      const updated = await stopStream()
      setStatus(updated)
      setConfirmStop(false)
    } finally {
      setActionBusy(false)
    }
  }

  async function handleDoneAndReset() {
    setActionBusy(true)
    try {
      const state = await doneWithPhase()
      onReset(state)
    } finally {
      setActionBusy(false)
    }
  }

  const progress = status?.progress ?? 0
  const runnerState = status?.state ?? 'drawing'
  const isFinished = runnerState === 'finished'
  const isStopped = runnerState === 'stopped'
  const isPaused = runnerState === 'paused'
  const isHoming = runnerState === 'homing'
  const isTerminal = isFinished || isStopped

  const title = isFinished
    ? 'Drawing Complete'
    : isStopped
    ? 'Drawing Stopped'
    : isHoming
    ? 'Returning Home…'
    : isPaused
    ? 'Drawing Paused'
    : 'Drawing in Progress'

  const bufferPct = status && status.capacity > 0
    ? Math.round((status.buffered / status.capacity) * 100)
    : 0

  return (
    <Card
      title={title}
      subtitle={
        isTerminal
          ? 'You can now inspect your wall drawing or start a new one.'
          : 'Keep this browser tab open — commands are streaming live to your HW-649.'
      }
    >
      {/* Status badges */}
      <div className="flex flex-wrap items-center justify-between gap-2 text-xs">
        <span
          className={`inline-flex items-center gap-1.5 px-2.5 py-1 rounded-full font-medium ${
            linkStatus === 'reconnecting'
              ? 'bg-amber-950 text-amber-300 border border-amber-700/60'
              : isFinished
              ? 'bg-emerald-950 text-emerald-300 border border-emerald-700/60'
              : isStopped
              ? 'bg-red-950 text-red-300 border border-red-700/60'
              : isPaused
              ? 'bg-yellow-950 text-yellow-300 border border-yellow-700/60'
              : 'bg-cyan-950 text-cyan-300 border border-cyan-700/60'
          }`}
        >
          <span
            className={`w-2 h-2 rounded-full ${
              linkStatus === 'reconnecting'
                ? 'bg-amber-400 animate-pulse'
                : isFinished
                ? 'bg-emerald-400'
                : isStopped
                ? 'bg-red-400'
                : isPaused
                ? 'bg-yellow-400'
                : 'bg-cyan-400 animate-pulse'
            }`}
          />
          {linkStatus === 'reconnecting'
            ? 'Reconnecting…'
            : isFinished
            ? 'Finished'
            : isStopped
            ? 'Stopped'
            : isHoming
            ? 'Returning Home'
            : isPaused
            ? 'Paused'
            : 'Streaming Live'}
        </span>

        {status && (
          <span
            className={`px-2.5 py-1 rounded-full font-mono ${
              status.unoConnected
                ? 'bg-gray-800 text-emerald-400'
                : 'bg-red-950 text-red-300'
            }`}
          >
            {status.unoConnected ? 'UNO Link OK' : 'UNO Disconnected'}
          </span>
        )}
      </div>

      {/* Main progress bar */}
      <div className="space-y-3 py-1">
        <ProgressBar
          label="Drawing Progress"
          value={progress}
          animated={!isTerminal && !isPaused}
          color={isFinished ? 'emerald' : 'indigo'}
        />

        {!isTerminal && status && (
          <ProgressBar
            label={`Device Buffer (${status.buffered} / ${status.capacity})`}
            value={bufferPct}
            animated={false}
            color="emerald"
          />
        )}
      </div>

      {/* Live telemetry grid */}
      <div className="grid grid-cols-3 gap-2 text-center bg-gray-800/60 border border-gray-800 rounded-xl p-3">
        <div>
          <p className="text-[11px] text-gray-400 uppercase">Streamed</p>
          <p className="text-sm font-mono text-gray-100">
            {sentCount.toLocaleString()}
            {totalCount > 0 ? ` / ${totalCount.toLocaleString()}` : ''}
          </p>
        </div>
        <div>
          <p className="text-[11px] text-gray-400 uppercase">Executed</p>
          <p className="text-sm font-mono text-gray-100">
            {(status?.executed ?? 0).toLocaleString()}
          </p>
        </div>
        <div>
          <p className="text-[11px] text-gray-400 uppercase">Elapsed</p>
          <p className="text-sm font-mono text-gray-100">{formatDuration(elapsedMs)}</p>
        </div>
      </div>

      {errorMsg && (
        <div className="rounded-xl bg-amber-950/50 border border-amber-700/60 p-3 text-xs text-amber-200">
          {errorMsg}
        </div>
      )}

      {/* Controls */}
      {isTerminal ? (
        <Button onClick={handleDoneAndReset} disabled={actionBusy}>
          {actionBusy ? 'Resetting…' : 'Start New Drawing'}
        </Button>
      ) : (
        <div className="space-y-2">
          <div className="flex gap-3">
            <Button
              variant="secondary"
              onClick={handlePauseToggle}
              disabled={actionBusy || isHoming}
            >
              {isPaused ? 'Resume' : 'Pause'}
            </Button>
            {!confirmStop ? (
              <Button
                variant="secondary"
                onClick={() => setConfirmStop(true)}
                disabled={actionBusy}
              >
                Stop
              </Button>
            ) : (
              <Button
                variant="danger"
                onClick={handleStop}
                disabled={actionBusy}
              >
                Confirm Stop
              </Button>
            )}
          </div>
          {confirmStop && (
            <button
              onClick={() => setConfirmStop(false)}
              className="w-full text-center text-xs text-gray-400 hover:text-gray-200"
            >
              Cancel stop
            </button>
          )}
        </div>
      )}
    </Card>
  )
}
