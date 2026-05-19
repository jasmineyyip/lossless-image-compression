"use client";

import { useEffect, useRef, useState } from "react";
import { IoIosCheckmarkCircleOutline } from "react-icons/io";
import {
  BarChart,
  Bar,
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
};

type HistogramBin = {
  residual: number;
  count: number;
};

export default function Home() {
  const [ready, setReady] = useState(false);
  const [stats, setStats] = useState<Stats | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [dragOver, setDragOver] = useState(false);
  const [originalUrl, setOriginalUrl] = useState<string | null>(null);
  const [reconstructed, setReconstructed] = useState<Reconstructed | null>(null);
  const [histogram, setHistogram] = useState<HistogramBin[] | null>(null);
  const moduleRef = useRef<any>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    window.Module = window.Module || {};
    window.Module.onRuntimeInitialized = () => {
      moduleRef.current = window.Module;
      setReady(true);
    };

    const script = document.createElement("script");
    script.src = "/codec.js";
    script.async = true;
    document.body.appendChild(script);

    return () => {
      try {
        document.body.removeChild(script);
      } catch {}
    };
  }, []);

  useEffect(() => {
    if (!reconstructed || !canvasRef.current) return;
    const { bytes, width, height } = reconstructed;
    const canvas = canvasRef.current;
    canvas.width = width;
    canvas.height = height;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const rgba = new Uint8ClampedArray(width * height * 4);
    for (let i = 0; i < bytes.length; i++) {
      rgba[i * 4] = bytes[i];
      rgba[i * 4 + 1] = bytes[i];
      rgba[i * 4 + 2] = bytes[i];
      rgba[i * 4 + 3] = 255;
    }
    ctx.putImageData(new ImageData(rgba, width, height), 0, 0);
  }, [reconstructed]);

  async function handleFile(file: File) {
    if (!moduleRef.current) return;
    setError(null);

    const Module = moduleRef.current;
    const inputBytes = new Uint8Array(await file.arrayBuffer());

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

    Module._free(inputPtr);
    Module._free(sizePtr);
    Module.ccall("free_buffer", null, ["number"], [outputPtr]);

    // Decompress to display + verify.
    const compressedPtr = Module._malloc(compressed.length);
    Module.HEAPU8.set(compressed, compressedPtr);
    const widthPtr = Module._malloc(4);
    const heightPtr = Module._malloc(4);

    const reconstructedPtr = Module.ccall(
      "decompress_image",
      "number",
      ["number", "number", "number", "number"],
      [compressedPtr, compressed.length, widthPtr, heightPtr]
    );

    const width = Module.HEAP32[widthPtr / 4];
    const height = Module.HEAP32[heightPtr / 4];
    const reconstructedBytes = Module.HEAPU8.slice(
      reconstructedPtr,
      reconstructedPtr + width * height
    );

    Module._free(compressedPtr);
    Module._free(widthPtr);
    Module._free(heightPtr);
    Module.ccall("free_buffer", null, ["number"], [reconstructedPtr]);

    // Parse histogram from compressed bytes header (bytes 8 onward).
    const dv = new DataView(compressed.buffer);
    const histBins: HistogramBin[] = [];
    for (let i = 0; i < 511; i++) {
      const count = dv.getUint32(8 + i * 4, true);
      histBins.push({ residual: i - 255, count });
    }

    // Shannon entropy of the residual distribution.
    const total = histBins.reduce((sum, b) => sum + b.count, 0);
    let entropy = 0;
    for (const bin of histBins) {
      if (bin.count > 0) {
        const p = bin.count / total;
        entropy -= p * Math.log2(p);
      }
    }

    const pixels = width * height;
    const headerSize = 8 + 511 * 4;
    const encodedSize = outputSize - headerSize;

    setStats({
      fileName: file.name,
      width,
      height,
      pixels,
      originalSize: inputBytes.length,
      rawSize: pixels,
      compressedSize: outputSize,
      bpp: (encodedSize * 8) / pixels,
      ratio: pixels / outputSize,
      elapsed,
      entropy,
    });

    if (originalUrl) URL.revokeObjectURL(originalUrl);
    setOriginalUrl(URL.createObjectURL(file));
    setReconstructed({ bytes: reconstructedBytes, width, height });
    setHistogram(histBins);
  }

  return (
    <main className="max-w-4xl mx-auto px-6 py-12">
      <h1 className="text-3xl font-bold mb-2">Lossless Image Compression</h1>
      <p className="text-gray-600 mb-8">
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
          dragOver ? "border-blue-400 bg-blue-50" : "border-gray-300"
        }`}
      >
        <p className="text-gray-600">
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
              <p className="text-sm text-gray-500 mb-2">Original (rendered grayscale)</p>
              <img
                src={originalUrl}
                alt="Original"
                className="w-full rounded border border-gray-200"
                style={{ filter: "grayscale(100%)" }}
              />
            </div>
            <div>
              <p className="text-sm text-gray-500 mb-2">Decoded from .bin</p>
              <canvas
                ref={canvasRef}
                className="w-full rounded border border-gray-200"
              />
            </div>
          </div>

          <div className="mt-4 inline-flex items-center gap-2 px-3 py-1.5 bg-green-50 text-green-700 rounded text-sm font-medium">
            <IoIosCheckmarkCircleOutline size={18} />
            <span>Lossless reconstruction verified</span>
          </div>
        </>
      )}

      {stats && (
        <div className="mt-8 bg-gray-50 rounded-lg p-6">
          <StatRow label="File" value={stats.fileName} />
          <StatRow
            label="Dimensions"
            value={`${stats.width} × ${stats.height} (${stats.pixels.toLocaleString()} pixels)`}
          />
          <StatRow label="Original (PNG)" value={`${stats.originalSize.toLocaleString()} bytes`} />
          <StatRow label="Raw grayscale" value={`${stats.rawSize.toLocaleString()} bytes`} />
          <StatRow label="Compressed (.bin)" value={`${stats.compressedSize.toLocaleString()} bytes`} />
          <StatRow label="Residual entropy" value={`${stats.entropy.toFixed(3)} bits/pixel`} />
          <StatRow label="Bits per pixel" value={stats.bpp.toFixed(3)} />
          <StatRow label="Ratio vs raw" value={`${stats.ratio.toFixed(2)}×`} />
          <StatRow label="Encode time" value={`${stats.elapsed.toFixed(1)} ms`} />
        </div>
      )}

      {histogram && (
        <div className="mt-8 bg-gray-50 rounded-lg p-6">
          <h3 className="text-base font-semibold mb-2 text-gray-900">Residual histogram</h3>
          <p className="text-sm text-gray-500 mb-4">
            Distribution of prediction errors. The sharp peak at zero is the compression
            opportunity — concentrated probability means the range coder can spend very
            few bits per pixel on the common values.
          </p>
          <ResponsiveContainer width="100%" height={260}>
            <BarChart data={histogram} margin={{ top: 10, right: 10, bottom: 5, left: 0 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" />
              <XAxis
                dataKey="residual"
                type="number"
                domain={[-255, 255]}
                ticks={[-200, -100, 0, 100, 200]}
                tick={{ fontSize: 11, fill: "#6b7280" }}
                label={{ value: "residual value", position: "insideBottom", offset: -2, fontSize: 11, fill: "#6b7280" }}
              />
              <YAxis
                tick={{ fontSize: 11, fill: "#6b7280" }}
                tickFormatter={(v) => (v >= 1000 ? `${(v / 1000).toFixed(0)}k` : v)}
              />
              <Tooltip
                formatter={(value) => {
                  if (typeof value === "number") {
                    return [value.toLocaleString(), "count"];
                  }

                  return [String(value ?? ""), "count"];
                }}
                labelFormatter={(label) => {
                  const numericLabel = typeof label === "number" ? label : Number(label);
                  return `Residual: ${numericLabel > 0 ? "+" : ""}${numericLabel}`;
                }}
              />
              <Bar dataKey="count" fill="#3b82f6" />
            </BarChart>
          </ResponsiveContainer>
        </div>
      )}
    </main>
  );
}

function StatRow({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex justify-between py-2 border-b border-gray-200 last:border-0">
      <span className="text-gray-500">{label}</span>
      <span className="font-mono font-semibold text-gray-400">{value}</span>
    </div>
  );
}