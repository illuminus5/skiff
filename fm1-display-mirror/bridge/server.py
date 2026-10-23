#!/usr/bin/env python3
"""Local, read-only FM-1 mirror server. Standard library + optional python-rtmidi."""
import argparse
import asyncio
import base64
import hashlib
import hmac
import json
import secrets
import struct
import time
from collections import deque
from pathlib import Path
from urllib.parse import urlsplit, parse_qs
from protocol import Decoder, ProtocolError, record, HELLO, BEGIN, RECT, COMMIT, FILL, RAW, unpack7

WEB = Path(__file__).resolve().parents[1] / 'web'
from adapters.skiff import hardware
MAX_CLIENTS = 4

def ws_frame(data, opcode=2):
    n = len(data)
    head = bytes([0x80 | opcode])
    return head + (bytes([n]) if n < 126 else b'\x7e'+struct.pack('>H', n) if n < 65536 else b'\x7f'+struct.pack('>Q', n)) + data

def full_transaction(seq, pixels):
    return record(BEGIN, seq, flags=1) + record(RECT, seq, pixels, w=240, h=240) + record(COMMIT, seq)

class Hub:
    def __init__(self):
        self.clients = set()
        self.latest = None
        self.status = {'state': 'disconnected', 'firmware': None, 'fps': 0, 'detail': 'Waiting for device'}
        self.transport_detail = None
        self.last_frame = 0
        self.frame_times = deque(maxlen=120)

    def publish(self, frame):
        now = time.monotonic()
        self.frame_times.append(now)
        while len(self.frame_times)>1 and now-self.frame_times[0]>2:
            self.frame_times.popleft()
        count=len(self.frame_times)
        elapsed=now-self.frame_times[0]
        gaps=[(b-a)*1000 for a,b in zip(self.frame_times,list(self.frame_times)[1:])]
        self.status.update(state='connected', firmware=frame['firmware'],
                           fps=round((count-1)/elapsed,1) if elapsed>0 else 0,
                           gap_ms=round(max(gaps),1) if gaps else 0,
                           detail=self.transport_detail or 'Rendered pixels received')
        self.last_frame = now
        self.latest = full_transaction(frame['seq'], frame['pixels'])
        self.broadcast()

    def state(self, state, detail):
        self.status.update(state=state, detail=detail, fps=0)
        if state != 'connected':
            self.latest = None
            self.transport_detail = None
            self.frame_times.clear()
            self.status['gap_ms']=0
        self.broadcast()

    def broadcast(self):
        # One replaceable snapshot per client: dropped WS updates never break a delta chain.
        for q in self.clients:
            if q.full():
                q.get_nowait()
            q.put_nowait((dict(self.status), self.latest))

class Server:
    def __init__(self, hub, port, token):
        self.hub, self.port, self.token = hub, port, token

    async def handle(self, reader, writer):
        q = None
        try:
            request = await asyncio.wait_for(reader.readuntil(b'\r\n\r\n'), 5)
            if len(request) > 8192:
                return
            lines = request.decode('ascii').split('\r\n')
            method, target, version = lines[0].split(' ')
            headers = {}
            for line in lines[1:]:
                if line:
                    key, value = line.split(':', 1)
                    key = key.lower()
                    if key in headers:
                        return
                    headers[key] = value.strip()
            if method != 'GET' or version != 'HTTP/1.1' or headers.get('host') != f'127.0.0.1:{self.port}':
                return
            url = urlsplit(target)
            authenticated = hmac.compare_digest(parse_qs(url.query).get('token', [''])[0], self.token)
            if url.path == '/ws':
                key = headers.get('sec-websocket-key', '')
                try:
                    valid_key = len(base64.b64decode(key, validate=True)) == 16
                except ValueError:
                    valid_key = False
                if (not authenticated or headers.get('origin') != f'http://127.0.0.1:{self.port}'
                    or headers.get('upgrade', '').lower() != 'websocket'
                    or 'upgrade' not in headers.get('connection', '').lower().split(', ')
                    or headers.get('sec-websocket-version') != '13' or not valid_key
                    or len(self.hub.clients) >= MAX_CLIENTS):
                    return
                accept = base64.b64encode(hashlib.sha1((key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
                writer.write(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: '+accept+b'\r\n\r\n')
                await writer.drain()
                q = asyncio.Queue(maxsize=1)
                self.hub.clients.add(q)
                q.put_nowait((dict(self.hub.status), self.hub.latest))
                sender = asyncio.create_task(self.send(q, writer))
                receiver = asyncio.create_task(self.receive(reader, writer))
                done, pending = await asyncio.wait([sender, receiver], return_when=asyncio.FIRST_COMPLETED)
                for task in pending:
                    task.cancel()
                await asyncio.gather(sender, receiver, return_exceptions=True)
                return
            if url.path == '/' and authenticated:
                data, mime = (WEB/'index.html').read_bytes(), 'text/html; charset=utf-8'
            elif url.path in ('/viewer.js', '/style.css'):
                data = (WEB/url.path[1:]).read_bytes()
                mime = 'text/javascript' if url.path.endswith('.js') else 'text/css'
            else:
                writer.write(b'HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
                await writer.drain()
                return
            writer.write(('HTTP/1.1 200 OK\r\nContent-Type: '+mime+'\r\nContent-Length: '+str(len(data))+
                          "\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'\r\nConnection: close\r\n\r\n").encode()+data)
            await writer.drain()
        except (ValueError, UnicodeError, asyncio.TimeoutError, asyncio.IncompleteReadError,
                asyncio.LimitOverrunError, ConnectionError, OSError):
            pass
        finally:
            if q is not None:
                self.hub.clients.discard(q)
            writer.close()
            try:
                await writer.wait_closed()
            except (ConnectionError, OSError):
                pass

    async def send(self, q, writer):
        last = None
        while True:
            status, pixels = await q.get()
            writer.write(ws_frame(json.dumps(status).encode(), 1))
            if pixels is not None and pixels is not last:
                writer.write(ws_frame(pixels))
                last = pixels
            await asyncio.wait_for(writer.drain(), 5)

    async def receive(self, reader, writer):
        # No application messages are accepted. Only RFC6455 close/ping/pong.
        while True:
            a, b = await reader.readexactly(2)
            n = b & 127
            if a & 0x70 or not a & 0x80 or not b & 0x80 or n > 125:
                return
            mask = await reader.readexactly(4)
            payload = await reader.readexactly(n)
            payload = bytes(v ^ mask[i % 4] for i, v in enumerate(payload))
            opcode = a & 15
            if opcode == 8:
                return
            if opcode == 9:
                writer.write(ws_frame(payload, 10))
                await writer.drain()
            elif opcode != 10:
                return

async def mock(hub):
    dec = Decoder()
    dec.feed(record(HELLO, payload=b'Mock rendered-pixel source'))
    seq = 0
    while True:
        # Deliberately a calibration pattern, not a simulated Skiff interface.
        data = record(BEGIN, seq, flags=1)
        data += record(RECT, seq, b'\x08\x41', codec=FILL, w=240, h=240)
        for x, color in [(0, b'\xf8\x00'), (80, b'\x07\xe0'), (160, b'\x00\x1f')]:
            data += record(RECT, seq, color, codec=FILL, x=x, y=40, w=80, h=120)
        data += record(RECT, seq, b'\xff\xff', codec=FILL, x=seq % 232, y=190, w=8, h=8)
        data += record(COMMIT, seq)
        for p in range(0, len(data), 17):
            for frame in dec.feed(data[p:p+17]):
                hub.publish(frame)
        seq = (seq+1) % 16384
        await asyncio.sleep(.1)

async def main(args):
    hub = Hub()
    token = secrets.token_urlsafe(32)
    service = Server(hub, args.listen_port, token)
    server = await asyncio.start_server(service.handle, '127.0.0.1', args.listen_port, limit=8192)
    print(f'Open in Safari: http://127.0.0.1:{args.listen_port}/?token={token}', flush=True)
    if args.bulk:
        from adapters.skiff_bulk import hardware as bulk_hardware
    producer = asyncio.create_task(mock(hub) if args.mock else bulk_hardware(hub, args.serial, args.diagnose, args.port) if args.bulk else hardware(hub, args.port))
    try:
        async with server:
            await server.serve_forever()
    finally:
        producer.cancel()
        await asyncio.gather(producer, return_exceptions=True)

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--mock', action='store_true', help='Calibration pixels; no hardware connection')
    ap.add_argument('--port', default='Felucca', help='Unique MIDI port name substring')
    ap.add_argument('--bulk', action='store_true', help='Experimental dedicated CDC pixel stream')
    ap.add_argument('--diagnose', action='store_true', help='Print bulk throughput and decode timing once per second')
    ap.add_argument('--serial', default='auto', help='CDC device path; auto selects a unique FM-1 VID/PID')
    ap.add_argument('--listen-port', type=int, default=8787)
    args = ap.parse_args()
    if not 1024 <= args.listen_port <= 65535:
        ap.error('listen port must be 1024..65535')
    try:
        asyncio.run(main(args))
    except KeyboardInterrupt:
        pass
