import numpy as np
import struct
import socket
import threading
import time

# ===================== CONFIG =====================
CMD_PORT = 47268
POINTS   = 512
DT_MS    = 1
VAR_RAW  = "seno_raw"   # variável enviada em plotRaw
VAR_TXT  = "seno_txt"   # variável enviada em texto tradicional
UNIT     = "V"
SEND_RATE = 40          # FPS dos pacotes
# ==================================================

def build_plotraw_u16_packet(
        varName: str,
        ts0: int,
        dt_ms: int,
        y_u16: np.ndarray,
        mn: float,
        mx: float,
        unit: str = None):
    
    assert y_u16.dtype == np.uint16
    packet = bytearray()

    # Cabeçalho ASCII: <var:TS0;STEP;
    header = f"<{varName}:{ts0};{dt_ms};"
    packet.extend(header.encode("ascii"))

    # min/max
    packet.extend(struct.pack("<f", mn))
    packet.extend(struct.pack("<f", mx))

    # pontos quantizados
    packet.extend(y_u16.tobytes())

    # unidade opcional
    if unit:
        packet.extend(b"\xC2\xA7")
        packet.extend(unit.encode("ascii"))

    packet.extend(b"|g\r\n")
    return bytes(packet)


# ==================================================
def generate_u16_and_float_sine(n=256, phase=0.0, amplitude=1.0):
    t = np.linspace(0, 2*np.pi, n, endpoint=False)
    raw = amplitude * np.sin(t + phase)

    mn = raw.min()
    mx = raw.max()
    u16 = np.round((raw - mn)/(mx - mn) * 65535).astype(np.uint16)

    return u16, raw, mn, mx


# ==================================================
def get_local_ip(target="8.8.8.8"):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((target, 9))
        return s.getsockname()[0]
    except:
        return "127.0.0.1"
    finally:
        s.close()


# ==================================================
class PlotRawUDPServer:
    def __init__(self, cmd_port=CMD_PORT):
        self.cmd_port  = cmd_port
        self.server_ip = get_local_ip()

        self.cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.cmd_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.cmd_sock.bind(("0.0.0.0", cmd_port))

        self.data_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        self.target = None
        self.stop_flag = False
        self.send_thread = threading.Thread(target=self.send_loop, daemon=True)

    # ========================================================
    def send_loop(self):
        phase = 0.0
        ts0_raw = 0      # timestamp base para plotRaw

        while not self.stop_flag:
            if self.target is None:
                time.sleep(0.05)
                continue

            # ----- GERA SENO -----
            u16, raw_float, mn, mx = generate_u16_and_float_sine(
                n=POINTS, phase=phase
            )
            phase += 0.05
            ts0_raw += POINTS * DT_MS

            # ----- ENVIA PLOTRAW -----
            pkt = build_plotraw_u16_packet(
                VAR_RAW, ts0_raw, DT_MS, u16, mn, mx, UNIT
            )
            try:
                self.data_sock.sendto(pkt, self.target)
            except Exception as e:
                print("[ERRO] envio raw:", e)
                self.target = None
                continue

            # ----- ENVIA TRADICIONAL (1 ponto por frame) -----
            timestamp_ms = int(time.time() * 1000)
            value_txt = raw_float[0]   # valor da primeira amostra
            line = f">{VAR_TXT}:{timestamp_ms}:{value_txt:.6f}|g\n"

            try:
                self.data_sock.sendto(line.encode("utf-8"), self.target)
            except Exception as e:
                print("[ERRO] envio texto:", e)
                self.target = None

            time.sleep(1.0 / SEND_RATE)

    # ========================================================
    def run(self):
        print(f"[SERVIDOR] Aguardando CONNECT em 0.0.0.0:{self.cmd_port}")
        self.send_thread.start()

        while True:
            data, addr = self.cmd_sock.recvfrom(4096)
            msg = data.decode("utf-8", "ignore").strip()
            print(f"[CMD] De {addr}: {msg}")

            # ------------------- CONNECT -------------------
            if msg.startswith("CONNECT:"):
                parts = msg.split(":")
                if len(parts) != 3:
                    print("[ERRO] CONNECT inválido")
                    continue

                client_ip   = parts[1].strip()
                client_port = int(parts[2])

                self.target = (client_ip, client_port)

                resp = f"CONNECTED:{self.server_ip}:{self.cmd_port}"
                self.data_sock.sendto(resp.encode("utf-8"), self.target)
                print(f"[CMD] >> {resp}")
                continue

            # ------------------- DISCONNECT -------------------
            if msg.startswith("DISCONNECT"):
                if self.target:
                    disc = f"DISCONNECTED:{self.server_ip}:{self.cmd_port}"
                    self.data_sock.sendto(disc.encode("utf-8"), self.target)
                    print(f"[CMD] >> {disc}")

                self.target = None
                print("[CMD] Desconectado; aguardando novo CONNECT...")
                continue


# ==================================================
if __name__ == "__main__":
    srv = PlotRawUDPServer()
    srv.run()