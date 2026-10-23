import asyncio
import base64
import os
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'bridge'))
from protocol import *
from server import Hub, Server, full_transaction, parse_args
from adapters.skiff import identify, hardware

class CliTests(unittest.TestCase):
    def test_default_bulk_without_diagnostics(self):
        args = parse_args([])
        self.assertTrue(args.bulk)
        self.assertFalse(args.diagnose)
        self.assertFalse(Hub().status["diagnostics"])
        self.assertTrue(Hub(diagnose=True).status["diagnostics"])

    def test_explicit_transport_and_diagnostics(self):
        self.assertTrue(parse_args(['--bulk']).bulk)
        self.assertFalse(parse_args(['--midi']).bulk)
        self.assertTrue(parse_args(['--diagnose']).diagnose)
        args = parse_args(['--mock'])
        self.assertTrue(args.mock)

class ProtocolTests(unittest.TestCase):
    def decoder(self):
        d=Decoder();d.feed(record(HELLO,payload=b'Skiff'));return d
    def full(self, seq=0):
        return record(BEGIN,seq,flags=1)+record(RECT,seq,b'\xf8\0',codec=FILL,w=240,h=240)+record(COMMIT,seq)
    def test_fragmentation_and_full(self):
        for size in (1,7,28,96,4096):
            d=self.decoder();frames=[];wire=self.full()
            for p in range(0,len(wire),size):frames+=d.feed(wire[p:p+size])
            self.assertEqual(len(frames),1);self.assertEqual(d.pixels,b'\xf8\0'*57600)
    def test_delta_raw_and_rle(self):
        d=self.decoder();d.feed(self.full())
        payload=record(BEGIN,1)+record(RECT,1,b'\x07\xe0\x00\x1f',x=5,y=6,w=2,h=1)+record(RECT,1,b'\x02\0\xff\xff',codec=RLE,x=8,y=9,w=2,h=1)+record(COMMIT,1)
        f=d.feed(payload)[0];self.assertFalse(f['full']);self.assertEqual(d.pixels[(6*240+5)*2:(6*240+7)*2],b'\x07\xe0\x00\x1f')
    def test_gap_invalidation_and_recovery(self):
        d=self.decoder();d.feed(self.full())
        with self.assertRaises(ProtocolError):d.feed(record(BEGIN,3))
        self.assertFalse(d.valid);d.feed(record(HELLO,payload=b'Skiff'));self.assertEqual(len(d.feed(self.full(8))),1)
        d.feed(record(INVALID));self.assertFalse(d.valid)
    def test_malformed(self):
        malformed=[record(RECT,w=241,h=1,payload=b'\0\0'),record(BEGIN,flags=2),record(BEGIN,width=65535),b'NOPE'+bytes(24)]
        for wire in malformed:
            d=self.decoder()
            with self.assertRaises(ProtocolError):d.feed(wire)
            self.assertFalse(d.valid)
        d=self.decoder()
        with self.assertRaises(ProtocolError):d.feed(record(BEGIN,flags=1)+record(RECT,payload=b'\0\0\xff\xff',codec=RLE,w=1,h=1))
    def test_no_partial_publication(self):
        d=self.decoder();self.assertEqual(d.feed(record(BEGIN,flags=1)+record(RECT,payload=b'\xff\xff',codec=FILL,w=1,h=1)),[])
        with self.assertRaises(ProtocolError):d.feed(record(COMMIT))
        self.assertFalse(d.valid)
    def test_identity_and_reconnect(self):
        d=Decoder()
        with self.assertRaises(ProtocolError):d.feed(record(HELLO,payload=b'\0unsupported'))
        d.feed(record(HELLO,payload=b'Skiff'));d.feed(self.full());d.reset();self.assertFalse(d.valid);self.assertIsNone(d.firmware)
    def test_seven_bit(self):
        for n in range(1,200):
            raw=bytes(i%256 for i in range(n));self.assertEqual(unpack7(pack7(raw)),raw)
        for raw in (b'\x01',b'\x02\x01',b'\0\xff'):
            with self.assertRaises(ProtocolError):unpack7(raw)
    def test_sequence_wrap(self):
        d=self.decoder();d.feed(self.full(16383));self.assertEqual(len(d.feed(record(BEGIN,0)+record(COMMIT,0))),1)
    def test_hub_latest_only(self):
        h=Hub();q=asyncio.Queue(maxsize=1);h.clients.add(q)
        for i in range(100):h.state('disconnected',str(i))
        self.assertEqual(q.qsize(),1);self.assertEqual(q.get_nowait()[0]['detail'],'99')

    def test_source_cadence_window(self):
        hub=Hub();frame={'firmware':'Skiff','seq':0,'pixels':bytes(115200)}
        with patch('server.time.monotonic',side_effect=[1,1.02,1.06]):
            for _ in range(3):hub.publish(frame)
        self.assertEqual(hub.status['fps'],33.3)
        self.assertEqual(hub.status['gap_ms'],40)
        hub.state('disconnected','test')
        self.assertEqual(hub.status['fps'],0)
        self.assertEqual(hub.status['gap_ms'],0)

    def test_adapter_identification(self):
        self.assertEqual(identify(b'\x01\x70\x01\x70\x01Skiff'),'Skiff')
        self.assertEqual(identify(b'\x02\x70\x01\x70\x01Skiff'),'Skiff')
        for cap in (b'', b'Groove OS', b'\x04\x70\x01\x70\x01Skiff'):
            with self.assertRaises(ProtocolError):identify(cap)
    def test_segmented_actual_overlays(self):
        root=Path(__file__).resolve().parents[2]/'build/host'
        for name in ('held-home','held-glo','lfo-initial','lfo-running'):
            wire=(root/f'display-mirror-{name}.bin').read_bytes()
            expected=(root/f'display-mirror-{name}-panel.bin').read_bytes()
            dec=self.decoder();frames=[]
            for at in range(0,len(wire),137):frames+=dec.feed(wire[at:at+137])
            self.assertEqual(len(frames),1,name)
            self.assertEqual(frames[0]['pixels'],expected,name)
        self.assertNotEqual((root/'display-mirror-lfo-initial-panel.bin').read_bytes(),(root/'display-mirror-lfo-running-panel.bin').read_bytes())

    def test_lfo_graph_only_after_busy_transfer(self):
        root=Path(__file__).resolve().parents[2]/'build/host'
        for name in ('lfo','lfo2','lfodest'):
            dec=self.decoder();frames=dec.feed((root/f'display-mirror-{name}-stream.bin').read_bytes())
            self.assertEqual(len(frames),3,name)
            self.assertTrue(frames[0]['full'])
            self.assertFalse(frames[1]['full']);self.assertFalse(frames[2]['full'])
            self.assertEqual(frames[-1]['pixels'],(root/f'display-mirror-{name}-stream-panel.bin').read_bytes(),name)

    def test_firmware_fixture(self):
        p=Path(__file__).resolve().parents[2]/'build/host/display-mirror-capture.bin'
        if not p.exists():self.skipTest('run C fixture test first')
        d=self.decoder();f=d.feed(p.read_bytes());self.assertEqual(len(f),1)
        self.assertEqual(d.pixels[:8],b'\xf8\0\x07\xe0\x00\x1f\xff\xff')
        self.assertEqual(d.pixels[240*10*2:240*20*2], bytes(i%32 for i in range(4800)))

    def test_lz4_bounds(self):
        self.assertEqual(lz4_decode(b'\x40test', 4), b'test')
        for block in (b'\x00\0\0', b'\xf0', b'\x10x\x02\0'):
            with self.assertRaises(ProtocolError):lz4_decode(block, 20)

    def test_actual_ui_render_fixture(self):
        root=Path(__file__).resolve().parents[2]/'build/host'
        if not (root/'display-mirror-ui.bin').exists():self.skipTest('run host-only UI sizing test first')
        d=self.decoder();frames=d.feed((root/'display-mirror-ui.bin').read_bytes())
        self.assertEqual(len(frames),1)
        self.assertEqual(d.pixels,(root/'display-mirror-ui-panel.bin').read_bytes())

class ServerTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.hub=Hub();self.service=Server(self.hub,0,'secret')
        self.server=await asyncio.start_server(self.service.handle,'127.0.0.1',0,limit=8192)
        self.port=self.server.sockets[0].getsockname()[1];self.service.port=self.port
    async def asyncTearDown(self):
        self.server.close();await self.server.wait_closed()
    async def connect(self,path='/ws?token=secret',origin=None,host=None):
        r,w=await asyncio.open_connection('127.0.0.1',self.port)
        origin=origin or f'http://127.0.0.1:{self.port}';host=host or f'127.0.0.1:{self.port}'
        w.write((f'GET {path} HTTP/1.1\r\nHost: {host}\r\nOrigin: {origin}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: '+base64.b64encode(bytes(16)).decode()+'\r\n\r\n').encode());await w.drain();return r,w
    async def test_origin_token_and_host(self):
        for kwargs in ({'origin':'https://evil.example'},{'path':'/ws?token=bad'},{'host':'evil.example'}):
            r,w=await self.connect(**kwargs);self.assertEqual(await asyncio.wait_for(r.read(),1),b'');w.close();await w.wait_closed()
    async def test_no_browser_control(self):
        r,w=await self.connect();head=await r.readuntil(b'\r\n\r\n');self.assertIn(b'101 Switching',head)
        # Masked application text is rejected, never forwarded to MIDI.
        w.write(b'\x81\x82'+b'\0'*4+b'{}');await w.drain()
        await asyncio.wait_for(r.read(),1);await asyncio.sleep(0)
        self.assertEqual(len(self.hub.clients),0);w.close();await w.wait_closed()
    async def test_private_http(self):
        r,w=await self.connect(path='/');self.assertIn(b'403 Forbidden',await r.read());w.close();await w.wait_closed()

class AdapterTests(unittest.IsolatedAsyncioTestCase):
    async def test_midi_fragment_reassembly(self):
        await self.check_midi_fragments(1, 96)

    async def test_fast_midi_fragment_reassembly(self):
        await self.check_midi_fragments(2, 154)

    async def check_midi_fragments(self, version, limit):
        wire=record(BEGIN,0,flags=1)+record(RECT,0,b"\xf8\0",codec=FILL,w=240,h=240)+record(RECT,0,b"\xf8\0"*480,w=240,h=2)+record(COMMIT,0)
        done=asyncio.Event();hub=Hub();published=[]
        def publish(frame):published.append(frame);done.set()
        hub.publish=publish
        class Fake:
            def __init__(self,*args):pass
            async def request(self,op,args=()):
                if op==0:return bytes([version])+b'\x70\x01\x70\x01Skiff'
                if op==2:
                    off=args[0]|args[1]<<7;n=len(wire)
                    return bytes([1,0,0,n&127,n>>7,off&127,off>>7])+pack7(wire[off:off+limit])
                return b''
            def close(self):pass
        with patch('adapters.skiff.MidiAdapter',Fake):
            task=asyncio.create_task(hardware(hub,'test'))
            try:await asyncio.wait_for(done.wait(),8)
            finally:task.cancel();await asyncio.gather(task,return_exceptions=True)
        self.assertEqual(len(published),1);self.assertEqual(published[0]['pixels'],b'\xf8\0'*57600)

if __name__=='__main__':unittest.main()
