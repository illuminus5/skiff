"""Read-only dedicated Skiff CDC pixel stream. No serial console commands."""
import asyncio
from protocol import Decoder, ProtocolError, record, HELLO


def select_port(ports, requested):
    if requested != 'auto':
        return requested
    matches = [p.device for p in ports if p.vid == 0x1209 and p.pid == 1]
    if len(matches) != 1:
        raise RuntimeError('Expected one FM-1 serial port; use --serial /dev/cu.…')
    return matches[0]


async def receive(hub, device, diagnose=False):
    dec = Decoder()
    loop = asyncio.get_running_loop()
    started = heartbeat = loop.time()
    device.write(b'FMIR\x00FMIR\x01')
    preamble = bytearray()
    signatures = [record(HELLO, payload=name) for name in (b'Skiff',b'Skiff stream1',b'Skiff stream2',b'Skiff stream3',b'Skiff stream4')]
    synchronized = False
    window = loop.time()
    byte_count = frame_count = full_count = read_count = 0
    decode_time = 0.0
    hub.state('synchronizing', 'Bulk USB: waiting for actual Skiff pixels')
    while True:
        now = loop.time()
        if now-heartbeat >= 1:
            device.write(b'FMIR\x02')
            heartbeat = now
        data = device.read(4096)
        if data:
            byte_count += len(data);read_count += 1
            if not synchronized:
                preamble.extend(data)
                positions = [preamble.find(s) for s in signatures]
                start = min((p for p in positions if p>=0),default=-1)
                if start < 0:
                    if len(preamble) > 32768:
                        raise ProtocolError('No bounded bulk mirror preamble')
                    if now-started > 3:
                        raise ProtocolError('No bulk mirror signature')
                    await asyncio.sleep(.001)
                    continue
                data = bytes(preamble[start:])
                preamble.clear()
                synchronized = True
            before_decode = loop.time()
            frames = dec.feed(data)
            decode_time += loop.time()-before_decode
            frame_count += len(frames)
            full_count += sum(bool(frame['full']) for frame in frames)
            for frame in frames:
                if frame['firmware'] not in ('Skiff','Skiff stream1','Skiff stream2','Skiff stream3','Skiff stream4'):
                    raise ProtocolError('Unsupported bulk mirror firmware')
                hub.publish(frame)
            if dec.firmware is not None and dec.firmware not in ('Skiff','Skiff stream1','Skiff stream2','Skiff stream3','Skiff stream4'):
                raise ProtocolError('Unsupported bulk mirror firmware')
        if dec.firmware is None and now-started > 3:
            raise ProtocolError('No bulk mirror signature; install the mirrorbulk profile')
        elapsed = loop.time()-window
        if diagnose and elapsed >= 1:
            detail = (f'USB {byte_count/elapsed/1000:.1f} KB/s | '
                      f'{frame_count/elapsed:.1f} frames/s | {full_count} full / {frame_count-full_count} delta | '
                      f'{read_count/elapsed:.0f} reads/s | '
                      f'{byte_count/max(1,read_count):.0f} bytes/read | '
                      f'decode {decode_time*1000/max(1,frame_count):.1f} ms/frame | '
                      +getattr(hub,'device_timing','device timing pending'))
            print('bulk: '+detail, flush=True)
            hub.transport_detail = detail
            window = loop.time()
            byte_count = frame_count = full_count = read_count = 0
            decode_time = 0.0
        # Drain already available data without an extra timer delay, while yielding
        # to the WebSocket sender. Sleep only when the serial queue is empty.
        await asyncio.sleep(0 if data else .001)


async def device_timing(hub, port):
    from .skiff import MidiAdapter
    adapter = None
    try:
        adapter = MidiAdapter(asyncio.get_running_loop(),port)
        while True:
            r = await adapter.request(5)
            if len(r)!=13 or r[0]!=1:
                raise ProtocolError('Device timing unsupported')
            capture,queue,wire,ack,size,seq = [r[i]|r[i+1]<<7 for i in range(1,13,2)]
            hub.device_timing = f'device last: capture {capture} ms / queue {queue} ms / USB {wire} ms / ACK {ack} ms / {size} B'
            await asyncio.sleep(1)
    except (RuntimeError,ImportError,OSError,ProtocolError,asyncio.TimeoutError) as error:
        hub.device_timing = 'device timing unavailable: '+(str(error) or 'no response')
    finally:
        if adapter is not None:
            adapter.close()


async def hardware(hub, port, diagnose=False, midi_port='Felucca'):
    while True:
        device = None
        timing = None
        try:
            import serial
            from serial.tools import list_ports
            path = select_port(list_ports.comports(), port)
            # Zero read timeout: the event loop and viewer are never blocked.
            device = serial.Serial(path, 115200, timeout=0, write_timeout=.1, exclusive=True)
            device.reset_input_buffer()
            if diagnose:
                timing=asyncio.create_task(device_timing(hub,midi_port))
            await receive(hub, device, diagnose)
        except (ImportError, RuntimeError, OSError, ProtocolError) as error:
            hub.state('disconnected', str(error))
            await asyncio.sleep(2)
        finally:
            if timing is not None:
                timing.cancel()
                await asyncio.gather(timing,return_exceptions=True)
            if device is not None:
                try:
                    device.write(b'FMIR\x00')
                except OSError:
                    pass
                device.close()
