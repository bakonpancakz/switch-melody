// Writes an ffmetadata file whose METADATA_BLOCK_PICTURE carries the image (for Opus tests)
const fs = require('fs');
const [img, out, title] = process.argv.slice(2);
const data = fs.readFileSync(img);
const mime = Buffer.from('image/jpeg');
const u32 = (v) => { const b = Buffer.alloc(4); b.writeUInt32BE(v); return b; };
const blk = Buffer.concat([u32(3), u32(mime.length), mime, u32(0), u32(0), u32(0), u32(24), u32(0), u32(data.length), data]);
fs.writeFileSync(out, `;FFMETADATA1\ntitle=${title}\nartist=Opus Artist\nalbum=Opus Album\nMETADATA_BLOCK_PICTURE=${blk.toString('base64')}\nzz_after=after the picture\n`);
