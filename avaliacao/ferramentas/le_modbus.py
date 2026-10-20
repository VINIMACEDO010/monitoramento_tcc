# A4: cliente Modbus generico (pymodbus), que nao faz parte do prototipo.
# Le os 24 registradores (48 bytes, estrutura Payload) do servidor Modbus do subscriber
# e decodifica planta, instante e as quatro variaveis.
import struct, sys, time
from pymodbus.client import ModbusTcpClient
c = ModbusTcpClient("localhost", port=1502); c.connect()
for k in range(int(sys.argv[1]) if len(sys.argv) > 1 else 3):
    r = c.read_holding_registers(0, count=24)
    raw = b"".join(struct.pack("<H", x) for x in r.registers)   # mesma ordem de bytes em que o publicador copia a estrutura
    nome = raw[:32].split(b"\0")[0].decode()
    t, = struct.unpack("<q", raw[32:40])
    d = struct.unpack("<4h", raw[40:48])
    vals = [d[i] / 10.0 + i for i in range(4)]   # desfaz a codificacao (valor - indice) x 10
    print(f"{time.strftime('%H:%M:%S')} registradores lidos: planta={nome} instante={t} temp={vals[0]:.1f} pfor={vals[1]:.1f} vazao={vals[2]:.1f} pvap={vals[3]:.1f}")
    time.sleep(5)
c.close()
