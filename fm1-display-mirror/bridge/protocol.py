"""FMIR v1: bounded rendered-pixel transactions, independent of firmware UI."""
import struct

HEADER = struct.Struct('<4sBBBBI6HI')
MAGIC = b'FMIR'
HELLO, BEGIN, RECT, COMMIT, INVALID = range(1, 6)
RAW, RLE, FILL, LZ4 = range(4)
MAX_PAYLOAD = 115200
MAX_BUFFER = MAX_PAYLOAD + HEADER.size

class ProtocolError(ValueError):
    pass

def lz4_decode(data, size):
    out = bytearray()
    p = 0
    def length(n):
        nonlocal p
        if n == 15:
            while True:
                if p >= len(data):
                    raise ProtocolError('truncated LZ4 length')
                v = data[p]; p += 1; n += v
                if v != 255:
                    break
        return n
    while p < len(data):
        token = data[p]; p += 1
        n = length(token >> 4)
        if p+n > len(data) or len(out)+n > size:
            raise ProtocolError('LZ4 literals')
        out.extend(data[p:p+n]); p += n
        if p == len(data):
            break
        if p+2 > len(data):
            raise ProtocolError('LZ4 offset')
        off = data[p] | data[p+1] << 8; p += 2
        n = length(token & 15)+4
        if not off or off > len(out) or len(out)+n > size:
            raise ProtocolError('LZ4 match')
        for _ in range(n):
            out.append(out[-off])
    if len(out) != size:
        raise ProtocolError('LZ4 output length')
    return out

def record(kind, seq=0, payload=b'', *, flags=0, codec=RAW,
           width=240, height=240, x=0, y=0, w=0, h=0):
    return HEADER.pack(MAGIC, 1, kind, flags, codec, seq,
                       width, height, x, y, w, h, len(payload)) + payload

def unpack7(data):
    out = bytearray()
    while data:
        mask, data = data[0], data[1:]
        n = min(7, len(data))
        if not n or mask >> n or any(v > 127 for v in data[:n]):
            raise ProtocolError('invalid seven-bit group')
        out.extend(v | (((mask >> i) & 1) << 7) for i, v in enumerate(data[:n]))
        data = data[n:]
    return bytes(out)

def pack7(data):
    out = bytearray()
    for p in range(0, len(data), 7):
        g = data[p:p+7]
        out.append(sum((v >> 7) << i for i, v in enumerate(g)))
        out.extend(v & 127 for v in g)
    return bytes(out)

class Decoder:
    """Single pending transaction; never publishes an incomplete frame."""
    def __init__(self):
        self.buffer = bytearray()
        self.reset()

    def reset(self):
        self.buffer.clear()
        self.firmware = None
        self.pixels = bytes(MAX_PAYLOAD)
        self.pending = None
        self.valid = False
        self.seq = None
        self.pending_seq = None
        self.coverage = None
        self.full = False
        self.rectangles = []

    def feed(self, data):
        if len(data) > MAX_BUFFER or len(self.buffer) + len(data) > 2*MAX_BUFFER:
            self.reset()
            raise ProtocolError('stream buffer limit')
        self.buffer.extend(data)
        frames = []
        try:
            while len(self.buffer) >= HEADER.size:
                fields = HEADER.unpack_from(self.buffer)
                magic, ver, kind, flags, codec, seq, width, height, x, y, w, h, n = fields
                if magic != MAGIC or ver != 1 or kind not in range(1, 6):
                    raise ProtocolError('unsupported protocol')
                if (width, height) != (240, 240) or n > MAX_PAYLOAD or flags & ~1 or codec > 3:
                    raise ProtocolError('invalid header')
                if len(self.buffer) < HEADER.size+n:
                    break
                payload = bytes(self.buffer[HEADER.size:HEADER.size+n])
                del self.buffer[:HEADER.size+n]
                frame = self.apply(kind, flags, codec, seq, x, y, w, h, payload)
                if frame is not None:
                    frames.append(frame)
        except (ValueError, struct.error):
            self.reset()
            raise
        return frames

    def apply(self, kind, flags, codec, seq, x, y, w, h, data):
        if kind == HELLO:
            if not data or len(data) > 64:
                raise ProtocolError('firmware identity')
            self.firmware = data.decode('ascii', errors='strict')
            if any(ord(c) < 32 or ord(c) > 126 for c in self.firmware):
                raise ProtocolError('invalid firmware identity')
            self.valid = False
            self.pending = None
        elif kind == INVALID:
            if data:
                raise ProtocolError('invalid invalidation')
            self.valid = False
            self.pending = None
        elif kind == BEGIN:
            if data or self.pending is not None or self.firmware is None:
                raise ProtocolError('invalid begin')
            self.full = bool(flags & 1)
            if not self.full and (not self.valid or seq != (self.seq+1) % 16384):
                raise ProtocolError('missing frame; full recovery required')
            self.pending = bytearray(MAX_PAYLOAD) if self.full else bytearray(self.pixels)
            self.coverage = bytearray(240*240) if self.full else None
            self.pending_seq = seq
            self.rectangles = []
        elif kind == RECT:
            if self.pending is None or seq != self.pending_seq:
                raise ProtocolError('rectangle outside transaction')
            if not w or not h or x+w > 240 or y+h > 240:
                raise ProtocolError('rectangle bounds')
            if len(self.rectangles) >= 1024:
                raise ProtocolError('rectangle count limit')
            pixels = w*h
            if codec == RAW:
                if len(data) != pixels*2:
                    raise ProtocolError('raw length')
                raw = data
            elif codec == FILL:
                if len(data) != 2:
                    raise ProtocolError('fill length')
                raw = data*pixels
            elif codec == LZ4:
                raw = lz4_decode(data, pixels*2)
            else:
                if len(data) % 4:
                    raise ProtocolError('RLE alignment')
                raw = bytearray()
                for p in range(0, len(data), 4):
                    run = int.from_bytes(data[p:p+2], 'little')
                    if not run or len(raw)+run*2 > pixels*2:
                        raise ProtocolError('RLE run')
                    raw.extend(data[p+2:p+4]*run)
                if len(raw) != pixels*2:
                    raise ProtocolError('RLE length')
            for row in range(h):
                dst = (y+row)*240+x
                self.pending[dst*2:(dst+w)*2] = raw[row*w*2:(row+1)*w*2]
                if self.coverage is not None:
                    self.coverage[dst:dst+w] = b'\1'*w
            self.rectangles.append((x, y, w, h, bytes(raw)))
        elif kind == COMMIT:
            if data or self.pending is None or seq != self.pending_seq:
                raise ProtocolError('invalid commit')
            if self.coverage is not None and 0 in self.coverage:
                raise ProtocolError('incomplete full frame')
            self.pixels = bytes(self.pending)
            self.pending = None
            self.coverage = None
            self.valid = True
            self.seq = seq
            return {'seq': seq, 'full': self.full, 'rects': self.rectangles,
                    'pixels': self.pixels, 'firmware': self.firmware}
