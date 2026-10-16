#!/usr/bin/env python3
import sys
import glob
import time

def find_port():
    ports = glob.glob('/dev/cu.usbmodem*')
    if not ports:
        ports = glob.glob('/dev/cu.usbserial*')
    return ports[0] if ports else None

def main():
    if len(sys.argv) < 2:
        print("Використання: ./switch_mode.sh <jbd|daly|toggle|status|help> [порт]")
        print("Приклади:")
        print("  ./switch_mode.sh jbd      -> Перемкнути емулятор у режим JBD BMS")
        print("  ./switch_mode.sh daly     -> Перемкнути емулятор у режим DALY BMS")
        print("  ./switch_mode.sh toggle   -> Змінити поточний режим на протилежний")
        print("  ./switch_mode.sh status   -> Зчитати поточний статус та режим")
        sys.exit(1)

    cmd_arg = sys.argv[1].lower()
    port = sys.argv[2] if len(sys.argv) > 2 else find_port()

    if not port:
        print("❌ Помилка: Не знайдено підключеного пристрою USB CDC (/dev/cu.usbmodem*)!")
        sys.exit(1)

    try:
        import serial
    except ImportError:
        print("❌ Помилка: Модуль 'pyserial' не встановлено. Встановіть через 'pip install pyserial'")
        sys.exit(1)

    print(f"🔌 Підключення до {port}...")
    try:
        ser = serial.Serial(port, 115200, timeout=1.5)
    except Exception as e:
        print(f"❌ Не вдалося відкрити порт {port}: {e}")
        sys.exit(1)

    time.sleep(0.3)
    # Clear input buffer
    ser.reset_input_buffer()

    cmd_map = {
        'jbd': 'j\n',
        'j': 'j\n',
        'daly': 'y\n',
        'y': 'y\n',
        'toggle': 'm\n',
        'm': 'm\n',
        'status': '\n',
        'help': 'help\n'
    }

    send_cmd = cmd_map.get(cmd_arg)
    if not send_cmd:
        print(f"⚠️ Невідома команда '{cmd_arg}'. Доступні: jbd, daly, toggle, status, help")
        ser.close()
        sys.exit(1)

    if cmd_arg != 'status':
        ser.write(send_cmd.encode('utf-8'))
        ser.flush()
        print(f"📡 Надіслано команду: {cmd_arg.upper()}")

    # Read output response
    t_end = time.time() + 2.5
    received_any = False
    while time.time() < t_end:
        line = ser.readline()
        if line:
            text = line.decode('utf-8', errors='replace').strip()
            if text:
                print(f"  {text}")
                received_any = True
                if "Restarting" in text or "Mode set" in text or "Switched" in text:
                    break

    ser.close()
    if received_any:
        print("✅ Команду успішно виконано!")
    else:
        print("⚠️ Відповідь не отримана (можливо порт зайнятий або емулятор перезавантажився).")

if __name__ == '__main__':
    main()
