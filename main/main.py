import sys
import glob
import serial
import pyautogui
import tkinter as tk
from tkinter import ttk, messagebox
from time import sleep

pyautogui.PAUSE = 0

def move_screen(axis, value):
    if axis == 0:
        pyautogui.moveRel(value, 0)
        print(f"Movendo tela horizontalmente: {value}")
    elif axis == 1:
        pyautogui.moveRel(0, value)
        print(f"Movendo tela verticalmente: {value}")

def move_player(axis, value):
    if axis == 2:
        if value > 0:
            pyautogui.press('right')
            print("Jogador: direita")
        elif value < 0:
            pyautogui.press('left')
            print("Jogador: esquerda")
    elif axis == 3:
        if value > 0:
            pyautogui.press('down')
            print("Jogador: para baixo")
        elif value < 0:
            pyautogui.press('up')
            print("Jogador: para cima")

def process_analog(axis, value):
    if axis in (0, 1):
        move_screen(axis, value)
    elif axis in (2, 3):
        move_player(axis, value)
    else:
        print("Evento analógico: eixo desconhecido", axis, "Valor:", value)

def process_button(command, state):
    if state == 1:
        if command == 32:
            pyautogui.press('space')
            print("Botão SPACE: Atirar")
        elif command == 82:
            pyautogui.press('r')
            print("Botão R: Recarregar")
        elif command == 1:
            pyautogui.click(button='right')
            print("Botão código 1: Clique direito")
        elif command == 2:
            pyautogui.keyDown('shift')
            pyautogui.keyUp('shift')
            print("Botão código 2: Aim")
        elif command == 69:
            pyautogui.press('e')
            print("Botão E acionado")
        else:
            print("Botão com código desconhecido:", command)
    else:
        # Se desejar tratar liberação, implemente aqui.
        print("Botão liberado:", command)

def handle_joystick_packet(packet):
    if packet[3] != 0xFF:
        print("Pacote do joystick com footer inválido.")
        return
    axis = packet[0]
    msb = packet[1]
    lsb = packet[2]
    value = (msb << 8) | lsb
    if value & 0x8000:
        value -= 0x10000
    process_analog(axis, value)

def handle_button_packet(packet):
    if packet[2] != 0xFE:
        print("Pacote de botão com footer inválido.")
        return
    codigo = packet[0]
    estado = packet[1]
    process_button(codigo, estado)

def serial_ports():
    ports = []
    if sys.platform.startswith('win'):
        for i in range(1, 256):
            port = f'COM{i}'
            try:
                s = serial.Serial(port)
                s.close()
                ports.append(port)
            except (OSError, serial.SerialException):
                pass
    elif sys.platform.startswith('linux') or sys.platform.startswith('cygwin'):
        ports = glob.glob('/dev/tty[A-Za-z]*')
    elif sys.platform.startswith('darwin'):
        ports = glob.glob('/dev/tty.*')
    else:
        raise EnvironmentError('Plataforma não suportada.')
    
    result = []
    for port in ports:
        try:
            s = serial.Serial(port)
            s.close()
            result.append(port)
        except:
            pass
    return result

def main():
    available_ports = serial_ports()
    if not available_ports:
        print("Nenhuma porta serial encontrada. Verifique a conexão.")
        sys.exit(1)
    print("Portas disponíveis:", available_ports)
    port = "/dev/ttyACM0"

    try:
        ser = serial.Serial(port, 115200, timeout=1)
    except Exception as e:
        print("Erro ao abrir a porta serial:", e)
        sys.exit(1)

    print("Conectado à porta", port)

    while True:
        header = ser.read(size=1)
        if not header:
            continue
        if header[0] == 0xA0:
            # Evento analógico: ler 4 bytes adicionais
            packet = ser.read(size=4)
            if len(packet) < 4:
                continue
            handle_joystick_packet(packet)
        elif header[0] == 0xB0:
            # Evento de botão: ler 3 bytes adicionais
            packet = ser.read(size=3)
            if len(packet) < 3:
                continue
            handle_button_packet(packet)
        else:
            print("Header desconhecido:", header[0])
            continue

if __name__ == "__main__":
    main()
