# A4 (complemento): cliente Modbus generico escrevendo uma leitura no servidor do subscriber,
# como faria um CLP configurado como cliente Modbus TCP (funcao 16, escrita de multiplos registradores).
import struct, time
from pymodbus.client import ModbusTcpClient
t = int(time.time())
vals = [1288.8, -2.2, 11.1, 14.4]
raw = b"caldeira_generico".ljust(32, b"\0") + struct.pack("<q", t) + struct.pack("<4h", *[round((v - i) * 10) for i, v in enumerate(vals)])
regs = list(struct.unpack("<24H", raw))
c = ModbusTcpClient("localhost", port=1502); c.connect()
r = c.write_registers(0, regs)
print(f"escrita de 24 registradores: {'ok' if not r.isError() else r} | planta=caldeira_generico instante={t} valores={vals}")
c.close()
