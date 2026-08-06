// MJPEG 스트림에서 SOI/EOI 로 프레임을 끊어 N번째 이후 프레임 1장을 저장한다.
const http = require('http'), fs = require('fs');
const port = parseInt(process.argv[2], 10);
const out  = process.argv[3];
const skip = parseInt(process.argv[4] || '10', 10); // 히스토리 수렴 대기용
let buf = Buffer.alloc(0), n = 0, done = false;
const req = http.get({host:'127.0.0.1', port, path:'/'}, res => {
  res.on('data', d => {
    if (done) return;
    buf = Buffer.concat([buf, d]);
    for (;;) {
      const s = buf.indexOf(Buffer.from([0xFF,0xD8]));
      if (s < 0) { if (buf.length > 4<<20) buf = Buffer.alloc(0); break; }
      const e = buf.indexOf(Buffer.from([0xFF,0xD9]), s+2);
      if (e < 0) { if (s > 0) buf = buf.slice(s); break; }
      const jpg = buf.slice(s, e+2);
      buf = buf.slice(e+2);
      n++;
      if (n >= skip) {
        fs.writeFileSync(out, jpg);
        console.log(`saved frame #${n} bytes=${jpg.length}`);
        done = true; req.destroy(); process.exit(0);
      }
    }
  });
});
req.on('error', e => { console.error('ERR', e.message); process.exit(1); });
setTimeout(() => { console.error(`TIMEOUT frames=${n}`); process.exit(2); }, 30000);
