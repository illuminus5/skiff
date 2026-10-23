import asyncio
from types import SimpleNamespace
import unittest
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"bridge"))
from adapters.skiff_bulk import receive, select_port, device_timing
from unittest.mock import patch
from protocol import record, HELLO, BEGIN, RECT, COMMIT, FILL

class BulkTests(unittest.IsolatedAsyncioTestCase):
    def test_port_selection(self):
        p=SimpleNamespace(device='/dev/test',vid=0x1209,pid=1)
        self.assertEqual(select_port([p], 'auto'),'/dev/test')
        for ports in ([],[p,p]):
            with self.assertRaises(RuntimeError):select_port(ports,'auto')
        self.assertEqual(select_port([], '/dev/explicit'),'/dev/explicit')

    async def test_fragmented_stream_and_stale_prefix(self):
        await self.check_stream(b'Skiff')

    async def test_stream1_signature(self):
        await self.check_stream(b'Skiff stream1')

    async def test_stream2_signature(self):
        await self.check_stream(b'Skiff stream2')

    async def test_stream3_signature(self):
        await self.check_stream(b'Skiff stream3')

    async def test_stream4_signature(self):
        await self.check_stream(b'Skiff stream4')

    async def test_device_timing_read_only(self):
        done=asyncio.Event();closed=[]
        class Fake:
            def __init__(self,*args):pass
            async def request(self,op):
                self_op=op
                assert self_op==5
                done.set()
                return bytes([1,5,0,1,0,23,0,6,0,1500&127,1500>>7,10,0])
            def close(self):closed.append(True)
        hub=SimpleNamespace()
        with patch('adapters.skiff.MidiAdapter',Fake):
            task=asyncio.create_task(device_timing(hub,'test'))
            try:
                await asyncio.wait_for(done.wait(),1)
                self.assertIn('capture 5 ms / queue 1 ms / USB 23 ms / ACK 6 ms / 1500 B',hub.device_timing)
            finally:task.cancel();await asyncio.gather(task,return_exceptions=True)
        self.assertEqual(closed,[True])

    async def check_stream(self,identity):
        wire=b'old partial transaction'+record(HELLO,payload=identity)+record(BEGIN,2,flags=1)+record(RECT,2,b'\xf8\0',codec=FILL,w=240,h=240)+record(COMMIT,2)
        class Device:
            def __init__(self):self.writes=[];self.data=wire
            def write(self,b):self.writes.append(b)
            def read(self,n):b=self.data[:7];self.data=self.data[7:];return b
        class Hub:
            def state(self,*args):pass
            def publish(self,f):self.frame=f;done.set()
        device=Device();hub=Hub();done=asyncio.Event()
        task=asyncio.create_task(receive(hub,device))
        try:await asyncio.wait_for(done.wait(),1)
        finally:task.cancel();await asyncio.gather(task,return_exceptions=True)
        self.assertEqual(device.writes[0],b'FMIR\0FMIR\1')
        self.assertEqual(hub.frame['pixels'],b'\xf8\0'*57600)
