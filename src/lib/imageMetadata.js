// imageMetadata.js; JPEG dimension extraction via direct binary
// parsing — no image-decoding library, consistent with this substrate's
// zero-npm-dependency design. This is NEW logic, stated plainly: no
// existing kernel parses binary file formats. It's a small, well-defined
// parser (walk the JFIF marker segments to the Start-Of-Frame marker,
// which carries width/height at a fixed offset), not a general image
// library — it reads dimensions and nothing else.

/**
 * Reads width/height from a JPEG buffer by walking its marker segments
 * to the SOF (Start Of Frame) marker, where dimensions are stored at a
 * fixed offset. Throws on anything that isn't a well-formed JPEG rather
 * than guessing.
 */
export function readJpegDimensions(buffer) {
  if (!Buffer.isBuffer(buffer)) throw new Error('readJpegDimensions needs a Buffer');
  if (buffer.length < 4 || buffer[0] !== 0xFF || buffer[1] !== 0xD8) {
    throw new Error('not a JPEG file (missing SOI marker)');
  }

  let offset = 2;
  while (offset + 4 <= buffer.length) {
    if (buffer[offset] !== 0xFF) throw new Error(`malformed JPEG marker at offset ${offset}`);
    const marker = buffer[offset + 1];

    // SOF0-SOF3, SOF5-SOF7, SOF9-SOF11, SOF13-SOF15 all carry width/height
    // at the same position (byte 5 = height hi, ...). 0xC4 (DHT), 0xC8
    // (JPG), 0xCC (DAC) fall numerically inside that range but are NOT
    // SOF markers and must be excluded explicitly.
    const isSof = (marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
      (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF);

    if (isSof) {
      if (offset + 9 > buffer.length) throw new Error('truncated JPEG: SOF marker found but not enough bytes for dimensions');
      const height = buffer.readUInt16BE(offset + 5);
      const width = buffer.readUInt16BE(offset + 7);
      return { width, height };
    }

    if (marker === 0xD8 || marker === 0xD9) { offset += 2; continue; } // SOI/EOI carry no length field
    const segmentLength = buffer.readUInt16BE(offset + 2);
    if (segmentLength < 2) throw new Error(`malformed JPEG: non-positive segment length at offset ${offset}`);
    offset += 2 + segmentLength;
  }

  throw new Error('no SOF marker found; could not determine JPEG dimensions');
}
