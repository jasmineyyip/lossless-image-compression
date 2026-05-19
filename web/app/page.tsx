"use client";

import { useEffect, useRef, useState } from "react";

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
};

export default function Home() {
  const [ready, setReady] = useState(false);
  const [stats, setStats] = useState<Stats | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [dragOver, setDragOver] = useState(false);
  const moduleRef = useRef<any>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    // Pre-register the ready callback before loading codec.js,
    // because Emscripten's generated code looks for window.Module on init.
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

    const dv = new DataView(compressed.buffer);
    const width = dv.getUint32(0, true);
    const height = dv.getUint32(4, true);
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
    });
  }

  return (
    <main className="max-w-2xl mx-auto px-6 py-12">
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

      {stats && (
        <div className="mt-8 bg-gray-50 rounded-lg p-6">
          <StatRow label="File" value={stats.fileName} />
          <StatRow
            label="Dimensions"
            value={`${stats.width} × ${stats.height} (${stats.pixels.toLocaleString()} pixels)`}
          />
          <StatRow
            label="Original (PNG)"
            value={`${stats.originalSize.toLocaleString()} bytes`}
          />
          <StatRow
            label="Raw grayscale"
            value={`${stats.rawSize.toLocaleString()} bytes`}
          />
          <StatRow
            label="Compressed (.bin)"
            value={`${stats.compressedSize.toLocaleString()} bytes`}
          />
          <StatRow label="Bits per pixel" value={stats.bpp.toFixed(3)} />
          <StatRow label="Ratio vs raw" value={`${stats.ratio.toFixed(2)}×`} />
          <StatRow label="Encode time" value={`${stats.elapsed.toFixed(1)} ms`} />
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