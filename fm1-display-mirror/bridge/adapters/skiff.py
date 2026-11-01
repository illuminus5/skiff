"""Opt-in Skiff display-diagnostic adapter. Groove OS is not supported."""
import asyncio
from protocol import Decoder, ProtocolError, record, HELLO, unpack7
PREFIX = [0x7D, 0x46, 0x4C, 83]

def identify(capability):
    if capability not in (b'\x01\x70\x01\x70\x01Skiff', b'\x02\x70\x01\x70\x01Skiff', b'\x03\x70\x01\x70\x01Skiff'):
        raise ProtocolError('unsupported display adapter')
    return 'Skiff'

class MidiAdapter:
    def __init__(self, loop, name):
        import rtmidi
        self.input, self.output = rtmidi.MidiIn(), rtmidi.MidiOut()
        ins, outs = self.input.get_ports(), self.output.get_ports()
        ip = [i for i, p in enumerate(ins) if name.lower() in p.lower()]
        op = [i for i, p in enumerate(outs) if name.lower() in p.lower()]
        if len(ip) != 1 or len(op) != 1:
            raise RuntimeError('Select one matching FM-1 MIDI port with --port')
        self.queue = asyncio.Queue(maxsize=8)
        self.input.ignore_types(sysex=False, timing=True, active_sense=True)
        self.input.open_port(ip[0])
        self.output.open_port(op[0])
        def callback(event, _):
            data = event[0]
            if len(data) <= 192 and data[:5] == [0xF0]+PREFIX and data[-1:] == [0xF7]:
                loop.call_soon_threadsafe(self.enqueue, bytes(data[5:-1]))
        self.input.set_callback(callback)

    def enqueue(self, data):
        if self.queue.full():
            self.queue.get_nowait()
        self.queue.put_nowait(data)

    async def request(self, op, args=()):
        while not self.queue.empty():
            self.queue.get_nowait()
        self.output.send_message([0xF0]+PREFIX+[op]+list(args)+[0xF7])
        end = asyncio.get_running_loop().time()+1
        while True:
            result = await asyncio.wait_for(self.queue.get(), max(.001, end-asyncio.get_running_loop().time()))
            if result and result[0] == op:
                return result[1:]

    def close(self):
        self.input.cancel_callback()
        self.input.close_port()
        self.output.close_port()

async def hardware(hub, port):
    while True:
        adapter = None
        try:
            adapter = MidiAdapter(asyncio.get_running_loop(), port)
            cap = await adapter.request(0)
            identify(cap)
            if cap[0] == 3:
                raise RuntimeError("Bulk mirror firmware: restart the bridge with --bulk")
            chunk_limit = 154 if cap[0] == 2 else 96
            await adapter.request(1)
            dec = Decoder()
            dec.feed(record(HELLO, payload=b'Skiff'))
            offset, generation, total = 0, None, None
            hub.state('synchronizing', 'Waiting for a complete captured screen')
            while True:
                poll_started = asyncio.get_running_loop().time()
                r = await adapter.request(2, [offset & 127, offset >> 7])
                if len(r) < 7:
                    raise ProtocolError('short MIDI chunk')
                state, gen, length, pos = r[0], r[1] | r[2] << 7, r[3] | r[4] << 7, r[5] | r[6] << 7
                if state == 2:
                    hub.state('synchronizing', 'Capture overflow; requesting full recovery')
                    dec.reset(); dec.feed(record(HELLO, payload=b'Skiff'))
                    offset, generation, total = 0, None, None
                    await adapter.request(1)
                elif state == 1:
                    if pos != offset or not length or length > 16383 or (offset and (gen != generation or length != total)):
                        raise ProtocolError('MIDI fragment sequence')
                    generation, total = gen, length
                    chunk = unpack7(r[7:])
                    if not chunk or len(chunk) > chunk_limit or offset+len(chunk) > total:
                        raise ProtocolError('MIDI fragment length')
                    for frame in dec.feed(chunk):
                        hub.publish(frame)
                    offset += len(chunk)
                    if offset == total:
                        if dec.buffer or dec.pending is not None:
                            raise ProtocolError('truncated transaction')
                        await adapter.request(4, [gen & 127, gen >> 7])
                        offset, generation, total = 0, None, None
                elif state != 0 or offset:
                    raise ProtocolError('capture disappeared')
                # Fast profile: active chunks at <=1000 requests/s; idle polls at 250.
                # Legacy firmware retains its original request budget.
                interval = .001 if state == 1 and cap[0] == 2 else .004
                await asyncio.sleep(max(0, interval - (asyncio.get_running_loop().time() - poll_started)))
        except (RuntimeError, ImportError, OSError, ProtocolError, asyncio.TimeoutError) as error:
            hub.state('disconnected', str(error) or 'No supported mirror firmware response')
            await asyncio.sleep(2)
        finally:
            if adapter is not None:
                adapter.close()
