import { useState, useEffect } from 'react'
import { run, doneWithPhase } from '../api'
import type { BackendState } from '../types'
import { Card } from '../components/Card'
import { Button } from '../components/Button'
import { Slider } from '../components/Slider'

interface Props {
  streamingMode?: boolean
  hasCommands?: boolean
  onBegin: (speed: number) => void
  onReset: (state: BackendState) => void
}

export function BeginDrawingScreen({ streamingMode, hasCommands = true, onBegin, onReset }: Props) {
  const [busy, setBusy] = useState(false)
  const [speed, setSpeed] = useState<number>(() =>
    parseInt(localStorage.getItem('muralSpeed') ?? '650', 10)
  )

  useEffect(() => {
    localStorage.setItem('muralSpeed', String(speed))
  }, [speed])

  const maxSpeed = streamingMode ? 1800 : 2000

  async function handleBegin() {
    setBusy(true)
    if (streamingMode) {
      // DrawingStreamScreen will call POST /run with totalDistance & totalCommands and start streaming
      onBegin(speed)
      return
    }
    await run(speed)
    onBegin(speed)
  }

  async function handleReset() {
    setBusy(true)
    const state = await doneWithPhase()
    onReset(state)
    location.reload()
  }

  const missingCommands = Boolean(streamingMode && !hasCommands)

  return (
    <Card
      title="Mural is Ready"
      subtitle={
        streamingMode
          ? 'Drawing commands will stream live from this browser tab over WiFi. Keep this tab open until drawing finishes.'
          : 'The drawing commands have been uploaded and verified.'
      }
    >
      {missingCommands ? (
        <div className="rounded-xl bg-amber-950/50 border border-amber-700/60 p-3 text-xs text-amber-200 leading-relaxed">
          No drawing commands are loaded in this browser tab (for example, if you switched devices or cleared browser storage).
          Click <strong>Reset</strong> below to select your image again.
        </div>
      ) : (
        <>
          <Slider
            label="Speed (steps/s)"
            min={300}
            max={maxSpeed}
            step={50}
            value={Math.min(speed, maxSpeed)}
            onChange={setSpeed}
          />
          <p className="text-xs text-yellow-300/90">
            ⚠ Speed control is experimental. Use 650 for best results.
          </p>
        </>
      )}

      <Button onClick={handleBegin} disabled={busy || missingCommands}>
        {busy ? 'Starting…' : streamingMode ? 'Begin Live Streaming' : 'Begin Drawing'}
      </Button>
      <Button variant="secondary" onClick={handleReset} disabled={busy}>
        Reset
      </Button>
    </Card>
  )
}
