import asyncio
from bleak import BleakScanner

async def scan():
    print("Starting 8s BLE scan...")
    devs = await BleakScanner.discover(timeout=8.0, return_adv=True)
    print(f"Found {len(devs)} device(s):")
    for addr, (dev, adv) in devs.items():
        rssi = getattr(adv, "rssi", "?")
        print(f"  {addr}  name={dev.name}  rssi={rssi}")

asyncio.run(scan())
