export type Phase =
  | 'RetractBelts'
  | 'SetTopDistance'
  | 'ExtendToHome'
  | 'PenCalibration'
  | 'SvgSelect'
  | 'BeginDrawing'
  | 'Drawing'

export interface BackendState {
  phase: Phase
  topDistance?: number
  pulleyDiameter?: number
  safeWidth?: number
  safeXFraction: number
  safeYFraction: number
  homeX?: number
  homeY?: number
  moving?: boolean
  startedHoming?: boolean
  freeKb?: number
  streamingMode?: boolean
  unoConnected?: boolean
}

export type StreamRunnerState =
  | 'idle'
  | 'drawing'
  | 'paused'
  | 'homing'
  | 'finished'
  | 'stopped'

export interface StreamStatus {
  state: StreamRunnerState
  accepted: number
  nextSeq: number
  executed: number
  totalCommands: number
  buffered: number
  capacity: number
  progress: number
  streamDone: boolean
  unoConnected: boolean
  unoFree: number
}

/** Internal UI phase — extends backend phases with SVG pipeline sub-states */
export type UiPhase =
  | 'loading'
  | Phase
  | 'InputSelect'
  | 'RasterSelect'
  | 'ChooseRenderer'
  | 'DrawingPreview'
  | 'UploadProgress'
  | 'DrawingBegan'

export type InfillDensity = 0 | 1 | 2 | 3 | 4

export interface RenderResult {
  commands: string[]
  svgJson: string
  distance: number
  drawDistance: number
  penLiftCount: number
}

export interface WorkerMessage {
  type: 'status' | 'log' | 'vectorizer' | 'renderer'
  payload: unknown
}
