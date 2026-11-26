import { useState, useEffect } from 'react'
import { getState, doneWithPhase, enableMockMode } from './api'
import { saveCachedCommands, loadCachedCommands, clearCachedCommands } from './commandStore'
import { showErrorAlert } from './components/AlertModal'
import type { BackendState, UiPhase } from './types'
import type { SvgState } from './svgControl'
import type { RasterImageState } from './rasterControl'
import type { RendererDefinition } from './renderers/index'
import { getRenderersByInputType } from './renderers/index'

import { LoadingScreen } from './screens/LoadingScreen'
import { RetractBeltsScreen } from './screens/RetractBeltsScreen'
import { SetTopDistanceScreen } from './screens/SetTopDistanceScreen'
import { ExtendToHomeScreen } from './screens/ExtendToHomeScreen'
import { PenCalibrationScreen } from './screens/PenCalibrationScreen'
import { InputSelectScreen } from './screens/InputSelectScreen'
import { SvgUploadScreen } from './screens/SvgUploadScreen'
import { RasterUploadScreen } from './screens/RasterUploadScreen'
import { ChooseRendererScreen } from './screens/ChooseRendererScreen'
import { DrawingPreviewScreen } from './screens/DrawingPreviewScreen'
import { UploadProgressScreen } from './screens/UploadProgressScreen'
import { BeginDrawingScreen } from './screens/BeginDrawingScreen'
import { DrawingBeganScreen } from './screens/DrawingBeganScreen'
import { DrawingStreamScreen } from './screens/DrawingStreamScreen'

export default function App() {
  const [backendState, setBackendState] = useState<BackendState | null>(null)
  const [uiPhase, setUiPhase] = useState<UiPhase>('loading')

  // SVG / Raster pipeline state
  const [svgState, setSvgState] = useState<SvgState | null>(null)
  const [rasterState, setRasterState] = useState<RasterImageState | null>(null)
  const [inputType, setInputType] = useState<'svg' | 'raster' | null>(null)
  const [selectedRenderer, setSelectedRenderer] = useState<RendererDefinition | null>(null)
  const [renderedCommands, setRenderedCommands] = useState<string | null>(null)
  const [drawSpeed, setDrawSpeed] = useState<number>(() =>
    parseInt(localStorage.getItem('muralSpeed') ?? '650', 10)
  )
  const [attachExistingDraw, setAttachExistingDraw] = useState(false)

  // The backend uses 'SvgSelect' as its "ready to draw" phase, but we intercept
  // it to show our own InputSelect screen instead.
  function backendPhaseToUiPhase(phase: string): UiPhase {
    if (phase === 'SvgSelect') return 'InputSelect'
    return phase as UiPhase
  }

  useEffect(() => {
    loadCachedCommands().then((cached) => {
      if (cached) setRenderedCommands(cached)
    })

    getState()
      .then((s) => {
        if (!s?.phase) throw new Error('No backend')
        setBackendState(s)
        if (s.phase === 'Drawing') {
          setAttachExistingDraw(true)
          setUiPhase('Drawing')
        } else {
          setUiPhase(backendPhaseToUiPhase(s.phase))
        }
      })
      .catch(() => {
        if (import.meta.env.DEV) {
          const mock: BackendState = {
            phase: 'SvgSelect',
            topDistance: 1667,
            safeWidth: 1000,
            homeX: 500,
            homeY: 350,
            safeXFraction: 0.2,
            safeYFraction: 0.25,
            streamingMode: true,
            unoConnected: true,
          }
          enableMockMode(mock)
          setBackendState(mock)
          setUiPhase('InputSelect')
        } else {
          showErrorAlert('Failed to retrieve state')
        }
      })
  }, [])

  function handleStateUpdate(s: BackendState) {
    setBackendState(s)
    setUiPhase(backendPhaseToUiPhase(s.phase))
  }

  function handleResetSession(s: BackendState) {
    clearCachedCommands()
    setRenderedCommands(null)
    setAttachExistingDraw(false)
    handleStateUpdate(s)
  }

  function handleSelectSvg() {
    setInputType('svg')
    setUiPhase('SvgSelect')
  }

  function handleSelectRaster() {
    setInputType('raster')
    setUiPhase('RasterSelect')
  }

  function handleBackToInputSelect() {
    setSvgState(null)
    setRasterState(null)
    setSelectedRenderer(null)
    setInputType(null)
    setUiPhase('InputSelect')
  }

  function handleSvgPreview(state: SvgState) {
    setSvgState(state)
    setUiPhase('ChooseRenderer')
  }

  function handleRasterPreview(state: RasterImageState) {
    setRasterState(state)
    const rasterRenderers = getRenderersByInputType('raster')
    if (rasterRenderers.length === 1) {
      setSelectedRenderer(rasterRenderers[0])
      setUiPhase('DrawingPreview')
    } else {
      setUiPhase('ChooseRenderer')
    }
  }

  function handleRendererChosen(renderer: RendererDefinition) {
    setSelectedRenderer(renderer)
    setUiPhase('DrawingPreview')
  }

  function handleBackFromChooseRenderer() {
    setUiPhase(inputType === 'raster' ? 'RasterSelect' : 'SvgSelect')
  }

  function handleBackFromDrawingPreview() {
    if (inputType === 'raster' && getRenderersByInputType('raster').length === 1) {
      setUiPhase('RasterSelect')
    } else {
      setUiPhase('ChooseRenderer')
    }
  }

  async function handleCommandsAccepted(commands: string) {
    setRenderedCommands(commands)
    await saveCachedCommands(commands)

    if (backendState?.streamingMode) {
      // No SD card: keep commands in browser memory/IndexedDB and advance straight to RetractBelts
      try {
        const nextState = await doneWithPhase()
        setBackendState(nextState)
        setUiPhase(backendPhaseToUiPhase(nextState.phase))
      } catch (err) {
        await showErrorAlert(`Failed to advance phase: ${err}`)
      }
      return
    }

    setUiPhase('UploadProgress')
  }

  function handleUploadDone(s: BackendState) {
    setBackendState(s)
    setUiPhase(s.phase)
  }

  async function handleUploadError(message: string) {
    await showErrorAlert(message)
  }

  function handleBeginDrawing(speed: number) {
    setDrawSpeed(speed)
    setAttachExistingDraw(false)
    if (backendState?.streamingMode) {
      setUiPhase('Drawing')
    } else {
      setUiPhase('DrawingBegan')
    }
  }

  let currentScreen
  switch (uiPhase) {
    case 'loading':
      currentScreen = <LoadingScreen />
      break
    case 'RetractBelts':
      currentScreen = <RetractBeltsScreen onDone={handleStateUpdate} />
      break
    case 'SetTopDistance':
      currentScreen = <SetTopDistanceScreen onDone={handleStateUpdate} />
      break
    case 'ExtendToHome':
      currentScreen = <ExtendToHomeScreen state={backendState!} onDone={handleStateUpdate} />
      break
    case 'PenCalibration':
      currentScreen = <PenCalibrationScreen onDone={handleStateUpdate} />
      break
    case 'InputSelect':
      currentScreen = (
        <InputSelectScreen
          onSelectSvg={handleSelectSvg}
          onSelectRaster={handleSelectRaster}
        />
      )
      break
    case 'SvgSelect':
      currentScreen = (
        <SvgUploadScreen
          state={backendState!}
          initialState={svgState ?? undefined}
          onPreview={handleSvgPreview}
          onBack={handleBackToInputSelect}
        />
      )
      break
    case 'RasterSelect':
      currentScreen = (
        <RasterUploadScreen
          state={backendState!}
          initialState={rasterState ?? undefined}
          onPreview={handleRasterPreview}
          onBack={handleBackToInputSelect}
        />
      )
      break
    case 'ChooseRenderer':
      currentScreen = (
        <ChooseRendererScreen
          renderers={getRenderersByInputType(inputType!)}
          onChoose={handleRendererChosen}
          onBack={handleBackFromChooseRenderer}
        />
      )
      break
    case 'DrawingPreview':
      currentScreen = (
        <DrawingPreviewScreen
          state={backendState!}
          svgState={svgState ?? undefined}
          imageState={rasterState ?? undefined}
          renderer={selectedRenderer!}
          onAccept={handleCommandsAccepted}
          onBack={handleBackFromDrawingPreview}
        />
      )
      break
    case 'UploadProgress':
      currentScreen = (
        <UploadProgressScreen
          commands={renderedCommands!}
          onDone={handleUploadDone}
          onError={handleUploadError}
        />
      )
      break
    case 'BeginDrawing':
      currentScreen = (
        <BeginDrawingScreen
          streamingMode={backendState?.streamingMode}
          hasCommands={Boolean(renderedCommands)}
          onBegin={handleBeginDrawing}
          onReset={handleResetSession}
        />
      )
      break
    case 'Drawing':
      currentScreen = (
        <DrawingStreamScreen
          commands={renderedCommands}
          speed={drawSpeed}
          attachOnly={attachExistingDraw}
          onReset={handleResetSession}
        />
      )
      break
    case 'DrawingBegan':
      currentScreen = <DrawingBeganScreen />
      break
    default:
      currentScreen = (
        <div className="text-center text-red-400 text-sm">Unrecognized phase: {uiPhase}</div>
      )
  }

  return (
    <div className="min-h-screen bg-gray-950 flex flex-col items-center justify-center p-4 sm:p-6 lg:p-8">
      {backendState?.streamingMode && backendState.unoConnected === false && uiPhase !== 'Drawing' && (
        <div className="w-full max-w-sm sm:max-w-md lg:max-w-xl mb-4 rounded-xl bg-amber-950/70 border border-amber-700/70 px-4 py-3 text-xs text-amber-200 leading-relaxed">
          <strong>ATmega328P not responding:</strong> Make sure the HW-649 DIP switches <strong>1 &amp; 2 are ON</strong> (3–8 OFF) and press Reset.
        </div>
      )}
      <div className="w-full max-w-sm sm:max-w-md lg:max-w-xl 2xl:max-w-4xl">{currentScreen}</div>
      <a
        href="https://github.com/nikivanov/mural"
        className="mt-6 text-xs sm:text-sm text-gray-600 hover:text-gray-400 transition-colors"
      >
        Mural
      </a>
    </div>
  )
}
