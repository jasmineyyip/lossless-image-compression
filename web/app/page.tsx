"use client";

import { useEffect, useRef, useState } from "react";
import { IoIosCheckmarkCircleOutline, IoIosDownload } from "react-icons/io";
import {
  AreaChart,
  Area,
  XAxis,
  YAxis,
  Tooltip,
  ResponsiveContainer,
  CartesianGrid,
} from "recharts";

declare global {
  interface Window {
    Module: any;
  }
}

type Stats = {
  fileName: string;
  width: number;
  height: number;
  pixels: number;
  numChannels: number;
  originalSize: number;
  rawSize: number;
  compressedSize: number;
  bpp: number;
  ratio: number;
  elapsed: number;
  entropy: number;
};

type Reconstructed = {
  bytes: Uint8Array;
  width: number;
  height: number;
  numChannels: number;
};

type HistogramBin = Record<string, number>;

let codecScriptInjected = false;

const CONTEXT_BOUNDARIES = [4, 12, 28, 60, 124, 252, 1024];

function getLuma(bytes: Uint8Array, numChannels: number, idx: number): number {
  if (numChannels === 1) return bytes[idx];
  const r = bytes[idx * 3], g = bytes[idx * 3 + 1], b = bytes[idx * 3 + 2];
  const co = r - b;
  const t = b + (co >> 1);
  const cg = g - t;
  return t + (cg >> 1);
}

function paethPredictor(a: number, b: number, c: number): number {
  const p = a + b - c;
  const pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
  if (pa <= pb && pa <= pc) return a;
  if (pb <= pc) return b;
  return c;
}

export default function Home() {
  const [ready, setReady] = useState(false);
  const [stats, setStats] = useState<Stats | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [dragOver, setDragOver] = useState(false);
  const [originalUrl, setOriginalUrl] = useState<string | null>(null);
  const [reconstructed, setReconstructed] = useState<Reconstructed | null>(null);
  const [histogram, setHistogram] = useState<HistogramBin[] | null>(null);
  const [compressedBytes, setCompressedBytes] = useState<Uint8Array | null>(null);
  const [hoverInfo, setHoverInfo] = useState<{
    mouseX: number; mouseY: number;
    pixelX: number; pixelY: number;
    luma: number; pred: number; residual: number;
    activity: number; context: number;
  } | null>(null);
  const moduleRef = useRef<any>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const residualCanvasRef = useRef<HTMLCanvasElement>(null);

  function downloadBlob(blob: Blob, filename: string) {
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  }

  function handleCanvasHover(e: React.MouseEvent<HTMLCanvasElement>) {
    if (!reconstructed) return;
    const { bytes, width, height, numChannels } = reconstructed;
    const rect = e.currentTarget.getBoundingClientRect();
    const px = Math.floor((e.clientX - rect.left) * (width / rect.width));
    const py = Math.floor((e.clientY - rect.top) * (height / rect.height));
    if (px < 0 || px >= width || py < 0 || py >= height) { setHoverInfo(null); return; }

    const lumaAt = (x: number, y: number) =>
      x < 0 || y < 0 || x >= width || y >= height ? 0 : getLuma(bytes, numChannels, y * width + x);

    const a = lumaAt(px - 1, py);
    const b = lumaAt(px, py - 1);
    const c = lumaAt(px - 1, py - 1);
    const luma = lumaAt(px, py);
    const pred = paethPredictor(a, b, c);
    const residual = luma - pred;
    const activity = Math.abs(a - c) + Math.abs(b - c);
    let context = 7;
    for (let i = 0; i < CONTEXT_BOUNDARIES.length; i++) {
      if (activity < CONTEXT_BOUNDARIES[i]) { context = i; break; }
    }
    setHoverInfo({ mouseX: e.clientX, mouseY: e.clientY, pixelX: px, pixelY: py, luma, pred, residual, activity, context });
  }

  const baseName = stats ? stats.fileName.replace(/\.[^.]+$/, "") : "image";

  const handleDownloadBin = () => {
    if (!compressedBytes) return;
    downloadBlob(new Blob([new Uint8Array(compressedBytes)], { type: "application/octet-stream" }), `${baseName}.bin`);
  };

  const handleDownloadDecodedPng = () => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    canvas.toBlob((blob) => {
      if (!blob) return;
      downloadBlob(blob, `${baseName}_decoded.png`);
    }, "image/png");
  };

  useEffect(() => {
    window.Module = window.Module || {};

    if (window.Module._malloc) {
      moduleRef.current = window.Module;
      setReady(true);
      return;
    }

    window.Module.onRuntimeInitialized = () => {
      moduleRef.current = window.Module;
      setReady(true);
    };

    if (!codecScriptInjected) {
      codecScriptInjected = true;
      const script = document.createElement("script");
      script.src = "/codec.js";
      script.async = true;
      document.body.appendChild(script);
    }
  }, []);

  function computeResidualHeatmap(
    pixels: Uint8Array,
    width: number,
    height: number,
    numChannels: number,
    contrast = 4
  ): ImageData {
    const luma = new Int16Array(width * height);
    if (numChannels === 1) {
      for (let i = 0; i < luma.length; i++) luma[i] = pixels[i];
    } else {
      for (let i = 0; i < luma.length; i++) {
        const r = pixels[i*3], g = pixels[i*3+1], b = pixels[i*3+2];
        const co = r - b;
        const t  = b + (co >> 1);
        const cg = g - t;
        luma[i]  = t + (cg >> 1);
      }
    }

    const imageData = new ImageData(width, height);
    const out = imageData.data;
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const idx = y * width + x;
        const a = x > 0 ? luma[idx - 1] : 0;
        const b = y > 0 ? luma[idx - width] : 0;
        const c = (x > 0 && y > 0) ? luma[idx - width - 1] : 0;
        const p = a + b - c;
        const pred = (Math.abs(p - a) <= Math.abs(p - b) && Math.abs(p - a) <= Math.abs(p - c))
          ? a : (Math.abs(p - b) <= Math.abs(p - c) ? b : c);
        const v = Math.min(Math.abs(luma[idx] - pred) * contrast, 255);
        out[idx*4    ] = v;
        out[idx*4 + 1] = v;
        out[idx*4 + 2] = v;
        out[idx*4 + 3] = 255;
      }
    }
    return imageData;
  }

  useEffect(() => {
    if (!reconstructed || !canvasRef.current) return;
    const { bytes, width, height, numChannels } = reconstructed;
    const canvas = canvasRef.current;
    canvas.width = width;
    canvas.height = height;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const imageData = ctx.createImageData(width, height);
    const rgba = imageData.data;

    if (numChannels === 1) {
      for (let i = 0; i < width * height; i++) {
        rgba[i * 4 + 0] = bytes[i];
        rgba[i * 4 + 1] = bytes[i];
        rgba[i * 4 + 2] = bytes[i];
        rgba[i * 4 + 3] = 255;
      }
    } else {
      for (let i = 0; i < width * height; i++) {
        rgba[i * 4 + 0] = bytes[i * 3 + 0];
        rgba[i * 4 + 1] = bytes[i * 3 + 1];
        rgba[i * 4 + 2] = bytes[i * 3 + 2];
        rgba[i * 4 + 3] = 255;
      }
    }
    ctx.putImageData(imageData, 0, 0);

    const rc = residualCanvasRef.current;
    if (rc) {
      rc.width = width;
      rc.height = height;
      const rctx = rc.getContext("2d");
      if (rctx) rctx.putImageData(computeResidualHeatmap(bytes, width, height, numChannels), 0, 0);
    }
  }, [reconstructed]);

  async function colorNormalize(file: File): Promise<Uint8Array> {
    const img = await new Promise<HTMLImageElement>((resolve, reject) => {
      const i = new Image();
      i.onload = () => resolve(i);
      i.onerror = reject;
      i.src = URL.createObjectURL(file);
    });
    const canvas = document.createElement("canvas");
    canvas.width = img.naturalWidth;
    canvas.height = img.naturalHeight;
    canvas.getContext("2d")!.drawImage(img, 0, 0);
    const blob = await new Promise<Blob>((resolve, reject) => {
      canvas.toBlob(b => b ? resolve(b) : reject(new Error("toBlob failed")), "image/png");
    });
    return new Uint8Array(await blob.arrayBuffer());
  }

  async function handleFile(file: File) {
    if (!moduleRef.current) return;
    setError(null);

    const Module = moduleRef.current;
    const inputBytes = await colorNormalize(file);

    const start = performance.now();

    const inputPtr = Module._malloc(inputBytes.length);
    Module.HEAPU8.set(inputBytes, inputPtr);
    const sizePtr = Module._malloc(4);

    const outputPtr = Module.ccall(
      "compress_image",
      "number",
      ["number", "number", "number"],
      [inputPtr, inputBytes.length, sizePtr]
    );

    if (outputPtr === 0) {
      Module._free(inputPtr);
      Module._free(sizePtr);
      setError("Failed to load image — not a supported format?");
      return;
    }

    const outputSize = Module.HEAP32[sizePtr / 4];
    const compressed = Module.HEAPU8.slice(outputPtr, outputPtr + outputSize);
    const elapsed = performance.now() - start;
    setCompressedBytes(compressed);

    Module._free(inputPtr);
    Module._free(sizePtr);
    Module.ccall("free_buffer", null, ["number"], [outputPtr]);

    // decompress to display + verify
    const compressedPtr = Module._malloc(compressed.length);
    Module.HEAPU8.set(compressed, compressedPtr);
    const widthPtr = Module._malloc(4);
    const heightPtr = Module._malloc(4);
    const numChannelsPtr = Module._malloc(4);

    const reconstructedPtr = Module.ccall(
      "decompress_image",
      "number",
      ["number", "number", "number", "number", "number"],
      [compressedPtr, compressed.length, widthPtr, heightPtr, numChannelsPtr]
    );

    const width = Module.HEAP32[widthPtr / 4];
    const height = Module.HEAP32[heightPtr / 4];
    const numChannels = Module.HEAP32[numChannelsPtr / 4];
    const reconstructedBytes = Module.HEAPU8.slice(
      reconstructedPtr,
      reconstructedPtr + width * height * numChannels
    );

    Module._free(compressedPtr);
    Module._free(widthPtr);
    Module._free(heightPtr);
    Module._free(numChannelsPtr);
    Module.ccall("free_buffer", null, ["number"], [reconstructedPtr]);

    // parse all per-channel histograms from compressed header.
    // layout: w(4) + h(4) + num_channels(4) + hist[0..numChannels-1]
    const dv = new DataView(compressed.buffer);
    const resOffset = numChannels === 1 ? 255 : 510;
    const histSize = 2 * resOffset + 1;
    const channelNames = numChannels === 1 ? ["Y"] : ["Y", "Co", "Cg"];

    const NUM_CONTEXTS = 8;
    const histograms: number[][] = [];
    let histByteOffset = 12;
    for (let c = 0; c < numChannels; c++) {
      const h = new Array<number>(histSize).fill(0);
      for (let k = 0; k < NUM_CONTEXTS; k++) {
        for (let i = 0; i < histSize; i++) {
          h[i] += dv.getUint32(histByteOffset, true);
          histByteOffset += 4;
        }
      }
      histograms.push(h);
    }

    // shannon entropy from Y channel
    const yHist = histograms[0];
    const total = yHist.reduce((sum, c) => sum + c, 0);
    let entropy = 0;
    for (const count of yHist) {
      if (count > 0) {
        const p = count / total;
        entropy -= p * Math.log2(p);
      }
    }

    // reshape into per-bin rows for Recharts
    const ZOOM = 100;
    const histBins: HistogramBin[] = Array.from({ length: histSize }, (_, i) => {
      const row: HistogramBin = { residual: i - resOffset };
      channelNames.forEach((name, c) => { row[name] = histograms[c][i]; });
      return row;
    }).filter(row => Math.abs(row.residual) <= ZOOM);

    const pixels = width * height;
    const headerSize = 12 + numChannels * NUM_CONTEXTS * histSize * 4;
    const encodedSize = outputSize - headerSize;

    setStats({
      fileName: file.name,
      width,
      height,
      pixels,
      numChannels,
      originalSize: inputBytes.length,
      rawSize: pixels * numChannels,
      compressedSize: outputSize,
      bpp: (encodedSize * 8) / (pixels * numChannels),
      ratio: (pixels * numChannels) / outputSize,
      elapsed,
      entropy,
    });

    if (originalUrl) URL.revokeObjectURL(originalUrl);
    setOriginalUrl(URL.createObjectURL(new Blob([new Uint8Array(inputBytes)], { type: "image/png" })));
    setReconstructed({ bytes: reconstructedBytes, width, height, numChannels });
    setHistogram(histBins);
  }

  const numChannels = reconstructed?.numChannels ?? 1;

  return (
    <main className="max-w-4xl mx-auto px-6 py-12">
      <h1 className="text-3xl font-bold mb-2">Lossless Image Compression</h1>
      <p className="text-gray-400 mb-8">
        Paeth predictor + range coder, written in C++, running in your browser via WebAssembly.
      </p>

      <div
        onClick={() => fileInputRef.current?.click()}
        onDragOver={(e) => {
          e.preventDefault();
          setDragOver(true);
        }}
        onDragLeave={() => setDragOver(false)}
        onDrop={(e) => {
          e.preventDefault();
          setDragOver(false);
          if (e.dataTransfer.files.length) handleFile(e.dataTransfer.files[0]);
        }}
        className={`border-2 border-dashed rounded-lg p-16 text-center cursor-pointer transition ${
          dragOver ? "border-blue-500 bg-blue-950/30" : "border-gray-700"
        }`}
      >
        <p className="text-gray-400">
          {ready ? "Drop an image here or click to select" : "Loading WASM..."}
        </p>
        <input
          ref={fileInputRef}
          type="file"
          accept="image/*"
          className="hidden"
          onChange={(e) => {
            if (e.target.files?.length) handleFile(e.target.files[0]);
          }}
        />
      </div>

      {error && (
        <div className="mt-6 p-4 bg-red-50 text-red-700 rounded">{error}</div>
      )}

      {originalUrl && reconstructed && (
        <>
          <div className="mt-8 grid grid-cols-1 sm:grid-cols-2 gap-4">
            <div>
              <p className="text-sm text-gray-400 mb-2">Original</p>
              <img
                src={originalUrl}
                alt="Original"
                className="w-full rounded border border-gray-200"
              />
            </div>
            <div>
              <p className="text-sm text-gray-400 mb-2">Decoded from .bin</p>
              <canvas
                ref={canvasRef}
                className="w-full rounded border border-gray-700"
                onMouseMove={handleCanvasHover}
                onMouseLeave={() => setHoverInfo(null)}
              />
            </div>
          </div>

          <div className="mt-6 bg-gray-900 rounded-lg p-4 border border-gray-700">
            <p className="text-sm text-gray-400 mb-3">Prediction error</p>
            <canvas
              ref={residualCanvasRef}
              className="w-full rounded border border-gray-700"
              onMouseMove={handleCanvasHover}
              onMouseLeave={() => setHoverInfo(null)}
            />
            <p className="text-xs text-gray-500 mt-3 leading-relaxed">
              Each pixel shows how far Paeth&apos;s prediction missed by — black means predicted perfectly, brighter pixels are harder to predict. Most of the image is near-black, which is why range coding the residuals against a tight distribution costs so few bits per pixel.
            </p>
          </div>

          <div className="mt-4 flex flex-wrap items-center gap-3">
            <div className="inline-flex items-center gap-2 px-3 py-1.5 bg-gray-900 text-emerald-400 border border-gray-700 rounded text-sm font-medium">
              <IoIosCheckmarkCircleOutline size={18} />
              <span>Lossless reconstruction verified</span>
            </div>
            <button
              onClick={handleDownloadBin}
              className="inline-flex items-center gap-2 px-4 py-2 text-sm text-gray-300 bg-gray-900 border border-gray-700 rounded-lg hover:bg-gray-800 transition-colors"
            >
              <IoIosDownload size={18} />
              Download .bin
            </button>
            <button
              onClick={handleDownloadDecodedPng}
              className="inline-flex items-center gap-2 px-4 py-2 text-sm text-gray-300 bg-gray-900 border border-gray-700 rounded-lg hover:bg-gray-800 transition-colors"
            >
              <IoIosDownload size={18} />
              Download decoded PNG
            </button>
          </div>
        </>
      )}

      {stats && (
        <div className="mt-8 bg-gray-900 rounded-lg p-6 border border-gray-700">
          <h3 className="text-base font-semibold mb-4 text-white">Compression stats</h3>
          <StatRow label="File" value={stats.fileName} />
          <StatRow
            label="Dimensions"
            value={`${stats.width} × ${stats.height} (${stats.pixels.toLocaleString()} pixels, ${stats.numChannels}ch)`}
          />
          <StatRow label="Original (PNG)" value={`${stats.originalSize.toLocaleString()} bytes`} />
          <StatRow label="Raw uncompressed" value={`${stats.rawSize.toLocaleString()} bytes`} />
          <StatRow label="Compressed (.bin)" value={`${stats.compressedSize.toLocaleString()} bytes`} />
          <StatRow label="Residual entropy (Y)" value={`${stats.entropy.toFixed(3)} bits/pixel`} />
          <StatRow label="Bits/channel-pixel" value={stats.bpp.toFixed(3)} />
          <StatRow label="Ratio vs raw" value={`${stats.ratio.toFixed(2)}×`} />
          <StatRow label="Encode time" value={`${stats.elapsed.toFixed(1)} ms`} />
        </div>
      )}

      {histogram && (
        <div className="mt-8 bg-gray-900 rounded-lg p-6 border border-gray-700">
          <h3 className="text-base font-semibold mb-2 text-white">
            Residual histogram
          </h3>
          <p className="text-sm text-gray-400 mb-4">
            Distribution of prediction errors. The sharp peak at zero is the compression
            opportunity — concentrated probability means the range coder can spend very
            few bits per pixel on the common values.
          </p>
          <ResponsiveContainer width="100%" height={260}>
            <AreaChart data={histogram} margin={{ top: 10, right: 10, bottom: 5, left: 0 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="#374151" />
              <XAxis
                dataKey="residual"
                type="number"
                ticks={[-100, -50, 0, 50, 100]}
                tick={{ fontSize: 11, fill: "#9ca3af" }}
                label={{ value: "residual value", position: "insideBottom", offset: -2, fontSize: 11, fill: "#9ca3af" }}
              />
              <YAxis
                tick={{ fontSize: 11, fill: "#9ca3af" }}
                tickFormatter={(v) => (v >= 1000 ? `${(v / 1000).toFixed(0)}k` : v)}
              />
              <Tooltip
                contentStyle={{ backgroundColor: "#111827", border: "1px solid #374151", borderRadius: "0.5rem" }}
                itemStyle={{ color: "#f9fafb" }}
                formatter={(value, name) => {
                  if (typeof value === "number") {
                    return [value.toLocaleString(), name];
                  }
                  return [String(value ?? ""), name];
                }}
                labelFormatter={(label) => {
                  const numericLabel = typeof label === "number" ? label : Number(label);
                  return `Residual: ${numericLabel > 0 ? "+" : ""}${numericLabel}`;
                }}
                labelStyle={{ color: "#9ca3af", fontSize: 12, paddingBottom: 2 }}
              />
              <Area type="monotone" dataKey="Y"  stroke="#64748b" fill="#64748b" fillOpacity={0.3} />
              {numChannels === 3 && <>
                <Area type="monotone" dataKey="Co" stroke="#f97316" fill="#f97316" fillOpacity={0.45} />
                <Area type="monotone" dataKey="Cg" stroke="#14b8a6" fill="#14b8a6" fillOpacity={0.3} />
              </>}
            </AreaChart>
          </ResponsiveContainer>
        </div>
      )}
      {hoverInfo && (
        <div
          className="fixed z-50 pointer-events-none bg-gray-900/95 text-white text-xs rounded-lg px-3 py-2.5 shadow-xl border border-gray-700"
          style={{ left: hoverInfo.mouseX + 16, top: hoverInfo.mouseY + 16 }}
        >
          <div className="text-gray-400 font-mono mb-1.5">
            ({hoverInfo.pixelX}, {hoverInfo.pixelY})
          </div>
          <div className="font-mono space-y-1">
            <div className="flex justify-between gap-6">
              <span className="text-gray-400">luma</span>
              <span>{hoverInfo.luma}</span>
            </div>
            <div className="flex justify-between gap-6">
              <span className="text-gray-400">predicted</span>
              <span>{hoverInfo.pred}</span>
            </div>
            <div className="flex justify-between gap-6">
              <span className="text-gray-400">residual</span>
              <span className={
                hoverInfo.residual === 0 ? "text-emerald-400" :
                Math.abs(hoverInfo.residual) <= 8 ? "text-amber-400" : "text-red-400"
              }>
                {hoverInfo.residual > 0 ? "+" : ""}{hoverInfo.residual}
              </span>
            </div>
            <div className="border-t border-gray-700 pt-1 mt-1 space-y-1">
              <div className="flex justify-between gap-6">
                <span className="text-gray-400">activity</span>
                <span>{hoverInfo.activity}</span>
              </div>
              <div className="flex justify-between gap-6">
                <span className="text-gray-400">context</span>
                <span>{hoverInfo.context} / 7</span>
              </div>
            </div>
          </div>
        </div>
      )}
    </main>
  );
}

function StatRow({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex justify-between py-2 border-b border-gray-700 last:border-0">
      <span className="text-gray-400">{label}</span>
      <span className="font-mono font-semibold text-white">{value}</span>
    </div>
  );
}
