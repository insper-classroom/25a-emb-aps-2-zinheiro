import sys
import glob
import serial
import pyautogui
from time import sleep, time

pyautogui.PAUSE = 0

vertical_state = None       
horizontal_state = None    
last_vertical_time = 0
last_horizontal_time = 0
TIMEOUT = 0.3  

def move_screen(axis, value):
    if axis == 0:
        pyautogui.moveRel(value, 0)
    else:
        pyautogui.moveRel(0, value)

def move_player(axis, value):
    if axis == 2:
        key = 'right' if value > 0 else 'left'
    else:
        key = 'down' if value > 0 else 'up'
    pyautogui.press(key)

def process_analog(axis, value):
    if axis in (0,1):
        move_screen(axis, value)
    else:
        move_player(axis, value)

def process_button(code, state):
    if state != 1:
        return
    if code == 32:      pyautogui.press('space')
    elif code == 82:    pyautogui.press('r')
    elif code == 1:     pyautogui.click(button='right')
    elif code == 2:     pyautogui.hotkey('shift')
    elif code == 69:    pyautogui.press('e')

def handle_joystick_packet(pkt):
    axis, msb, lsb, footer = pkt
    if footer != 0xFF: return
    val = (msb<<8)|lsb
    if val & 0x8000: val -= 0x10000
    process_analog(axis, val)

def handle_button_packet(pkt):
    code, state, footer = pkt
    if footer != 0xFE: return
    process_button(code, state)

def handle_direcional_packet(pkt):
    global vertical_state, horizontal_state, last_vertical_time, last_horizontal_time
    dir_axis, cmd, footer = pkt
    if footer != 0xCF: return
    now = time()
    if dir_axis == 0:
        last_horizontal_time = now
        target = 'a' if cmd==65 else ('d' if cmd==68 else None)
        if target and horizontal_state != target:
            if horizontal_state: pyautogui.keyUp(horizontal_state)
            pyautogui.keyDown(target)
            horizontal_state = target
    else:
        last_vertical_time = now
        target = 'w' if cmd==87 else ('s' if cmd==83 else None)
        if target and vertical_state != target:
            if vertical_state: pyautogui.keyUp(vertical_state)
            pyautogui.keyDown(target)
            vertical_state = target

def check_timeouts():
    global vertical_state, horizontal_state
    now = time()
    if vertical_state and now - last_vertical_time > TIMEOUT:
        pyautogui.keyUp(vertical_state)
        vertical_state = None
    if horizontal_state and now - last_horizontal_time > TIMEOUT:
        pyautogui.keyUp(horizontal_state)
        horizontal_state = None

def serial_ports():
    ports = []
    if sys.platform.startswith('win'):
        rng = [f'COM{i}' for i in range(1,256)]
    elif sys.platform.startswith(('linux','cygwin')):
        import glob
        rng = glob.glob('/dev/tty[A-Za-z]*')
    elif sys.platform.startswith('darwin'):
        import glob
        rng = glob.glob('/dev/tty.*')
    else:
        return []
    for p in rng:
        try:
            s = serial.Serial(p); s.close(); ports.append(p)
        except: pass
    return ports

def main():
    ports = serial_ports()
    if not ports:
        print("Nenhuma porta serial encontrada.")
        sys.exit(1)
    port = ports[0]
    print("Usando porta:", port)
    ser = serial.Serial(port, 115200, timeout=0.05)

    while True:
        hdr = ser.read(1)
        if not hdr:
            check_timeouts()
            continue
        h = hdr[0]
        if h == 0xA0:
            pkt = ser.read(4)
            if len(pkt)==4:
                handle_joystick_packet(pkt)
        elif h == 0xB0:
            pkt = ser.read(3)
            if len(pkt)==3:
                handle_button_packet(pkt)
        elif h == 0xC0:
            pkt = ser.read(3)
            if len(pkt)==3:
                handle_direcional_packet(pkt)
        check_timeouts()

if __name__ == "__main__":
    main()
